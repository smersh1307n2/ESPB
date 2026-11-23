using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using LLVMSharp.Interop;

namespace ESPB_Runtime
{
    #region Graph Data Structures

    public class GraphNode
    {
        public LLVMValueRef Value { get; }
        
        // ОПТИМИЗАЦИЯ: List для быстрой итерации, HashSet для быстрой проверки
        public HashSet<GraphNode> InterferencesSet { get; } = new HashSet<GraphNode>();
        public List<GraphNode> InterferencesList { get; } = new List<GraphNode>();
        
        public HashSet<GraphNode> Preferences { get; } = new HashSet<GraphNode>();

        public int Color { get; set; } = -1; // -1: none, -2: removed (on stack), -3: spilled
        
        // Степень узла (игнорируя удаленные в стек узлы)
        public int Degree => InterferencesSet.Count(n => n.Color != -2);

        public GraphNode(LLVMValueRef value)
        {
            Value = value;
        }

        public void AddInterference(GraphNode other)
        {
            if (other != this && InterferencesSet.Add(other))
            {
                InterferencesList.Add(other);
                // Зеркально
                if (other.InterferencesSet.Add(this))
                {
                    other.InterferencesList.Add(this);
                }
            }
        }

        public void AddPreference(GraphNode other)
        {
            if (other != this && !InterferencesSet.Contains(other))
            {
                Preferences.Add(other);
                other.Preferences.Add(this);
            }
        }
    }

    public class InterferenceGraph
    {
        public Dictionary<LLVMValueRef, GraphNode> Nodes { get; } = new Dictionary<LLVMValueRef, GraphNode>();

        public GraphNode GetOrAddNode(LLVMValueRef val)
        {
            if (!Nodes.TryGetValue(val, out var node))
            {
                node = new GraphNode(val);
                Nodes[val] = node;
            }
            return node;
        }

        public void AddInterferenceEdge(LLVMValueRef val1, LLVMValueRef val2)
        {
            if (val1.Equals(val2)) return;
            if (Nodes.TryGetValue(val1, out var node1) && Nodes.TryGetValue(val2, out var node2))
            {
                node1.AddInterference(node2);
            }
        }

        public void AddPreferenceEdge(LLVMValueRef val1, LLVMValueRef val2)
        {
            if (val1.Equals(val2)) return;
            if (Nodes.TryGetValue(val1, out var node1) && Nodes.TryGetValue(val2, out var node2))
            {
                node1.AddPreference(node2);
            }
        }
    }

    #endregion

    public class GraphColoringAllocator
    {
        private readonly LLVMValueRef _function;
        private readonly LLVMTargetDataRef _targetData;
        private readonly TextWriter _logger;
        
        // Promoted allocas - these should be skipped (they don't need pointer registers)
        private readonly HashSet<LLVMValueRef>? _promotedAllocas;

        private InterferenceGraph _graph = new InterferenceGraph();
        private Stack<GraphNode> _simplifyStack = new Stack<GraphNode>();
        
        // Кэш LiveIn/LiveOut
        private readonly Dictionary<LLVMBasicBlockRef, HashSet<LLVMValueRef>> _blockLiveIn = new Dictionary<LLVMBasicBlockRef, HashSet<LLVMValueRef>>();
        private Dictionary<LLVMBasicBlockRef, HashSet<LLVMValueRef>> _blockLiveOut = new Dictionary<LLVMBasicBlockRef, HashSet<LLVMValueRef>>();

        // Block order index for loop/backedge heuristics
        private readonly Dictionary<LLVMBasicBlockRef, int> _blockIndex = new Dictionary<LLVMBasicBlockRef, int>();

        // Union-Find parent map for conservative coalescing (LLVMValueRef -> representative)
        private readonly Dictionary<LLVMValueRef, LLVMValueRef> _ufParent = new Dictionary<LLVMValueRef, LLVMValueRef>();

        // Конфигурация регистров
        private const int TOTAL_VIRTUAL_REGS = 255;

        // For non-leaf functions we avoid argument registers (R0..R7) because calls clobber them.
        // For leaf functions (no calls) we can use R0..R7 freely, which eliminates many parameter MOVs.
        private readonly int _generalPurposeRegsStart;
        private readonly int _k;

        // ОПТИМИЗАЦИЯ: Буфер для проверки цветов (убираем аллокации HashSet в цикле)
        private bool[] _colorsBuffer = new bool[TOTAL_VIRTUAL_REGS];

        public GraphColoringAllocator(LLVMValueRef function, LLVMTargetDataRef targetData, FunctionHints? hints, TextWriter logger, HashSet<LLVMValueRef>? promotedAllocas = null)
        {
            _function = function;
            _targetData = targetData;
            _logger = logger;
            _promotedAllocas = promotedAllocas;

            bool hasCalls = FunctionHasCalls(_function);
            _generalPurposeRegsStart = hasCalls ? 32 : 0;
            _k = TOTAL_VIRTUAL_REGS - _generalPurposeRegsStart;

            _logger.WriteLine($"[GraphColoringAllocator] Leaf-mode: {!hasCalls}. GENERAL_PURPOSE_REGS_START={_generalPurposeRegsStart}");
            if (_promotedAllocas != null && _promotedAllocas.Count > 0)
            {
                _logger.WriteLine($"[GraphColoringAllocator] Will apply extended liveness for {_promotedAllocas.Count} promoted allocas");
            }
        }

        private static unsafe bool FunctionHasCalls(LLVMValueRef function)
        {
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    LLVMOpcode op = LLVM.GetInstructionOpcode(instr);
                    if (op == LLVMOpcode.LLVMCall || op == LLVMOpcode.LLVMInvoke || op == LLVMOpcode.LLVMCallBr)
                        return true;
                }
            }
            return false;
        }

        public AllocationResult Allocate()
        {
            // Union-find for conservative coalescing
            _ufParent.Clear();
            // 1. Точный анализ Liveness (гарантия корректности циклов)
            AnalyzeLiveness();
            
            // 2. Построение графа
            BuildInterferenceGraphExact();

            // 2.5 Conservative PHI coalescing (merge PHI with latch incoming when safe)
            CoalescePhiNodesConservative();

            // 3. Расширенные подсказки (удаляют лишние MOV)
            AddExtendedPreferences();

            // 3.5 Pre-color leaf parameters to ABI registers (eliminates prologue param shuffles)
            PrecolorLeafArguments();

            // 3.55 Pre-color return value to R0 for leaf functions (eliminates return MOV)
            PrecolorReturnToR0();

            // 3.6 Pre-color call argument values to ABI registers when it is safe (reduces Arg Setup MOV)
            PrecolorCallArguments();

            // 3.65 Pre-color call results directly into ABI arg registers when the result is later used
            //      as an argument of the next call (and does not live across calls). This collapses
            //      MOV chains (R0->tmp->Ri) into a single MOV (R0->Ri) safely.
            PrecolorCallResultsToNextCallArgRegs();

            // 4. Упрощение графа (Stack-based - обеспечивает качество раскраски)
            SimplifyGraph();

            // 5. Выбор регистров (С использованием буфера для скорости)
            SelectRegisters();

            var result = new AllocationResult();
            byte maxReg = 0;
            int peakLive = 0;

            foreach (var node in _graph.Nodes.Values.Distinct())
            {
                if (node.Color >= 0)
                {
                    // Assign the chosen physical register to ALL LLVM values that map to this node.
                    foreach (var kv in _graph.Nodes)
                    {
                        if (ReferenceEquals(kv.Value, node))
                            result.Locations[kv.Key] = new RegisterLocation((byte)node.Color);
                    }
                    if (node.Color > maxReg) maxReg = (byte)node.Color;
                    peakLive++;
                }
                else if (node.Color == -3)
                {
                    _logger.WriteLine($"[GraphColoringAllocator] ERROR: Node {node.Value} spilled!");
                }
            }

            result.MaxRegisterIndex = maxReg;
            result.TotalSpillSlotSize = 0; 
            result.PeakLiveRegisters = peakLive;
            
            // Compaction теперь в RegisterManager, до PARAM-SHUFFLE
            return result;
        }

        private unsafe void AnalyzeLiveness()
        {
            var blockOrder = new List<LLVMBasicBlockRef>();
            
            // Параметры: все живые одновременно в начале функции, поэтому должны интерферировать друг с другом
            var functionParams = new List<LLVMValueRef>();
            for (uint i = 0; i < LLVM.CountParams(_function); i++)
            {
                var param = LLVM.GetParam(_function, i);

                // Skip effectively-unused arguments entirely (eliminates pointless PARAM-SHUFFLE in prologue).
                if (IsEffectivelyUnusedParameter(param))
                    continue;

                _graph.GetOrAddNode(param);
                functionParams.Add(param);
            }
            
            // КРИТИЧНО: Создаем интерференции между всеми параметрами (они все живые одновременно!)
            for (int i = 0; i < functionParams.Count; i++)
            {
                for (int j = i + 1; j < functionParams.Count; j++)
                {
                    _graph.AddInterferenceEdge(functionParams[i], functionParams[j]);
                }
            }

            // Сбор блоков и Defs
            var liveIn = new Dictionary<LLVMBasicBlockRef, HashSet<LLVMValueRef>>();
            
            _blockIndex.Clear();
            int bbIdx = 0;
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                blockOrder.Add(bb);
                _blockIndex[bb] = bbIdx++;
                liveIn[bb] = new HashSet<LLVMValueRef>();
                _blockLiveOut[bb] = new HashSet<LLVMValueRef>();
                
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                    {
                        _graph.GetOrAddNode(instr);
                    }
                }
            }

            // Dataflow Analysis
            _blockLiveIn.Clear();
            bool changed = true;
            int iterations = 0;
            while(changed && iterations++ < 100)
            {
                changed = false;
                foreach(var bb in blockOrder.AsEnumerable().Reverse())
                {
                    var liveOut = new HashSet<LLVMValueRef>();
                    LLVMValueRef terminator = LLVM.GetLastInstruction(bb);
                    
                    if (terminator.Handle != IntPtr.Zero)
                    {
                        for (int i = 0; i < LLVM.GetNumSuccessors(terminator); i++)
                        {
                            var successor = LLVM.GetSuccessor(terminator, (uint)i);
                            
                            // PHI-correct liveness:
                            // 1) When propagating succLiveIn to pred liveOut, PHI *definitions* in successor are NOT live on the edge.
                            // 2) PHI *incoming values* are used on the edge pred->succ and must be added to pred liveOut.

                            if (liveIn.TryGetValue(successor, out var succLiveIn))
                            {
                                // Copy succLiveIn minus PHI defs
                                var succFiltered = new HashSet<LLVMValueRef>(succLiveIn);
                                for (LLVMValueRef phi = LLVM.GetFirstInstruction(successor);
                                     phi.Handle != IntPtr.Zero && LLVM.GetInstructionOpcode(phi) == LLVMOpcode.LLVMPHI;
                                     phi = LLVM.GetNextInstruction(phi))
                                {
                                    succFiltered.Remove(phi);
                                }
                                liveOut.UnionWith(succFiltered);
                            }

                            // PHI incoming handling (edge uses)
                            for (LLVMValueRef phi = LLVM.GetFirstInstruction(successor);
                                 phi.Handle != IntPtr.Zero && LLVM.GetInstructionOpcode(phi) == LLVMOpcode.LLVMPHI;
                                 phi = LLVM.GetNextInstruction(phi))
                            {
                                for (uint opIdx = 0; opIdx < LLVM.CountIncoming(phi); opIdx++)
                                {
                                    if (LLVM.GetIncomingBlock(phi, opIdx) == bb)
                                    {
                                        LLVMValueRef val = LLVM.GetIncomingValue(phi, opIdx);
                                        if (IsValidVariable(val)) liveOut.Add(val);
                                    }
                                }
                            }
                        }
                    }
                    
                    _blockLiveOut[bb] = new HashSet<LLVMValueRef>(liveOut);
                    var currentLiveIn = new HashSet<LLVMValueRef>(liveOut);

                    for(LLVMValueRef instr = LLVM.GetLastInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetPreviousInstruction(instr))
                    {
                        if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                            currentLiveIn.Remove(instr);

                        if (LLVM.GetInstructionOpcode(instr) != LLVMOpcode.LLVMPHI)
                        {
                            for(int i=0; i < LLVM.GetNumOperands(instr); ++i)
                            {
                                LLVMValueRef op = LLVM.GetOperand(instr, (uint)i);
                                if (IsValidVariable(op)) currentLiveIn.Add(op);
                            }
                        }
                    }
                    
                    if(!liveIn[bb].SetEquals(currentLiveIn))
                    {
                        liveIn[bb] = currentLiveIn;
                        changed = true;
                    }

                    // Keep a copy for later interference construction
                    _blockLiveIn[bb] = new HashSet<LLVMValueRef>(liveIn[bb]);
                }
            }
        }

        private unsafe void BuildInterferenceGraphExact()
        {
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                var live = new HashSet<LLVMValueRef>(_blockLiveOut[bb]);
                // NOTE: Do NOT connect live-out set into a full clique.
                // It introduces false interferences (especially with PHI/backedge values) and blocks coalescing.
                // Def-vs-live edges added during reverse scan are sufficient.

                for (LLVMValueRef instr = LLVM.GetLastInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetPreviousInstruction(instr))
                {
                    // SSA rule: PHI nodes conceptually belong to the block boundary and should not participate
                    // in reverse-scan interference inside the block body. PHI defs are handled separately via liveAtEntry.
                    if (LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMPHI)
                        continue;

                    var opcode = LLVM.GetInstructionOpcode(instr);
                    
                    // === REGISTER PROMOTION: Special handling for store/load to promoted allocas ===
                    // For promoted allocas, we treat:
                    // - store to alloca as DEF of the alloca (the value is being written)
                    // - load from alloca as USE of the alloca (the value is being read)
                    // This extends the alloca's liveness to cover its actual value lifetime.
                    
                    if (opcode == LLVMOpcode.LLVMStore && _promotedAllocas != null)
                    {
                        LLVMValueRef storePtr = LLVM.GetOperand(instr, 1);
                        if (storePtr.IsAAllocaInst.Handle != IntPtr.Zero && _promotedAllocas.Contains(storePtr))
                        {
                            // Store to promoted alloca = DEF of the alloca
                            // Add interference edges: alloca vs all currently live values
                            foreach (var l in live)
                            {
                                if (!l.Equals(storePtr)) AddInterferenceEdgeDebug(storePtr, l, "promotedAlloca:store-def-vs-live");
                            }
                            // After def, alloca is no longer live (until next use)
                            live.Remove(storePtr);
                            
                            // The stored VALUE is a use (add it to live)
                            LLVMValueRef storeVal = LLVM.GetOperand(instr, 0);
                            if (IsValidVariable(storeVal)) live.Add(storeVal);
                            continue; // Skip normal processing for this instruction
                        }
                    }
                    
                    if (opcode == LLVMOpcode.LLVMLoad && _promotedAllocas != null)
                    {
                        LLVMValueRef loadPtr = LLVM.GetOperand(instr, 0);
                        if (loadPtr.IsAAllocaInst.Handle != IntPtr.Zero && _promotedAllocas.Contains(loadPtr))
                        {
                            // Load from promoted alloca:
                            // 1. The load RESULT is a def (normal handling)
                            // 2. The alloca is a USE (add to live)
                            
                            // Normal def handling for load result
                            if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                            {
                                foreach (var l in live)
                                {
                                    if (!l.Equals(instr)) AddInterferenceEdgeDebug(instr, l, "reverseScan:def-vs-live");
                                }
                                live.Remove(instr);
                            }
                            
                            // Add alloca as USE (extends its liveness)
                            live.Add(loadPtr);
                            continue; // Skip normal operand processing
                        }
                    }
                    
                    // === Normal instruction handling ===
                    if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                    {
                        // Skip def handling for promoted alloca instructions themselves
                        // (they don't define a value - the value comes from stores)
                        bool isPromotedAllocaDef = instr.IsAAllocaInst.Handle != IntPtr.Zero && 
                                                   _promotedAllocas != null && 
                                                   _promotedAllocas.Contains(instr);
                        if (!isPromotedAllocaDef)
                        {
                            foreach (var l in live)
                            {
                                if (!l.Equals(instr)) AddInterferenceEdgeDebug(instr, l, "reverseScan:def-vs-live");
                            }
                            live.Remove(instr);
                        }
                    }

                    for (int i = 0; i < LLVM.GetNumOperands(instr); ++i)
                    {
                        LLVMValueRef op = LLVM.GetOperand(instr, (uint)i);
                        // Skip promoted alloca operands in normal instructions (handled specially above)
                        if (op.IsAAllocaInst.Handle != IntPtr.Zero && _promotedAllocas != null && _promotedAllocas.Contains(op))
                            continue;
                        if (IsValidVariable(op)) live.Add(op);
                    }
                }
                
                // PHI Defs:
                // PHI results are defined at block entry. Their interference should be computed against
                // values live-in to this block (not against block live-out), otherwise we get false
                // interference on loop backedges and coalescing cannot eliminate MOVs in hot loops.
                var liveAtEntry = _blockLiveIn.TryGetValue(bb, out var entrySet)
                    ? new HashSet<LLVMValueRef>(entrySet)
                    : new HashSet<LLVMValueRef>();

                for (LLVMValueRef phi = LLVM.GetFirstInstruction(bb);
                     phi.Handle != IntPtr.Zero && LLVM.GetInstructionOpcode(phi) == LLVMOpcode.LLVMPHI;
                     phi = LLVM.GetNextInstruction(phi))
                {
                    foreach (var l in liveAtEntry)
                    {
                        if (!l.Equals(phi)) AddInterferenceEdgeDebug(phi, l, "phiDef-vs-liveAtEntry");
                    }
                    liveAtEntry.Add(phi);
                }

                // NOTE: Do NOT connect live-at-entry set into a full clique.
                // This introduces false interferences for loop PHIs and blocks coalescing (causing extra MOVs in hot loops).
                // Def-vs-live edges computed above are sufficient.
                // ConnectClique(liveAtEntry, "clique:liveAtEntry");
            }
        }

private unsafe void AddExtendedPreferences()
        {
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    var opcode = LLVM.GetInstructionOpcode(instr);

                    if (opcode == LLVMOpcode.LLVMPHI)
                    {
                        for (uint i = 0; i < LLVM.CountIncoming(instr); i++)
                        {
                            LLVMValueRef incoming = LLVM.GetIncomingValue(instr, i);
                            if (IsValidVariable(incoming)) _graph.AddPreferenceEdge(instr, incoming);
                        }
                    }
                    // Добавляем подсказки для операций приведения типов (часто no-op)
                    else if (opcode == LLVMOpcode.LLVMBitCast || 
                             opcode == LLVMOpcode.LLVMIntToPtr || 
                             opcode == LLVMOpcode.LLVMPtrToInt ||
                             opcode == LLVMOpcode.LLVMZExt || 
                             opcode == LLVMOpcode.LLVMSExt)
                    {
                        LLVMValueRef src = LLVM.GetOperand(instr, 0);
                        if (IsValidVariable(src)) _graph.AddPreferenceEdge(instr, src);
                    }
                }
            }
        }

        private void ConnectClique(HashSet<LLVMValueRef> nodes, string reason)
        {
            var list = nodes.ToList();
            int count = list.Count;
            for (int i = 0; i < count; i++)
            {
                for (int j = i + 1; j < count; j++)
                {
                    AddInterferenceEdgeDebug(list[i], list[j], reason);
                }
            }
        }

        private unsafe void AddInterferenceEdgeDebug(LLVMValueRef a, LLVMValueRef b, string reason)
        {
            _graph.AddInterferenceEdge(a, b);

            // Very targeted debug: only for fibonacci_iterative and PHI/incoming candidates.
            if (!_function.Name.Contains("fibonacci", StringComparison.OrdinalIgnoreCase))
                return;

            bool aIsPhi = a.IsAInstruction != null && LLVM.GetInstructionOpcode(a) == LLVMOpcode.LLVMPHI;
            bool bIsPhi = b.IsAInstruction != null && LLVM.GetInstructionOpcode(b) == LLVMOpcode.LLVMPHI;

            // Also log edges involving add instructions (likely incoming for PHI)
            bool aIsAdd = a.IsAInstruction != null && (LLVM.GetInstructionOpcode(a) == LLVMOpcode.LLVMAdd);
            bool bIsAdd = b.IsAInstruction != null && (LLVM.GetInstructionOpcode(b) == LLVMOpcode.LLVMAdd);

            if (aIsPhi || bIsPhi || aIsAdd || bIsAdd)
            {
                _logger.WriteLine($"[Interf-DIAG] {reason}: {a} <-> {b}");
            }
        }
        
        private unsafe string GetValueDebugString(LLVMValueRef val)
        {
            if (val.Handle == IntPtr.Zero)
                return "<null>";
            
            string name = val.Name ?? "";
            if (val.IsAInstruction != null)
            {
                LLVMOpcode op = LLVM.GetInstructionOpcode(val);
                if (!string.IsNullOrEmpty(name))
                    return $"%{name} ({op})";
                return $"[{op}@{val.Handle.ToInt64():X}]";
            }
            if (val.IsAArgument != null)
            {
                return $"arg:{name}";
            }
            return $"val:{name}@{val.Handle.ToInt64():X}";
        }

        private bool IsValidVariable(LLVMValueRef val)
        {
            return (val.IsAInstruction != null || val.IsAArgument != null) && _graph.Nodes.ContainsKey(val);
        }

        private static unsafe bool IsPhiNode(LLVMValueRef value)
        {
            return value.IsAInstruction != null && LLVM.GetInstructionOpcode(value) == LLVMOpcode.LLVMPHI;
        }

        private static unsafe bool IsEffectivelyUnusedParameter(LLVMValueRef param)
        {
            LLVMUseRef use = LLVM.GetFirstUse(param);
            if (use == null)
                return true;

            var storedAllocas = new HashSet<IntPtr>();

            for (; use != null; use = LLVM.GetNextUse(use))
            {
                LLVMValueRef user = LLVM.GetUser(use);
                if (LLVM.GetValueKind(user) != LLVMValueKind.LLVMInstructionValueKind)
                    return false;

                LLVMOpcode op = LLVM.GetInstructionOpcode(user);
                if (op != LLVMOpcode.LLVMStore)
                    return false;

                LLVMValueRef storedVal = LLVM.GetOperand(user, 0);
                LLVMValueRef storePtr = LLVM.GetOperand(user, 1);
                if (!storedVal.Equals(param))
                    return false;

                if (storePtr.IsAAllocaInst == null || storePtr.IsAAllocaInst.Handle == IntPtr.Zero)
                    return false;

                storedAllocas.Add(storePtr.Handle);
            }

            if (storedAllocas.Count == 0)
                return false;

            foreach (var allocaHandle in storedAllocas)
            {
                LLVMValueRef allocaVal = new LLVMValueRef(allocaHandle);
                LLVMUseRef aUse = LLVM.GetFirstUse(allocaVal);
                while (aUse != null)
                {
                    LLVMValueRef aUser = LLVM.GetUser(aUse);
                    if (LLVM.GetValueKind(aUser) == LLVMValueKind.LLVMInstructionValueKind)
                    {
                        LLVMOpcode aOp = LLVM.GetInstructionOpcode(aUser);
                        if (aOp == LLVMOpcode.LLVMLoad)
                            return false;

                        if (aOp == LLVMOpcode.LLVMStore)
                        {
                            LLVMValueRef ptr = LLVM.GetOperand(aUser, 1);
                            LLVMValueRef val = LLVM.GetOperand(aUser, 0);
                            if (!ptr.Equals(allocaVal) || !val.Equals(param))
                                return false;
                        }
                        else
                        {
                            return false;
                        }
                    }
                    else
                    {
                        return false;
                    }

                    aUse = LLVM.GetNextUse(aUse);
                }
            }

            return true;
        }

        // --- Conservative Register Coalescing (Union-Find) ---
        private LLVMValueRef FindRep(LLVMValueRef v)
        {
            if (!_ufParent.TryGetValue(v, out var parent))
            {
                _ufParent[v] = v;
                return v;
            }
            if (parent.Equals(v))
                return v;
            var root = FindRep(parent);
            _ufParent[v] = root;
            return root;
        }

        private void Union(LLVMValueRef a, LLVMValueRef b)
        {
            var ra = FindRep(a);
            var rb = FindRep(b);
            if (ra.Equals(rb)) return;
            // Simple deterministic union: keep smaller handle as root
            if (ra.Handle.ToInt64() <= rb.Handle.ToInt64())
                _ufParent[rb] = ra;
            else
                _ufParent[ra] = rb;
        }

        private unsafe void CoalescePhiNodesConservative()
        {
            // Conservative merge: for each PHI, try to merge it with its latch/backedge incoming value
            // if they do NOT interfere (no simultaneous liveness).
            // This targets hot-loop MOVs.

            // Snapshot current graph edges
            var oldNodes = _graph.Nodes.ToDictionary(k => k.Key, v => v.Value);

            // 1) Decide unions
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb);
                     instr.Handle != IntPtr.Zero && LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMPHI;
                     instr = LLVM.GetNextInstruction(instr))
                {
                    if (!oldNodes.TryGetValue(instr, out var phiNode))
                        continue;

                    LLVMBasicBlockRef phiBlock = LLVM.GetInstructionParent(instr);
                    _blockIndex.TryGetValue(phiBlock, out int phiIdx);

                    // Find latch incoming (heuristic: predecessor with greatest block index >= phiIdx)
                    uint incCount = LLVM.CountIncoming(instr);
                    int bestIncBlockIdx = int.MinValue;
                    LLVMValueRef bestIncValue = default;

                    for (uint i = 0; i < incCount; i++)
                    {
                        LLVMBasicBlockRef incBlock = LLVM.GetIncomingBlock(instr, i);
                        if (!_blockIndex.TryGetValue(incBlock, out int incIdx))
                            continue;

                        // Do NOT require incIdx >= phiIdx, because enumeration order of blocks is arbitrary.
                        if (incIdx > bestIncBlockIdx)
                        {
                            bestIncBlockIdx = incIdx;
                            bestIncValue = LLVM.GetIncomingValue(instr, i);
                        }
                    }

                    if (bestIncValue.Handle == IntPtr.Zero)
                        continue;
                    if (!IsValidVariable(bestIncValue))
                        continue;
                    if (!oldNodes.TryGetValue(bestIncValue, out var incNode))
                        continue;

                    // Conservative, loop-friendly coalescing:
                    // 1) Prefer the self-loop/backedge incoming (incomingBlock == phiBlock) because it removes MOV in the hot loop.
                    // 2) Additionally, do Result-to-PHI coalescing: if an incoming instruction result is used ONLY by this PHI
                    //    (typical temp = a+b feeding PHI), merge it as well (still only if no interference).
                    // 3) If nothing merged, fall back to merging with any non-interfering incoming.
                    bool mergedAny = false;

                    // Pass 1: self-loop/backedge incoming (incoming block equals PHI block)
                    for (uint j = 0; j < incCount; j++)
                    {
                        LLVMBasicBlockRef incBlock = LLVM.GetIncomingBlock(instr, j);
                        if (!incBlock.Equals(phiBlock))
                            continue;

                        LLVMValueRef incoming = LLVM.GetIncomingValue(instr, j);
                        if (!IsValidVariable(incoming))
                            continue;
                        if (!oldNodes.TryGetValue(incoming, out var inNode))
                            continue;

                        bool hasInterf = phiNode.InterferencesSet.Contains(inNode) || inNode.InterferencesSet.Contains(phiNode);
                        if (hasInterf)
                        {
                            _logger.WriteLine($"[GraphColoringAllocator] [Coalesce-DIAG] self-loop merge BLOCKED by interference: phi={instr} incoming={incoming}");
                            continue;
                        }

                        Union(instr, incoming);
                        mergedAny = true;
                        _logger.WriteLine($"[GraphColoringAllocator] [Coalesce] Merging PHI with self-loop incoming (phi={instr}, incoming={incoming})");
                    }

                    // Pass 2: any incoming (only if we didn't merge via self-loop)
                    if (!mergedAny)
                    {
                        for (uint j = 0; j < incCount; j++)
                        {
                            LLVMValueRef incoming = LLVM.GetIncomingValue(instr, j);
                            if (!IsValidVariable(incoming))
                                continue;
                            if (!oldNodes.TryGetValue(incoming, out var inNode))
                                continue;

                            if (phiNode.InterferencesSet.Contains(inNode) || inNode.InterferencesSet.Contains(phiNode))
                                continue;

                            Union(instr, incoming);
                            mergedAny = true;
                            _logger.WriteLine($"[GraphColoringAllocator] [Coalesce] Merging PHI with incoming (phi={instr}, incoming={incoming})");
                        }
                    }

                    if (!mergedAny)
                    {
                        // Fallback: keep previous behavior of trying best latch incoming only.
                        if (!(phiNode.InterferencesSet.Contains(incNode) || incNode.InterferencesSet.Contains(phiNode)))
                        {
                            Union(instr, bestIncValue);
                            _logger.WriteLine($"[GraphColoringAllocator] [Coalesce] Merging PHI with latch incoming (phi={instr}, incoming={bestIncValue})");
                        }
                    }
                }
            }

            // 2) Rebuild graph with representatives
            var newGraph = new InterferenceGraph();

            // Ensure representative nodes exist
            foreach (var kv in oldNodes)
            {
                var rep = FindRep(kv.Key);
                newGraph.GetOrAddNode(rep);
            }

            // Re-add edges between representatives
            foreach (var kv in oldNodes)
            {
                var v = kv.Key;
                var vNode = kv.Value;
                var rv = FindRep(v);

                foreach (var n in vNode.InterferencesList)
                {
                    var rn = FindRep(n.Value);
                    if (!rv.Equals(rn))
                        newGraph.AddInterferenceEdge(rv, rn);
                }

                foreach (var p in vNode.Preferences)
                {
                    var rp = FindRep(p.Value);
                    if (!rv.Equals(rp))
                        newGraph.AddPreferenceEdge(rv, rp);
                }
            }

            // Alias all original values to their representative node
            foreach (var kv in oldNodes)
            {
                var rep = FindRep(kv.Key);
                newGraph.Nodes[kv.Key] = newGraph.Nodes[rep];
            }

            _graph = newGraph;
        }

        // --- STACK BASED SIMPLIFICATION (CHAITIN-BRIGGS) ---
        // Это ключ к качественной раскраске. Мы удаляем узлы из графа,
        // а потом добавляем их обратно, раскрашивая самые "сложные" последними (когда их соседи уже известны).
        private void SimplifyGraph()
        {
            // NOTE: We only push *uncolored* nodes. Pre-colored nodes (e.g., leaf params) are kept as-is.
            while (true)
            {
                GraphNode? nodeToPush = null;
                int maxDegree = -1;

                // Stop when there are no more uncolored nodes
                bool anyUncolored = _graph.Nodes.Values.Any(n => n.Color == -1);
                if (!anyUncolored)
                    break;

                // 1. Ищем узел, который МОЖНО гарантированно раскрасить (< K соседей)
                //    Для PHI-узлов важно coalescing: если PHI будет раскрашен ПОСЛЕ incoming-значений,
                //    он сможет выбрать их цвет по preference-edge и убрать MOV. Поэтому PHI стараемся
                //    класть в стек как можно РАНЬШЕ (чтобы при pop он обрабатывался позже).

                // Pass 1: PHI nodes first
                foreach (var node in _graph.Nodes.Values)
                {
                    if (node.Color == -1 && IsPhiNode(node.Value) && node.Degree < _k)
                    {
                        nodeToPush = node;
                        break;
                    }
                }

                // Pass 2: any node
                if (nodeToPush == null)
                {
                    foreach (var node in _graph.Nodes.Values)
                    {
                        if (node.Color == -1)
                        {
                            if (node.Degree < _k)
                            {
                                nodeToPush = node;
                                break;
                            }
                        }
                    }
                }

                // 2. Если такого нет, выбираем кандидата на "оптимистичный spill" (удаляем узел с макс степенью)
                if (nodeToPush == null) 
                {
                    foreach (var node in _graph.Nodes.Values)
                    {
                        if (node.Color == -1)
                        {
                            if (nodeToPush == null || node.Degree > maxDegree)
                            {
                                nodeToPush = node;
                                maxDegree = node.Degree;
                            }
                        }
                    }
                }

                if (nodeToPush != null)
                {
                    nodeToPush.Color = -2; // Помечаем как "в стеке"
                    _simplifyStack.Push(nodeToPush);
                }
                else
                {
                    break;
                }
            }
        }

        private unsafe int ChooseBestPreferenceColor(GraphNode node)
        {
            // Score colors from already-colored preference neighbors.
            // We only consider colors that are not blocked by interferences (colorsBuffer).
            var scores = new Dictionary<int, int>();

            foreach (var pref in node.Preferences)
            {
                if (pref.Color < 0) continue;
                int c = pref.Color;
                if (_colorsBuffer[c]) continue;

                // Base score: prefer any coalescing
                int score = 10;

                // Stronger preference: coalesce with non-PHI values (often "temp" values that flow into PHI)
                if (!IsPhiNode(pref.Value)) score += 2;

                // Small tie-breaker: prefer lower register numbers (tends to reduce max_reg)
                score += Math.Max(0, 3 - c);

                if (scores.TryGetValue(c, out int cur))
                    scores[c] = cur + score;
                else
                    scores[c] = score;
            }

            // Extra boost for loop-backedge incoming of PHI (hot path): choose the color of the latch incoming if possible.
            if (IsPhiNode(node.Value))
            {
                LLVMBasicBlockRef phiBlock = LLVM.GetInstructionParent(node.Value);
                _blockIndex.TryGetValue(phiBlock, out int phiIdx);

                uint incCount = LLVM.CountIncoming(node.Value);
                int bestIncBlockIdx = int.MinValue;
                LLVMValueRef bestIncValue = default;

                for (uint i = 0; i < incCount; i++)
                {
                    LLVMBasicBlockRef incBlock = LLVM.GetIncomingBlock(node.Value, i);
                    if (!_blockIndex.TryGetValue(incBlock, out int incIdx))
                        continue;

                    // Heuristic: incoming from the "latest" predecessor is typically the loop latch/backedge.
                    // NOTE: do NOT require incIdx >= phiIdx, because block order is not necessarily topological.
                    if (incIdx > bestIncBlockIdx)
                    {
                        bestIncBlockIdx = incIdx;
                        bestIncValue = LLVM.GetIncomingValue(node.Value, i);
                    }
                }

                if (bestIncValue.Handle != IntPtr.Zero && _graph.Nodes.TryGetValue(bestIncValue, out var incNode))
                {
                    int c = incNode.Color;
                    if (c >= 0 && !_colorsBuffer[c])
                    {
                        // Large bias: removing backedge move saves per-iteration cost.
                        scores[c] = scores.TryGetValue(c, out int cur) ? cur + 1000 : 1000;
                    }
                }
            }

            if (scores.Count == 0)
                return -1;

            // Pick the highest score
            int bestColor = -1;
            int bestScore = int.MinValue;
            foreach (var kvp in scores)
            {
                if (kvp.Value > bestScore)
                {
                    bestScore = kvp.Value;
                    bestColor = kvp.Key;
                }
            }
            return bestColor;
        }

        private unsafe void PrecolorReturnToR0()
        {
            // Only safe for leaf-mode where R0 is not clobbered by calls.
            if (_generalPurposeRegsStart != 0)
                return;

            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                LLVMValueRef terminator = LLVM.GetLastInstruction(bb);
                if (terminator.Handle == IntPtr.Zero)
                    continue;

                if (LLVM.GetInstructionOpcode(terminator) != LLVMOpcode.LLVMRet)
                    continue;

                // ret void => no operand
                if (LLVM.GetNumOperands(terminator) == 0)
                    continue;

                LLVMValueRef retVal = LLVM.GetOperand(terminator, 0);
                if (!IsValidVariable(retVal))
                    continue;

                // Pin the representative (after coalescing), not necessarily the raw retVal
                LLVMValueRef rep = FindRep(retVal);
                if (!_graph.Nodes.TryGetValue(rep, out var node))
                {
                    _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Return pin skipped: no node for rep={rep} (retVal={retVal})");
                    continue;
                }

                // Do not override pre-colored/stack-marked nodes
                if (node.Color != -1)
                {
                    _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Return pin skipped: node already colored={node.Color}");
                    continue;
                }

                // Check if any already-colored neighbor uses R0
                bool conflict = false;
                foreach (var n in node.InterferencesList)
                {
                    if (n.Color == 0)
                    {
                        conflict = true;
                        break;
                    }
                }

                if (!conflict)
                {
                    node.Color = 0;
                    _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Return value pinned to R0 (rep={rep}, retVal={retVal})");
                }
                else
                {
                    _logger.WriteLine("[GraphColoringAllocator] [Precolor] Return pin skipped: conflict on R0");
                }

                // only one return needed
                return;
            }
        }

        private unsafe void PrecolorLeafArguments()
        {
            // Leaf-mode is detected by GENERAL_PURPOSE_REGS_START == 0.
            if (_generalPurposeRegsStart != 0)
                return;

            uint paramCount = LLVM.CountParams(_function);
            uint limit = Math.Min(paramCount, 8); // ABI arg regs R0..R7

            for (uint i = 0; i < limit; i++)
            {
                LLVMValueRef param = LLVM.GetParam(_function, i);
                if (_graph.Nodes.TryGetValue(param, out var node))
                {
                    // If already colored by something else, don't override.
                    if (node.Color == -1)
                    {
                        node.Color = (int)i;
                        _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Leaf param {i} pinned to R{i}");
                    }
                }
            }
        }

        private unsafe void PrecolorCallArguments()
        {
            // Try to pin values that are passed as call arguments into ABI regs R0..R7.
            // This reduces arg-setup MOVs for values like select/phi results.
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    LLVMOpcode op = LLVM.GetInstructionOpcode(instr);
                    if (op != LLVMOpcode.LLVMCall && op != LLVMOpcode.LLVMInvoke && op != LLVMOpcode.LLVMCallBr)
                        continue;

                    uint argCount = LLVM.GetNumArgOperands(instr);
                    uint limit = Math.Min(argCount, 16u);
                    for (uint i = 0; i < limit; i++)
                    {
                        // Correct: fetch call argument i (not generic operand)
                        LLVMValueRef arg = LLVM.GetArgOperand(instr, i);
                        if (!IsValidVariable(arg))
                            continue;

                        if (!_graph.Nodes.TryGetValue(arg, out var node))
                            continue;

                        if (node.Color != -1)
                            continue; // already pinned

                        // If the value lives across a call, don't force it into caller-saved arg regs.
                        if (LivesAcrossCall(arg))
                            continue;

                        int desired = (int)i;

                        // Only pin if no already pre-colored neighbor uses the same color.
                        bool conflict = false;
                        foreach (var n in node.InterferencesList)
                        {
                            if (n.Color == desired)
                            {
                                conflict = true;
                                break;
                            }
                        }
                        if (conflict)
                            continue;

                        node.Color = desired;
                        _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Call-arg pinned to R{desired} (value={arg})");
                    }
                }
            }
        }

        private unsafe bool IsNonMaterializingIntrinsicCall(LLVMValueRef instr)
        {
            if (instr.Kind != LLVMValueKind.LLVMInstructionValueKind)
                return false;
            LLVMOpcode op = LLVM.GetInstructionOpcode(instr);
            if (op != LLVMOpcode.LLVMCall && op != LLVMOpcode.LLVMInvoke && op != LLVMOpcode.LLVMCallBr)
                return false;

            LLVMValueRef cv = LLVM.GetCalledValue(instr);
            string name = cv.Name ?? "";
            return name.StartsWith("llvm.dbg.") || name.StartsWith("llvm.lifetime.");
        }

        private unsafe void PrecolorCallResultsToNextCallArgRegs()
        {
            // Universal optimization for MOV chains:
            // When a call result is consumed by the next call as argument i (i<8), we try to pin
            // the producer call's result value directly into Ri. This removes the need for an intermediate
            // virtual register and collapses R0->tmp->Ri into a single R0->Ri move (or even 0 moves).
            //
            // Safety:
            // - Only within the same basic block.
            // - Only if there are no material instructions between the two calls (dbg/lifetime ignored).
            // - Only if the value does NOT live across calls (LivesAcrossCall).
            // - Only if node is not already pre-colored and desired color doesn't conflict.
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                LLVMValueRef prevMaterial = default;

                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    LLVMOpcode op = LLVM.GetInstructionOpcode(instr);

                    // Track previous *material* instruction in block.
                    if (IsNonMaterializingIntrinsicCall(instr))
                        continue;

                    bool isCall = (op == LLVMOpcode.LLVMCall || op == LLVMOpcode.LLVMInvoke || op == LLVMOpcode.LLVMCallBr);
                    if (!isCall)
                    {
                        prevMaterial = instr;
                        continue;
                    }

                    // If we have a previous material instruction, and it is a call producing a value,
                    // try to pin it into the ABI arg reg of this call.
                    if (prevMaterial.Handle != IntPtr.Zero)
                    {
                        LLVMOpcode prevOp = LLVM.GetInstructionOpcode(prevMaterial);
                        bool prevIsCall = (prevOp == LLVMOpcode.LLVMCall || prevOp == LLVMOpcode.LLVMInvoke || prevOp == LLVMOpcode.LLVMCallBr);
                        if (prevIsCall && prevMaterial.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind && IsValidVariable(prevMaterial))
                        {
                            if (!LivesAcrossCall(prevMaterial))
                            {
                                // Find if current call uses previous call result as arg i<16.
                                uint argCount = LLVM.GetNumArgOperands(instr);
                                uint limit = Math.Min(argCount, 16u);
                                for (uint i = 0; i < limit; i++)
                                {
                                    LLVMValueRef arg = LLVM.GetArgOperand(instr, i);
                                    if (!arg.Equals(prevMaterial))
                                        continue;

                                    LLVMValueRef rep = FindRep(prevMaterial);
                                    if (!_graph.Nodes.TryGetValue(rep, out var node))
                                        break;

                                    if (node.Color != -1)
                                        break;

                                    int desired = (int)i;

                                    bool conflict = false;
                                    foreach (var n in node.InterferencesList)
                                    {
                                        if (n.Color == desired)
                                        {
                                            conflict = true;
                                            break;
                                        }
                                    }
                                    if (!conflict)
                                    {
                                        node.Color = desired;
                                        _logger.WriteLine($"[GraphColoringAllocator] [Precolor] Call-result pinned to R{desired} (value={prevMaterial})");
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    // Current call becomes previous material instruction.
                    prevMaterial = instr;
                }
            }
        }

        private static unsafe bool TryGetSingleUser(LLVMValueRef value, out LLVMValueRef user)
        {
            user = default;
            LLVMUseRef use = LLVM.GetFirstUse(value);
            if (use == null) return false;

            LLVMValueRef u0 = LLVM.GetUser(use);
            if (u0.Handle == IntPtr.Zero) return false;
            if (LLVM.GetNextUse(use) != null) return false; // more than 1 use

            user = u0;
            return true;
        }

        private static unsafe bool TryGetCallArgIndex(LLVMValueRef callInstr, LLVMValueRef argValue, out int argIndex)
        {
            argIndex = -1;
            uint n = LLVM.GetNumArgOperands(callInstr);
            for (uint i = 0; i < n; i++)
            {
                LLVMValueRef a = LLVM.GetArgOperand(callInstr, i);
                if (a.Equals(argValue))
                {
                    argIndex = (int)i;
                    return true;
                }
            }
            return false;
        }

        private void SelectRegisters()
        {
            // _logger.WriteLine("[GraphColoringAllocator] Phase 4: Selecting registers...");
            
            while (_simplifyStack.Count > 0)
            {
                var node = _simplifyStack.Pop();
                
                // 1. Сброс буфера (через очистку предыдущих значений было бы дольше, 
                //    но мы оптимизируем это циклом ниже)
                
                // Сбор занятых цветов
                // Используем InterferencesList для скорости
                foreach (var neighbor in node.InterferencesList)
                {
                    if (neighbor.Color >= 0)
                    {
                        _colorsBuffer[neighbor.Color] = true;
                    }
                }

                int selectedColor = -1;

                // 2. Проверяем подсказки (Preferences). Для PHI-узлов делаем усиленный coalescing:
                //    выбираем лучший цвет среди уже окрашенных preference-соседей, а не "первый попавшийся".
                if (IsPhiNode(node.Value))
                {
                    selectedColor = ChooseBestPreferenceColor(node);
                }
                else
                {
                    foreach (var prefNeighbor in node.Preferences)
                    {
                        // Если сосед уже покрашен (т.к. мы идем обратно по стеку, многие соседи уже могут иметь цвет)
                        if (prefNeighbor.Color >= 0 && !_colorsBuffer[prefNeighbor.Color])
                        {
                            selectedColor = prefNeighbor.Color;
                            break;
                        }
                    }
                }

                // 3. Call-Safe First-Fit: переменные, живущие через CALL, предпочитают callee-saved регистры
                if (selectedColor == -1)
                {
                    bool livesAcrossCall = LivesAcrossCall(node.Value);
                    
                    if (livesAcrossCall)
                    {
                        // Для переменных, живущих через CALL, предпочитаем R40..R47 (соответствуют s3..s10 в RISC-V)
                        // Эти регистры сохраняются вызываемой функцией (callee-saved)
                        for (int r = 40; r <= 47; r++)
                        {
                            if (!_colorsBuffer[r])
                            {
                                selectedColor = r;
                                break;
                            }
                        }
                    }
                    
                    // Если не нашли в callee-saved или переменная не живет через CALL, используем обычный First-Fit
                    if (selectedColor == -1)
                    {
                        for (int r = _generalPurposeRegsStart; r < TOTAL_VIRTUAL_REGS; r++)
                        {
                            if (!_colorsBuffer[r])
                            {
                                selectedColor = r;
                                break;
                            }
                        }
                    }
                }

                if (selectedColor >= 0)
                {
                    node.Color = selectedColor;
                }
                else
                {
                    node.Color = -3; // Spill
                    _logger.WriteLine($"[GraphColoringAllocator] ERROR: SPILL required for node {node.Value}.");
                }

                // Очистка буфера только для использованных индексов
                foreach (var neighbor in node.InterferencesList)
                {
                    if (neighbor.Color >= 0)
                    {
                        _colorsBuffer[neighbor.Color] = false;
                    }
                }
            }
        }
        
        /// <summary>
        /// Определяет, живет ли значение через инструкцию CALL.
        /// Такие переменные должны получать callee-saved регистры для минимизации spill/reload.
        /// </summary>
        private unsafe bool LivesAcrossCall(LLVMValueRef value)
        {
            // Проверяем, есть ли CALL на пути от определения value до любого его использования
            if (value.IsAInstruction == null) return false;
            
            LLVMBasicBlockRef valueBlock = LLVM.GetInstructionParent(value);
            
            // 1. Проверяем есть ли CALL в том же блоке после value
            for (LLVMValueRef instr = LLVM.GetNextInstruction(value); 
                 instr.Handle != IntPtr.Zero; 
                 instr = LLVM.GetNextInstruction(instr))
            {
                if (LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMCall)
                {
                    return true;
                }
            }
            
            // 2. Проверяем использования value - если любое использование в другом блоке,
            //    и между определением и использованием есть CALL, то value "lives across call"
            // Используем liveness info из _blockLiveIn - если value живёт на входе в какой-то блок,
            // значит он пересекает границу блока, и вероятно пересекает CALL
            if (_blockLiveIn != null)
            {
                foreach (var bb in _blockLiveIn.Keys)
                {
                    if (bb.Handle == valueBlock.Handle)
                        continue; // пропускаем блок определения
                    
                    if (_blockLiveIn[bb].Contains(value))
                    {
                        // value живёт на входе в другой блок - проверим есть ли там CALL
                        for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); 
                             instr.Handle != IntPtr.Zero; 
                             instr = LLVM.GetNextInstruction(instr))
                        {
                            if (LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMCall)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
            
            // 3. Проверяем непосредственные использования value
            for (LLVMUseRef use = LLVM.GetFirstUse(value); use.Handle != IntPtr.Zero; use = LLVM.GetNextUse(use))
            {
                LLVMValueRef user = LLVM.GetUser(use);
                if (user.IsAInstruction == null) continue;
                
                LLVMBasicBlockRef userBlock = LLVM.GetInstructionParent(user);
                if (userBlock.Handle != valueBlock.Handle)
                {
                    // Использование в другом блоке - если блок определения или любой блок между ними содержит CALL
                    // Консервативно: если есть использование в другом блоке, считаем что пересекает CALL
                    // (так как в функции с вызовами скорее всего есть CALL на пути)
                    if (HasAnyCallInFunction())
                    {
                        return true;
                    }
                }
            }
            
            return false;
        }
        
        private bool _hasAnyCallCached;
        private bool _hasAnyCallCacheValid;
        
        private unsafe bool HasAnyCallInFunction()
        {
            if (_hasAnyCallCacheValid) return _hasAnyCallCached;
            
            for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(_function); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
            {
                for (LLVMValueRef instr = LLVM.GetFirstInstruction(bb); instr.Handle != IntPtr.Zero; instr = LLVM.GetNextInstruction(instr))
                {
                    if (LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMCall)
                    {
                        _hasAnyCallCached = true;
                        _hasAnyCallCacheValid = true;
                        return true;
                    }
                }
            }
            
            _hasAnyCallCached = false;
            _hasAnyCallCacheValid = true;
            return false;
        }
        
        /// <summary>
        /// Compaction: переназначает регистры в плотный диапазон [0..N-1]
        /// где N = количество реально используемых регистров
        /// </summary>
        private AllocationResult CompactRegisters(AllocationResult result)
        {
            // Compaction работает на уровне result.Locations (LLVMValueRef → Register)
            // Нам нужно переназначить номера регистров в Locations
            
            // Собираем все используемые номера регистров
            HashSet<byte> usedRegs = new HashSet<byte>();
            
            foreach (var kvp in result.Locations)
            {
                if (kvp.Value is RegisterLocation regLoc)
                {
                    usedRegs.Add(regLoc.Register);
                }
            }
            
            // Создаем плотный маппинг: старый_регистр → новый_регистр
            var usedRegsList = usedRegs.OrderBy(r => r).ToList();
            Dictionary<byte, byte> remap = new Dictionary<byte, byte>();
            
            for (int i = 0; i < usedRegsList.Count; i++)
            {
                remap[usedRegsList[i]] = (byte)i;
            }
            
            // Создаем новый результат
            var compactedResult = new AllocationResult();
            compactedResult.MaxRegisterIndex = (byte)(usedRegsList.Count - 1);  // Новый max (плотный)
            compactedResult.PeakLiveRegisters = result.PeakLiveRegisters;
            compactedResult.TotalSpillSlotSize = result.TotalSpillSlotSize;
            
            // Применяем маппинг к Locations (добавляем в новый результат)
            foreach (var kvp in result.Locations)
            {
                if (kvp.Value is RegisterLocation regLoc && remap.ContainsKey(regLoc.Register))
                {
                    compactedResult.Locations[kvp.Key] = new RegisterLocation(remap[regLoc.Register]);
                }
                else
                {
                    compactedResult.Locations[kvp.Key] = kvp.Value; // SpillSlot или другое - не меняем
                }
            }
            
            return compactedResult;
        }
    }
}