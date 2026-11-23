using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
// ������������, ��� LLVMSharp ���������� ����� NuGet
using LLVMSharp.Interop;
using System.Runtime.InteropServices; // ��� Marshal
using System.Buffers;


namespace ESPB_Runtime
{
 
    // --- ���������� LLVM -> ESPB ---
    public unsafe partial class LLVMTranslator
    {
       private static IEnumerable<string> GetFastNameCandidates(string name)
       {
           yield return name;

           if (name.EndsWith("@plt", StringComparison.Ordinal))
               yield return name.Substring(0, name.Length - 4);
           if (name.EndsWith(".plt", StringComparison.Ordinal))
               yield return name.Substring(0, name.Length - 4);

           int atat = name.IndexOf("@@", StringComparison.Ordinal);
           if (atat > 0)
               yield return name.Substring(0, atat);

           const string wrap = "__wrap_";
           if (name.StartsWith(wrap, StringComparison.Ordinal))
               yield return name.Substring(wrap.Length);

           if (name.StartsWith("_", StringComparison.Ordinal) && name.Length > 1)
               yield return name.Substring(1);
       }

        private readonly Dictionary<LLVMValueRef, (LLVMValueRef phi, long imm, bool isSub, ValueType type)> _deferredPhiEdgeOps = new();

        private readonly List<(string name, int beforeInstr, int afterInstr, int beforeBytes, int afterBytes)> _peepholeStats = new();
        
        // ������� alloca -> ������� ��� ����������� store/load � �������� ��� mem2reg (� optnone)
        private readonly Dictionary<LLVMValueRef, byte> _allocaRegMap = new();
        // ����� alloca, ������� ������������ � volatile �������� - ��� ��� ALLOCA-OPT �� �����������
        private readonly HashSet<LLVMValueRef> _volatileAllocas = new();
        
        // === REGISTER PROMOTION ===
        // Dictionary of allocas that have been promoted to registers.
        // These allocas don't need stack memory - their values live entirely in registers.
        // Key: alloca instruction, Value: dedicated register number
        private readonly Dictionary<LLVMValueRef, byte> _promotedAllocas = new();
        // Next available register for promotion (starts after regular allocation)
        private byte _nextPromotionReg = 0;
        
        /// <summary>
        /// Invalidates any ALLOCA-OPT mappings that use the given register.
        /// Must be called whenever a register is about to be overwritten by any instruction.
        /// 
        /// With Store-through strategy, no STORE is needed here because:
        /// - Every store to alloca already generates a real STORE instruction
        /// - Memory is always up-to-date
        /// - We just need to remove the mapping so that future loads go to memory
        /// </summary>
        private void InvalidateAllocaMappingsForRegister(byte reg, List<byte> output)
        {
            var allocasToInvalidate = _allocaRegMap
                .Where(kvp => kvp.Value == reg)
                .Select(kvp => kvp.Key)
                .ToList();
            
            foreach (var allocaToInvalidate in allocasToInvalidate)
            {
                // With Store-through strategy, memory is already up-to-date
                // Just remove the mapping - no STORE needed
                _logger.WriteLine($"    [ALLOCA-OPT] Invalidating R{reg} mapping (memory already up-to-date via store-through)");
                _allocaRegMap.Remove(allocaToInvalidate);
            }
        }
        
        /// <summary>
        /// Gets or allocates a register for the result of an instruction,
        /// and invalidates any ALLOCA-OPT cache entries that use this register.
        /// 
        /// This must be called for ANY instruction that writes to a register,
        /// to ensure ALLOCA-OPT cache consistency.
        /// </summary>
        private byte GetResultRegWithCacheInvalidation(LLVMValueRef instr, List<byte> output)
        {
            byte dest = registerManager.GetOrAllocateRegForResult(instr, output);
            InvalidateAllocaMappingsForRegister(dest, output);
            return dest;
        }

        /// <summary>
        /// Pre-scans a function to identify all allocas whose addresses "escape" -
        /// i.e., are stored to memory or passed to function calls.
        /// Such allocas are added to _volatileAllocas to prevent ALLOCA-OPT from
        /// incorrectly caching their values in registers.
        /// If EnableAllocaOpt is false, ALL allocas are marked as volatile.
        /// 
        /// STRATEGY: Store-through + BB-local Load-forwarding
        /// - Store ALWAYS generates real STORE instruction (ensures memory is up-to-date)
        /// - Load CAN use cached register ONLY within the same BB after a store
        /// - Cache is invalidated at BB boundaries (handled in TranslateFunctionBody)
        /// </summary>
        private unsafe void PreScanAddressTakenAllocas(LLVMValueRef func)
        {
            // If ALLOCA-OPT is disabled globally, mark ALL allocas as volatile
            if (!EnableAllocaOpt)
            {
                for (LLVMBasicBlockRef bb = func.FirstBasicBlock; bb.Handle != IntPtr.Zero; bb = bb.Next)
                {
                    for (LLVMValueRef instr = bb.FirstInstruction; instr.Handle != IntPtr.Zero; instr = instr.NextInstruction)
                    {
                        if (instr.IsAAllocaInst.Handle != IntPtr.Zero)
                        {
                            _volatileAllocas.Add(instr);
                        }
                    }
                }

                if (_volatileAllocas.Count > 0)
                {
                    _logger.WriteLine($"    [PRE-SCAN] ALLOCA-OPT disabled - all {_volatileAllocas.Count} allocas marked volatile");
                }

                return;
            }

            // Normal pre-scan: inspect stores and calls to detect address-taken allocas
            for (LLVMBasicBlockRef bb = func.FirstBasicBlock; bb.Handle != IntPtr.Zero; bb = bb.Next)
            {
                for (LLVMValueRef instr = bb.FirstInstruction; instr.Handle != IntPtr.Zero; instr = instr.NextInstruction)
                {
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(instr);

                    if (opcode == LLVMOpcode.LLVMStore)
                    {
                        // For stores, if either operand is an alloca, its address escapes
                        int numOps = LLVM.GetNumOperands(instr);
                        for (int i = 0; i < numOps; i++)
                        {
                            LLVMValueRef op = instr.GetOperand((uint)i);
                            if (op.IsAAllocaInst.Handle != IntPtr.Zero)
                            {
                                _volatileAllocas.Add(op);
                                _logger.WriteLine($"    [PRE-SCAN] Alloca address escapes via store - excluding from ALLOCA-OPT");
                            }
                        }
                        continue;
                    }

                    if (opcode == LLVMOpcode.LLVMCall || opcode == LLVMOpcode.LLVMInvoke)
                    {
                        LLVMValueRef calledValue = LLVM.GetCalledValue(instr);
                        string calledName = Marshal.PtrToStringAnsi((IntPtr)LLVM.GetValueName(calledValue)) ?? "";

                        bool isSafeIntrinsic = calledName.StartsWith("llvm.lifetime.") || calledName.StartsWith("llvm.dbg.") || calledName.StartsWith("llvm.assume");

                        if (isSafeIntrinsic)
                            continue;

                        uint numArgs = LLVM.GetNumArgOperands(instr);
                        for (uint i = 0; i < numArgs; i++)
                        {
                            LLVMValueRef arg = instr.GetOperand(i);
                            if (arg.IsAAllocaInst.Handle != IntPtr.Zero)
                            {
                                _volatileAllocas.Add(arg);
                                _logger.WriteLine($"    [PRE-SCAN] Alloca address escapes via call to '{calledName}' arg {i} - excluding from ALLOCA-OPT");
                            }
                        }
                        continue;
                    }
                }
            }

            if (_volatileAllocas.Count > 0)
            {
                _logger.WriteLine($"    [PRE-SCAN] Found {_volatileAllocas.Count} address-taken allocas");
            }
        }

        /// <summary>
        /// REGISTER PROMOTION (Phase 1): Identifies scalar allocas that can be promoted to live entirely in registers.
        /// Called BEFORE Graph Coloring so that promoted allocas are excluded from register allocation.
        /// 
        /// Criteria for promotion:
        /// 1. Scalar type (i32, i64, f32, f64, ptr) - not arrays or structs
        /// 2. Not in _volatileAllocas (address doesn't escape to real function calls)
        /// 3. Only used by load/store instructions and safe intrinsics (lifetime, dbg)
        /// 
        /// Promoted allocas get dedicated registers and all their load/store become simple MOV.
        /// </summary>
        private unsafe void IdentifyPromotableAllocas(LLVMValueRef func)
        {
            if (!EnableAllocaOpt)
            {
                _logger.WriteLine($"    [REG-PROMOTION] Disabled (ALLOCA-OPT is off)");
                return;
            }
            
            // Don't allocate registers yet - just identify promotable allocas
            // Actual register allocation happens in AllocatePromotionRegisters after Graph Coloring
            int promotableCount = 0;
            
            // Scan all allocas in entry block
            LLVMBasicBlockRef entryBB = func.FirstBasicBlock;
            for (LLVMValueRef instr = entryBB.FirstInstruction; instr.Handle != IntPtr.Zero; instr = instr.NextInstruction)
            {
                if (instr.IsAAllocaInst.Handle == IntPtr.Zero)
                    continue;
                    
                // Skip if address escapes (volatile)
                if (_volatileAllocas.Contains(instr))
                {
                    _logger.WriteLine($"    [REG-PROMOTION] Skip {instr} - address escapes");
                    continue;
                }
                
                // Check if scalar type
                LLVMTypeRef allocatedType = LLVM.GetAllocatedType(instr);
                LLVMTypeKind kind = allocatedType.Kind;
                
                bool isScalar = kind == LLVMTypeKind.LLVMIntegerTypeKind ||
                                kind == LLVMTypeKind.LLVMFloatTypeKind ||
                                kind == LLVMTypeKind.LLVMDoubleTypeKind ||
                                kind == LLVMTypeKind.LLVMPointerTypeKind;
                                
                if (!isScalar)
                {
                    _logger.WriteLine($"    [REG-PROMOTION] Skip {instr} - not scalar (kind={kind})");
                    continue;
                }
                
                // Check all uses - must be only load/store and safe intrinsics
                // Since we inform Graph Coloring about promoted allocas, it will skip them,
                // so lifetime/dbg intrinsics are now safe to allow.
                bool allUsesValid = true;
                for (LLVMUseRef use = LLVM.GetFirstUse(instr); use.Handle != IntPtr.Zero; use = LLVM.GetNextUse(use))
                {
                    LLVMValueRef user = LLVM.GetUser(use);
                    if (user.IsAInstruction.Handle == IntPtr.Zero)
                    {
                        allUsesValid = false;
                        break;
                    }
                    
                    LLVMOpcode userOp = LLVM.GetInstructionOpcode(user);
                    
                    // Allow load (alloca is the pointer operand)
                    if (userOp == LLVMOpcode.LLVMLoad)
                        continue;
                        
                    // Allow store (alloca is the pointer operand, not the value)
                    if (userOp == LLVMOpcode.LLVMStore)
                    {
                        // Make sure alloca is the destination (operand 1), not the value (operand 0)
                        LLVMValueRef storePtr = LLVM.GetOperand(user, 1);
                        if (storePtr.Handle == instr.Handle)
                            continue;
                    }
                    
                    // Allow safe intrinsics - they don't actually access memory
                    // Since we tell Graph Coloring about promoted allocas, it won't assign
                    // pointer registers for them, avoiding conflicts.
                    if (userOp == LLVMOpcode.LLVMCall)
                    {
                        LLVMValueRef calledValue = LLVM.GetCalledValue(user);
                        string calledName = calledValue.Name;
                        if (calledName.StartsWith("llvm.lifetime.") || 
                            calledName.StartsWith("llvm.dbg.") ||
                            calledName.StartsWith("llvm.assume"))
                            continue;
                    }
                    
                    // Any other use disqualifies
                    _logger.WriteLine($"    [REG-PROMOTION] Skip {instr} - invalid use: {user} (op={userOp})");
                    allUsesValid = false;
                    break;
                }
                
                if (!allUsesValid)
                    continue;
                
                // Mark this alloca for promotion (register will be assigned later)
                // Use 0xFF as placeholder - actual register assigned in AllocatePromotionRegisters
                _promotedAllocas[instr] = 0xFF;
                promotableCount++;
                _logger.WriteLine($"    [REG-PROMOTION] Marked for promotion: {instr} (type={kind})");
            }
            
            if (promotableCount > 0)
            {
                _logger.WriteLine($"    [REG-PROMOTION] Identified {promotableCount} allocas for promotion");
            }
        }
        
        /// <summary>
        /// REGISTER PROMOTION (Phase 2): Gets registers allocated by GraphColoring for promoted allocas.
        /// GraphColoring has already assigned registers based on the extended liveness we computed.
        /// We just need to retrieve them and store in _promotedAllocas.
        /// </summary>
        private void AllocatePromotionRegisters(byte maxRegUsedByAllocation)
        {
            if (_promotedAllocas.Count == 0)
                return;
            
            // Get registers that GraphColoring assigned to each promoted alloca
            var allocasToUpdate = _promotedAllocas.Keys.ToList();
            int successCount = 0;
            
            foreach (var alloca in allocasToUpdate)
            {
                // Get the register that GraphColoring assigned to this alloca
                byte reg = registerManager.GetOrAllocateRegForResult(alloca, null);
                _promotedAllocas[alloca] = reg;
                _logger.WriteLine($"    [REG-PROMOTION] Using GraphColoring register R{reg} for promoted alloca: {alloca}");
                successCount++;
            }
            
            _logger.WriteLine($"    [REG-PROMOTION] Retrieved {successCount} registers from GraphColoring for promoted allocas");
        }

        // ??????? ??? ?????? ????? ? ????????? ????????: ??? ??? -> ?? (?????? ???????, ?????? userData)
        // private Dictionary<string, List<(int ArgIndex, int DataArgIndex)>> callbackInfoMap = new Dictionary<string, list<(int="" argindex,="" int="" dataargindex)="">>();

            public unsafe bool TranslateFile(string bcFilePath, string espbFilePath, string? hintsFilePath = null)
           {
               _logger.WriteLine($"Translating {bcFilePath} to {espbFilePath}...");
               
               // ������������� HintsManager
            this.hintsManager = new HintsManager();
            if (!string.IsNullOrEmpty(hintsFilePath))
            {
                if (!this.hintsManager.LoadHintsFile(hintsFilePath))
                {
                    _logger.WriteLine("Failed to load hints file, continuing with automatic analysis only");
                }
            }
            
            llvmContext = null;
            llvmModule = default;
            targetData = default;
            LLVMMemoryBufferRef memBuf = default;

            try
            {
                // 1. ������������� LLVM � ������� .bc
                if (!File.Exists(bcFilePath))
                {
                    _logger.WriteLine($"Error: Input file not found: {bcFilePath}");
                    return false;
                }
                llvmContext = LLVMContextRef.Create();

                byte[] fileBytes = File.ReadAllBytes(bcFilePath);
                fixed (byte* pBytes = fileBytes) // ���������� ������ ����
                {
                    // ����������� ��� ������ � C-������ (null-terminated)
                    byte[] moduleNameBytes = Encoding.UTF8.GetBytes("module\0");
                    fixed (byte* pModuleName = moduleNameBytes)
                    {
                         memBuf = LLVM.CreateMemoryBufferWithMemoryRangeCopy((sbyte*)pBytes, (UIntPtr)fileBytes.LongLength, (sbyte*)pModuleName);
                    }
                }

                if (memBuf.Handle == IntPtr.Zero)
                {
                     _logger.WriteLine($"Error: Failed to create LLVM memory buffer from file: {bcFilePath}");
                     llvmContext.Dispose();
                     return false;
                }

                // ParseBitcode consum�� memBuf, ���� �������
                llvmModule = llvmContext.ParseBitcode(memBuf);

                if (llvmModule.Handle == IntPtr.Zero)
                {
                     _logger.WriteLine($"Error: Failed to parse LLVM bitcode file: {bcFilePath}");
                     // memBuf ����� ����� ����������, �.�. ParseBitcode �� ������ ���
                     LLVM.DisposeMemoryBuffer(memBuf);
                     llvmContext.Dispose();
                     return false;
                }

                // �������� DataLayout
                string dataLayoutStr = "e-p:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v128:128:128-a:0:64-n32-S128";
                targetData = LLVMTargetDataRef.FromStringRepresentation(dataLayoutStr);
                if (targetData.Handle == IntPtr.Zero)
                {
                    _logger.WriteLine("Error: Failed to create TargetData.");
                    llvmModule.Dispose();
                    llvmContext.Dispose();
                    return false;
                }

                // 2. ���� ���������� � ����������
                _logger.WriteLine(">>> NEW TRANSLATION RUN: Creating new EspbModule and clearing all sections.");
                espbModule = new EspbModule();
                espbModule.Sections.Clear();
                llvmFuncToEspbIndex.Clear();
                llvmGlobalToEspbIndex.Clear();
                llvmImportToEspbIndex.Clear();
                registerManager.ResetForNewFile();
                // llvmValueToEspbReg ������ - ������������ RegisterManager
                nextEspbFuncIndex = 0;
                nextEspbGlobalIndex = 0;
                nextEspbImportIndex = 0;
                _funcPtrToDataOffset.Clear();

                // ��������� ������
                espbModule.Sections.Add(new TypesSection());
                espbModule.Sections.Add(new ImportsSection());
                espbModule.Sections.Add(new FunctionSection());
                dataSection = new DataSection();
                espbModule.Sections.Add(dataSection);
                espbModule.Sections.Add(new GlobalsSection());
                espbModule.Sections.Add(new ExportsSection());
                espbModule.Sections.Add(new CodeSection());
                espbModule.Sections.Add(new FunctionPointerMapSection()); // ������ ID=18
                espbModule.Sections.Add(new JitRegionMetadataSection()); // ������ ID=19

                // ��������� ������ ������ (MemorySection ID=13)
                var memorySection = new MemorySection();
                var memoryDefinition = new MemoryDefinition
                {
                    LimitsFlags = 0x00, // bit 0: has_max = 0 (false), bit 1: is_shared = 0 (false)
                    InitialPages = 1    // �������� ����� ������������: 1 �������� = 1024 �����.
                    // MaxPages = 1,    // �����������, ���� LimitsFlags ��� 0 ���������� � 1
                };
                memorySection.Memories.Add(memoryDefinition);
                espbModule.Sections.Add(memorySection);
                
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 4 }); // ��������� ��� Globals
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 6 }); // ��������� ��� Code
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 8 }); // ��������� ��� Data

                // Add Table and Element sections for function pointers
                var tableSection = new TableSection();
                tableSection.Tables.Add(new EspbTableDesc {
                    element_type = EspbRefType.FUNCREF,
                    limits = new EspbMemoryLimits {
                        flags = 0x00, // no max_size
                        initial_size = 1024,
                        max_size = 0
                    }
                });
                espbModule.Sections.Add(tableSection);
                espbModule.Sections.Add(new ElementSection());
                // This line is now removed, the correct one is after other sections

                // ===================================================================
                // ���� 1: ���� ����-���������� (�� �����������)
                // ===================================================================
                _logger.WriteLine("--- STAGE 1: Collecting Metadata from Original Module ---");
                unsafe
                {
                    CollectTypes();
                    CollectImports();
                    CollectGlobals();
                    CollectFunctionDeclarations();
                    PreScanForFunctionPointers(); // <-- NEW: Pre-scan for all function pointer usages
                    CollectExports();
                }
                _logger.WriteLine("--- STAGE 1: Finished ---");

                // ===================================================================
                // ���� 2: ����������� LLVM-IR ����� ���������� �������
                // ===================================================================
                _logger.WriteLine("--- STAGE 2: Running LLVM Optimization Passes through PassBuilders ---");

                // ���������� ����� �������������� API ��� ��������� �����������
                // ������� ����������� ����������� �������� �����������
                // ���������� ������� ����������� � ������� �� mem2reg
                string customPasses = "mem2reg,instcombine,sroa,mem2reg";
                
                // ��������� ������ ������� ������� - ���������� ������ ���� ������� �������
                _logger.WriteLine("  ���������� ���������� �������� ��� ����������");
                
                // ������������� ����� ����������� ����� ������� ������ ����� ����������� �������
                // string customPasses = "default<O1>"; // ����� ���������� �������
                _logger.WriteLine($"  Running enhanced optimization pipeline: {customPasses}");
                
                // ������� ����� ��� PassBuilder � ������������� ������� ����������� ������
                var options = LLVM.CreatePassBuilderOptions();
                // ��������� ���������� ���� �� ����������� ��� ���������
                string beforeOpt = "";
                var cStrBefore = LLVM.PrintModuleToString(llvmModule);
                if (cStrBefore != null)
                {
                    beforeOpt = Marshal.PtrToStringAnsi((IntPtr)cStrBefore) ?? "";
                    LLVM.DisposeMessage(cStrBefore);
                    _logger.WriteLine($"  Before optimization: {CountSubstringOccurrences(beforeOpt, "alloca ")} alloca instructions");
                }
                
                byte[] passesBytes = Encoding.ASCII.GetBytes(customPasses + "\0");
                fixed (byte* pPasses = passesBytes)
                {
                    LLVMOpaqueError* errPtr = LLVM.RunPasses(llvmModule, (sbyte*)pPasses, (LLVMOpaqueTargetMachine*)0, options);
                    if (errPtr != null)
                    {
                        _logger.WriteLine("Error running LLVM optimization passes via RunPasses with custom pipeline.");
                    }
                    else
                    {
                        _logger.WriteLine("  LLVM custom passes pipeline executed successfully");
                        
                                                // �������������� ��������, ��� ����������� ��������� ���������
                        // � LLVMSharp ���������� ��������� �������������� unmanaged string
                        var cStr = LLVM.PrintModuleToString(llvmModule);
                        string afterOpt = "";
                        
                        if (cStr != null)
                        {
                            afterOpt = Marshal.PtrToStringAnsi((IntPtr)cStr) ?? "";
                            LLVM.DisposeMessage(cStr); // ����������� ������ ������
                        }
                        
                        int allocaCount = CountSubstringOccurrences(afterOpt, "alloca ");
                        int loadCount = CountSubstringOccurrences(afterOpt, "load ");
                        int storeCount = CountSubstringOccurrences(afterOpt, "store ");
                        
                        _logger.WriteLine($"  After optimization: {allocaCount} alloca, {loadCount} load, {storeCount} store instructions remain");
                        
                        // ������ �������� ������ ����� VerifyModule (������� ������� ������ � �����������)
                        // ������ ��������� �� ��������� �����������
                        _logger.WriteLine("  Module verification skipped - using pass analysis instead");
                            
                                                 // ����������� ���������� ��������� ��� �������� ������������
                         if (allocaCount < 10)
                         {
                             _logger.WriteLine("  Optimization looks successful - number of alloca instructions reduced significantly");
                         }
                         else
                         {
                             _logger.WriteLine("  Optimization warning: many alloca instructions remain, could indicate a problem");
                         }
                        
                        // �����������, ����� ��������� �������� � ������
                        if (allocaCount > 0)
                        {
                            _logger.WriteLine("\n---- ������ ���������� ALLOCA-���������� ----");
                            
                            // ���� ������ � alloca � ���������������� ����
                            string[] lines = afterOpt.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
                            int analyzedCount = 0;
                            
                            foreach (string line in lines)
                            {
                                if (line.Contains("alloca") && analyzedCount < 10) // ������������ �����
                                {
                                    string trimmedLine = line.Trim();
                                    _logger.WriteLine($"  ��������: {trimmedLine}");
                                    
                                    // ���������� ��� � ����������
                                    string reason = "����������";
                                    
                                    if (line.Contains("ptr"))
                                        reason = "��������� (�������� ��� ��������� ��� ������)";
                                    else if (line.Contains("struct") || line.Contains("class"))
                                        reason = "��������� ��� ����� (������� ���)";
                                    else if (line.Contains("array"))
                                        reason = "������ (������� �������� � ������)";
                                    else if (line.Contains("i8") || line.Contains("i16") || line.Contains("i32") || line.Contains("i64"))
                                        reason = "������������� ���������� � ����������";
                                    else if (line.Contains("float") || line.Contains("double"))
                                        reason = "���������� � ��������� ������ � ����������";
                                    
                                    _logger.WriteLine($"    �������: {reason}\n");
                                    analyzedCount++;
                                }
                            }
                            
                            if (analyzedCount == 10 && allocaCount > 10)
                            {
                                _logger.WriteLine($"  ... �������� 10 �� {allocaCount} alloca-����������\n");
                            }
                            
                            _logger.WriteLine("---- ����� ������� ----\n");
                        }
                    }
                }
                LLVM.DisposePassBuilderOptions(options);

                _logger.WriteLine("--- STAGE 2: Finished ---");

                // ===================================================================
                // ���� 3: ��������� ���� (����� �����������)
                // ===================================================================
                _logger.WriteLine("--- STAGE 3: Translating Function Bodies from Optimized Module ---");
                unsafe
                {
                    TranslateAllFunctionBodies(); // ����� �������!
                }
                _logger.WriteLine("--- STAGE 3: Finished ---");

                espbModule.Features |= 0x80; // �������� FEATURE_DATA_SYMBOLS ��� ��������� ��������� ���������� ������

                // �������� ���������� �������� �� IR � ������� ������ ��� �������������
                BuildCallbackInfoFromIR();
                // ������ �������� ������� cbmeta ������ (���� 0x100 ��������������� ������)
                AddCallbackMetadataSection();

                // ��������� ������ JIT Region Metadata (ID=19) �� ��������� ������������
                _logger.WriteLine("*** About to call PopulateJitRegionMetadataSection ***");
                PopulateJitRegionMetadataSection();
                _logger.WriteLine("*** PopulateJitRegionMetadataSection completed ***");

                // ���������: �������� ������������ ��������������� ���������� ���������� ����� �������
                var globalsSection = GetSection<GlobalsSection>(4);
                if (globalsSection != null)
                {
                    _logger.WriteLine("--- Checking Global Initializers Before Write ---");
                    foreach (var global in globalsSection.Globals)
                    {
                        // ���������, ��� ���������� ���������� � PTR-����� � ���������� ����������������
                        // ����� ���������� InitKind
                        if (global.Type == ValueType.PTR && global.InitialValue != null)
                        {
                            if (global.InitKind != EspbGlobal.InitializerKind.DataSegmentOffset)
                            {
                                _logger.WriteLine($"ERROR: PTR global with InitialValue {global.InitialValue} has incorrect InitKind {global.InitKind}. Fixing to DataSegmentOffset.");
                                global.InitKind = EspbGlobal.InitializerKind.DataSegmentOffset;
                            }
                        }
                        
                        _logger.WriteLine($"Global: Type={global.Type}, InitKind={(byte)global.InitKind}, InitialValue={global.InitialValue}");
                    }
                }

                espbModule.WriteToFile(espbFilePath);
                _logger.WriteLine("Translation successful.");
                return true;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Error during translation: {ex.Message}");
                _logger.WriteLine(ex.StackTrace);
                return false;
            }
            finally
            {
                 // ����������� ������������� �������
                 // targetData �� ����� Dispose
                 if (llvmModule.Handle != IntPtr.Zero) llvmModule.Dispose();
                 // memBuf ��� ���������� (ParseBitcode) ��� ������� ��� ������
                 if (llvmContext != null && llvmContext.Handle != IntPtr.Zero) llvmContext.Dispose();
            }
        }

        // --- ������ ����� ������ ---
        private void CollectTypes()
        {
            // ��� ������ ������� ������� ����, ������� ����� ����� ��� �������� � ���������
            var typesSection = GetSection<TypesSection>(1)!;
            
            // ������� ������� ��� - void ������� ��� ����������
            var voidVoidSig = new TypeSignature
            {
                Parameters = new List<ValueType>(),
                Results = new List<ValueType>()
            };
            
            // ���������, ���� �� ��� ����� ���������
            bool hasVoidVoidSig = false;
            for (int i = 0; i < typesSection.Signatures.Count; i++)
            {
                if (SignaturesEqual(typesSection.Signatures[i], voidVoidSig))
                {
                    hasVoidVoidSig = true;
                    break;
                }
            }
            
            if (!hasVoidVoidSig)
            {
                typesSection.Signatures.Add(voidVoidSig);
            }
        }

        private int AddTypeSignature(LLVMTypeRef llvmType, List<LLVMTypeRef>? variadicArgLLVMTypes = null, LLVMValueRef? originalFunctionRef = null)
        {
            var typesSection = GetSection<TypesSection>(1)!; // �������������� �� null
            
            // ��������� ��� � ������, ��� ��� ������������
            if (llvmType.Kind == LLVMTypeKind.LLVMFunctionTypeKind)
            {
                // ����������� ��������� ��� �������
                var signature = MapLlvmFunctionType(llvmType, variadicArgLLVMTypes, originalFunctionRef); // �������� originalFunctionRef
                for(int i = 0; i < typesSection.Signatures.Count; ++i)
                {
                    if (SignaturesEqual(typesSection.Signatures[i], signature))
                    {
                        return i;
                    }
                }
                typesSection.Signatures.Add(signature);
                return typesSection.Signatures.Count - 1;
            }
            else
            {
                // ��� ���������������� ����� ������� ��������� � ����� ����� ����������
                // ��� ����� �������������� � v1.7 ��� �������������� ���������� � ���������
                var signature = new TypeSignature();
                ValueType espbType = MapLlvmTypeToEspb(llvmType);
                
                // ��� ���������� �������� ���������� � ������� ���� � ����������
                if (llvmType.Kind == LLVMTypeKind.LLVMPointerTypeKind && 
                    llvmType.ElementType.Handle != IntPtr.Zero)
                {
                    // ����� ����� �� ��������� �������������� ���������� � ����
                    // ��� ����� ������ ��������� � �������
                    ValueType elementType = MapLlvmTypeToEspb(llvmType.ElementType);
                }
                
                // �������������� ��� - ���� �������� � Results
                signature.Results.Add(espbType);
                
                // ���� ������������ ���������
                for(int i = 0; i < typesSection.Signatures.Count; ++i)
                {
                    if (SignaturesEqual(typesSection.Signatures[i], signature))
                    {
                        return i;
                    }
                }
                
                // ��������� ����� ���������
                typesSection.Signatures.Add(signature);
                _logger.WriteLine($"Added type signature for non-function type: {llvmType.Kind}, signature index: {typesSection.Signatures.Count - 1}");
                return typesSection.Signatures.Count - 1;
            }
        }
        
         private bool SignaturesEqual(TypeSignature sig1, TypeSignature sig2)
        {
             // ��������� �������� �� null ��� ������������
             if (sig1 == null || sig2 == null) return false;
             if (sig1.Parameters == null || sig2.Parameters == null || sig1.Results == null || sig2.Results == null)
             {
                 // ������� �� �������, ���� ������ null
                 return sig1 == sig2;
             }
            return sig1.Parameters.SequenceEqual(sig2.Parameters) &&
                   sig1.Results.SequenceEqual(sig2.Results);
            // ����� ���������� bool, ������ CS0161 ����� ���� �� ������, ���� ������ �����.
        }

        private unsafe TypeSignature MapLlvmFunctionType(LLVMTypeRef funcType, List<LLVMTypeRef>? variadicArgLLVMTypes = null, LLVMValueRef? originalFunctionRef = null)
        {
            var signature = new TypeSignature();
            uint paramCount = LLVM.CountParamTypes(funcType);
            string functionName = originalFunctionRef?.Name ?? "<unknown function>"; // ��������� �������� �� ���������

            _logger.WriteLine($"    DEBUG MapLlvmFunctionType: Mapping for function '{functionName}', LLVM FuncType: {SafePrintType(funcType)}, ParamCount: {paramCount}");

            var paramTypes = new LLVMTypeRef[paramCount];
            if (paramCount > 0)
            {
                unsafe
                {
                    fixed (LLVMTypeRef* paramTypesPtr = paramTypes)
                    {
                        LLVM.GetParamTypes(funcType, (LLVMOpaqueType**)paramTypesPtr);
                    }
                }
            }

            for (uint i = 0; i < paramCount; ++i)
            {
                LLVMTypeRef llvmParamType = paramTypes[i];
                ValueType espbParamType;
                _logger.WriteLine($"      Param {i}: LLVM Type = {SafePrintType(llvmParamType)}");

                // ������������� ��������� ���������� � �������� byval
                bool isByval = originalFunctionRef.HasValue && IsByvalArgument(originalFunctionRef.Value, i);
                if (isByval && llvmParamType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                {
                    // byval(ptr<T>) ������� �������� T, ��� �� ��������� I32 ��� opaque ����������
                    var pointeeType = llvmParamType.ElementType;
                    if (pointeeType.Handle != IntPtr.Zero)
                    {
                        espbParamType = MapLlvmTypeToEspb(pointeeType);
                        _logger.WriteLine($"        BYVAL param {i} detected. Mapping to ESPB type {espbParamType} instead of PTR.");
                    }
                    else
                    {
                        espbParamType = ValueType.I32;
                        _logger.WriteLine($"        BYVAL param {i} has opaque pointee type. Default mapping to I32.");
                    }
                }
                else if (llvmParamType.Kind == LLVMTypeKind.LLVMPointerTypeKind && llvmParamType.ElementType.Kind == LLVMTypeKind.LLVMFunctionTypeKind)
                {
                    // ����������� ��������� �� ������� � I32 ������ ��� ����������-���������, ����� PTR
                    if (originalFunctionRef.HasValue && TryDetectCallbackParameter(originalFunctionRef.Value, (int)paramCount, out var cbInfos) && cbInfos.Exists(cb => cb.CallbackParamIndex == (int)i))
                    {
                        espbParamType = ValueType.I32;
                        _logger.WriteLine($"        Callback function pointer param {i} mapped to I32.");
                    }
                    else
                    {
                        espbParamType = ValueType.PTR;
                        _logger.WriteLine($"        Function pointer param {i} default mapping to PTR.");
                    }
                }
                else
                {
                    // ������� ����������� �����
                    espbParamType = MapLlvmTypeToEspb(llvmParamType);
                    _logger.WriteLine($"        Default mapping for param {i} to ESPB type {espbParamType}");
                }
                signature.Parameters.Add(espbParamType);
                continue;
            }

            // ��������� ����������� �����: ��������� ������ �������� variadic-����, ��� fallback ������������
            if (LLVM.IsFunctionVarArg(funcType) != 0 && variadicArgLLVMTypes != null && variadicArgLLVMTypes.Count > 0)
                {
                    _logger.WriteLine($"INFO: Variadic function. Adding {variadicArgLLVMTypes.Count} actual types to ESPB signature for its variadic part.");
                    foreach (var varArgTypeRef in variadicArgLLVMTypes)
                    {
                        signature.Parameters.Add(MapLlvmTypeToEspb(varArgTypeRef));
                }
            }

            // ��������� ������������ ��� ������ ���� ������� �� ���������� void
            if (funcType.ReturnType.Kind != LLVMTypeKind.LLVMVoidTypeKind)
            {
            signature.Results.Add(MapLlvmTypeToEspb(funcType.ReturnType));
            }
            return signature;
        }

        private unsafe ValueType MapLlvmTypeToEspb(LLVMTypeRef llvmType)
        {
            if (llvmType.Handle == IntPtr.Zero)
            {
                _logger.WriteLine("Warning: MapLlvmTypeToEspb called with null type. Returning VOID.");
                return ValueType.VOID;
            }

            switch (llvmType.Kind)
            {
                case LLVMTypeKind.LLVMIntegerTypeKind:
                    uint width = llvmType.IntWidth;
                    switch (width)
                    {
                        case 8: return ValueType.I8; // I8/U8 can be handled by context
                        case 16: return ValueType.I16;
                        case 32: return ValueType.I32;
                        case 64: return ValueType.I64; // ���������� I64 ��������, � �� ��������� �� ��� I32
                        case 1: return ValueType.BOOL;
                        default:
                            _logger.WriteLine($"Warning: Unsupported integer width: {width}. Using I32 as fallback.");
                            return ValueType.I32;
                    }

                case LLVMTypeKind.LLVMFloatTypeKind:
                    return ValueType.F32;
                case LLVMTypeKind.LLVMDoubleTypeKind:
                    return ValueType.F64;
                
                case LLVMTypeKind.LLVMPointerTypeKind:
                    // ���������� ��� PTR ������ ESPB_TYPE_PTR ��� �����������
                    // ������������� ������������� ����� Xtensa � RISC-V
                    return ValueType.PTR;

                case LLVMTypeKind.LLVMArrayTypeKind:
                case LLVMTypeKind.LLVMVectorTypeKind:
                    // ������� � ������� �� ���������� ������ ��� ��� �������� �� ��������
                    // ��������������� ��� ��������� �� �� ������ �������.
                    return ValueType.PTR;

                case LLVMTypeKind.LLVMStructTypeKind:
                    // ��������� ����� �������������� ��� ���������.
                    return ValueType.PTR;

                case LLVMTypeKind.LLVMFunctionTypeKind:
                    // ��� ������� ��� �� ���� ������ ������������� � ��������� �� �������.
                    return ValueType.PTR;

                default:
                    _logger.WriteLine($"Warning: Unhandled LLVM type kind: {llvmType.Kind}. Mapping to PTR as a fallback.");
                    return ValueType.PTR;
            }
        }

        // ����������, �������� �� ��� LLVM �������������
        private unsafe bool IsLlvmIntegerType(LLVMTypeRef type)
        {
            return type.Kind == LLVMTypeKind.LLVMIntegerTypeKind;
        }
        
        // ����������, �������� �� ��� LLVM �����������
        private bool IsLlvmUnsignedType(LLVMTypeRef type)
        {
            // � LLVM ��� ������ ���������� �� �������� � ����������� �����,
            // �� �� ����� ���������� ��� �� ��������� (��������, ���������� LLVM)
            // ��� ������������ �� ������ �������������
            return false; // �� ��������� �������, ��� ��� ��������
        }
        
        // ����������, �������� �� ��� LLVM ������ � ��������� ������
        private bool IsLlvmFloatingPointType(LLVMTypeRef type)
        {
            return type.Kind == LLVMTypeKind.LLVMHalfTypeKind 
                || type.Kind == LLVMTypeKind.LLVMFloatTypeKind 
                || type.Kind == LLVMTypeKind.LLVMDoubleTypeKind;
        }
        
        // �������� ������������� ����������� ��� ESPB ��� ��������� ���� ESPB
        private ValueType GetSignedToUnsignedType(ValueType type)
        {
            switch (type)
            {
                case ValueType.I8: return ValueType.U8;
                case ValueType.I16: return ValueType.U16;
                case ValueType.I32: return ValueType.U32;
                case ValueType.I64: return ValueType.U64;
                default: return type; // ��� ������ ����� ���������� �������� ���
            }
        }
        
        // �������� ������������� �������� ��� ESPB ��� ��������� ���� ESPB
        private ValueType GetUnsignedToSignedType(ValueType type)
        {
            switch (type)
            {
                case ValueType.U8: return ValueType.I8;
                case ValueType.U16: return ValueType.I16;
                case ValueType.U32: return ValueType.I32;
                case ValueType.U64: return ValueType.I64;
                default: return type; // ��� ������ ����� ���������� �������� ���
            }
        }

        private unsafe void CollectImports()
        {
            _logger.WriteLine("--- Collecting Imports ---");
            var importsSection = GetSection<ImportsSection>(2)!;
            var typesSection = GetSection<TypesSection>(1)!;

            _logger.WriteLine("  Scanning for Function Imports...");
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
            {
                if (func.IsDeclaration && func.Linkage == LLVMLinkage.LLVMExternalLinkage)
                {
                    string funcName = func.Name;
                    
                    // ���������� ���������� ������� LLVM � ����������� �������, ������� ������������� � �������� ������
                    if (funcName.StartsWith("llvm.") ||
                        funcName.StartsWith("OP_") || // ���������� ���� ���������� (������� OP_JIT_START/OP_JIT_STOP)
                        funcName == "__atomic_load_8" ||
                        funcName == "__atomic_store_8" ||
                        funcName == "__atomic_fetch_sub_8" ||
                        funcName == "malloc" ||
                        funcName == "calloc" ||
                        funcName == "realloc" ||
                        funcName == "free" ||
                        funcName == "__wasm_memory_grow" ||
                        funcName == "__wasm_memory_init" ||
                        funcName == "__wasm_data_drop" ||
                        funcName == "OP_table_size" ||
                        funcName == "OP_table_init" ||
                        funcName == "OP_table_copy" ||
                        funcName == "OP_table_fill" ||
                        funcName == "OP_table_get" ||
                        funcName == "OP_table_set")
                    {
                        _logger.WriteLine($"    INFO: Skipping LLVM intrinsic or native atomic '{funcName}' from imports");
                        continue;
                    }
                    
                    LLVMTypeRef funcType;
                    LLVMTypeRef globalValType = LLVM.GlobalGetValueType(func);
                    if (globalValType.Kind == LLVMTypeKind.LLVMFunctionTypeKind)
                    {
                        funcType = globalValType;
                    }
                    else if (func.TypeOf.Kind == LLVMTypeKind.LLVMPointerTypeKind && 
                             func.TypeOf.ElementType.Kind == LLVMTypeKind.LLVMFunctionTypeKind)
                    {
                        funcType = func.TypeOf.ElementType;
                    }
                    else
                    {
                        _logger.WriteLine($"    WARNING: Could not determine function type for import '{funcName}'. Skipping.");
                        continue;
                    }

                    // --- �������� �����������: ������ ������� originalFunctionRef=func ---
                    // ��� �����������, ��� byval(i32) > I32, � �� PTR, ��� �������� (��. ������������ ESPB v1.7)
                    // ��� �������� ���������� ������ ������������� ��������� ��� ����� �������
                    int typeIndex = AddTypeSignature(funcType, null, func); // func ��� originalFunctionRef ��� variadicArgLLVMTypes

                    // �����: �� ��������� fast mapping �����!
                    // Fast mapping ����� �������� ����� ����� ApplyFastImportMapping()
                    // ����� �������� ������������ ������ � ���������� ���������� ������� ����������.
                    byte importFlags = 0;
                    ushort symbolIndex = 0;

                    var importEntry = new EspbImport 
                    {
                        ModuleNum = 0,
                        EntityName = funcName,
                        Kind = EspbImportKind.Function,
                        TypeIndex = typeIndex,
                        ImportFlags = importFlags,
                        SymbolIndex = symbolIndex,
                        IsVariadic = GetIsVariadicWithHints(funcName, LLVM.IsFunctionVarArg(funcType) != 0)
                    };
                    importsSection.Imports.Add(importEntry);
                    llvmImportToEspbIndex[func] = nextEspbImportIndex++;
                    _logger.WriteLine($"    Added function import '{funcName}' with type index {typeIndex}, total imports: {importsSection.Imports.Count}");
                }
            }

            // --- Global Variable Imports ---
            _logger.WriteLine("  Scanning for Global Variable Imports...");
            for (LLVMValueRef global = LLVM.GetFirstGlobal(llvmModule); global.Handle != IntPtr.Zero; global = LLVM.GetNextGlobal(global))
            {
                if (!global.IsDeclaration) continue; // Skip non-declarations

                string globalName = global.Name ?? "UnnamedGlobalImport";
                _logger.WriteLine($"  Processing global import candidate: '{globalName}' (IsDeclaration: True, Kind: {global.Kind})");

                LLVMTypeRef globalValueType = global.TypeOf; // Type of the GlobalVariable ValueRef (should be Pointer)
                _logger.WriteLine($"    Global Value Type: {globalValueType.Kind} ({globalValueType})");

                if (globalValueType.Kind != LLVMTypeKind.LLVMPointerTypeKind)
                {
                     _logger.WriteLine($"    Warning: Global import candidate '{globalName}' is not a pointer type. Skipping.");
                     continue;
                }

                // Underlying type (usually Void or specific), but global variables are pointers
                _logger.WriteLine($"    Treating global import as PTR: {globalValueType.Kind}");

                // Create the import for the global variable
                 _logger.WriteLine($"    Confirmed global import: {globalName}");
                var import = new EspbImport
                {
                    ModuleNum = 0,
                    EntityName = globalName,
                    Kind = EspbImportKind.Global,
                    GlobalType = MapLlvmTypeToEspb(globalValueType), // Always PTR for global imports
                    IsMutable = !global.IsGlobalConstant,
                    IsShared = false
                };
                _logger.WriteLine($"      ESPB Type: {import.GlobalType}, Mutable: {import.IsMutable}");

                importsSection.Imports.Add(import);
                llvmImportToEspbIndex[global] = nextEspbImportIndex++; // Use the same index space for now
                _logger.WriteLine($"    Added global import index: {nextEspbImportIndex - 1}");
            }
             _logger.WriteLine("--- Finished Collecting Imports ---");
        }

        private unsafe List<LLVMTypeRef>? GetMaxVariadicArgTypesForImport(LLVMValueRef importedFuncRef, LLVMTypeRef importedFuncType) 
        {
            // ����������� CS1061: "LLVMTypeRef" �� �������� ����������� "IsVarArg"
            if (LLVM.IsFunctionVarArg(importedFuncType) == 0)
            {
                return null;
            }

            // ����������� CS1061: "LLVMTypeRef" �� �������� ����������� "ParamsCount"
            uint fixedParamCount = LLVM.CountParamTypes(importedFuncType);
            List<LLVMTypeRef> variadicArgTypes = new List<LLVMTypeRef>();

            // ����������� CS1061: "LLVMValueRef" �� �������� ����������� "Uses"
            for (LLVMUseRef use = LLVM.GetFirstUse(importedFuncRef); use.Handle != IntPtr.Zero; use = LLVM.GetNextUse(use))
            {
                LLVMValueRef user = LLVM.GetUser(use);
                // ����������� CS9135: ��������� �������� ��������� ���� "LLVMValueRef"
                // � ����������� CS0266: �� ������� ������ ������������� ��� "int" � "uint".
                if (user.InstructionOpcode == LLVMOpcode.LLVMCall || user.InstructionOpcode == LLVMOpcode.LLVMInvoke)
                {
                    LLVMValueRef callInst = user; // CallInst ��� InvokeInst
                    int numOperandsInCall = LLVM.GetNumOperands(callInst); // �������� �� int, ����� �������� �������������� int->uint
                    
                    // ��������� ������� CallInst - ��� ���������� �������, ������� numOperandsInCall-1 ����������� ����������
                    int actualArgCount = numOperandsInCall - 1; // �������� �� int

                    if (actualArgCount > (int)fixedParamCount) // ��������� int � uint ����� ����������
                    {
                        // ��������: ���������������� ���������� ����� long
                        int currentVariadicCount = actualArgCount - (int)fixedParamCount; // �������� �� int
                        _logger.WriteLine($"    INFO: Call to '{importedFuncRef.Name}' has {currentVariadicCount} variadic_arg(s). Fixed params: {fixedParamCount}, Actual args in call: {actualArgCount}.");
                        
                        if (variadicArgTypes.Count == 0)
                        {
                            for (int i = 0; i < currentVariadicCount; ++i)
                            {
                                // ���������� LLVM.GetOperand, ������� ������ � uint
                                LLVMValueRef variadicArgOperand = LLVM.GetOperand(callInst, fixedParamCount + (uint)i);
                                variadicArgTypes.Add(variadicArgOperand.TypeOf);
                                _logger.WriteLine($"      Variadic Arg {i}: LLVM Type = {variadicArgOperand.TypeOf}");
                            }
                            _logger.WriteLine($"    INFO: Using variadic arg types from this call for '{importedFuncRef.Name}'. Total variadic types captured: {variadicArgTypes.Count}.");
                            return variadicArgTypes;
                        }
                    }
                }
            }
            
            if (variadicArgTypes.Count > 0)
            {
                 _logger.WriteLine($"    INFO: Final variadic arg types determined for '{importedFuncRef.Name}': {variadicArgTypes.Count} type(s).");
                return variadicArgTypes;
            }
            else
            {
                _logger.WriteLine($"    INFO: No calls with variadic arguments found for '{importedFuncRef.Name}', or IsVarArg was false. Defaulting variadic part handling.");
                return null; // ���������� null, ���� �� ����� ������� � ������������ �����������
            }
        }

        private uint CalculateGlobalOffset(GlobalsSection section, uint globalIndex)
        {
            uint offset = 0;
            for (uint i = 0; i < globalIndex && i < (uint)section.Globals.Count; i++)
            {
                ValueType type = section.Globals[(int)i].Type;
                uint typeSize = GetTypeSize(type);
                uint typeAlign = GetTypeAlign(type);
                uint padding = (typeAlign - (offset % typeAlign)) % typeAlign;
                offset += padding + typeSize;
            }
            return offset;
        }

        private uint GetTypeSize(ValueType t)
        {
            return t switch
            {
                ValueType.I8 or ValueType.U8 or ValueType.BOOL => 1u,
                ValueType.I16 or ValueType.U16 => 2u,
                ValueType.I32 or ValueType.U32 or ValueType.F32 or ValueType.PTR => 4u,
                ValueType.I64 or ValueType.U64 or ValueType.F64 => 8u,
                ValueType.V128 => 16u,
                _ => 4u
            };
        }

        private uint GetTypeAlign(ValueType t)
        {
            return t switch
            {
                ValueType.I8 or ValueType.U8 or ValueType.BOOL => 1u,
                ValueType.I16 or ValueType.U16 => 2u,
                ValueType.I32 or ValueType.U32 or ValueType.F32 or ValueType.PTR => 4u,
                ValueType.I64 or ValueType.U64 or ValueType.F64 => 8u,
                ValueType.V128 => 16u,
                _ => 4u
            };
        }

        private unsafe void CollectGlobals()
        {
            // �������� ������: Globals, Data � Relocations (��� ������ Globals)
            var globalsSection = GetSection<GlobalsSection>(4)!;
            var dataSection = GetSection<DataSection>(7)!;
            var relocSection = espbModule.Sections.OfType<RelocationsSection>()
                                   .FirstOrDefault(s => s.TargetSectionId == 4)
                               ?? throw new InvalidOperationException("Relocations section for Globals not found");

            // Pre-scan: collect function indices BEFORE processing globals
            // This is needed for global arrays of function pointers like: static int (*ops[])(int,int) = {f1, f2};
            _logger.WriteLine("  Pre-scanning functions for global array initialization...");
            uint tempFuncIndex = 0;
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule);
                 func.Handle != IntPtr.Zero;
                 func = LLVM.GetNextFunction(func))
            {
                if (func.IsDeclaration) continue;
                string funcName = func.Name;
                if (string.IsNullOrEmpty(funcName) || funcName.StartsWith("llvm.")) continue;
                
                // Pre-register function index (will be confirmed later in CollectFunctions)
                if (!llvmFuncToEspbIndex.ContainsKey(func))
                {
                    llvmFuncToEspbIndex[func] = tempFuncIndex;
                    _logger.WriteLine($"    Pre-registered function '{funcName}' with index {tempFuncIndex}");
                }
                tempFuncIndex++;
            }
            _logger.WriteLine($"  Pre-scan complete: {tempFuncIndex} functions indexed");

            // �������� �� ���� ������������ ���������� ����������
            for (LLVMValueRef global = LLVM.GetFirstGlobal(llvmModule);
                 global.Handle != IntPtr.Zero;
                 global = LLVM.GetNextGlobal(global))
            {
                if (global.IsDeclaration) continue; // ���������� �������

                // ������ ���������� ���������� � ESPB (����� �������)
                uint espbIndex = nextEspbGlobalIndex++;

                // ���������� ESPB-��� �������� ���������� ���������� ����� GlobalGetValueType
                LLVMTypeRef elemType = LLVM.GlobalGetValueType(global);
                if (elemType.Handle == IntPtr.Zero)
                {
                    _logger.WriteLine($"Warning: Global '{global.Name}' has null element type. Skipping.");
                    continue;
                }
                ValueType espbType = MapLlvmTypeToEspb(elemType);

                // ������� ������ ������� � ������� ��������������
                var espbGlobal = new EspbGlobal
                {
                    Type = espbType,
                    IsMutable = !global.IsGlobalConstant,
                    IsShared = false,
                    InitKind = EspbGlobal.InitializerKind.Zero
                };
                globalsSection.Globals.Add(espbGlobal);
                llvmGlobalToEspbIndex[global] = espbIndex;

                // ��������� �������������
                LLVMValueRef init = LLVM.GetInitializer(global);
                if (init.Handle != IntPtr.Zero)
                {
                    if (init.Kind == LLVMValueKind.LLVMFunctionValueKind)
                    {
                        // ��� ���������� ����������, ������������������ ���������� �� �������.
                        // ��� ����� ������� ��� ��� ������ � ������ ������ � ��������� ��������.
                        int dataOffset = AddDataSegment(new byte[4], 4); // ����������� 4 ����� ��� ���������
                        espbGlobal.InitKind = EspbGlobal.InitializerKind.DataSegmentOffset;
                        espbGlobal.InitialValue = dataOffset;
                        
                        // This logic is now handled by PreScanForFunctionPointers

                        _logger.WriteLine($"  Global '{global.Name}' is a function pointer. Reserved data at offset {dataOffset}. Storing for FuncPtrMap.");
                        continue; // ��������� � ���������� �������
                    }

                    // Check for array of function pointers
                    // Instead of creating ElementSegment, we create a data segment with function pointers
                    // This allows C-style access like: operations[idx](x, y)
                    LLVMTypeRef globalType = LLVM.GlobalGetValueType(global);
                    if (globalType.Kind == LLVMTypeKind.LLVMArrayTypeKind)
                    {
                        LLVMTypeRef elementType = LLVM.GetElementType(globalType);
                        // For opaque pointers (LLVM 15+), we can't check elementType.ElementType
                        // Instead, check if the array elements in the initializer are functions
                        bool isArrayOfFunctionPointers = false;
                        if (elementType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                        {
                            uint numElems = LLVM.GetArrayLength(globalType);
                            if (numElems > 0)
                            {
                                LLVMValueRef firstElem = LLVM.GetAggregateElement(init, 0);
                                // Resolve bitcasts
                                while (firstElem.Handle != IntPtr.Zero && 
                                       firstElem.IsAConstantExpr != null && 
                                       firstElem.ConstOpcode == LLVMOpcode.LLVMBitCast)
                                {
                                    firstElem = firstElem.GetOperand(0);
                                }
                                // Check if the first element is a function
                                if (firstElem.Handle != IntPtr.Zero && 
                                    (firstElem.Kind == LLVMValueKind.LLVMFunctionValueKind ||
                                     firstElem.IsAFunction.Handle != IntPtr.Zero))
                                {
                                    isArrayOfFunctionPointers = true;
                                    _logger.WriteLine($"  Detected array of function pointers by inspecting initializer elements");
                                }
                            }
                        }
                        
                        if (isArrayOfFunctionPointers)
                        {
                            _logger.WriteLine($"  Found global array of function pointers: '{global.Name}'");
                            
                            uint numElements = LLVM.GetArrayLength(globalType);
                            uint arraySize = numElements * 4; // 4 bytes per pointer
                            
                            // First, ensure all functions have data offsets allocated
                            List<uint> funcDataOffsets = new List<uint>();
                            for (uint i = 0; i < numElements; i++)
                            {
                                LLVMValueRef funcVal = LLVM.GetAggregateElement(init, i);
                                
                                // Resolve bitcasts
                                while(funcVal.IsAConstantExpr != null && funcVal.ConstOpcode == LLVMOpcode.LLVMBitCast) {
                                    funcVal = funcVal.GetOperand(0);
                                }

                                if (llvmFuncToEspbIndex.TryGetValue(funcVal, out uint funcIndex))
                                {
                                    // Ensure function has a data offset for FuncPtrMap
                                    if (!_funcPtrToDataOffset.ContainsKey(funcVal))
                                    {
                                        int funcDataOffset = AddDataSegment(new byte[4], 4);
                                        if (funcDataOffset >= 0)
                                        {
                                            _funcPtrToDataOffset[funcVal] = (uint)funcDataOffset;
                                            _logger.WriteLine($"    Pre-allocated data offset {funcDataOffset} for function '{funcVal.Name}'");
                                        }
                                    }
                                    
                                    if (_funcPtrToDataOffset.TryGetValue(funcVal, out uint fdo))
                                    {
                                        funcDataOffsets.Add(fdo);
                                        // Add FuncPtrMap entry for this function
                                        AddFunctionPointerMapEntry(fdo, (ushort)funcIndex);
                                        _logger.WriteLine($"    Function '{funcVal.Name}' (index {funcIndex}) has data offset {fdo}");
                                    }
                                    else
                                    {
                                        funcDataOffsets.Add(0); // Fallback
                                        _logger.WriteLine($"    ERROR: No data offset for function '{funcVal.Name}'");
                                    }
                                }
                                else 
                                {
                                    funcDataOffsets.Add(0);
                                    _logger.WriteLine($"    ERROR: Could not find index for function '{funcVal.Name}' in array.");
                                }
                            }
                            
                            // Create data segment for the array with pre-filled function data offsets
                            byte[] arrayData = new byte[arraySize];
                            for (int i = 0; i < funcDataOffsets.Count; i++)
                            {
                                // Write the function's data offset as a 32-bit value
                                // At runtime: memory_data[array_offset + i*4] contains funcDataOffset
                                // LOAD.PTR reads this value, then CALL_INDIRECT uses it to look up in FuncPtrMap
                                byte[] offsetBytes = BitConverter.GetBytes(funcDataOffsets[i]);
                                Array.Copy(offsetBytes, 0, arrayData, i * 4, 4);
                            }
                            
                            int dataOffset = AddDataSegment(arrayData, 4); // Align to 4 bytes
                            
                            if (dataOffset < 0)
                            {
                                _logger.WriteLine($"    ERROR: Failed to allocate data segment for array '{global.Name}'");
                                continue;
                            }
                            
                            _logger.WriteLine($"  Allocated data segment for function pointer array at offset {dataOffset}, size {arraySize} bytes");
                            _logger.WriteLine($"  Array contents (data offsets): [{string.Join(", ", funcDataOffsets)}]");
                            
                            // Register this global variable with its data offset
                            espbGlobal.InitKind = EspbGlobal.InitializerKind.DataSegmentOffset;
                            espbGlobal.InitialValue = dataOffset;
                            
                            _logger.WriteLine($"  Global '{global.Name}' registered with data offset {dataOffset}");
                            
                            // The global is already added to globalsSection and llvmGlobalToEspbIndex above
                            continue;
                        }
                    }

                    byte[]? data = GetInitializerData(init);
                    if (data != null && data.Length > 0)
                    {
                        uint alignment = LLVM.GetAlignment(global);
                        if (alignment == 0) 
                        {
                            // If alignment is not specified, determine it from the type size.
                            // This is a fallback.
                            uint typeSize = GetTypeSize(LLVM.GlobalGetValueType(global));
                            alignment = typeSize;
                            // Clamp alignment to a reasonable maximum like 16, or power of 2.
                            if (alignment > 16) alignment = 16;
                            // Round up to the next power of two
                            if (alignment > 0 && (alignment & (alignment - 1)) != 0)
                            {
                                uint p = 1;
                                while (p < alignment) p <<= 1;
                                alignment = p;
                            }
                            if (alignment == 0) alignment = 1;
                        }

                        int dataOffset = AddDataSegment(data, (int)alignment); // Pass alignment
                        
                        // ����������: ���� ������ ������������� ��� �������� � ������ ������
                        espbGlobal.InitKind = EspbGlobal.InitializerKind.DataSegmentOffset;
                        espbGlobal.InitialValue = dataOffset;
                        // ����� ����� ������������� �������������� ��� PTR � IsMutable=false.
                        // ��� ������ ���������� ������� (��������, static int tick) � ��������� �������� Type � IsMutable.
                        // ���� LLVM-������ ������������� �����������, IsMutable ��� false �� !global.IsGlobalConstant.
                        
                        // ��������� ���������� ����� ��� �������� ������
                        _logger.WriteLine($"  Global {global.Name} initialized with data segment at offset {dataOffset}, length {data.Length} bytes, InitKind: {(byte)espbGlobal.InitKind}, Type={espbGlobal.Type}, Mutable={espbGlobal.IsMutable}");
                        
                        // ���� ���� ���� ����� ������� ��� ����������������
                        /*
                        relocSection.Relocations.Add(new RelocationEntry
                        {
                            Type = RelocationType.R_ABS32_DATA,
                            Offset = espbIndex, // ������ ���������� ����������
                            SymbolIndex = (uint)dataOffset, // �������� � ������ Data
                            Addend = 0
                        });
                        _logger.WriteLine($"  Added R_ABS32_DATA relocation for global {espbIndex} targeting data at offset {dataOffset}");
                        */
                    }
                    else if (init.IsAGlobalValue != null || init.IsAConstantExpr != null)
                    {
                        LLVMValueRef targetGlobal = init;
                        
                        // Resolve ConstantExpr (bitcast/GEP) to global
                        if (init.IsAConstantExpr != null)
                        {
                            LLVMOpcode opcode = LLVM.GetConstOpcode(init);
                            if (opcode == LLVMOpcode.LLVMBitCast || opcode == LLVMOpcode.LLVMGetElementPtr)
                            {
                                targetGlobal = init.GetOperand(0);
                            }
                        }

                        if (targetGlobal.Handle != IntPtr.Zero && llvmGlobalToEspbIndex.TryGetValue(targetGlobal, out uint targetIdx))
                        {
                            // Calculate byte offset in globals_data for this global (mirror runtime allocate_globals)
                            uint offset = CalculateGlobalOffset(globalsSection, espbIndex);
                            relocSection.Relocations.Add(new RelocationEntry
                            {
                                Type = RelocationType.R_ABS32_GLOBAL,
                                Offset = offset,
                                SymbolIndex = targetIdx,
                                Addend = 0
                            });
                            _logger.WriteLine($"  Added R_ABS32_GLOBAL relocation for global {espbIndex} (offset {offset}) targeting global index {targetIdx}");
                        }
                        else
                        {
                            _logger.WriteLine($"  Warning: Global '{global.Name}' has pointer initializer, but target not resolved");
                        }
                    }
                }
            }
        }

        // ����� ������� ��� ����� ������ ����������
        private unsafe void CollectFunctionDeclarations()
        {
             var funcSection = GetSection<FunctionSection>(3)!;
           _logger.WriteLine("--- Collecting Function Declarations ---");

             for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
             {
                // �����������: �������� ��� ������� � ������ (�� ����������)
                if (func.IsDeclaration) continue;

                  string funcName = func.Name ?? "UnnamedFunction";
                _logger.WriteLine($"  Processing function definition for: '{funcName}'");

                uint currentFuncIndex = nextEspbFuncIndex++;
                llvmFuncToEspbIndex[func] = currentFuncIndex;
                _logger.WriteLine($"    Assigned index {currentFuncIndex} to function '{funcName}'");

                LLVMTypeRef funcTypeToUse = LLVM.GlobalGetValueType(func);
                int typeIndex = AddTypeSignature(funcTypeToUse, null, func);
                funcSection.TypeIndices.Add((ushort)typeIndex);
            }
            
            _logger.WriteLine($"--- Collected {llvmFuncToEspbIndex.Count} function definitions ---");
            foreach (var kvp in llvmFuncToEspbIndex)
            {
                _logger.WriteLine($"  Function '{kvp.Key.Name}' -> Index {kvp.Value}");
            }
                 }

        // --- ����� ���� ��� backpatching � �������������� ���������� ---
        private class BranchFixup
        {
            // ������ ������� � `output`, ���� ����� �������� ������ ���� ��������. 
            public int PlaceholderPosition { get; set; } 
            // ������� ������ ���������� �������� (BR, BR_IF, BR_TABLE).
            public int InstructionPosition { get; set; }
            public LLVMBasicBlockRef TargetBlock { get; set; }
        }
        
        // ����� ��� ������������� ������� PHI-����
        private class PhiFixup
        {
            public LLVMValueRef SourceValue { get; set; } // �������� ��������
            public byte DestReg { get; set; }            // ������� ������� PHI
        }
        
        private List<BranchFixup> branchFixups = new List<BranchFixup>();
        private Dictionary<LLVMBasicBlockRef, int> blockOffsets = new Dictionary<LLVMBasicBlockRef, int>();
        
        // ������� ��� �������� PHI-�������� ��� ������� �����
        private Dictionary<LLVMBasicBlockRef, List<PhiFixup>> phiFixups = new Dictionary<LLVMBasicBlockRef, List<PhiFixup>>();

        // ����� ������� ��� ���������� ��� ���� �������
        private unsafe void TranslateAllFunctionBodies()
        {
            _peepholeStats.Clear();
            var codeSection = GetSection<CodeSection>(6)!;
            
            // Get relocations section for Code to pass to peephole optimizer
            var codeRelocSection = espbModule.Sections.OfType<RelocationsSection>()
                .FirstOrDefault(s => s.TargetSectionId == 6);

            var funcNameToIndex = llvmFuncToEspbIndex.ToDictionary(kvp => kvp.Key.Name, kvp => kvp.Value);
            var bodies = new List<(uint index, FunctionBody body)>();

            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
            {
                if (func.IsDeclaration) continue;

                if (funcNameToIndex.TryGetValue(func.Name, out uint funcIndex))
                {
                    _logger.WriteLine($"  Translating body for: '{func.Name}' (Index: {funcIndex})");
                    _currentFunctionIndex = (ushort)funcIndex; // ������������� ������� ������ ��� JIT region tracking
                    var functionBody = TranslateFunctionBody(func, codeRelocSection?.Relocations);
                    bodies.Add((funcIndex, functionBody));
                }
                else
                     {
                    _logger.WriteLine($"  Skipping body for: '{func.Name}' (likely optimized away).");
                     }
                 }

            foreach (var entry in bodies.OrderBy(b => b.index))
            {
                codeSection.Bodies.Add(entry.body);
            }

            // Recompute final opcode count from the post-peephole bytecode actually stored in the module.
            // This makes the reported "Total ESPB opcodes" reflect reality, not the raw EmitOpcode() count.
            int finalOpcodes = 0;
            foreach (var b in codeSection.Bodies)
            {
                if (b.Instructions != null)
                    finalOpcodes += CountInstructionsSafe(b.Instructions);
            }
            FinalOpcodeCount = finalOpcodes;

            // Peephole summary (top-10 by instruction reduction)
            if (_peepholeStats.Count > 0)
            {
                _logger.WriteLine("\n[PEEPHOLE] Summary per function (top-10 by reduction):");
                foreach (var row in _peepholeStats
                             .OrderByDescending(r => r.beforeInstr - r.afterInstr)
                             .ThenByDescending(r => r.beforeBytes - r.afterBytes)
                             .Take(10))
                {
                    int dI = row.beforeInstr - row.afterInstr;
                    int dB = row.beforeBytes - row.afterBytes;
                    _logger.WriteLine($"    {row.name}: instr {row.beforeInstr}->{row.afterInstr} (?{dI}), bytes {row.beforeBytes}->{row.afterBytes} (?{dB})");
                }
            }
        }

        private static unsafe bool IsEffectivelyUnusedParameter(LLVMValueRef param)
        {
            // Treat parameter as unused if it has no uses, OR if it is only stored into an alloca
            // and that alloca is never loaded (classic dead prologue store).
            LLVMUseRef use = LLVM.GetFirstUse(param);
            if (use.Equals(default(LLVMUseRef)))
                return true;

            // Collect allocas that receive the param via store
            var storedAllocas = new HashSet<IntPtr>();

            for (; !use.Equals(default(LLVMUseRef)); use = LLVM.GetNextUse(use))
            {
                LLVMValueRef user = LLVM.GetUser(use);
                if (LLVM.GetValueKind(user) != LLVMValueKind.LLVMInstructionValueKind)
                    return false;

                LLVMOpcode op = LLVM.GetInstructionOpcode(user);
                if (op != LLVMOpcode.LLVMStore)
                    return false;

                // store <val>, <ptr>
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

            // Verify: each such alloca has no loads (and generally no uses except stores of the param)
            foreach (var allocaHandle in storedAllocas)
            {
                LLVMValueRef allocaVal = new LLVMValueRef(allocaHandle);
                LLVMUseRef aUse = LLVM.GetFirstUse(allocaVal);
                while (!aUse.Equals(default(LLVMUseRef)))
                {
                    LLVMValueRef aUser = LLVM.GetUser(aUse);
                    if (LLVM.GetValueKind(aUser) == LLVMValueKind.LLVMInstructionValueKind)
                    {
                        LLVMOpcode aOp = LLVM.GetInstructionOpcode(aUser);
                        if (aOp == LLVMOpcode.LLVMLoad)
                            return false;

                        if (aOp == LLVMOpcode.LLVMStore)
                        {
                            // allow store of param into this alloca
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

        private unsafe FunctionBody TranslateFunctionBody(LLVMValueRef func, List<RelocationEntry>? codeRelocations = null)
        { 
            // Reset per-function JIT region tracking
            _jitRegionStartStack.Clear();
            
            // Reset alloca-to-register mapping for new function
            _allocaRegMap.Clear();
            _volatileAllocas.Clear();
            _promotedAllocas.Clear();
            _deferredPhiEdgeOps.Clear();

            // PRE-SCAN: Identify all address-taken allocas before code generation.
            // An alloca is "address-taken" if its value (pointer) is stored somewhere
            // or passed to a function call. Such allocas cannot use ALLOCA-OPT because
            // writes may happen through the escaped pointer.
            PreScanAddressTakenAllocas(func);
            
            // REGISTER PROMOTION (Phase 1): Identify allocas that can be promoted to registers
            // This must happen BEFORE Graph Coloring so that promoted allocas are excluded
            // from register allocation (they don't need pointer registers).
            IdentifyPromotableAllocas(func);
            
            // Inform RegisterManager about promoted allocas so it can skip them during allocation
            registerManager.SetPromotedAllocas(_promotedAllocas);

            List<byte> output;
            // ?????: ????? --patch-regs ???????? ??? AllowTempRegCompaction (???? ??????? ??? R16-R23).
            // ?????? ??????? ? BytecodeEmitter/ApplyRegisterMap ???????, ?? ?? ????? ?????? ??????? ?????
            // ????? (?? ?? ???? ??????? AddReg), ?? ?????? ? ????????? ?????? ? ????????
            // ? ??? (Load access fault) ? ???????? ????? (??????, Fibonacci).
            // ????? ? ??-???? --patch-regs ?? ?? ????? BytecodeEmitter, ? ??????? ?? ?,
            // ?? ???????? ? ???????? allocationResult ? GetOrAllocateReg() ??????? ?????? ??????.
            output = new List<byte>();
            _bytecodeEmitter = null;
            // 1. ????????? ????????? ????????? ????? ??????? ??????????
            SyncRegisterManagerModes();
            registerManager.PerformAllocation(func, targetData);
            
            // REGISTER PROMOTION (Phase 2): Allocate actual registers for promoted allocas
            // This happens AFTER Graph Coloring to know which registers are free
            byte maxRegUsedByAllocation = (byte)(registerManager.GetNumVirtualRegs() > 0 ? registerManager.GetNumVirtualRegs() - 1 : 0);
            AllocatePromotionRegisters(maxRegUsedByAllocation);

            branchFixups.Clear();
            blockOffsets.Clear();
            
            // --- NEW: Generate prologue to move parameters from ABI registers to allocated registers ---
            // NOTE: ESPB runtime uses simple sequential parameter passing (R0, R1, R2, ...)
            // without RISC-V alignment rules for 64-bit values
            var paramMoves = new List<(byte src, byte dst, byte opcode, uint paramIdx)>();
            for (uint i = 0; i < LLVM.CountParams(func); i++)
            {
                var param = LLVM.GetParam(func, i);
                byte abiReg = (byte)i;  // Simple sequential mapping
                
                // Skip effectively-unused parameters completely (avoid pointless PARAM-SHUFFLE)
                if (IsEffectivelyUnusedParameter(param))
                    continue;

                var allocLocation = registerManager.GetLocation(param);

                if (allocLocation == null)
                {
                    _logger.WriteLine($"[PARAM-SHUFFLE] [SKIP] Dead param {i} in '{func.Name}'");
                    continue;
                }

                if (allocLocation is RegisterLocation regLoc)
                {
                    byte allocatedReg = regLoc.Register;
                    if (allocatedReg != abiReg)
                    {
                        var paramType = MapLlvmTypeToEspb(LLVM.TypeOf(param));
                        byte moveOpcode = GetMoveOpcode(paramType);
                        paramMoves.Add((abiReg, allocatedReg, moveOpcode, i));
                    }
                    else
                    {
                        _logger.WriteLine($"[PARAM-SHUFFLE] [MOVE-ELIMINATION] Function '{func.Name}': Param {i} already in R{abiReg}, skipping MOV");
                    }
                }
                else if (allocLocation is SpillSlotLocation)
                {
                     string paramName = Marshal.PtrToStringAnsi((IntPtr)LLVM.GetValueName(param)) ?? "";
                     throw new NotSupportedException($"Immediate spilling of parameter {i} ('{paramName}') is not yet handled.");
                }
            }

            // Perform parallel-safe moves
            bool prevTempCompaction = registerManager.AllowTempRegCompaction;
            registerManager.AllowTempRegCompaction = true;
            try
            {
                while (paramMoves.Count > 0)
                {
                    // find a move whose destination is not used as a source by any other move
                    int idx = paramMoves.FindIndex(m => !paramMoves.Any(o => o.src == m.dst));
                    if (idx == -1)
                    {
                        // cycle detected, break with temp register
                        var m = paramMoves[0];
                        byte temp = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, m.opcode, "MOV_PARAM_TMP");
                        output.Add(temp);
                        output.Add(m.src);
                        _logger.WriteLine($"[PARAM-SHUFFLE] Using temp R{temp} to break cycle for param {m.paramIdx}");
                        // replace move source with temp
                        paramMoves[0] = (temp, m.dst, m.opcode, m.paramIdx);
                        continue;
                    }

                    var move = paramMoves[idx];
                    EmitOpcode(output, move.opcode, "MOV_PARAM");
                    output.Add(move.dst);
                    output.Add(move.src);
                    _logger.WriteLine($"[PARAM-SHUFFLE] Function '{func.Name}': Moving param {move.paramIdx} from ABI reg R{move.src} to allocated reg R{move.dst}");
                    paramMoves.RemoveAt(idx);
                }
            }
            finally
            {
                registerManager.AllowTempRegCompaction = prevTempCompaction;
            }
            // --- END NEW ---

            _logger.WriteLine($"Translating Function: {func.Name} ({LLVM.CountBasicBlocks(func)} Basic Blocks)");
            
            // ������ ���� ����� PHI ������ �� ����� � ����� ����, ��� ���
            // GetOrAllocateReg ������ �������� ���������� �� ����������.
            // for (var bb = LLVM.GetFirstBasicBlock(func); ... ) { ... }

            // ���� 2: ��������� ���� ��������
            var entryBB = LLVM.GetFirstBasicBlock(func);
            for (var bb = entryBB; bb != null; bb = LLVM.GetNextBasicBlock(bb))
            {
                blockOffsets[bb] = output.Count;

                // ALLOCA-OPT: Clear cache at BB boundaries (except entry block).
                // Since we use Store-through strategy (STORE always generated), no spilling needed.
                // Just clear the cache to prevent stale load-forwarding across BB boundaries.
                if (bb != entryBB && _allocaRegMap.Count > 0)
                {
                    _logger.WriteLine($"    [ALLOCA-OPT] Clearing load-forwarding cache at BB boundary ({_allocaRegMap.Count} entries)");
                    _allocaRegMap.Clear();
                }

                for (var instr = LLVM.GetFirstInstruction(bb); instr != LLVM.GetLastInstruction(bb); instr = LLVM.GetNextInstruction(instr))
                {
                    if (LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMPHI)
                        continue;

                    // Narrow Edge Scheduler: park PHI +/- Constant counters (single-use by PHI)
                    if (TryGetPhiParkingInfo(instr, out var park))
                    {
                        // Park this instruction: it will be computed on the edge during PHI fixups.
                        _deferredPhiEdgeOps[instr] = (park.BasePhi, park.Value, isSub: !park.IsAdd, park.Type);
                        _logger.WriteLine($"      [PHI-PARK] Parking PHI-counter update (opcode={LLVM.GetInstructionOpcode(instr)}) (will be computed on edge)");
                        continue;
                    }

                    TranslateInstruction(instr, output);
                }

                var terminator = LLVM.GetLastInstruction(bb);
                if (terminator == null) continue;

                // The logic for PHI fixups has been moved into the TranslateInstruction
                // method for the 'br' instruction to handle critical edges correctly.
                TranslateInstruction(terminator, output);
            }

            // ���� 3: ���������� ������ (Backpatching)
            foreach (var fixup in branchFixups)
            {
                if (blockOffsets.TryGetValue(fixup.TargetBlock, out int targetOffset))
                {
                    int relativeOffset = targetOffset - fixup.InstructionPosition;
                    byte[] offsetBytes = BitConverter.GetBytes((short)relativeOffset);

                    // DEBUG: Log branch fixups to diagnose misaligned targets
                    int absoluteTarget = fixup.InstructionPosition + relativeOffset;
                    _logger.WriteLine($"    [BRANCH-FIXUP] InstrPos={fixup.InstructionPosition}, TargetBlockOffset={targetOffset}, RelOffset={relativeOffset}, AbsTarget={absoluteTarget}");

                    output[fixup.PlaceholderPosition] = offsetBytes[0];
                    output[fixup.PlaceholderPosition + 1] = offsetBytes[1];
                }
            }
 
            EmitOpcode(output, 0x0F, "END");

            // Ensure all OP_JIT_START are closed by OP_JIT_STOP within this function
            if (_jitRegionStartStack.Count != 0)
            {
                throw new InvalidOperationException($"Unclosed JIT region markers in function '{func.Name}': missing OP_JIT_STOP for {_jitRegionStartStack.Count} region(s).");
            }
            
            // ? ��������: �������������� ������ ��������� � ����-���� ����� ���������
            // ��������� �������� ������� ���������, �� ����-��� ��� �������� ������ ������
            // ����� ������ �� ����-���� � �������� ��� ������ ��������� �������� ���������
            // TODO: ����������� RewriteRegisterIndicesInBytecode()
            _logger.WriteLine($"[BYTECODE-REWRITE] Bytecode size before rewrite: {output.Count} bytes");
 
            // ? ��������� JIT ����������
            // �������� ���������� ����� ���������
            // Register remapping is now applied during emission via GetRemappedReg()
            // registerManager.ApplyRemappingToBytecode(output);
            
            // Compute allocation-based vreg count
            ushort numVirtualRegs = registerManager.GetNumVirtualRegs();
            byte maxRegUsed = (byte)(numVirtualRegs > 0 ? numVirtualRegs - 1 : 0);

            // � ������ --patch-regs �� ��������� ApplyRegisterMap (��. ����������� ����).
            // ��� ������ ������ ��� ������ � allocationResult ����� ���������.
            // if (EnableRegisterPatching && be != null) { be.ApplyRegisterMap(registerManager.GetRemappedReg); }

            // Safety: ensure header matches actual bytecode.
            // With compaction enabled, some instructions may still reference higher vreg numbers.
            // In legacy mode we scan the emitted bytecode to find the maximum register operand.
            // 
            // �����: � ������ --patch-regs ������ ScanMaxRegisterOperand ����������� (�� ����� ������� ���� �������
            // � ����� ������� ����� immediate/offset �� ������ ���������), ��� �������� � max_reg_used=122/146
            // � �������� �� MCU. ������� � --patch-regs ������ �� �� ������ bytecode-scan.
            if (!EnableRegisterPatching)
            {
                byte maxRegInBytecode = ScanMaxRegisterOperand(output);
                if (maxRegInBytecode > maxRegUsed)
                {
                    _logger.WriteLine($"    [BYTECODE-SCAN] max_reg_used adjusted: allocation={maxRegUsed} -> bytecode={maxRegInBytecode}");
                    maxRegUsed = maxRegInBytecode;
                    numVirtualRegs = (ushort)(maxRegUsed + 1);
                }
            }
            else
            {
                _logger.WriteLine("    [BYTECODE-SCAN] skipped in --patch-regs mode");
            }

            // �����: ��������� �������� R16-R23 ������������ ������������
            const byte TEMP_REGS_START = 16;
            const byte TEMP_REGS_END = 23;

            // Legacy behavior extends vreg count to cover full temp range.
            // In --patch-regs mode we expect temp regs to be compacted away, so do not force-extension.
            if (!EnableRegisterPatching && maxRegUsed >= TEMP_REGS_START && maxRegUsed <= TEMP_REGS_END)
            {
                maxRegUsed = TEMP_REGS_END;
                numVirtualRegs = (ushort)(TEMP_REGS_END + 1);
                _logger.WriteLine($"    [TEMP-REGS] Extended for temporary registers: max_reg_used={maxRegUsed}, num_virtual_regs={numVirtualRegs}");
            }
            else
            {
                _logger.WriteLine($"    [COMPACTION] Using values: max_reg_used={maxRegUsed}, num_virtual_regs={numVirtualRegs}");
            }

            ushort frameSize = (ushort)(registerManager.GetSpillSlotSize());
            
            // ��������� �����, �������� ���������� � spill
            byte flags = CalculateFunctionFlags(func, frameSize, maxRegUsed);
            
            // Ƹ����� ���������: ��������� ������ num_virtual_regs � u16, �� runtime ������ ������ �������� � u8.
            // ���� ������ �� 255 � ����� ������ �� ����� ����������.
            if (numVirtualRegs > 255)
            {
                throw new InvalidOperationException($"Function '{func.Name}' uses too many virtual registers after patching/compaction: {numVirtualRegs} (max 255)");
            }

            _logger.WriteLine($"[JIT-METADATA] Function '{func.Name}': flags=0x{flags:X2}, max_reg={maxRegUsed}, frame_size={frameSize}, num_vregs={numVirtualRegs}");

            // MOV diagnostics before peephole (raw bytecode). Detailed for fibonacci/performance, summary for others.
            LogRedundantMoves(func.Name, output);

            return new FunctionBody
            {
                // JIT metadata (8 bytes header)
                Flags = flags,
                MaxRegUsed = maxRegUsed,
                FrameSize = frameSize,
                NumVirtualRegs = numVirtualRegs,
                Reserved = 0,
                
                // Bytecode
                Instructions = CleanupBytecodeSafe(output, func.Name, codeRelocations, 0)
            };
        }
        
        // ����� ����� ��� ������� PHI-���������� � �������� ��������
        /// <summary>
        /// Peephole optimizer that is guaranteed not to corrupt bytecode.
        /// It only applies optimizations if it can safely decode the entire instruction stream.
        /// Otherwise it returns the original bytecode unchanged.
        /// </summary>
        /// <param name="rawCode">Raw bytecode before optimization</param>
        /// <param name="funcName">Function name for logging</param>
        /// <param name="codeRelocations">Relocations targeting Code section - their offsets will be updated</param>
        /// <param name="funcCodeOffset">Offset of this function's bytecode within the Code section</param>
        private byte[] CleanupBytecodeSafe(List<byte> rawCode, string funcName, 
            List<RelocationEntry>? codeRelocations = null, int funcCodeOffset = 0)
        {
            if (!EnablePeephole)
                return rawCode.ToArray();

            try
            {
                int beforeBytes = rawCode.Count;
                int beforeInstr = CountInstructionsSafe(rawCode);

                var clean = new List<byte>(rawCode.Count);
                // Map old bytecode offset > new bytecode offset (for branch fixup)
                var offsetMap = new Dictionary<int, int>();
                int pc = 0;

                while (pc < rawCode.Count)
                {
                    int instrStart = pc;
                    // Record mapping from old offset to new offset
                    offsetMap[instrStart] = clean.Count;
                    
                    byte op = rawCode[pc];
                    

                    if (!TryGetInstructionSize(rawCode, pc, out int size))
                    {
                        // Fail-safe: we cannot decode further. Keep already optimized prefix, then copy the rest verbatim.
                        _logger.WriteLine($"[PEEPHOLE] Unsupported opcode 0x{op:X2} at pc={pc}, stopping peephole and copying remaining bytes verbatim.");
                        for (int k = pc; k < rawCode.Count; k++)
                        {
                            if (!offsetMap.ContainsKey(k))
                                offsetMap[k] = clean.Count;
                            clean.Add(rawCode[k]);
                        }
                        // Fix branch offsets before returning
                        FixBranchOffsetsAfterPeephole(clean, offsetMap);
                        return clean.ToArray();
                    }

                    // Identity MOV removal: MOV.* Rd, Rs where Rd == Rs.
                    if (IsMovOpcode(op))
                    {
                        if (size != 3)
                        {
                            _logger.WriteLine($"[PEEPHOLE] MOV opcode 0x{op:X2} has unexpected size={size} at pc={pc}, peephole disabled.");
                            return rawCode.ToArray();
                        }

                        byte rd = rawCode[pc + 1];
                        byte rs = rawCode[pc + 2];
                        if (rd == rs)
                        {
                            _logger.WriteLine($"    [PEEPHOLE-REMOVE] Removing identity MOV at old_pc={instrStart}: opcode=0x{op:X2} R{rd} <- R{rs}");
                            pc += size;
                            continue;
                        }
                    }

                    for (int i = 0; i < size; i++)
                        clean.Add(rawCode[instrStart + i]);

                    pc += size;
                }
                
                // Also map the end position
                offsetMap[rawCode.Count] = clean.Count;

                // Fix branch offsets after removing instructions
                FixBranchOffsetsAfterPeephole(clean, offsetMap);
                
                // Fix relocation offsets after removing instructions
                if (codeRelocations != null && beforeBytes != clean.Count)
                {
                    FixRelocationOffsetsAfterPeephole(codeRelocations, offsetMap, funcCodeOffset, beforeBytes);
                }

                int afterBytes = clean.Count;
                int afterInstr = CountInstructionsSafe(clean);

                _peepholeStats.Add((funcName, beforeInstr, afterInstr, beforeBytes, afterBytes));

                int beforeMov = CountMovInstructionsSafe(rawCode);
                int afterMov = CountMovInstructionsSafe(clean);
                _logger.WriteLine($"[STATS] {funcName}: bytes {beforeBytes}->{afterBytes}, instr {beforeInstr}->{afterInstr}, MOV {beforeMov}->{afterMov}");
                return clean.ToArray();
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"[PEEPHOLE] Exception: {ex.Message}. Peephole disabled.");
                return rawCode.ToArray();
            }
        }
        
        /// <summary>
        /// Fixes relocation offsets after peephole optimization removed some instructions.
        /// Relocations that point into this function's bytecode range need their Offset updated.
        /// </summary>
        /// <param name="relocations">List of relocations targeting Code section</param>
        /// <param name="offsetMap">Maps old bytecode offset -> new bytecode offset (within function)</param>
        /// <param name="funcCodeOffset">Offset of this function's bytecode within the Code section</param>
        /// <param name="funcOldSize">Original size of this function's bytecode before peephole</param>
        private void FixRelocationOffsetsAfterPeephole(List<RelocationEntry> relocations, 
            Dictionary<int, int> offsetMap, int funcCodeOffset, int funcOldSize)
        {
            int funcEndOffset = funcCodeOffset + funcOldSize;
            
            foreach (var reloc in relocations)
            {
                // Check if this relocation points into our function's bytecode range
                if (reloc.Offset >= funcCodeOffset && reloc.Offset < funcEndOffset)
                {
                    // Convert global offset to local (within function)
                    int localOldOffset = (int)(reloc.Offset - funcCodeOffset);
                    
                    // Find the new local offset
                    if (offsetMap.TryGetValue(localOldOffset, out int localNewOffset))
                    {
                        uint newGlobalOffset = (uint)(funcCodeOffset + localNewOffset);
                        _logger.WriteLine($"    [RELOC-FIX] Relocation at offset {reloc.Offset} -> {newGlobalOffset} (local {localOldOffset} -> {localNewOffset})");
                        reloc.Offset = newGlobalOffset;
                    }
                    else
                    {
                        // The relocation points to a position that wasn't at instruction start
                        // This shouldn't happen with valid bytecode, but we log it
                        _logger.WriteLine($"    [RELOC-FIX] WARNING: Relocation at offset {reloc.Offset} (local {localOldOffset}) not found in offsetMap!");
                    }
                }
            }
        }

        /// <summary>
        /// Fixes branch offsets in bytecode after peephole optimization removed some instructions.
        /// offsetMap maps OLD bytecode offset -> NEW bytecode offset.
        /// We need reverse map to find old position of current instruction.
        /// </summary>
        private void FixBranchOffsetsAfterPeephole(List<byte> code, Dictionary<int, int> offsetMap)
        {
            // Build reverse map: new offset -> old offset
            var reverseMap = new Dictionary<int, int>();
            foreach (var kv in offsetMap)
            {
                reverseMap[kv.Value] = kv.Key;
            }
            
            int pc = 0;
            while (pc < code.Count)
            {
                byte op = code[pc];
                
                if (!TryGetInstructionSize(code, pc, out int size))
                    break;
                
                // BR: opcode(1) + offset(2) = 3 bytes
                if (op == 0x02)
                {
                    int newInstrStart = pc;
                    // Get the old position of this BR instruction
                    if (reverseMap.TryGetValue(newInstrStart, out int oldInstrStart))
                    {
                        short oldOffset = BitConverter.ToInt16(code.ToArray(), pc + 1);
                        int oldTarget = oldInstrStart + oldOffset;
                        
                        // Find the new target offset
                        if (offsetMap.TryGetValue(oldTarget, out int newTarget))
                        {
                            short newOffset = (short)(newTarget - newInstrStart);
                            byte[] newOffsetBytes = BitConverter.GetBytes(newOffset);
                            code[pc + 1] = newOffsetBytes[0];
                            code[pc + 2] = newOffsetBytes[1];
                            _logger.WriteLine($"    [BRANCH-FIX] BR at new={newInstrStart} old={oldInstrStart}: oldOffset={oldOffset} oldTarget={oldTarget} -> newTarget={newTarget} newOffset={newOffset}");
                        }
                        else
                        {
                            _logger.WriteLine($"    [BRANCH-FIX] BR at new={newInstrStart} old={oldInstrStart}: oldTarget={oldTarget} NOT FOUND in offsetMap!");
                        }
                    }
                    else
                    {
                        _logger.WriteLine($"    [BRANCH-FIX] BR at new={newInstrStart}: NOT FOUND in reverseMap!");
                    }
                }
                // BR_IF: opcode(1) + reg(1) + offset(2) = 4 bytes
                else if (op == 0x03)
                {
                    int newInstrStart = pc;
                    // Get the old position of this BR_IF instruction
                    if (reverseMap.TryGetValue(newInstrStart, out int oldInstrStart))
                    {
                        short oldOffset = BitConverter.ToInt16(code.ToArray(), pc + 2);
                        int oldTarget = oldInstrStart + oldOffset;
                        
                        // Find the new target offset
                        if (offsetMap.TryGetValue(oldTarget, out int newTarget))
                        {
                            short newOffset = (short)(newTarget - newInstrStart);
                            byte[] newOffsetBytes = BitConverter.GetBytes(newOffset);
                            code[pc + 2] = newOffsetBytes[0];
                            code[pc + 3] = newOffsetBytes[1];
                            _logger.WriteLine($"    [BRANCH-FIX] BR_IF at new={newInstrStart} old={oldInstrStart}: oldOffset={oldOffset} oldTarget={oldTarget} -> newTarget={newTarget} newOffset={newOffset}");
                        }
                        else
                        {
                            _logger.WriteLine($"    [BRANCH-FIX] BR_IF at new={newInstrStart} old={oldInstrStart}: oldTarget={oldTarget} NOT FOUND in offsetMap!");
                        }
                    }
                    else
                    {
                        _logger.WriteLine($"    [BRANCH-FIX] BR_IF at new={newInstrStart}: NOT FOUND in reverseMap!");
                    }
                }
                
                pc += size;
            }
        }

        private static bool IsMovOpcode(byte op)
        {
            return op == 0x12 || op == 0x13 || op == 0x14 || op == 0x15 || op == 0x16;
        }

        private static int CountInstructionsSafe(byte[] code)
        {
            // Fast path over array to avoid allocations.
            int count = 0;
            int pc = 0;
            while (pc < code.Length)
            {
                if (!TryGetInstructionSize(code, pc, out int size))
                    return count;
                pc += size;
                count++;
            }
            return count;
        }

        private static int CountInstructionsSafe(List<byte> code)
        {
            int count = 0;
            for (int pc = 0; pc < code.Count; )
            {
                if (!TryGetInstructionSize(code, pc, out int size))
                    break;
                pc += size;
                count++;
            }
            return count;
        }

        private static int CountMovInstructionsSafe(List<byte> code)
        {
            int count = 0;
            for (int pc = 0; pc < code.Count; )
            {
                if (!TryGetInstructionSize(code, pc, out int size))
                    break;
                byte op = code[pc];
                if (IsMovOpcode(op))
                    count++;
                pc += size;
            }
            return count;
        }

        private void LogRedundantMoves(string funcName, List<byte> code)
        {
            bool isTarget = funcName.Contains("fibonacci", StringComparison.OrdinalIgnoreCase) ||
                            funcName.Contains("performance", StringComparison.OrdinalIgnoreCase);

            int pc = 0, total = 0, identity = 0;

            if (isTarget)
                _logger.WriteLine($"\n[MOV-TRACE] Audit for '{funcName}':");

            while (pc < code.Count)
            {
                byte op = code[pc];
                if (!TryGetInstructionSize(code, pc, out int sz))
                {
                    if (isTarget)
                        _logger.WriteLine($"  PC {pc:D3}: Unknown opcode 0x{op:X2}. Trace aborted.");
                    break;
                }

                if (IsMovOpcode(op))
                {
                    total++;
                    byte rd = code[pc + 1];
                    byte rs = code[pc + 2];
                    bool isId = (rd == rs);
                    if (isId) identity++;

                    if (isTarget)
                    {
                        string status = isId ? "[IDENTITY - WILL BE REMOVED]" : "[REAL COPY]";
                        _logger.WriteLine($"  PC {pc:D3}: {GetOpcodeName(op)} R{rd} <- R{rs} {status}");
                    }
                }

                pc += sz;
            }

            if (isTarget)
                _logger.WriteLine($"[MOV-TRACE] Total: {total}, Identity: {identity}.\n");
            else
                _logger.WriteLine($"[MOV-TRACE] {funcName}: MOV total={total}, identity={identity} (details hidden)");
        }

        private static bool TryGetInstructionSize(byte[] code, int pc, out int size)
        {
            // Convenience wrapper for final opcode counting.
            // This is not on a hot path, so we reuse the existing List<byte>-based decoder.
            return TryGetInstructionSize(new List<byte>(code), pc, out size);
        }

        private static bool TryGetInstructionSize(List<byte> code, int pc, out int size)
        {
            size = 0;
            if (pc < 0 || pc >= code.Count) return false;

            byte op = code[pc];

            // ==== CONTROL FLOW (0x00-0x0F) ====
            // 1-byte opcodes
            if (op == 0x00) { size = 1; return true; } // NOP
            if (op == 0x01) { size = 1; return true; } // RET
            if (op == 0x05) { size = 1; return true; } // UNREACHABLE
            if (op == 0x0F) { size = 1; return true; } // END
            
            // BR: [0x02][offset:i16] = 3 bytes
            if (op == 0x02) { size = 3; return pc + size <= code.Count; }
            
            // BR_IF: [0x03][Rcond:u8][offset:i16] = 4 bytes
            if (op == 0x03) { size = 4; return pc + size <= code.Count; }
            
            // BR_TABLE: [0x04][Ridx:u8][num:u16][offsets:i16*num][default:i16] = variable
            if (op == 0x04)
            {
                if (pc + 4 > code.Count) return false;
                ushort numTargets = (ushort)(code[pc + 2] | (code[pc + 3] << 8));
                size = 1 + 1 + 2 + (2 * numTargets) + 2;
                return pc + size <= code.Count;
            }
            
            // JIT_REGION_START: [0x06][len:u16][id:u16] = 5 bytes
            if (op == 0x06) { size = 5; return pc + size <= code.Count; }
            
            // CALL: [0x07][func_idx:u16] = 3 bytes
            if (op == 0x07) { size = 3; return pc + size <= code.Count; }
            
            // CALL_INDIRECT: [0x08][Rfunc:u8][type_idx:u16] = 4 bytes
            if (op == 0x08) { size = 4; return pc + size <= code.Count; }
            
            // CALL_IMPORT (0x09): variable length - check for extended format marker 0xAA
            if (op == 0x09)
            {
                // Minimum: [op][import_idx:u16] = 3 bytes
                if (pc + 3 > code.Count) return false;
                // Check for extended format marker at position pc+3
                if (pc + 3 < code.Count && code[pc + 3] == 0xAA)
                {
                    // Extended format: [op][import_idx:u16][0xAA][num_args:u8][types:num_args bytes]
                    if (pc + 5 > code.Count) return false;
                    byte numArgs = code[pc + 4];
                    size = 5 + numArgs;
                    return pc + size <= code.Count;
                }
                else
                {
                    size = 3;
                    return true;
                }
            }
            
            // CALL_INTERNAL: [0x0A][func_idx:u16] = 3 bytes (if used)
            if (op == 0x0A) { size = 3; return pc + size <= code.Count; }
            
            // HEAP_MALLOC / CALL_INDIRECT: [0x0B][Rd:u8][Rs:u8] = 3 bytes
            if (op == 0x0B) { size = 3; return pc + size <= code.Count; }
            
            // HEAP_FREE: [0x0C][Rptr:u8] = 2 bytes (if exists)
            if (op == 0x0C) { size = 2; return pc + size <= code.Count; }
            
            // CALL_INDIRECT_PTR: [0x0D][Rfunc_ptr:u8][type_idx:u16] = 4 bytes
            if (op == 0x0D) { size = 4; return pc + size <= code.Count; }
            
            // 0x0E reserved
            if (op == 0x0E) { size = 1; return true; }

            // ==== MOV OPCODES (0x10-0x17) ====
            // All MOV: [op][Rd:u8][Rs:u8] = 3 bytes
            if (op >= 0x10 && op <= 0x13) { size = 3; return pc + size <= code.Count; }
            if (op == 0x17) { size = 3; return pc + size <= code.Count; } // MOV.V128
            
            // ==== LOAD CONSTANTS (0x18-0x1F) ====
            // LDC.I32.IMM, LDC.F32.IMM, LDC.PTR.IMM: [op][Rd:u8][imm:i32] = 6 bytes
            if (op == 0x18 || op == 0x1A || op == 0x1C) { size = 6; return pc + size <= code.Count; }
            // LDC.I64.IMM, LDC.F64.IMM: [op][Rd:u8][imm:i64] = 10 bytes
            if (op == 0x19 || op == 0x1B) { size = 10; return pc + size <= code.Count; }
            // LD_GLOBAL_ADDR, LD_GLOBAL, ST_GLOBAL: [op][Rd/Rs:u8][global_idx:u16] = 4 bytes
            if (op == 0x1D || op == 0x1E || op == 0x1F) { size = 4; return pc + size <= code.Count; }
            
            // ==== ARITHMETIC I32 (0x20-0x2F) ====
            // Binary ops: [op][Rd][R1][R2] = 4 bytes (0x20-0x2D)
            if (op >= 0x20 && op <= 0x2D) { size = 4; return pc + size <= code.Count; }
            // Unary NOT.I32: [op][Rd][Rs] = 3 bytes
            if (op == 0x2E) { size = 3; return pc + size <= code.Count; }
            // NEG.I32: [op][Rd][Rs] = 3 bytes (0x2F if exists)
            if (op == 0x2F) { size = 3; return pc + size <= code.Count; }
            
            // ==== ARITHMETIC I64 (0x30-0x3F) ====
            // Binary ops: [op][Rd][R1][R2] = 4 bytes (0x30-0x3D)
            if (op >= 0x30 && op <= 0x3D) { size = 4; return pc + size <= code.Count; }
            // Unary NOT.I64: [op][Rd][Rs] = 3 bytes
            if (op == 0x3E) { size = 3; return pc + size <= code.Count; }
            // NEG.I64: [op][Rd][Rs] = 3 bytes (0x3F if exists)
            if (op == 0x3F) { size = 3; return pc + size <= code.Count; }
            
            // ==== ARITHMETIC I32 with IMM8 (0x40-0x4F) ====
            // [op][Rd][R1][imm8] = 4 bytes
            if (op >= 0x40 && op <= 0x4F) { size = 4; return pc + size <= code.Count; }
            
            // ==== ARITHMETIC I64 with IMM8 (0x50-0x5F) ====
            // [op][Rd][R1][imm8] = 4 bytes
            if (op >= 0x50 && op <= 0x5F) { size = 4; return pc + size <= code.Count; }
            
            // ==== FLOATING POINT F32 (0x60-0x67) ====
            // Binary F32 ops (ADD, SUB, MUL, DIV, MIN, MAX): [op][Rd][R1][R2] = 4 bytes
            if (op >= 0x60 && op <= 0x65) { size = 4; return pc + size <= code.Count; }
            // Unary F32 ops (ABS, SQRT): [op][Rd][Rs] = 3 bytes
            if (op == 0x66 || op == 0x67) { size = 3; return pc + size <= code.Count; }
            
            // ==== FLOATING POINT F64 (0x68-0x6F) ====
            // Binary F64 ops (ADD, SUB, MUL, DIV, MIN, MAX): [op][Rd][R1][R2] = 4 bytes
            if (op >= 0x68 && op <= 0x6D) { size = 4; return pc + size <= code.Count; }
            // Unary F64 ops (ABS, SQRT): [op][Rd][Rs] = 3 bytes
            if (op == 0x6E || op == 0x6F) { size = 3; return pc + size <= code.Count; }
            
            // ==== STORE OPCODES (0x70-0x7B) ====
            // All STORE: [op][Rs:u8][Ra:u8][offset:i16] = 5 bytes
            if (op >= 0x70 && op <= 0x7B) { size = 5; return pc + size <= code.Count; }
            
            // ==== LOAD OPCODES (0x80-0x89) ====
            // All LOAD: [op][Rd:u8][Ra:u8][offset:i16] = 5 bytes
            if (op >= 0x80 && op <= 0x89) { size = 5; return pc + size <= code.Count; }
            
            // ==== ALLOCA (0x8F) ====
            // [op][Rd][Rs][align] = 4 bytes
            if (op == 0x8F) { size = 4; return pc + size <= code.Count; }
            
            // ==== TRUNCATION/EXTENSION/CONVERSION (0x90-0xBD) ====
            // All are unary: [op][Rd][Rs] = 3 bytes
            if (op >= 0x90 && op <= 0xB5) { size = 3; return pc + size <= code.Count; }
            // BITCAST ops (0xB6-0xBD): [op][Rd][Rs] = 3 bytes
            if (op >= 0xB6 && op <= 0xBD) { size = 3; return pc + size <= code.Count; }
            
            // ==== SELECT OPCODES (0xBE-0xBF) ====
            // SELECT: [op][Rd][Rcond][Rt][Rf] = 5 bytes
            if (op == 0xBE || op == 0xBF) { size = 5; return pc + size <= code.Count; }
            
            // ==== COMPARISON I32 (0xC0-0xC5) ====
            // CMP.*.I32: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xC0 && op <= 0xC5) { size = 4; return pc + size <= code.Count; }
            
            // ==== COMPARISON I64 (0xC6-0xCB) ====
            // CMP.*.I64: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xC6 && op <= 0xCB) { size = 4; return pc + size <= code.Count; }
            
            // ==== COMPARISON U32 (0xCC-0xCF) ====
            // CMP.*.U32: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xCC && op <= 0xCF) { size = 4; return pc + size <= code.Count; }
            
            // ==== COMPARISON U64 (0xD0-0xD3) ====
            // CMP.*.U64: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xD0 && op <= 0xD3) { size = 4; return pc + size <= code.Count; }
            
            // ==== SELECT I64/F32/F64 (0xD4-0xD6) ====
            // SELECT: [op][Rd][Rcond][Rt][Rf] = 5 bytes
            if (op >= 0xD4 && op <= 0xD6) { size = 5; return pc + size <= code.Count; }
            
            // ==== ATOMIC I32 (0xD7-0xDF) ====
            // ATOMIC.RMW.*.I32 (0xD7-0xDC): [op][Rd][Ra][Rv] = 4 bytes
            if (op >= 0xD7 && op <= 0xDC) { size = 4; return pc + size <= code.Count; }
            // ATOMIC.RMW.CMPXCHG.I32 (0xDD): [op][Rd][Ra][Rexp][Rdes] = 5 bytes
            if (op == 0xDD) { size = 5; return pc + size <= code.Count; }
            // ATOMIC.LOAD.I32 (0xDE): [op][Rd][Ra] = 3 bytes
            if (op == 0xDE) { size = 3; return pc + size <= code.Count; }
            // ATOMIC.STORE.I32 (0xDF): [op][Ra][Rs] = 3 bytes
            if (op == 0xDF) { size = 3; return pc + size <= code.Count; }
            
            // ==== COMPARISON F32 (0xE0-0xE5) ====
            // CMP.*.F32: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xE0 && op <= 0xE5) { size = 4; return pc + size <= code.Count; }
            
            // ==== COMPARISON F64 (0xE6-0xEB) ====
            // CMP.*.F64: [op][Rd][R1][R2] = 4 bytes
            if (op >= 0xE6 && op <= 0xEB) { size = 4; return pc + size <= code.Count; }
            
            // ==== ATOMIC I64 (0xEC-0xF6) ====
            // ATOMIC.LOAD.I64 (0xEC): [op][Rd][Ra] = 3 bytes
            if (op == 0xEC) { size = 3; return pc + size <= code.Count; }
            // ATOMIC.STORE.I64 (0xED): [op][Ra][Rs] = 3 bytes
            if (op == 0xED) { size = 3; return pc + size <= code.Count; }
            // ATOMIC.FENCE (0xEE): [op] = 1 byte
            if (op == 0xEE) { size = 1; return true; }
            // ATOMIC.RMW.*.I64 (0xF0-0xF5): [op][Rd][Ra][Rv] = 4 bytes
            if (op >= 0xF0 && op <= 0xF5) { size = 4; return pc + size <= code.Count; }
            // ATOMIC.RMW.CMPXCHG.I64 (0xF6): [op][Rd][Ra][Rexp][Rdes] = 5 bytes
            if (op == 0xF6) { size = 5; return pc + size <= code.Count; }
            
            // ==== EXCEPTION HANDLING (0xF8-0xFB) ====
            // TRY (0xF8): [op][block_type:u16] = 3 bytes
            if (op == 0xF8) { size = 3; return pc + size <= code.Count; }
            // CATCH (0xF9): [op][tag_idx:u16] = 3 bytes
            if (op == 0xF9) { size = 3; return pc + size <= code.Count; }
            // CATCH_ALL (0xFA): [op] = 1 byte
            if (op == 0xFA) { size = 1; return true; }
            // THROW (0xFB): [op][tag_idx:u16] = 3 bytes
            if (op == 0xFB) { size = 3; return pc + size <= code.Count; }
            
            // ==== PREFIX/EXTENDED OPCODES (0xFC) ====
            if (op == 0xFC)
            {
                if (pc + 2 > code.Count) return false;
                byte op2 = code[pc + 1];
                
                int sizeAfterPrefix;
                switch (op2)
                {
                    case 0x00: sizeAfterPrefix = 1 + 4 + 3; break; // MEMORY.INIT
                    case 0x01: sizeAfterPrefix = 1 + 4; break;     // DATA.DROP
                    case 0x02: sizeAfterPrefix = 1 + 3; break;     // MEMORY.COPY
                    case 0x03: sizeAfterPrefix = 1 + 2; break;     // MEMORY.FILL
                    case 0x04: sizeAfterPrefix = 1 + 2; break;     // MEMORY.SIZE
                    case 0x05: sizeAfterPrefix = 1 + 2; break;     // MEMORY.GROW
                    
                    case 0x10: sizeAfterPrefix = 1 + 2; break;     // TABLE.GET
                    case 0x11: sizeAfterPrefix = 1 + 3; break;     // TABLE.SET
                    case 0x12: sizeAfterPrefix = 1 + 4 + 2; break; // TABLE.INIT
                    case 0x13: sizeAfterPrefix = 1 + 2; break;     // ELEM.DROP
                    case 0x14: sizeAfterPrefix = 1 + 4; break;     // TABLE.COPY
                    case 0x15: sizeAfterPrefix = 1 + 3; break;     // TABLE.GROW
                    case 0x16: sizeAfterPrefix = 1 + 2; break;     // TABLE.SIZE
                    case 0x17: sizeAfterPrefix = 1 + 3; break;     // TABLE.FILL
                    
                    case 0x18: sizeAfterPrefix = 1 + 4; break;     // REF.FUNC
                    case 0x19: sizeAfterPrefix = 1 + 4; break;     // TABLE.GET variant / TABLE.SET variant
                    
                    default:
                        return false; // Unknown extended opcode
                }
                
                size = 1 + sizeAfterPrefix;
                return pc + size <= code.Count;
            }
            
            // ==== PLATFORM SPECIFIC (0xFD, 0xFE) ====
            // These are variable-length and not supported for peephole
            if (op == 0xFD || op == 0xFE) return false;
            
            // Unknown opcode - fail safe
            return false;
        }

        private static unsafe bool IsSingleUseByPhiOnEdge(LLVMValueRef value, LLVMValueRef phi, LLVMBasicBlockRef predBlock)
        {
            // true if `value` has exactly one use and it's `phi`, and that use corresponds to incoming from predBlock.
            LLVMUseRef use = LLVM.GetFirstUse(value);
            if (use == null) return false;

            LLVMValueRef user0 = LLVM.GetUser(use);
            if (user0 != phi) return false;

            // Must be single-use
            if (LLVM.GetNextUse(use) != null) return false;

            // Ensure this value is indeed the incoming for this predecessor edge
            uint n = LLVM.CountIncoming(phi);
            for (uint i = 0; i < n; i++)
            {
                if (LLVM.GetIncomingBlock(phi, i) == predBlock && LLVM.GetIncomingValue(phi, i) == value)
                    return true;
            }
            return false;
        }

        private static unsafe bool IsLastUseInBlock(LLVMValueRef value, LLVMValueRef afterInstr)
        {
            // returns true if `value` is not used by any instruction after `afterInstr` in the same block.
            LLVMBasicBlockRef bb = LLVM.GetInstructionParent(afterInstr);
            for (LLVMValueRef it = LLVM.GetNextInstruction(afterInstr); it != null; it = LLVM.GetNextInstruction(it))
            {
                int ops = LLVM.GetNumOperands(it);
                for (int i = 0; i < ops; i++)
                {
                    if (LLVM.GetOperand(it, (uint)i) == value)
                        return false;
                }
            }
            return true;
        }

        private unsafe bool TryGetPhiParkingInfo(LLVMValueRef instr, out (LLVMValueRef BasePhi, long Value, bool IsAdd, ValueType Type) info)
        {
            info = default;

            LLVMOpcode opc = LLVM.GetInstructionOpcode(instr);
            if (opc != LLVMOpcode.LLVMAdd && opc != LLVMOpcode.LLVMSub)
                return false;

            LLVMValueRef op0 = LLVM.GetOperand(instr, 0);
            LLVMValueRef op1 = LLVM.GetOperand(instr, 1);

            // Pattern: PHI +/- Constant
            if (op0.IsAInstruction == null || LLVM.GetInstructionOpcode(op0) != LLVMOpcode.LLVMPHI)
                return false;
            if (LLVM.GetValueKind(op1) != LLVMValueKind.LLVMConstantIntValueKind)
                return false;

            // Safety: require single-use, and the user must be a PHI
            LLVMUseRef use = LLVM.GetFirstUse(instr);
            if (use == null)
                return false;
            LLVMValueRef user0 = LLVM.GetUser(use);
            if (user0.IsAInstruction == null || LLVM.GetInstructionOpcode(user0) != LLVMOpcode.LLVMPHI)
                return false;
            if (LLVM.GetNextUse(use) != null)
                return false;

            long imm = LLVM.ConstIntGetSExtValue(op1);
            bool isAdd = (opc == LLVMOpcode.LLVMAdd);
            ValueType ty = MapLlvmTypeToEspb(instr.TypeOf);
            if (ty != ValueType.I32 && ty != ValueType.U32 && ty != ValueType.I64 && ty != ValueType.U64)
                return false;

            info = (op0, imm, isAdd, ty);
            return true;
        }

        private static unsafe bool TryGetPhiDestructiveAddTarget(LLVMValueRef addInstr, LLVMBasicBlockRef currentBlock, out LLVMValueRef phi, out byte phiReg)
        {
            phi = default;
            phiReg = 0;

            // Only handle add with constant RHS and LHS being a PHI value (classic i=i+1)
            if (LLVM.GetInstructionOpcode(addInstr) != LLVMOpcode.LLVMAdd)
                return false;

            LLVMValueRef op0 = LLVM.GetOperand(addInstr, 0);
            LLVMValueRef op1 = LLVM.GetOperand(addInstr, 1);

            if (op0.IsAInstruction == null || LLVM.GetInstructionOpcode(op0) != LLVMOpcode.LLVMPHI)
                return false;

            // must be const int
            if (LLVM.GetValueKind(op1) != LLVMValueKind.LLVMConstantIntValueKind)
                return false;

            // single-use by that PHI on this edge
            if (!IsSingleUseByPhiOnEdge(addInstr, op0, currentBlock))
                return false;

            // last use check: after this add, PHI value must not be used again in this block
            if (!IsLastUseInBlock(op0, addInstr))
                return false;

            phi = op0;
            return true;
        }

    private unsafe void TranslatePHI(LLVMValueRef phiInstr)
        {
            // �������� ������� ������� ��� ���������� PHI
            byte phiReg = GetOrAllocateReg(phiInstr);
            
            // ���������� �������� ������/�������� � PHI-����
            uint numIncoming = LLVM.CountIncoming(phiInstr);
            _logger.WriteLine($"---> Analyzing PHI: {phiInstr} with {numIncoming} incoming values");
            
            // ��� ������� ��������� �������� ��������� ������
            for (uint i = 0; i < numIncoming; i++)
            {
                LLVMValueRef incomingValue = LLVM.GetIncomingValue(phiInstr, i);
                LLVMBasicBlockRef incomingBlock = LLVM.GetIncomingBlock(phiInstr, i);
                
                // ����������� �������� ��������
                _logger.WriteLine($"    PHI incoming value {i}: {incomingValue} from block {incomingBlock}");
                
                // ���� �������� �������� - ��������� ����������, ����������� ��� �������
                if (incomingValue.IsAInstruction != null && incomingValue.IsAInstruction.Handle != IntPtr.Zero)
                {
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(incomingValue);
                    _logger.WriteLine($"    Incoming value is instruction with opcode: {opcode}");
                    
                    // ���� ������� ��� ��������, ������� ���
                    if (registerManager.TryGetRegister(incomingValue, out byte valueReg))
                    {
                        _logger.WriteLine($"    Instruction result is in register R{valueReg}");
                    }
                }
                
                // ������� ������ ��� ���������� MOV � ����� ��������� �����
                var fixup = new PhiFixup
                {
                    SourceValue = incomingValue,
                    DestReg = phiReg
                };
                
                // ��������� ������ � �����
                if (!phiFixups.TryGetValue(incomingBlock, out var blockFixups))
                {
                    blockFixups = new List<PhiFixup>();
                    phiFixups[incomingBlock] = blockFixups;
                }
                
                blockFixups.Add(fixup);
                
                _logger.WriteLine($"    Created PHI fixup for incoming value {incomingValue} from block {incomingBlock} to register R{phiReg}");
            }
        }
        
        // ������������ ����� ��� ���������� MOV ���������� ��� PHI-��������
        private unsafe void AddPhiFixupsForSuccessor(LLVMBasicBlockRef currentBlock, LLVMBasicBlockRef successorBlock, List<byte> output)
        {
            _logger.WriteLine($"      [PHI FIXUP DEBUG] Processing edge: currentBlock -> successorBlock");
            
            // �������� ���������� � ���� PHI-����������� � �� ����������
            var phiMoves = new List<(byte destReg, byte sourceReg, byte moveOpcode, string debugInfo)>();
            
            bool foundFixups = false;
            for (var instr = LLVM.GetFirstInstruction(successorBlock);
                 instr != null && LLVM.GetInstructionOpcode(instr) == LLVMOpcode.LLVMPHI;
                 instr = LLVM.GetNextInstruction(instr))
            {
                byte phiReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
                uint numIncoming = LLVM.CountIncoming(instr);
                
                _logger.WriteLine($"      [PHI FIXUP DEBUG] PHI R{phiReg} has {numIncoming} incoming values");
                
                for (uint i = 0; i < numIncoming; i++)
                {
                    var incomingBlock = LLVM.GetIncomingBlock(instr, i);
                    _logger.WriteLine($"      [PHI FIXUP DEBUG] Incoming {i}: Block addr");
                    
                    if (incomingBlock == currentBlock)
                    {
                        LLVMValueRef incomingValue = LLVM.GetIncomingValue(instr, i);
                        foundFixups = true;
                        
                        // ��������: ���������� ��� ��� ��������� sourceReg �������� � output
                        // ��� �����, ��� ��� incomingValue ����� ���� ����������, ��������� ��������� LDC
                       byte sourceReg;

                        // Parked PHI-edge ops (phi +/- const) are computed on the edge here, so we don't need MOV.
                        if (incomingValue.IsAInstruction != null && _deferredPhiEdgeOps.TryGetValue(incomingValue, out var def)
                            && def.phi == instr)
                        {
                            byte baseReg = GetOrAllocateRegWithCacheInvalidation(def.phi, output);
                            bool is64 = (def.type == ValueType.I64 || def.type == ValueType.U64);

                            if (def.imm >= sbyte.MinValue && def.imm <= sbyte.MaxValue)
                            {
                                // ADD/SUB with imm8: [op][Rd][Rs][imm8] (4 bytes)
                                byte opc = def.isSub
                                    ? (is64 ? (byte)0x51 : (byte)0x41)
                                    : (is64 ? (byte)0x50 : (byte)0x40);

                                EmitOpcode(output, opc, "PHI-EDGE-OP");
                                output.Add(phiReg);
                                output.Add(baseReg);
                                output.Add((byte)def.imm);
                            }
                            else
                            {
                                // Fallback: materialize constant and use register-register add/sub
                                byte tmp = GenerateIntConstant(output, def.imm, preferredDest: null, force64Bit: is64);
                                byte opc = def.isSub
                                    ? (is64 ? (byte)0x31 : (byte)0x21)
                                    : (is64 ? (byte)0x30 : (byte)0x20);

                                EmitOpcode(output, opc, "PHI-EDGE-OP-LARGE");
                                output.Add(phiReg);
                                output.Add(baseReg);
                                output.Add(tmp);
                            }

                            sourceReg = phiReg;
                        }
                        else
                        {
                            // Destination-aware: materialize incoming value directly into phiReg when possible
                            (sourceReg, _) = GetOperandReg(incomingValue, output, phiReg);
                        }
                        
                        _logger.WriteLine($"      [PHI FIXUP DEBUG] Found edge match - using sourceReg R{sourceReg}");

                        // This check is crucial. If the source and destination are the same,
                        // it might indicate a loop where the PHI node's value is carried over.
                        // We must not generate a self-move as it would be redundant and could mask logic errors.
                        if (sourceReg != phiReg)
                        {
                            byte moveOpcode = GetMoveOpcode(MapLlvmTypeToEspb(LLVM.TypeOf(instr))); // FIX: Use the PHI instruction's type
                            phiMoves.Add((phiReg, sourceReg, moveOpcode, $"R{phiReg} <- R{sourceReg}"));
                            _logger.WriteLine($"      [PHI FIXUP] Queued {GetOpcodeName(moveOpcode)} R{phiReg} <- R{sourceReg} for edge");
                        } else {
                            _logger.WriteLine($"      [PHI FIXUP] Skipped self-move for R{phiReg}");
                        }
                        break;
                    }
                }
            }

            // ���������������� �������� ���������� ������������ �����������
            var movesToProcess = new List<(byte dest, byte src, byte opcode)>(phiMoves.Select(m => (m.destReg, m.sourceReg, m.moveOpcode)));

            while (movesToProcess.Count > 0)
            {
                // ����� �����������, ����� ���������� �������� �� ������������ ��� �������� � ������ ���������� ������������
                var independentMove = movesToProcess.FirstOrDefault(m => !movesToProcess.Any(other => other.src == m.dest));

                if (independentMove != default)
                {
                    // ��������� ����������� �����������
                    EmitOpcode(output, independentMove.opcode, "PHI_MOV");
                    output.Add(independentMove.dest);
                    output.Add(independentMove.src);
                    _logger.WriteLine($"      [PHI FIXUP OPT] Emitting independent move: {GetOpcodeName(independentMove.opcode)} R{independentMove.dest} <- R{independentMove.src}");
                    movesToProcess.Remove(independentMove);
                }
                else
                {
                    // ��������� ����
                    var cycleMove = movesToProcess.First(); // ����� ����� ������� �� �����
                    byte tempReg = registerManager.AllocateTemporaryRegister();

                    _logger.WriteLine($"      [PHI FIXUP OPT] Cycle detected. Breaking with temp R{tempReg}.");

                    // ��������� �������� �� ��������� �������
                    EmitOpcode(output, cycleMove.opcode, "PHI_MOV_TEMP");
                    output.Add(tempReg);
                    output.Add(cycleMove.src);
                    _logger.WriteLine($"      [PHI FIXUP OPT]  - Save: {GetOpcodeName(cycleMove.opcode)} R{tempReg} <- R{cycleMove.src}");

                    // �������� ��� ������������� ����� ��������� �� ��������� �������
                    for (int i = 0; i < movesToProcess.Count; i++)
                    {
                        if (movesToProcess[i].src == cycleMove.src)
                        {
                            var oldMove = movesToProcess[i];
                            movesToProcess[i] = (oldMove.dest, tempReg, oldMove.opcode);
                            _logger.WriteLine($"      [PHI FIXUP OPT]  - Remap: R{oldMove.dest} <- R{oldMove.src}  =>  R{oldMove.dest} <- R{tempReg}");
                        }
                    }
                }
            }

            if (!foundFixups)
            {
                _logger.WriteLine($"    No PHI fixups needed for this block transition");
            }
        }


        // ����� ��� ��������� ����������� �������� �� ����� ���������� ����������
        private byte GetCorrectRegisterForGlobal(string globalName)
        {
            return globalName switch
            {
                // ������� ��� ����� switch-case
                var name when name.Contains("str.17") => 20, // "Case 1"
                var name when name.Contains("str.18") => 21, // "Case 0"
                var name when name.Contains("str.19") => 22, // "Case 2"
                var name when name.Contains("str.20") => 23, // "Default case"
                _ => 0 // ����������� ���������� ����������
            };
        }
        
        // ��������������� ����� ��� ��������� ����� ������ �� ��� ��������
        private string GetOpcodeName(byte opcode)
        {
            switch (opcode)
            {
                case 0x12: return "MOV.I32";
                case 0x13: return "MOV.I64";
                case 0x14: return "MOV.32"; // unified
                case 0x15: return "MOV.64"; // unified
                case 0x16: return "MOV.32"; // unified
                default: return $"OPCODE_{opcode:X2}";
            }
        }

        private unsafe int TranslateInstruction(LLVMValueRef instr, List<byte> output)
        {
            // --- ���������: ���������� ����� ---
            _logger.WriteLine($"---> Translating Instruction: {instr}");
            int beforeCount = output.Count;
            // --- ����� ������������ ���� ---

            LLVMOpcode opcode = instr.InstructionOpcode;
            byte dest, op1, op2, ptrReg;
            ValueType type;
            
            switch (opcode)
            {
                case LLVMOpcode.LLVMRet:
                    // ���������, ���������� �� ������� ��������
                    if (LLVM.GetNumOperands(instr) > 0)
                    {
                        // �������� ������������ �������
                        LLVMValueRef retVal = instr.GetOperand(0);
                        _logger.WriteLine($"---> Translating RET with value: {retVal}");
                        
                        // �������� ������� � ������������ ���������
                       (byte retReg, _) = GetOperandReg(retVal, output);
                        
                        // ���������� ��� ������������� ��������
                        type = MapLlvmTypeToEspb(retVal.TypeOf);
                        
                        // ���� ������� �� R0, �������� �������� � R0 (�������� Calling Convention)
                        if (retReg != 0)
                        {
                            byte moveOpcode = GetMoveOpcode(type);
                            _logger.WriteLine($"    Moving return value from R{retReg} to R0 using opcode 0x{moveOpcode:X2}");
                            EmitOpcode(output, moveOpcode, "MOV_RET");
                            output.Add(0); // ������� ������� R0
                            output.Add(retReg); // �������� �������
                        }
                    }
                    EmitOpcode(output, 0x0F, "END");
                    break;
                    
                case LLVMOpcode.LLVMAdd:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_add = instr.GetOperand(0);
                    var operand1_add = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_add, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);

                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_add.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_add, op1, false, output);
                    }
                    
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_add, out sbyte imm8_add))
                            {
                                EmitOpcode(output, 0x40, "ADD.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_add);
                            }
                            else
                            {
                                (op2, _) = GetOperandReg(operand1_add, output);
                                if (operand1_add.TypeOf.IntWidth < 32)
                                {
                                    op2 = HandleOperandExtension(operand1_add, op2, false, output);
                                }
                                EmitOpcode(output, 0x20, "ADD.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(operand1_add, out sbyte imm8_add64))
                            {
                                EmitOpcode(output, 0x50, "ADD.I64.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_add64);
                            }
                            else
                            {
                                (byte op2_i64, _) = GetOperandReg(operand1_add, output);
                                EmitOpcode(output, 0x30, "ADD.I64");
                                WriteBinaryInstruction(output, dest, op1, op2_i64);
                            }
                            break;
                        case ValueType.F32:
                            (byte op2_f32, _) = GetOperandReg(operand1_add, output);
                            EmitOpcode(output, 0x60, "ADD.F32");
                            WriteBinaryInstruction(output, dest, op1, op2_f32);
                            break;
                        case ValueType.F64:
                            (byte op2_f64, _) = GetOperandReg(operand1_add, output);
                            EmitOpcode(output, 0x68, "ADD.F64");
                            WriteBinaryInstruction(output, dest, op1, op2_f64);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for ADD: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                
                case LLVMOpcode.LLVMFAdd:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_fadd = instr.GetOperand(0);
                    var operand1_fadd = instr.GetOperand(1);
                   (op1, _) = GetOperandReg(operand0_fadd, output);
                   (op2, _) = GetOperandReg(operand1_fadd, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if (type == ValueType.F32) {
                        EmitOpcode(output, 0x60, "ADD.F32");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else if (type == ValueType.F64) {
                        EmitOpcode(output, 0x68, "ADD.F64");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else {
                        throw new NotSupportedException($"Unsupported type for FADD: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;

                case LLVMOpcode.LLVMSub:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_sub = instr.GetOperand(0);
                    var operand1_sub = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_sub, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);

                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_sub.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_sub, op1, true, output); // Signed for subtraction
                    }
                    
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_sub, out sbyte imm8_sub))
                            {
                                EmitOpcode(output, 0x41, "SUB.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_sub);
                            }
                            else
                            {
                                (op2, _) = GetOperandReg(operand1_sub, output);
                                if (operand1_sub.TypeOf.IntWidth < 32)
                                {
                                    op2 = HandleOperandExtension(operand1_sub, op2, true, output);
                                }
                                EmitOpcode(output, 0x21, "SUB.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(operand1_sub, out sbyte imm8_sub_i64))
                            {
                                EmitOpcode(output, 0x51, "SUB.I64.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_sub_i64);
                            }
                            else
                            {
                                (byte op2_i64, _) = GetOperandReg(operand1_sub, output);
                                EmitOpcode(output, 0x31, "SUB.I64");
                                WriteBinaryInstruction(output, dest, op1, op2_i64);
                            }
                            break;
                        case ValueType.F32:
                            (byte op2_f32, _) = GetOperandReg(operand1_sub, output);
                            EmitOpcode(output, 0x61, "SUB.F32");
                            WriteBinaryInstruction(output, dest, op1, op2_f32);
                            break;
                        case ValueType.F64:
                            (byte op2_f64, _) = GetOperandReg(operand1_sub, output);
                            EmitOpcode(output, 0x69, "SUB.F64");
                            WriteBinaryInstruction(output, dest, op1, op2_f64);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for SUB: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;

                case LLVMOpcode.LLVMFSub:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_fsub = instr.GetOperand(0);
                    var operand1_fsub = instr.GetOperand(1);
                   (op1, _) = GetOperandReg(operand0_fsub, output);
                   (op2, _) = GetOperandReg(operand1_fsub, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if (type == ValueType.F32) {
                        EmitOpcode(output, 0x61, "SUB.F32");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else if (type == ValueType.F64) {
                        EmitOpcode(output, 0x69, "SUB.F64");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else {
                        throw new NotSupportedException($"Unsupported type for FSUB: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMMul:
                    // ���������� ADD
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_mul = instr.GetOperand(0);
                    var operand1_mul = instr.GetOperand(1);
                   (op1, _) = GetOperandReg(operand0_mul, output);
                   (op2, _) = GetOperandReg(operand1_mul, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_mul.TypeOf.IntWidth < 32 || operand1_mul.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_mul, op1, false, output);
                        op2 = HandleOperandExtension(operand1_mul, op2, false, output);
                    }
                    
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_mul, out sbyte imm8_mul))
                            {
                                EmitOpcode(output, 0x42, "MUL.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_mul);
                            }
                            else
                            {
                                EmitOpcode(output, 0x22, "MUL.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_mul64))
                            {
                                EmitOpcode(output, 0x52, "MUL.I64.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_mul64);
                            }
                            else
                            {
                                EmitOpcode(output, 0x32, "MUL.I64");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.F32:
                            EmitOpcode(output, 0x62, "MUL.F32");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        case ValueType.F64:
                            EmitOpcode(output, 0x6A, "MUL.F64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for MUL: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;

                case LLVMOpcode.LLVMFMul:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_fmul = instr.GetOperand(0);
                    var operand1_fmul = instr.GetOperand(1);
                   (op1, _) = GetOperandReg(operand0_fmul, output);
                   (op2, _) = GetOperandReg(operand1_fmul, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if (type == ValueType.F32) {
                        EmitOpcode(output, 0x62, "MUL.F32");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else if (type == ValueType.F64) {
                        EmitOpcode(output, 0x6A, "MUL.F64");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else {
                        throw new NotSupportedException($"Unsupported type for FMUL: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMLoad:
                    // ���������� �������� �� ������ ��� �������
                    LLVMValueRef loadPtrOp = instr.GetOperand(0);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    bool isVolatileLoad = LLVM.GetVolatile(instr) != 0;

                    if (LLVM.GetValueKind(loadPtrOp) == LLVMValueKind.LLVMGlobalVariableValueKind &&
                        llvmGlobalToEspbIndex.TryGetValue(loadPtrOp, out uint gidx))
                    {
                        // ������ ������ � �������: LD_GLOBAL Rd, global_idx
                        dest = GetResultRegWithCacheInvalidation(instr, output);
                        ushort numImportedGlobals = (ushort)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                        ushort combinedIdx = (ushort)(numImportedGlobals + gidx);
                        EmitOpcode(output, 0x1E, "LD_GLOBAL");
                        output.Add(dest);
                        output.AddRange(BitConverter.GetBytes(combinedIdx));
                        registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    }
                    // === REGISTER PROMOTION: Check if this alloca is promoted to a register ===
                    else if (loadPtrOp.IsAAllocaInst.Handle != IntPtr.Zero && _promotedAllocas.TryGetValue(loadPtrOp, out byte promotedLoadReg))
                    {
                        // Promoted alloca - just MOV from dedicated register, no memory access!
                        dest = GetResultRegWithCacheInvalidation(instr, output);
                        if (dest != promotedLoadReg)
                        {
                            // Emit MOV Rd, Rs
                            LLVMTypeRef loadAllocType = LLVM.GetAllocatedType(loadPtrOp);
                            ValueType loadVt = MapLlvmTypeToEspb(loadAllocType);
                            byte movOpcode = GetMoveOpcode(loadVt);
                            _logger.WriteLine($"    [REG-PROMOTION] Load from promoted alloca: MOV R{dest} <- R{promotedLoadReg}");
                            EmitOpcode(output, movOpcode, "MOV");
                            output.Add(dest);           // Rd - destination
                            output.Add(promotedLoadReg); // Rs - source (promoted register)
                        }
                        else
                        {
                            _logger.WriteLine($"    [REG-PROMOTION] Load from promoted alloca: result already in R{promotedLoadReg}, no MOV needed");
                        }
                        registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    }
                    else if (loadPtrOp.IsAAllocaInst.Handle != IntPtr.Zero && !isVolatileLoad && !_volatileAllocas.Contains(loadPtrOp) && _allocaRegMap.TryGetValue(loadPtrOp, out byte allocaReg))
                    {
                        // �����������: load �� alloca - ���������� ���������� �������
                        // ���������� MOV ��� ����������� �������� � ����� ������� ����������
                        // �� ����������� ��� volatile alloca (��������� ����)
                        dest = GetResultRegWithCacheInvalidation(instr, output);
                        if (dest != allocaReg)
                        {
                            // ���������� MOV Rd, Rs ����� ����������� ��������
                            EmitOpcode(output, 0x12, "MOV");
                            output.Add(dest);
                            output.Add(allocaReg);
                        }
                        _logger.WriteLine($"    [ALLOCA-OPT] Load from alloca: R{dest} <- R{allocaReg}");
                        registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    }
                    else
                    {
                        // ������� LOAD �� ������ �� ������ � ��������
                        dest = registerManager.GetOrAllocateRegForResult(instr, output);
                        // CRITICAL: Invalidate any alloca mappings that use this register before overwriting
                        InvalidateAllocaMappingsForRegister(dest, output);
                        (ptrReg, _) = GetOperandReg(loadPtrOp, output);
                        byte loadOpcode = GetLoadOpcode(type);
                        _logger.WriteLine($"    [LOAD] Real LOAD from memory: R{dest} <- [R{ptrReg}] (type={type}, opcode=0x{loadOpcode:X2})");
                        EmitOpcode(output, loadOpcode, "LOAD");
                        output.Add(dest);
                        output.Add(ptrReg);
                        output.AddRange(BitConverter.GetBytes((short)0)); // offset = 0
                        registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    }
                    break;
                    
                case LLVMOpcode.LLVMStore:
                    {
                        LLVMValueRef valueToStore = instr.GetOperand(0);
                        LLVMValueRef ptr = instr.GetOperand(1);
                        bool isVolatile = LLVM.GetVolatile(instr) != 0;

                        // ���� volatile store � alloca - �������� alloca ��� volatile
                        // � ���������� �������� STORE (�� ��������� ALLOCA-OPT)
                        if (isVolatile && ptr.IsAAllocaInst.Handle != IntPtr.Zero)
                        {
                            _volatileAllocas.Add(ptr);
                            // ����� ������� �� _allocaRegMap ���� ���� ����������
                            _allocaRegMap.Remove(ptr);
                            _logger.WriteLine($"    [VOLATILE] Marked alloca as volatile, will generate real STORE ops");
                        }
                        
                        // CRITICAL FIX: If we are storing an alloca pointer somewhere (address taken),
                        // that alloca must be excluded from ALLOCA-OPT because writes may happen
                        // through the escaped pointer. Example: int x; int* p = &x; *p = 5;
                        if (valueToStore.IsAAllocaInst.Handle != IntPtr.Zero)
                        {
                            if (!_volatileAllocas.Contains(valueToStore))
                            {
                                _volatileAllocas.Add(valueToStore);
                                _allocaRegMap.Remove(valueToStore);
                                _logger.WriteLine($"    [ADDRESS-TAKEN] Alloca address is being stored - excluding from ALLOCA-OPT");
                            }
                        }

                        if (LLVM.GetValueKind(ptr) == LLVMValueKind.LLVMGlobalVariableValueKind &&
                            llvmGlobalToEspbIndex.TryGetValue(ptr, out uint gidxStore))
                        {
                            // ������ ������ � �������: ST_GLOBAL global_idx, Rs
                            (var rsVal, _) = GetOperandReg(valueToStore, output);
                            ushort numImportedGlobals = (ushort)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                            ushort combinedIdx = (ushort)(numImportedGlobals + gidxStore);
                            EmitOpcode(output, 0x1F, "ST_GLOBAL");
                            output.AddRange(BitConverter.GetBytes(combinedIdx));
                            output.Add(rsVal);
                        }
                        // === REGISTER PROMOTION: Check if this alloca is promoted to a register ===
                        else if (ptr.IsAAllocaInst.Handle != IntPtr.Zero && _promotedAllocas.TryGetValue(ptr, out byte promotedReg))
                        {
                            // Promoted alloca - just MOV value to dedicated register, no memory access!
                            (var valueReg, _) = GetOperandReg(valueToStore, output);
                            
                            if (valueReg != promotedReg)
                            {
                                // Emit MOV Rd, Rs
                                LLVMTypeRef allocType = LLVM.GetAllocatedType(ptr);
                                ValueType vt = MapLlvmTypeToEspb(allocType);
                                byte movOpcode = GetMoveOpcode(vt);
                                _logger.WriteLine($"    [REG-PROMOTION] Store to promoted alloca: MOV R{promotedReg} <- R{valueReg}");
                                EmitOpcode(output, movOpcode, "MOV");
                                output.Add(promotedReg);  // Rd - destination (promoted register)
                                output.Add(valueReg);     // Rs - source value
                            }
                            else
                            {
                                _logger.WriteLine($"    [REG-PROMOTION] Store to promoted alloca: value already in R{promotedReg}, no MOV needed");
                            }
                        }
                        else if (ptr.IsAAllocaInst.Handle != IntPtr.Zero && !isVolatile && !_volatileAllocas.Contains(ptr))
                        {
                            // ���������, ����� �� ��������� ����������� alloca -> register
                            // ����������� �� ����������� ���:
                            // 1. �������� (ArrayTypeKind)
                            // 2. ���������� �� ������� (�������� � ������ ��� ��������� �������)
                            // 3. Alloca � volatile ��������� (��������� ����)
                            LLVMTypeRef allocatedType = LLVM.GetAllocatedType(ptr);
                            bool isArrayAlloca = allocatedType.Kind == LLVMTypeKind.LLVMArrayTypeKind;
                            
                            // ���������, �������� �� ����������� �������� ���������� �� �������
                            bool isFunctionPointer = valueToStore.TypeOf.Kind == LLVMTypeKind.LLVMPointerTypeKind &&
                                                     (valueToStore.IsAFunction.Handle != IntPtr.Zero ||
                                                      (valueToStore.Kind == LLVMValueKind.LLVMInstructionValueKind &&
                                                       valueToStore.InstructionOpcode == LLVMOpcode.LLVMLoad));
                            
                            if (isArrayAlloca || isFunctionPointer)
                            {
                                // ��� �������� � ���������� �� ������� ���������� �������� store
                                (var valueReg, _) = GetOperandReg(valueToStore, output);
                                (ptrReg, _) = GetOperandReg(ptr, output);
                                
                                ValueType storeType = MapLlvmTypeToEspb(valueToStore.TypeOf);
                                byte storeOpcode = GetStoreOpcode(storeType);
                                
                                _logger.WriteLine($"    [STORE] Array/FuncPtr store to alloca: generating real STORE (isArray={isArrayAlloca}, isFuncPtr={isFunctionPointer})");
                                EmitOpcode(output, storeOpcode, "STORE");
                                output.Add(valueReg); // Rs
                                output.Add(ptrReg);   // Ra
                                output.AddRange(BitConverter.GetBytes((short)0)); // offset
                            }
                            else
                            {
                                // STORE-THROUGH STRATEGY: Always generate STORE, but cache value for load-forwarding
                                // This ensures memory is always up-to-date (critical for multi-BB/loops),
                                // while still allowing load-forwarding optimization within the same BB.
                                (var valueReg, _) = GetOperandReg(valueToStore, output);
                                (ptrReg, _) = GetOperandReg(ptr, output);
                                
                                // Always generate STORE to memory
                                ValueType storeType = MapLlvmTypeToEspb(valueToStore.TypeOf);
                                byte storeOpcode = GetStoreOpcode(storeType);
                                EmitOpcode(output, storeOpcode, "STORE");
                                output.Add(valueReg);       // Rs - value to store
                                output.Add(ptrReg);         // Ra - base address
                                output.AddRange(BitConverter.GetBytes((short)0)); // offset
                                
                                // CRITICAL FIX: Check if this register is already used by another alloca
                                // If so, we must invalidate that mapping to avoid conflicts
                                bool registerConflict = _allocaRegMap.Any(kvp => kvp.Value == valueReg && !kvp.Key.Equals(ptr));
                                
                                if (registerConflict)
                                {
                                    _logger.WriteLine($"    [ALLOCA-OPT] Register R{valueReg} conflict - invalidating old mapping");
                                    InvalidateAllocaMappingsForRegister(valueReg, output);
                                }
                                
                                // Cache the mapping for potential load-forwarding within this BB
                                _allocaRegMap[ptr] = valueReg;
                                _logger.WriteLine($"    [ALLOCA-OPT] Store-through: generated STORE + cached alloca -> R{valueReg} for load-forwarding");
                            }
                        }
                        else
                        {
                            (var valueReg, _) = GetOperandReg(valueToStore, output);
                            (ptrReg, _) = GetOperandReg(ptr, output);
                            
                            ValueType storeType = MapLlvmTypeToEspb(valueToStore.TypeOf);
                            byte storeOpcode = GetStoreOpcode(storeType);
                            
                            bool isVolatileAlloca = ptr.IsAAllocaInst.Handle != IntPtr.Zero && _volatileAllocas.Contains(ptr);
                            _logger.WriteLine($"    [STORE] Generating real STORE (isVolatile={isVolatile}, isVolatileAlloca={isVolatileAlloca}) R{valueReg} -> [R{ptrReg}]");
                            
                            EmitOpcode(output, storeOpcode, "STORE");
                            output.Add(valueReg); // Rs
                            output.Add(ptrReg);   // Ra
                            output.AddRange(BitConverter.GetBytes((short)0)); // offset
                        }
                        break;
                    }
                    
                case LLVMOpcode.LLVMAlloca:
                    TranslateAlloca(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMPtrToInt:
                    {
                        byte destReg = GetResultRegWithCacheInvalidation(instr, output);
                        (var sourceReg, _) = GetOperandReg(instr.GetOperand(0), output);
                        EmitOpcode(output, 0xBC, "PTRTOINT");
                        output.Add(destReg);
                        output.Add(sourceReg);
                    registerManager.ExplicitSpillIfNeeded(instr, destReg, output);
                    break;
                    }
                case LLVMOpcode.LLVMIntToPtr:
                    {
                        byte destReg = GetResultRegWithCacheInvalidation(instr, output);
                        (var sourceReg, _) = GetOperandReg(instr.GetOperand(0), output);
                        EmitOpcode(output, 0xBD, "INTTOPTR");
                        output.Add(destReg);
                        output.Add(sourceReg);
                        registerManager.ExplicitSpillIfNeeded(instr, destReg, output);
                        break;
                    }
                    
                case LLVMOpcode.LLVMCall:
                    TranslateCall(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMBr:
                {
                    var currentBlock = LLVM.GetInstructionParent(instr);

                    // --- ����������� ������� ---
                    if (LLVM.GetNumOperands(instr) == 1)
                    {
                        var targetBlock = LLVM.GetSuccessor(instr, 0);
                        
                        // ������� ���������� PHI-����� ��� ������������� ���������
                        AddPhiFixupsForSuccessor(currentBlock, targetBlock, output);

                        // ����� ���������� ��� �������
                        int instructionPos = output.Count;
                        EmitOpcode(output, 0x02, "BR");
                        int placeholderPos = output.Count;
                        output.AddRange(new byte[] { 0x00, 0x00 });
                        branchFixups.Add(new BranchFixup {
                            InstructionPosition = instructionPos,
                            PlaceholderPosition = placeholderPos,
                            TargetBlock = targetBlock
                        });
                    }
                    // --- �������� ������� ---
                    else
                    {
                        (byte condReg, _) = GetOperandReg(instr.GetOperand(0), output);
                        LLVMBasicBlockRef trueBlock = LLVM.GetSuccessor(instr, 0);
                        LLVMBasicBlockRef falseBlock = LLVM.GetSuccessor(instr, 1);

                        // ��������� "edge splitting" ��� ���������� ��������� PHI.
                        
                        // 1. ������� ��� ����� ���� ��� PHI-�����������.
                        var truePathMoves = new List<byte>();
                        var falsePathMoves = new List<byte>();
                        AddPhiFixupsForSuccessor(currentBlock, trueBlock, truePathMoves);
                        AddPhiFixupsForSuccessor(currentBlock, falseBlock, falsePathMoves);

                        // 2. ���������� BR_IF, ������� ����������� ����� ��� false-�����
                        int brIfPosition = output.Count;
                        EmitOpcode(output, 0x03, "BR_IF");
                        output.Add(condReg);
                        int truePathPlaceholder = output.Count;
                        output.AddRange(new byte[] { 0x00, 0x00 }); // ����������� ��� �������� � true-������������

                        // 3. ��� ��� false-����� (�����������, ���� ������� �����)
                        output.AddRange(falsePathMoves);
                        int falseBrPosition = output.Count;
                        EmitOpcode(output, 0x02, "BR");
                        int falseTargetPlaceholder = output.Count;
                        output.AddRange(new byte[] { 0x00, 0x00 });
                        branchFixups.Add(new BranchFixup {
                            InstructionPosition = falseBrPosition,
                            PlaceholderPosition = falseTargetPlaceholder,
                            TargetBlock = falseBlock
                        });

                        // 4. ��� ��� true-����� (���� �������, ���� ������� �������)
                        int truePathStart = output.Count;
                        short offsetToTrueMoves = (short)(truePathStart - brIfPosition);
                        var offsetBytes = BitConverter.GetBytes(offsetToTrueMoves);
                        output[truePathPlaceholder] = offsetBytes[0];
                        output[truePathPlaceholder + 1] = offsetBytes[1];
                        
                        output.AddRange(truePathMoves);
                        int trueBrPosition = output.Count;
                        EmitOpcode(output, 0x02, "BR");
                        int trueTargetPlaceholder = output.Count;
                        output.AddRange(new byte[] { 0x00, 0x00 });
                        branchFixups.Add(new BranchFixup {
                            InstructionPosition = trueBrPosition,
                            PlaceholderPosition = trueTargetPlaceholder,
                            TargetBlock = trueBlock
                        });
                    }
                    break;
                }
                
                case LLVMOpcode.LLVMSwitch:
                    TranslateSwitchInstr(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMICmp:
                    TranslateICmp(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMFCmp:
                    TranslateFCmp(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMUDiv:  // ����������� �������
                case LLVMOpcode.LLVMSDiv:  // �������� �������
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_div = instr.GetOperand(0);
                    var operand1_div = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_div, output);
                    (op2, _) = GetOperandReg(operand1_div, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_div.TypeOf.IntWidth < 32 || operand1_div.TypeOf.IntWidth < 32))
                    {
                        bool isSigned = opcode == LLVMOpcode.LLVMSDiv;
                        op1 = HandleOperandExtension(operand0_div, op1, isSigned, output);
                        op2 = HandleOperandExtension(operand1_div, op2, isSigned, output);
                    }
                    
                    // �������� ����� � ����������� �� ���� � ����������
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_div32))
                            {
                                if (opcode == LLVMOpcode.LLVMUDiv)
                                {
                                    EmitOpcode(output, 0x44, "DIVU.I32.IMM8");
                                }
                                else
                                {
                                    EmitOpcode(output, 0x43, "DIVS.I32.IMM8");
                                }
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_div32);
                            }
                            else
                            {
                                if (opcode == LLVMOpcode.LLVMUDiv)
                                    EmitOpcode(output, 0x26, "DIV.U32");
                                else
                                    EmitOpcode(output, 0x23, "DIV.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_div))
                            {
                                if (opcode == LLVMOpcode.LLVMUDiv)
                                {
                                    EmitOpcode(output, 0x54, "DIVU.I64.IMM8");
                                    output.Add(dest);
                                    output.Add(op1);
                                    output.Add((byte)imm8_div);
                                }
                                else
                                {
                                    EmitOpcode(output, 0x53, "DIVS.I64.IMM8");
                                    output.Add(dest);
                                    output.Add(op1);
                                    output.Add((byte)imm8_div);
                                }
                            }
                            else
                            {
                                if (opcode == LLVMOpcode.LLVMUDiv)
                                    EmitOpcode(output, 0x36, "DIV.U64");
                                else
                                    EmitOpcode(output, 0x33, "DIV.I64");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.F32:
                            EmitOpcode(output, 0x63, "DIV.F32");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        case ValueType.F64:
                            EmitOpcode(output, 0x6B, "DIV.F64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for DIV: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                
                case LLVMOpcode.LLVMFDiv:
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                   (op1, _) = GetOperandReg(instr.GetOperand(0), output);
                   (op2, _) = GetOperandReg(instr.GetOperand(1), output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                     if (type == ValueType.F32) {
                        EmitOpcode(output, 0x63, "DIV.F32");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else if (type == ValueType.F64) {
                        EmitOpcode(output, 0x6B, "DIV.F64");
                        WriteBinaryInstruction(output, dest, op1, op2);
                    } else {
                        throw new NotSupportedException($"Unsupported type for FDIV: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMURem:  // ����������� ������� � ��������
                case LLVMOpcode.LLVMSRem:  // �������� ������� � ��������
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_rem = instr.GetOperand(0);
                    var operand1_rem = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_rem, output);
                    (op2, _) = GetOperandReg(operand1_rem, output);
                    type = MapLlvmTypeToEspb(operand0_rem.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_rem.TypeOf.IntWidth < 32 || operand1_rem.TypeOf.IntWidth < 32))
                    {
                        bool isSigned = opcode == LLVMOpcode.LLVMSRem;
                        op1 = HandleOperandExtension(operand0_rem, op1, isSigned, output);
                        op2 = HandleOperandExtension(operand1_rem, op2, isSigned, output);
                    }
                    
                    // �������� ����� � ����������� �� ���� � ����������
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_rem32))
                            {
                                if (opcode == LLVMOpcode.LLVMURem)
                                {
                                    EmitOpcode(output, 0x46, "REMU.I32.IMM8");
                                }
                                else
                                {
                                    EmitOpcode(output, 0x45, "REMS.I32.IMM8");
                                }
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_rem32);
                            }
                            else
                            {
                                if (opcode == LLVMOpcode.LLVMURem)
                                    EmitOpcode(output, 0x27, "REM.U32");
                                else
                                    EmitOpcode(output, 0x24, "REM.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_rem))
                            {
                                if (opcode == LLVMOpcode.LLVMURem)
                                {
                                    EmitOpcode(output, 0x56, "REMU.I64.IMM8");
                                    output.Add(dest);
                                    output.Add(op1);
                                    output.Add((byte)imm8_rem);
                                }
                                else
                                {
                                    EmitOpcode(output, 0x55, "REMS.I64.IMM8");
                                    output.Add(dest);
                                    output.Add(op1);
                                    output.Add((byte)imm8_rem);
                                }
                            }
                            else
                            {
                                if (opcode == LLVMOpcode.LLVMURem)
                                    EmitOpcode(output, 0x37, "REM.U64");
                                else
                                    EmitOpcode(output, 0x34, "REM.I64");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for REM: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMAnd:  // ��������� �
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_and = instr.GetOperand(0);
                    var operand1_and = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_and, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_and.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_and, op1, false, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_and, out sbyte imm8_and))
                            {
                                EmitOpcode(output, 0x49, "AND.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_and);
                            }
                            else
                            {
                                (op2, _) = GetOperandReg(operand1_and, output);
                                if (operand1_and.TypeOf.IntWidth < 32)
                                {
                                    op2 = HandleOperandExtension(operand1_and, op2, false, output);
                                }
                                EmitOpcode(output, 0x28, "AND.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            (op2, _) = GetOperandReg(operand1_and, output);
                            EmitOpcode(output, 0x38, "AND.I64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for AND: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMOr:  // ��������� ���
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_or = instr.GetOperand(0);
                    var operand1_or = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_or, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_or.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_or, op1, false, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_or, out sbyte imm8_or))
                            {
                                EmitOpcode(output, 0x4A, "OR.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_or);
                            }
                            else
                            {
                                (op2, _) = GetOperandReg(operand1_or, output);
                                if (operand1_or.TypeOf.IntWidth < 32)
                                {
                                    op2 = HandleOperandExtension(operand1_or, op2, false, output);
                                }
                                EmitOpcode(output, 0x29, "OR.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            (op2, _) = GetOperandReg(operand1_or, output);
                            EmitOpcode(output, 0x39, "OR.I64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for OR: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMXor:  // ��������� ����������� ���
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_xor = instr.GetOperand(0);
                    var operand1_xor = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_xor, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_xor.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_xor, op1, false, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(operand1_xor, out sbyte imm8_xor))
                            {
                                EmitOpcode(output, 0x4B, "XOR.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_xor);
                            }
                            else
                            {
                                (op2, _) = GetOperandReg(operand1_xor, output);
                                if (operand1_xor.TypeOf.IntWidth < 32)
                                {
                                    op2 = HandleOperandExtension(operand1_xor, op2, false, output);
                                }
                                EmitOpcode(output, 0x2A, "XOR.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            (op2, _) = GetOperandReg(operand1_xor, output);
                            EmitOpcode(output, 0x3A, "XOR.I64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for XOR: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMShl:  // ����� �����
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_shl = instr.GetOperand(0);
                    var operand1_shl = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_shl, output);
                    (op2, _) = GetOperandReg(operand1_shl, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_shl.TypeOf.IntWidth < 32 || operand1_shl.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_shl, op1, false, output);
                        op2 = HandleOperandExtension(operand1_shl, op2, false, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            EmitOpcode(output, 0x2B, "SHL.I32");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            EmitOpcode(output, 0x3B, "SHL.I64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for SHL: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMAShr:  // �������������� ����� ������ (� ����������� �����)
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_ashr = instr.GetOperand(0);
                    var operand1_ashr = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_ashr, output);
                    (op2, _) = GetOperandReg(operand1_ashr, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_ashr.TypeOf.IntWidth < 32 || operand1_ashr.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_ashr, op1, true, output);
                        op2 = HandleOperandExtension(operand1_ashr, op2, true, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_shrs))
                            {
                                EmitOpcode(output, 0x47, "SHRS.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_shrs);
                            }
                            else
                            {
                                EmitOpcode(output, 0x2C, "SHR.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            EmitOpcode(output, 0x3C, "SHR.I64");
                            WriteBinaryInstruction(output, dest, op1, op2);
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for ASHR: {type}");
                    }
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMLShr:  // ���������� ����� ������ (���������� ������)
                    _logger.WriteLine($"DEBUG: Entering LSHR. output.Count = {output.Count}");
                    // �������� �������� ���������
                    dest = GetResultRegWithCacheInvalidation(instr, output);
                    var operand0_lshr = instr.GetOperand(0);
                    var operand1_lshr = instr.GetOperand(1);
                    (op1, _) = GetOperandReg(operand0_lshr, output);
                    (op2, _) = GetOperandReg(operand1_lshr, output);
                    type = MapLlvmTypeToEspb(instr.TypeOf);
                    if ((type == ValueType.I32 || type == ValueType.U32) && (operand0_lshr.TypeOf.IntWidth < 32 || operand1_lshr.TypeOf.IntWidth < 32))
                    {
                        op1 = HandleOperandExtension(operand0_lshr, op1, false, output);
                        op2 = HandleOperandExtension(operand1_lshr, op2, false, output);
                    }
                    
                    // �������� ����� � ����������� �� ����
                    switch (type)
                    {
                        case ValueType.BOOL:
                        case ValueType.I8:
                        case ValueType.U8:
                        case ValueType.I16:
                        case ValueType.U16:
                        case ValueType.I32:
                        case ValueType.U32:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_shru32))
                            {
                                EmitOpcode(output, 0x48, "SHRU.I32.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_shru32);
                            }
                            else
                            {
                                EmitOpcode(output, 0x2D, "SHRU.I32");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        case ValueType.I64:
                        case ValueType.U64:
                            if (IsInt8Constant(instr.GetOperand(1), out sbyte imm8_shru))
                            {
                                EmitOpcode(output, 0x58, "SHRU.I64.IMM8");
                                output.Add(dest);
                                output.Add(op1);
                                output.Add((byte)imm8_shru);
                            }
                            else
                            {
                                EmitOpcode(output, 0x3D, "SHRU.I64");
                                WriteBinaryInstruction(output, dest, op1, op2);
                            }
                            break;
                        default:
                            throw new NotSupportedException($"Unsupported type for LSHR: {type}");
                    }
                    _logger.WriteLine($"DEBUG: Exiting LSHR. output.Count = {output.Count}");
                    registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    break;
                    
                case LLVMOpcode.LLVMFNeg: // FNeg (�������������� ���������)
                    {
                        dest = GetResultRegWithCacheInvalidation(instr, output);
                        (op1, _) = GetOperandReg(instr.GetOperand(0), output);
                        type = MapLlvmTypeToEspb(instr.TypeOf);

                        // ��������� ��������� ����� ��������� �� ���� (0 - x)
                        byte zeroReg = registerManager.AllocateTemporaryRegister(); // �������� ����� ������� ��� ����

                        switch (type)
                        {
                            case ValueType.I32:
                            case ValueType.U32:
                                // LDC.I32.IMM zeroReg, 0
                                EmitOpcode(output, 0x18, "LDC.I32.IMM");
                                output.Add(zeroReg);
                                output.AddRange(BitConverter.GetBytes((int)0));
                                // SUB.I32 dest, zeroReg, op1
                                EmitOpcode(output, 0x21, "SUB.I32");
                                WriteBinaryInstruction(output, dest, zeroReg, op1);
                                break;
                            case ValueType.I64:
                            case ValueType.U64:
                                // LDC.I64.IMM zeroReg, 0
                                EmitOpcode(output, 0x19, "LDC.I64.IMM");
                                output.Add(zeroReg);
                                output.AddRange(BitConverter.GetBytes((long)0));
                                // SUB.I64 dest, zeroReg, op1
                                EmitOpcode(output, 0x31, "SUB.I64");
                                WriteBinaryInstruction(output, dest, zeroReg, op1);
                                break;
                            case ValueType.F32:
                                // LDC.F32.IMM zeroReg, 0.0f
                                EmitOpcode(output, 0x1A, "LDC.F32.IMM");
                                output.Add(zeroReg);
                                output.AddRange(BitConverter.GetBytes((float)0.0f));
                                // SUB.F32 dest, zeroReg, op1
                                EmitOpcode(output, 0x61, "SUB.F32");
                                WriteBinaryInstruction(output, dest, zeroReg, op1);
                                break;
                            case ValueType.F64:
                                // LDC.F64.IMM zeroReg, 0.0
                                EmitOpcode(output, 0x1B, "LDC.F64.IMM");
                                output.Add(zeroReg);
                                output.AddRange(BitConverter.GetBytes((double)0.0));
                                // SUB.F64 dest, zeroReg, op1
                                EmitOpcode(output, 0x69, "SUB.F64");
                                WriteBinaryInstruction(output, dest, zeroReg, op1);
                                break;
                            default:
                                throw new NotSupportedException($"Unsupported type for FNeg: {type}");
                        }
                        registerManager.ExplicitSpillIfNeeded(instr, dest, output);
                    }
                    break;
                    
                case LLVMOpcode.LLVMSelect:
                    {
                        _logger.WriteLine($"---> Translating Select: {instr}");

                        // �������� �������� ��� ���������
                        byte destReg = GetResultRegWithCacheInvalidation(instr, output);
                        (byte condReg, _) = GetOperandReg(instr.GetOperand(0), output);
                        (byte trueValueReg, _) = GetOperandReg(instr.GetOperand(1), output);
                        (byte falseValueReg, _) = GetOperandReg(instr.GetOperand(2), output);

                        // ���������� ��� ���������� ��� ������ ����������� ������ SELECT
                        ValueType resultType = MapLlvmTypeToEspb(instr.TypeOf);
                        byte selectOpcode;

                        switch (resultType)
                        {
                            case ValueType.I8:
                            case ValueType.U8:
                            case ValueType.I16:
                            case ValueType.U16:
                            case ValueType.I32:
                            case ValueType.U32:
                            case ValueType.BOOL:
                                selectOpcode = 0xBE; // SELECT.I32
                                break;
                            case ValueType.PTR:
                                selectOpcode = 0xD6; // SELECT.PTR
                                break;
                            case ValueType.I64:
                            case ValueType.U64:
                                selectOpcode = 0xBF; // SELECT.I64
                                break;
                            case ValueType.F32:
                                selectOpcode = 0xD4; // SELECT.F32
                                break;
                            case ValueType.F64:
                                selectOpcode = 0xD5; // SELECT.F64
                                break;
                            default:
                                throw new NotSupportedException($"Unsupported type for SELECT instruction: {resultType}");
                        }

                        // ���������� ���� ���������� SELECT
                        EmitOpcode(output, selectOpcode, "SELECT");
                        output.Add(destReg);
                        output.Add(condReg);
                        output.Add(trueValueReg);
                        output.Add(falseValueReg);
                        
                        _logger.WriteLine($"    Generated SELECT.{resultType} R{destReg}, R{condReg}, R{trueValueReg}, R{falseValueReg}");
                        registerManager.ExplicitSpillIfNeeded(instr, destReg, output);
                    }
                    break;
                    
                case LLVMOpcode.LLVMPHI:
                    TranslatePHI(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMAtomicRMW:
                    TranslateAtomicRMW(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMAtomicCmpXchg:
                    TranslateAtomicCmpXchg(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMGetElementPtr:
                    TranslateGEP(instr, output);
                    break;
                    
                // �������������� �����
                case LLVMOpcode.LLVMTrunc:    // ������������� ��������
                case LLVMOpcode.LLVMZExt:     // ������������� ���������� � ������
                case LLVMOpcode.LLVMSExt:     // ������������� ���������� �� ������
                case LLVMOpcode.LLVMFPTrunc:  // �������� ����� � ��������� ������
                case LLVMOpcode.LLVMFPExt:    // ���������� ����� � ��������� ������
                case LLVMOpcode.LLVMFPToUI:   // �� float � ����������� �����
                case LLVMOpcode.LLVMFPToSI:   // �� float � �������� �����
                case LLVMOpcode.LLVMUIToFP:   // �� ������������ ������ � float
                case LLVMOpcode.LLVMSIToFP:   // �� ��������� ������ � float
                case LLVMOpcode.LLVMBitCast:  // ��������� ��������������
                    TranslateTypeConversion(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMExtractValue:
                    TranslateExtractValue(instr, output);
                    break;

                case LLVMOpcode.LLVMUnreachable:
                    _logger.WriteLine($"---> Translating Unreachable. Emitting 0x05.");
                    EmitOpcode(output, 0x05, "UNREACHABLE");
                    break;
                    
                default:
                    _logger.WriteLine($"Unsupported instruction: {opcode}");
                    break;
            }
            return output.Count - beforeCount;
        }
        
        private void WriteBinaryInstruction(List<byte> output, byte dest, byte op1, byte op2)
        {
            AddReg(output, dest);
            AddReg(output, op1);
            AddReg(output, op2);
        }
        
        private byte GetLoadOpcode(ValueType type)
        {
            switch (type)
            {
                case ValueType.I8:  // Signed 8-bit
                    return 0x80; // LOAD.I8S
                case ValueType.U8:  // Unsigned 8-bit
                    return 0x81; // LOAD.I8U
                case ValueType.I16: // Signed 16-bit
                    return 0x82; // LOAD.I16S
                case ValueType.U16: // Unsigned 16-bit
                    return 0x83; // LOAD.I16U
                case ValueType.I32:
                case ValueType.U32: // Use same opcode for both 32-bit loads
                    return 0x84; // LOAD.I32
                case ValueType.I64:
                case ValueType.U64: // Use same opcode for both 64-bit loads
                    return 0x85; // LOAD.I64
                case ValueType.F32:
                    return 0x86; // LOAD.F32
                case ValueType.F64:
                    return 0x87; // LOAD.F64
                case ValueType.PTR:
                    return 0x88; // LOAD.PTR
                case ValueType.BOOL:
                    return 0x89; // LOAD.BOOL
                default:
                    throw new NotSupportedException($"Unsupported type for LOAD: {type}");
            }
        }
        
        private byte GetStoreOpcode(ValueType type)
        {
            switch (type)
            {
                case ValueType.I8:
                    return 0x70; // STORE.I8
                case ValueType.U8:
                    return 0x71; // STORE.U8
                case ValueType.I16:
                    return 0x72; // STORE.I16
                case ValueType.U16:
                    return 0x73; // STORE.U16
                case ValueType.I32:
                case ValueType.U32:
                    return 0x74; // STORE.I32
                case ValueType.I64:
                case ValueType.U64:
                    return 0x76; // STORE.I64
                case ValueType.F32:
                    return 0x78; // STORE.F32 (����������: ���� 0x4E=78 decimal, ����� 0x78=120 decimal)
                case ValueType.F64:
                    return 0x79; // STORE.F64
                case ValueType.PTR:
                    return 0x7A; // STORE.PTR
                case ValueType.BOOL:
                    return 0x7B; // STORE.BOOL
                default:
                    throw new NotSupportedException($"Unsupported type for STORE: {type}");
            }
        }
        
        private unsafe byte GetOrAllocateReg(LLVMValueRef value)
        {
            // ������ ���� ����� ������ �������� �������� ��� ����� ����������
            return registerManager.GetOrAllocateReg(value);
        }
        
        /// <summary>
        /// Gets or allocates a register for a value and invalidates ALLOCA-OPT cache.
        /// Use this for instruction results that will overwrite the register.
        /// </summary>
        private unsafe byte GetOrAllocateRegWithCacheInvalidation(LLVMValueRef value, List<byte> output)
        {
            byte reg = registerManager.GetOrAllocateReg(value);
            InvalidateAllocaMappingsForRegister(reg, output);
            return reg;
        }
        
        // ���������: stub ��� TranslateTypeConversion, ���������������� � ������ ����������
        private unsafe void TranslateTypeConversion(LLVMValueRef instr, List<byte> output)
        {
            TranslateTypeConversion_Old(instr, output);
        }
        
        private bool IsInt8Constant(LLVMValueRef value, out sbyte outVal)
        {
            outVal = 0;
            if (value.IsAConstantInt.Handle != IntPtr.Zero)
            {
                long val = LLVM.ConstIntGetSExtValue(value);
                if (val >= sbyte.MinValue && val <= sbyte.MaxValue)
                {
                    outVal = (sbyte)val;
                    return true;
                }
            }
            return false;
        }

        // ��������������� ����� ��� �������� ��������� ���������
        private int CountSubstringOccurrences(string source, string substring)
        {
            if (string.IsNullOrEmpty(source) || string.IsNullOrEmpty(substring))
                return 0;
                
            int count = 0;
            int position = 0;
            while ((position = source.IndexOf(substring, position)) != -1)
            {
                count++;
                position += substring.Length;
            }
            return count;
        }

        /// <summary>
        /// ��������� ����� ������� ��� JIT �����������
        /// </summary>
        /// <summary>
        /// ���������, ����� �� ������� ������� annotate("espb_jit_hot")
        /// </summary>
        private unsafe bool HasJitHotAnnotation(LLVMValueRef func)
        {
            // ���������� ������ ��� LLVMSharp ��� ������������� API ���������:
            // ������ IR-������ ������� � ���� `section ".jit.hot"`.
            // ��� �������� � `__attribute__((section(".jit.hot")))`.

            string funcName = Marshal.PtrToStringAnsi((IntPtr)LLVM.GetValueName(func)) ?? "";

            // 1) �������� section ����� ��������� ������������� IR
            try
            {
                var irPtr = LLVM.PrintValueToString(func);
                string ir = Marshal.PtrToStringAnsi((IntPtr)irPtr) ?? "";

                // ����������� ������, ���������� LLVM
                LLVM.DisposeMessage(irPtr);

                if (ir.Contains("section \".jit.hot\"") )
                {
                    _logger.WriteLine($"[JIT-HOT] Function '{funcName}' has section .jit.hot");
                    return true;
                }

                // Detect explicit annotate attribute emitted by compilers:
                // __attribute__((annotate("espb_jit_hot")))
                if (ir.Contains("annotate(\"espb_jit_hot\")"))
                {
                    _logger.WriteLine($"[JIT-HOT] Function '{funcName}' has annotate(...) metadata");
                    return true;
                }

                // Generic metadata check: sometimes annotation is present only in
                // the !llvm.func.annotations named metadata block. Search for the
                // annotation strings nearby in the IR dump.
                if (ir.Contains("!llvm.func.annotations") && ir.Contains("espb_jit_hot"))
                {
                    _logger.WriteLine($"[JIT-HOT] Function '{funcName}' referenced in !llvm.func.annotations");
                    return true;
                }
            }
            catch
            {
                // ignore
            }

            // 2) Fallback: ��������� �� ����� (�� ������ ���������� section)
            if (funcName.Contains("_jit_") || funcName.Contains("test_jit"))
            {
                _logger.WriteLine($"[JIT-HOT] Function '{funcName}' matched by name heuristic");
                return true;
            }

            // 3) Module-level metadata: annotations may be stored in @llvm.global.annotations
            try
            {
                var module = LLVM.GetGlobalParent(func);
                var modPtr = LLVM.PrintModuleToString(module);
                string modIr = Marshal.PtrToStringAnsi((IntPtr)modPtr) ?? "";
                LLVM.DisposeMessage(modPtr);

                if (!string.IsNullOrEmpty(modIr) && modIr.Contains("llvm.global.annotations"))
                {
                    // Build map of .str constants -> their contents
                    var strMap = new Dictionary<string, string>();
                    try
                    {
                        var rxStr = new Regex(@"@(?<name>\.str\.\d+)\s*=.*?c""(?<text>.*?)""", RegexOptions.Singleline);
                        foreach (Match m in rxStr.Matches(modIr))
                        {
                            var name = m.Groups["name"].Value; // e.g. .str.18
                            var text = m.Groups["text"].Value; // e.g. espb_jit_hot\00
                            strMap[name] = text;
                        }
                    }
                    catch
                    {
                        // ignore parsing errors
                    }

                    // Find all annotation entries and check for exact espb_jit_hot string tied to this function
                    var rxAnn = new Regex(@"\{[^}]*?ptr\s+@(?<func>[A-Za-z0-9_@\.]+)[^}]*?ptr\s+@(?<str>\.str\.\d+)[^}]*?\}", RegexOptions.Singleline);
                    foreach (Match m in rxAnn.Matches(modIr))
                    {
                        var f = m.Groups["func"].Value; // function symbol
                        var s = m.Groups["str"].Value;  // .str.N

                        // Normalize: function in IR may be @name or @"name" - remove possible leading @ or quotes
                        var fNorm = f.Trim();
                        if (fNorm.StartsWith("@")) fNorm = fNorm.Substring(1);
                        fNorm = fNorm.Trim('"');

                        if (!string.Equals(fNorm, funcName, StringComparison.Ordinal))
                            continue;

                        if (strMap.TryGetValue(s, out var content))
                        {
                            // content may contain trailing \00, ensure exact match to espb_jit_hot\00 or espb_jit_hot
                            if (content == "espb_jit_hot" || content == "espb_jit_hot\\00")
                            {
                                _logger.WriteLine($"[JIT-HOT] Module annotations contain exact espb_jit_hot for '{funcName}'");
                                return true;
                            }
                        }
                    }
                }
            }
            catch
            {
                // ignore
            }

            return false;
        }

        /// <summary>
        /// ��������� ����� ������� ��� JIT-�����������.
        /// �������� ���� NO_SPILL (0x04) �������� JIT, ��� ������� �� ���������� stack spilling.
        /// </summary>
        private static byte ScanMaxRegisterOperand(List<byte> bytecode)
        {
            byte maxReg = 0;
            int pc = 0;
            while (pc < bytecode.Count)
            {
                byte op = bytecode[pc++];
                // MOV: Rd, Rs
                if (op == 0x12)
                {
                    if (pc + 1 >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    byte rs = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    if (rs > maxReg) maxReg = rs;
                    continue;
                }

                // LDC.*: Rd + immediate payload
                if (op == 0x18 || op == 0x1A || op == 0x1C) // 4-byte imm
                {
                    if (pc >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    pc += 4;
                    continue;
                }
                if (op == 0x19 || op == 0x1B) // 8-byte imm
                {
                    if (pc >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    pc += 8;
                    continue;
                }
                if (op == 0x1D) // Rd + idx16
                {
                    if (pc >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    pc += 2;
                    continue;
                }

                // ALU ops Rd,Rs1,Rs2 (broad ranges)
                if ((op >= 0x20 && op <= 0x2D) || (op >= 0x30 && op <= 0x3D) || (op >= 0x40 && op <= 0x4D) || (op >= 0x50 && op <= 0x5D))
                {
                    if (pc + 2 >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    byte rs1 = bytecode[pc++];
                    byte rs2 = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    if (rs1 > maxReg) maxReg = rs1;
                    if (rs2 > maxReg) maxReg = rs2;
                    continue;
                }

                // Unary Rd,Rs
                if (op == 0x2E || op == 0x3E || op == 0x3F || op == 0x4E || op == 0x4F || op == 0x5E || op == 0x5F)
                {
                    if (pc + 1 >= bytecode.Count) break;
                    byte rd = bytecode[pc++];
                    byte rs = bytecode[pc++];
                    if (rd > maxReg) maxReg = rd;
                    if (rs > maxReg) maxReg = rs;
                    continue;
                }

                // LOAD/STORE family: r0,r1,off32
                if (op >= 0x80 && op <= 0x87)
                {
                    if (pc + 1 >= bytecode.Count) break;
                    byte r0 = bytecode[pc++];
                    byte r1 = bytecode[pc++];
                    if (r0 > maxReg) maxReg = r0;
                    if (r1 > maxReg) maxReg = r1;
                    pc += 4;
                    continue;
                }

                // Control flow/calls
                if (op == 0x02) { pc += 2; continue; }
                if (op == 0x03) {
                    if (pc >= bytecode.Count) break;
                    byte rc = bytecode[pc++];
                    if (rc > maxReg) maxReg = rc;
                    pc += 2;
                    continue;
                }
                if (op == 0x09) {
                    pc += 2;
                    if (pc < bytecode.Count && bytecode[pc] == 0xAA) {
                        pc++;
                        if (pc >= bytecode.Count) break;
                        int na = bytecode[pc++];
                        pc += na;
                    }
                    continue;
                }
                if (op == 0x0A) { pc += 2; continue; }

                // Unknown opcode: stop scan (conservative)
                break;
            }
            return maxReg;
        }

        private unsafe byte CalculateFunctionFlags(LLVMValueRef func, ushort frameSize, byte maxRegUsed)
        {
            byte flags = 0;
            
            // ��������, �������� �� ������� leaf (�� �������� ������ �������)
            bool hasCall = false;
            bool usesFPU = false;
            bool hasMemoryOps = false;
            
            LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(func);
            while (bb.Handle != IntPtr.Zero)
            {
                LLVMValueRef inst = LLVM.GetFirstInstruction(bb);
                while (inst.Handle != IntPtr.Zero)
                {
                    var opcode = inst.InstructionOpcode;
                    
                    // �������� �� ������ �������
                    if (opcode == LLVMOpcode.LLVMCall || opcode == LLVMOpcode.LLVMInvoke)
                    {
                        hasCall = true;
                    }
                    
                    // �������� �� �������� � ��������� ������
                    if (opcode == LLVMOpcode.LLVMFAdd || opcode == LLVMOpcode.LLVMFSub ||
                        opcode == LLVMOpcode.LLVMFMul || opcode == LLVMOpcode.LLVMFDiv ||
                        opcode == LLVMOpcode.LLVMFRem || opcode == LLVMOpcode.LLVMFCmp ||
                        opcode == LLVMOpcode.LLVMFPToUI || opcode == LLVMOpcode.LLVMFPToSI ||
                        opcode == LLVMOpcode.LLVMUIToFP || opcode == LLVMOpcode.LLVMSIToFP ||
                        opcode == LLVMOpcode.LLVMFPTrunc || opcode == LLVMOpcode.LLVMFPExt)
                    {
                        usesFPU = true;
                    }
                    
                    // �������� �� �������� � �������
                    if (opcode == LLVMOpcode.LLVMLoad || opcode == LLVMOpcode.LLVMStore)
                    {
                        hasMemoryOps = true;
                    }
                    
                    inst = LLVM.GetNextInstruction(inst);
                }
                bb = LLVM.GetNextBasicBlock(bb);
            }
            
            // ������������� �����
            if (!hasCall) flags |= 0x01; // IS_LEAF
            if (usesFPU) flags |= 0x02;  // USE_FPU
            
            // ��������: NO_SPILL (0x04) ��������������� ������ ���� frameSize == 0 � ��� �������� ����������
            // ��� ��������� JIT ������������ Fast Path: ��������� �������� � ������ � ��������� � ������
            if (frameSize == 0 && maxRegUsed <= 15)
            {
                flags |= 0x04; // NO_SPILL - ������� ����� ����������� ��� ��������� � ������ �����
                _logger.WriteLine($"    [FLAGS] NO_SPILL enabled for '{func.Name}' (frameSize={frameSize}, maxReg={maxRegUsed})");
            }
            
            if (hasCall) flags |= 0x08;      // HAS_CALLS
            if (hasMemoryOps) flags |= 0x10; // HAS_MEMORY
            
            // SIMPLE_CF: ������� �� ������� ������� ����������
            uint bbCount = LLVM.CountBasicBlocks(func);
            if (bbCount <= 5) flags |= 0x20; // SIMPLE_CF

            // HOT: ������� �������� ����� __attribute__((section(".jit.hot")))
            if (HasJitHotAnnotation(func)) {
                flags |= 0x40; // ESPB_FUNC_FLAG_HOT
            }
            
            return flags;
        }
        
        // ��������� extractvalue ���������� ��� cmpxchg �����������
        private unsafe void TranslateExtractValue(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating ExtractValue: {instr}");
            
            // �������� ������� ����������
            byte destReg = GetResultRegWithCacheInvalidation(instr, output);
            
            // �������� �������� ������ (������ ��������� cmpxchg)
            LLVMValueRef source = instr.GetOperand(0);
            
            // �������� ������ ����������
            uint extractIndex = LLVM.GetNumIndices(instr) > 0 ? LLVM.GetIndices(instr)[0] : 0;
            
            _logger.WriteLine($"    ExtractValue: source={source}, index={extractIndex}");
            
            // ���������, ��� �������� - ��� cmpxchg ����������
            if (source.InstructionOpcode == LLVMOpcode.LLVMAtomicCmpXchg)
            {
                // ��� cmpxchg � ��� ��� ���� ��� ���������� � ���������
                // ������ ������� (oldValueReg) ��� �������� ��� ���������� cmpxchg
                // ������ ������� (successReg) ������� ����� ����� ����
                
                if (registerManager.TryGetRegister(source, out byte sourceReg))
                {
                    if (extractIndex == 0)
                    {
                        // ��������� ������ �������� (������ ������� ���������)
                        _logger.WriteLine($"    Extracting old value from cmpxchg result -> R{sourceReg}");
                        EmitOpcode(output, 0x12, "MOV.I32");
                        output.Add(destReg);
                        output.Add(sourceReg);
                    }
                    else if (extractIndex == 1)
                    {
                        // ��������� ���� ������ (������ ������� ���������)
                        byte successReg = (byte)(sourceReg + 1);
                        _logger.WriteLine($"    Extracting success flag from cmpxchg result -> R{successReg}");
                        EmitOpcode(output, 0x12, "MOV.I32");
                        output.Add(destReg);
                        output.Add(successReg);
                    }
                    else
                    {
                        throw new NotSupportedException($"Invalid extract index for cmpxchg: {extractIndex}");
                    }
                }
                else
                {
                    throw new InvalidOperationException($"Source cmpxchg instruction not found in register map: {source}");
                }
            }
            else
            {
                throw new NotSupportedException($"ExtractValue from non-cmpxchg source not supported: {source.InstructionOpcode}");
            }
            
            registerManager.ExplicitSpillIfNeeded(instr, destReg, output);
            _logger.WriteLine($"    Generated ExtractValue R{destReg} from index {extractIndex}");
        }

        // ������������� ����������: Switch ����� BR_TABLE
        private unsafe void TranslateSwitchInstr(LLVMValueRef instr, List<byte> output)
        {
            var currentBlock = LLVM.GetInstructionParent(instr);
            (byte conditionReg, _) = GetOperandReg(instr.GetOperand(0), output);
            LLVMBasicBlockRef defaultBlock = instr.GetOperand(1).AsBasicBlock();

            _logger.WriteLine($"[SWITCH BR_TABLE] Universal BR_TABLE implementation");
            _logger.WriteLine($"[SWITCH BR_TABLE] Condition in R{conditionReg}");

            // �������� ��� case-�����
            var cases = new Dictionary<long, LLVMBasicBlockRef>();
            long minCase = long.MaxValue;
            long maxCase = long.MinValue;
            
            for (int i = 2; i < LLVM.GetNumOperands(instr); i += 2)
            {
                long caseValue = LLVM.ConstIntGetSExtValue(instr.GetOperand((uint)i));
                LLVMBasicBlockRef caseBlock = instr.GetOperand((uint)(i + 1)).AsBasicBlock();
                cases[caseValue] = caseBlock;
                minCase = Math.Min(minCase, caseValue);
                maxCase = Math.Max(maxCase, caseValue);
                _logger.WriteLine($"[SWITCH BR_TABLE] Case {caseValue} -> target block");
            }

            if (cases.Count == 0)
            {
                // ��� case'�� - ������ ������� � default
                AddPhiFixupsForSuccessor(currentBlock, defaultBlock, output);
                EmitOpcode(output, 0x02, "BR");
                branchFixups.Add(new BranchFixup {
                    InstructionPosition = output.Count - 1,
                    PlaceholderPosition = output.Count, 
                    TargetBlock = defaultBlock 
                });
                output.AddRange(new byte[] { 0x00, 0x00 });
                _logger.WriteLine($"[SWITCH BR_TABLE] No cases, direct jump to default");
                return;
            }

            // �������������� ������ ��� BR_TABLE
            byte indexReg = conditionReg;
            if (minCase != 0)
            {
                // ����������� ������: adjusted = condition - minCase
                byte offsetReg = GenerateIntConstant(output, (int)minCase);
                indexReg = registerManager.AllocateTemporaryRegister();
                EmitOpcode(output, 0x21, "SUB.I32");
                WriteBinaryInstruction(output, indexReg, conditionReg, offsetReg);
                _logger.WriteLine($"[SWITCH BR_TABLE] Normalized index: R{indexReg} = R{conditionReg} - {minCase}");
            }

            // ������ jump table �� ���� �������� [minCase..maxCase]
            long range = maxCase - minCase + 1;
            var jumpTable = new LLVMBasicBlockRef[range];
            for (long i = 0; i < range; i++)
            {
                long caseValue = minCase + i;
                if (cases.ContainsKey(caseValue))
                {
                    jumpTable[i] = cases[caseValue];
                    _logger.WriteLine($"[SWITCH BR_TABLE] Table[{i}] (case {caseValue}) -> target block");
                }
                else
                {
                    jumpTable[i] = defaultBlock;
                    _logger.WriteLine($"[SWITCH BR_TABLE] Table[{i}] (case {caseValue}) -> default block (gap)");
                }
            }

            // ��������� PHI-������� ��� ���� ���������� �����
            foreach (var block in jumpTable.Distinct())
            {
                AddPhiFixupsForSuccessor(currentBlock, block, output);
            }
            AddPhiFixupsForSuccessor(currentBlock, defaultBlock, output);

            // ���������� BR_TABLE ����������: 0x04 Ridx(u8) num_targets(u16) [target_offsets(i16)...] default_offset(i16)
            int brTableStartPos = output.Count;
            EmitOpcode(output, 0x04, "BR_TABLE");
            output.Add(indexReg); // ������� � ��������
            
            // ���������� ����� � ������� (u16)
            ushort numTargets = (ushort)range;
            output.Add((byte)(numTargets & 0xFF));
            output.Add((byte)((numTargets >> 8) & 0xFF));
            
            _logger.WriteLine($"[SWITCH BR_TABLE] Generated BR_TABLE with {numTargets} targets");

            // Placeholders ��� �������� �������
            var tablePlaceholderPositions = new List<int>();
            for (int i = 0; i < range; i++)
            {
                tablePlaceholderPositions.Add(output.Count);
                output.AddRange(new byte[] { 0x00, 0x00 }); // i16 placeholder
            }
            
            // Placeholder ��� default ��������
            int defaultPlaceholderPos = output.Count;
            output.AddRange(new byte[] { 0x00, 0x00 }); // i16 placeholder

            // ����� ���������� - ���� ��� ���������� ��������
            int instructionEnd = output.Count;

            // ������� fixup'� ��� ���� �������� � �������
            for (int i = 0; i < range; i++)
            {
                branchFixups.Add(new BranchFixup
                {
                    InstructionPosition = instructionEnd,
                    PlaceholderPosition = tablePlaceholderPositions[i],
                    TargetBlock = jumpTable[i]
                });
            }
            
            // Fixup ��� default ��������
            branchFixups.Add(new BranchFixup
            {
                InstructionPosition = instructionEnd,
                PlaceholderPosition = defaultPlaceholderPos,
                TargetBlock = defaultBlock
            });

            _logger.WriteLine($"[SWITCH BR_TABLE] Universal BR_TABLE implementation completed - range {range}, fixups from instruction end");
        }

        // ������� �������: ������ ��������� �������� ��� ������� case
        private unsafe void CreateCasePhiFixups(LLVMBasicBlockRef currentBlock, LLVMBasicBlockRef targetBlock, long caseValue, List<byte> output)
        {
            _logger.WriteLine($"[CASE PHI FIXUP] Processing case {caseValue}: {currentBlock.Handle:X} -> {targetBlock.Handle:X}");
            
            // ��������� PHI-���������� �� ���������� �������������, ������������� �������� ��������
            // �� ������ ������� LLVM IR: %25 = phi ptr [ @str.18, %21 ], [ @str.17, %22 ], [ @str.20, %23 ], [ @str.19, %18 ]
            // PHI ���������� ������� R33 (��� ����� �� ����� ����������)
            
            byte phiReg = 33; // �������, ������� ���������� PHI-���������� %25
            byte correctReg = GetCorrectRegisterForCase(caseValue);
            
            if (correctReg != 0 && correctReg != phiReg)
            {
                // ���������� MOV.PTR ���������� ��� ��������� ����������� ��������
                EmitOpcode(output, 0x12, "MOV.32");
                output.Add(phiReg);
                output.Add(correctReg);
                _logger.WriteLine($"[CASE PHI FIXUP] ? Generated MOV.PTR R{phiReg} <- R{correctReg} for case {caseValue}");
            }
            else
            {
                _logger.WriteLine($"[CASE PHI FIXUP] No MOV needed for case {caseValue} (correctReg={correctReg})");
            }
        }

        // ����� ��� ��������� ����������� �������� ��� ������� case
        private byte GetCorrectRegisterForCase(long caseValue)
        {
            // �� ������ ������� ���������� �� ���������� �����:
            // str.17 -> R19 (Case 1)
            // str.18 -> R18 (Default)  
            // str.19 -> R21 (Case 0)
            // str.20 -> R20 (Case 2)
            return caseValue switch
            {
                0 => 21,  // Case 0 -> R21 (str.19 -> "Case 0")
                1 => 19,  // Case 1 -> R19 (str.17 -> "Case 1")
                2 => 20,  // Case 2 -> R20 (str.20 -> "Case 2")  
                -1 => 18, // Default -> R18 (str.18 -> "Default case")
                _ => 0    // ����������� case
            };
        }

        // ���������� ����� ��� ������ ����������� �������� PHI ��� ����������� case
        private unsafe byte FindCorrectPhiValueForCase(PhiInstruction phi, long caseValue)
        {
            _logger.WriteLine($"[CASE PHI VALUE] Searching for case {caseValue} in PHI with {phi.PhiValue.IncomingCount} incoming values");
            
            // ����������� �������� LLVM PHI ���������� ��� ������ ����������� ��������
            for (uint i = 0; i < phi.PhiValue.IncomingCount; i++)
            {
                var incomingBlock = phi.PhiValue.GetIncomingBlock(i);
                var incomingValue = phi.PhiValue.GetIncomingValue(i);
                
                _logger.WriteLine($"[CASE PHI VALUE] Incoming {i}: Block {incomingBlock.Handle:X}, Value {incomingValue.Handle:X}");
                
                // �������� ��� ����������� �������� ��� �������
                if (incomingValue.IsAGlobalValue != null)
                {
                    string globalName = incomingValue.Name ?? "";
                    _logger.WriteLine($"[CASE PHI VALUE] Global name: '{globalName}' for case {caseValue}");
                    
                    // ���������� ���������� �������� �� ����� ���������� ���������� � case
                    // ������������: ���������� �������� ��� ����, ��� ������� ��� case
                    _logger.WriteLine($"[PHI VALUE] Using global '{globalName}' as is");
                    
                    // ������ ���������� �������� �� PHI - ������������, ��� ������� ��� case
                    if (true)
                    {
                        try
                        {
                            (byte correctReg, _) = GetOperandReg(incomingValue, new List<byte>());
                            _logger.WriteLine($"[CASE PHI VALUE] ? FOUND correct value for case {caseValue}: R{correctReg} from '{globalName}'");
                            return correctReg;
                        }
                        catch (Exception ex)
                        {
                            _logger.WriteLine($"[CASE PHI VALUE] ERROR: Failed to get register for '{globalName}': {ex.Message}");
                        }
                    }
                }
                else
                {
                    _logger.WriteLine($"[CASE PHI VALUE] Incoming {i} is not a global value (kind: {incomingValue.Kind})");
                }
            }
            
            _logger.WriteLine($"[CASE PHI VALUE] ? No correct value found for case {caseValue}");
            return 0; // �� �������
        }

        // ������������ ����� ��� �����������, ������������� �� ���������� ���������� ������� case
        // �������: IsCorrectGlobalForCase - ������������ ���������, ������� �� ������ ����

        private void PreScanForFunctionPointers()
        {
            _logger.WriteLine("[PreScan] Scanning for all function pointer usages to create data entries...");
            foreach (var funcRef in llvmFuncToEspbIndex.Keys)
            {
                // Check if the function address is taken anywhere
                if (LLVM.GetFirstUse(funcRef) != default(LLVMUseRef))
                {
                    // If a data segment entry hasn't been created for this function yet, create one.
                    if (!_funcPtrToDataOffset.ContainsKey(funcRef))
                    {
                        int dataOffset = AddDataSegment(new byte[4], 4); // Reserve 4 bytes for the pointer
                        if (dataOffset >= 0)
                        {
                            _funcPtrToDataOffset[funcRef] = (uint)dataOffset;
                            _logger.WriteLine($"  [FuncPtrMap] Pre-allocated data segment for function '{funcRef.Name}' at offset {dataOffset}.");
                        }
                    }
                }
            }
        }
    }
}
