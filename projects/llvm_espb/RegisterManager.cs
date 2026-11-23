using System;
using System.Collections.Generic;
using System.Linq;
using LLVMSharp.Interop;

namespace ESPB_Runtime
{
    /// <summary>
    /// Manages register allocation for a single function translation.
    /// This class has been refactored to use a LinearScanAllocator internally.
    /// It requires PerformAllocation to be called once per function before any other methods are used.
    /// </summary>
    public class RegisterManager
    {
        public bool EnableCompaction { get; set; } = true;

        private readonly TextWriter _logger;
        private AllocationResult? _allocationResult;
        private byte _nextTempReg;
        private byte? _tempStartAfterCompaction; // fixed temp pool base in --patch-regs mode
        private Dictionary<byte, byte>? _registerRemap; // old_reg -> new_reg mapping after compaction
        
        // Promoted allocas - these should be skipped by Graph Coloring
        private HashSet<LLVMValueRef>? _promotedAllocas;

        public RegisterManager(TextWriter logger)
        {
            _logger = logger;
        }
        
        /// <summary>
        /// Sets the collection of promoted allocas. Graph Coloring will skip these allocas
        /// since they don't need pointer registers (they live entirely in dedicated registers).
        /// </summary>
        public void SetPromotedAllocas(Dictionary<LLVMValueRef, byte> promotedAllocas)
        {
            _promotedAllocas = new HashSet<LLVMValueRef>(promotedAllocas.Keys);
            _logger.WriteLine($"[RegisterManager] Received {_promotedAllocas.Count} promoted allocas to skip");
        }
        
        /// <summary>
        /// Checks if an alloca has been promoted to a register.
        /// </summary>
        public bool IsPromotedAlloca(LLVMValueRef alloca)
        {
            return _promotedAllocas?.Contains(alloca) ?? false;
        }

        // Constants to define register pools, must match LinearScanAllocator
        private const int TEMP_REGS_START = 16;
        private const int TEMP_REGS_END = 23;

        // If true, allow temp regs to be compacted away (used with --patch-regs)
        public bool AllowTempRegCompaction { get; set; } = false;


        /// <summary>
        /// Resets the manager for a new function, clearing any previous allocation results.
        /// </summary>
        public void ResetForNewFunction()
        {
            _allocationResult = null;
            _nextTempReg = TEMP_REGS_START;
            _tempStartAfterCompaction = null;
            _registerRemap = null;
            // NOTE: Do NOT clear _promotedAllocas here - it's set by SetPromotedAllocas 
            // BEFORE PerformAllocation is called, and we need it for GraphColoringAllocator
        }

        /// <summary>
        /// Runs the Linear Scan register allocation algorithm for the given function.
        /// This MUST be called once at the beginning of translating a function.
        /// </summary>
        public unsafe void PerformAllocation(LLVMValueRef function, LLVMTargetDataRef targetData, FunctionHints? hints = null)
        {
            // Reset state before new allocation
            ResetForNewFunction();

            _logger.WriteLine($"[RegisterManager] Performing graph coloring allocation for function '{function.Name}'...");
            var allocator = new GraphColoringAllocator(function, targetData, hints, _logger, _promotedAllocas);
            
            // Запускаем аллокацию и получаем результат
            _allocationResult = allocator.Allocate();
            
            // КОМПАКЦИЯ v5: Безопасна при правильной обработке byval
            // GetOrAllocateReg() читает из _allocationResult.Locations, который уже скомпактирован
            if (EnableCompaction)
            {
                (_allocationResult, _registerRemap) = CompactAllocationResult(_allocationResult, function);
            }
            else
            {
                _logger.WriteLine("    [COMPACTION] Disabled by command line flag");
                _registerRemap = null;
            }
            
            _logger.WriteLine($"[RegisterManager] Allocation complete. Peak live registers: {_allocationResult.PeakLiveRegisters}, Max register index used: {_allocationResult.MaxRegisterIndex}, Spilled size: {_allocationResult.TotalSpillSlotSize} bytes.");
        }
        
        /// <summary>
        /// Gets the pre-allocated register for an LLVM value.
        /// For spilled values, returns a temporary register and generates reload code.
        /// </summary>
        public byte GetOrAllocateReg(LLVMValueRef value, List<byte>? output = null)
        {
            if (TryGetRegister(value, out byte reg))
            {
                return reg;
            }

            if (_allocationResult != null && _allocationResult.Locations.TryGetValue(value, out var location) && location is SpillSlotLocation spillLoc)
            {
                // Value был spilled - генерируем reload в временный регистр
                byte tempReg = AllocateTemporaryRegister();
                if (output != null)
                {
                    GenerateReloadInstruction(output, tempReg, spillLoc);
                    _logger.WriteLine($"[SPILL] Auto-reload R{tempReg} <- spill_slot[{spillLoc.Offset}] for value {value}");
                }
                return tempReg;
            }

            throw new InvalidOperationException($"No register allocated for LLVM value: {value}. Ensure PerformAllocation was called and the value is part of the function's data flow.");
        }
        
        /// <summary>
        /// Tries to get the pre-allocated register for an LLVM value.
        /// </summary>
        /// <returns>True if a register was found, false otherwise (e.g., value is spilled).</returns>
        public bool TryGetRegister(LLVMValueRef value, out byte reg)
        {
            reg = 0;
            if (_allocationResult == null)
            {
                throw new InvalidOperationException("RegisterManager.TryGetRegister called before PerformAllocation. This indicates incorrect usage.");
            }

            if (_allocationResult.Locations.TryGetValue(value, out var location) && location is RegisterLocation regLoc)
            {
                reg = regLoc.Register;
                return true;
            }
            
            return false;
        }
        
        /// <summary>
        /// Gets the location (register or spill slot) for a given value.
        /// This is the preferred method for a spill-aware translator.
        /// </summary>
        public AllocationLocation? GetLocation(LLVMValueRef value)
        {
            if (_allocationResult != null && _allocationResult.Locations.TryGetValue(value, out var location))
            {
                return location;
            }
            return null;
        }
        
        /// <summary>
        /// Gets the total number of virtual registers required for the function.
        /// </summary>
        public ushort GetNumVirtualRegs()
        {
            if (_allocationResult == null)
            {
                throw new InvalidOperationException("Cannot get register count before allocation is performed.");
            }
            // The number of registers is max_index + 1
            return (ushort)(_allocationResult.MaxRegisterIndex + 1);
        }
        
        /// <summary>
        /// Gets the total stack space required for spilled values.
        /// </summary>
        public int GetSpillSlotSize()
        {
             if (_allocationResult == null)
            {
                throw new InvalidOperationException("Cannot get spill size before allocation is performed.");
            }
            return _allocationResult.TotalSpillSlotSize;
        }

        // The following methods are now obsolete as allocation is done upfront.
        // They are removed to enforce the new usage pattern.
        public void ResetForNewFile() => ResetForNewFunction();
        public void AddFunctionParameter(LLVMValueRef param, byte reg) { /* No-op, handled by allocator */ }
        public bool IsRegisterContainingFunctionParameter(byte reg) => false; // Logic is now internal to allocator

        /// <summary>
        /// Allocates a temporary register from a dedicated pool.
        /// These are for short-lived intermediate calculations within a single instruction's translation.
        /// The pool cycles, assuming temporaries don't live long enough to clash.
        /// 
        /// In --patch-regs mode with AllowTempRegCompaction=true, temp registers are allocated
        /// AFTER the compacted range to avoid conflicts with already-allocated registers.
        /// </summary>
        public byte AllocateTemporaryRegister()
        {
            // In --patch-regs mode, allocate temps starting after the compacted registers
            if (AllowTempRegCompaction && _allocationResult != null)
            {
                // ВАЖНО: tempStart должен быть ФИКСИРОВАННЫМ для всей функции.
                // Нельзя вычислять его от MaxRegisterIndex на каждом вызове, потому что MaxRegisterIndex
                // сам увеличивается из-за temp-регистров => tempStart "уползает" вверх (max_reg_used=122/146...).
                if (_tempStartAfterCompaction == null)
                {
                    _tempStartAfterCompaction = (byte)(_allocationResult.MaxRegisterIndex + 1);

                    // Включаем ВЕСЬ temp-пул (8 регистров) в диапазон виртуальных регистров.
                    // Это гарантирует, что locals[]/v_regs[] всегда достаточно большой.
                    byte poolEnd = (byte)(_tempStartAfterCompaction.Value + 7);
                    if (poolEnd > _allocationResult.MaxRegisterIndex)
                    {
                        _allocationResult.MaxRegisterIndex = poolEnd;
                    }

                    // Инициализируем указатель выдачи temp-регистров
                    if (_nextTempReg < _tempStartAfterCompaction.Value)
                    {
                        _nextTempReg = _tempStartAfterCompaction.Value;
                    }
                }

                byte tempStart = _tempStartAfterCompaction.Value;
                byte reg = _nextTempReg++;

                // Cycle within fixed pool [tempStart .. tempStart+7]
                if (_nextTempReg > tempStart + 7)
                {
                    _nextTempReg = tempStart;
                }

                return reg;
            }
            else
            {
                // Legacy mode: use fixed R16-R23 range
                byte reg = _nextTempReg++;
                if (_nextTempReg > TEMP_REGS_END)
                {
                    _nextTempReg = TEMP_REGS_START; // Cycle back to the start of the temp pool
                }
                return reg;
            }
        }

        /// <summary>
        /// Generates code to spill a register to a stack slot.
        /// Uses STORE.I32/STORE.I64/STORE.PTR instruction based on value type.
        /// </summary>
        public void GenerateSpillInstruction(List<byte> output, byte sourceReg, SpillSlotLocation spillSlot)
        {
            _logger.WriteLine($"[SPILL] Generating spill R{sourceReg} -> stack[SP+{spillSlot.Offset}] (size: {spillSlot.Size})");
            
            // Используем SP (R7) как базовый регистр для spill area согласно спецификации
            byte spReg = 7; // SP register
            
            // Выбираем опкод STORE в зависимости от размера spillSlot
            byte storeOpcode = spillSlot.Size switch
            {
                4 => 0x74, // STORE.I32 - для I32/PTR/F32/BOOL
                8 => 0x76, // STORE.I64 - для I64/F64  
                _ => 0x74  // По умолчанию I32
            };
            
            // Формат: STORE.* Rs(u8), Ra(u8), offset(i16)
            output.Add(storeOpcode);           // STORE.I32/I64
            output.Add(sourceReg);             // Rs - источник
            output.Add(spReg);                 // Ra - базовый адрес (SP)
            output.Add((byte)(spillSlot.Offset & 0xFF));       // offset младший байт
            output.Add((byte)((spillSlot.Offset >> 8) & 0xFF)); // offset старший байт
        }

        /// <summary>
        /// Generates code to reload a value from a stack slot to a register.
        /// Uses LOAD.I32/LOAD.I64/LOAD.PTR instruction based on value type.
        /// </summary>
        public void GenerateReloadInstruction(List<byte> output, byte destReg, SpillSlotLocation spillSlot)
        {
            _logger.WriteLine($"[SPILL] Generating reload stack[SP+{spillSlot.Offset}] -> R{destReg} (size: {spillSlot.Size})");
            
            // Используем SP (R7) как базовый регистр для spill area
            byte spReg = 7; // SP register  
            
            // Выбираем опкод LOAD в зависимости от размера spillSlot
            byte loadOpcode = spillSlot.Size switch
            {
                4 => 0x84, // LOAD.I32 - для I32/PTR/F32/BOOL
                8 => 0x85, // LOAD.I64 - для I64/F64
                _ => 0x84  // По умолчанию I32
            };
            
            // Формат: LOAD.* Rd(u8), Ra(u8), offset(i16)
            output.Add(loadOpcode);            // LOAD.I32/I64
            output.Add(destReg);               // Rd - назначение
            output.Add(spReg);                 // Ra - базовый адрес (SP)
            output.Add((byte)(spillSlot.Offset & 0xFF));       // offset младший байт
            output.Add((byte)((spillSlot.Offset >> 8) & 0xFF)); // offset старший байт
        }

        /// <summary>
        /// Spill-aware версия GetOrAllocateReg для результатов инструкций.
        /// Если значение должно быть spilled, генерирует spill инструкцию.
        /// </summary>
        public byte GetOrAllocateRegForResult(LLVMValueRef value, List<byte> output)
        {
            if (TryGetRegister(value, out byte reg))
            {
                return reg;
            }

            if (_allocationResult != null && _allocationResult.Locations.TryGetValue(value, out var location) && location is SpillSlotLocation spillLoc)
            {
                // Результат должен быть spilled - используем временный регистр и генерируем spill
                byte tempReg = AllocateTemporaryRegister();
                _logger.WriteLine($"[SPILL] Result R{tempReg} will be spilled to slot[{spillLoc.Offset}] for value {value}");
                return tempReg; // spill будет сгенерирован позже через ExplicitSpill
            }

            throw new InvalidOperationException($"No allocation for result value: {value}");
        }

        /// <summary>
        /// Явно генерирует spill для значения, если оно должно быть spilled.
        /// Используется после генерации инструкции, которая записала результат в временный регистр.
        /// </summary>
        public void ExplicitSpillIfNeeded(LLVMValueRef value, byte tempReg, List<byte> output)
        {
            if (_allocationResult != null && _allocationResult.Locations.TryGetValue(value, out var location) && location is SpillSlotLocation spillLoc)
            {
                GenerateSpillInstruction(output, tempReg, spillLoc);
                _logger.WriteLine($"[SPILL] Explicit spill R{tempReg} -> spill_slot[{spillLoc.Offset}] for value {value}");
            }
        }
        
        /// <summary>
        /// Compaction v4: переназначает регистры в плотный диапазон [0..N-1]
        /// Цель: минимизировать max_reg_used для активации Fast JIT режима (≤15 регистров).
        /// Вызывается ПОСЛЕ аллокации, ДО PARAM-SHUFFLE.
        /// </summary>
        private unsafe (AllocationResult, Dictionary<byte, byte>) CompactAllocationResult(AllocationResult result, LLVMValueRef function)
        {
            static unsafe bool FunctionHasCalls(LLVMValueRef func)
            {
                for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(func); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
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

            // Получаем параметры функции
            uint paramCount = LLVM.CountParams(function);
            var functionParams = new List<LLVMValueRef>();
            for (uint i = 0; i < paramCount; i++)
            {
                functionParams.Add(LLVM.GetParam(function, i));
            }

            // 1) Собираем все используемые номера регистров
            HashSet<byte> usedRegs = new HashSet<byte>();
            foreach (var kvp in result.Locations)
            {
                if (kvp.Value is RegisterLocation regLoc)
                {
                    usedRegs.Add(regLoc.Register);
                }
            }
            
            if (usedRegs.Count == 0)
            {
                result.MaxRegisterIndex = 0;
                return (result, new Dictionary<byte, byte>());
            }

            // 2) Строим плотный маппинг: старый регистр → новый регистр
            // По умолчанию резервируем R0-R7 для ABI вызовов и R16-R23 для временных регистров.
            // НО для leaf-функций (без CALL) можно использовать R0-R7 как обычные регистры — это убирает PARAM-SHUFFLE.
            bool hasCalls = FunctionHasCalls(function);
            Dictionary<byte, byte> remap = new Dictionary<byte, byte>();
            byte nextReg = hasCalls ? (byte)8 : (byte)0; // leaf => start from R0, otherwise keep ABI regs free
            if (!hasCalls)
                _logger.WriteLine($"    [COMPACTION] Leaf function '{function.Name}': allowing R0-R7 for allocation/compaction");
            
            // Сначала назначаем параметры функции в порядке их следования (начиная с R8)
            for (int i = 0; i < functionParams.Count; i++)
            {
                var param = functionParams[i];
                if (result.Locations.TryGetValue(param, out var location) && location is RegisterLocation regLoc)
                {
                    if (!remap.ContainsKey(regLoc.Register))
                    {
                        remap[regLoc.Register] = nextReg;
                        nextReg++;
                        
                        // Optionally skip temp reg range (legacy behavior)
                        if (!AllowTempRegCompaction && nextReg == TEMP_REGS_START)
                        {
                            nextReg = (byte)(TEMP_REGS_END + 1); // Переходим к R24
                        }
                    }
                }
            }
            
            // Затем назначаем все остальные регистры в порядке возрастания
            foreach (var oldReg in usedRegs.OrderBy(r => r))
            {
                if (!remap.ContainsKey(oldReg))
                {
                    remap[oldReg] = nextReg;
                    nextReg++;
                    
                    // Optionally skip temp reg range (legacy behavior)
                    if (!AllowTempRegCompaction && nextReg == TEMP_REGS_START)
                    {
                        nextReg = (byte)(TEMP_REGS_END + 1); // Переходим к R24
                    }
                }
            }

            // 3) Применяем маппинг
            var compacted = new AllocationResult();
            compacted.PeakLiveRegisters = result.PeakLiveRegisters;
            compacted.TotalSpillSlotSize = result.TotalSpillSlotSize;

            byte maxRegUsed = 0;
            foreach (var kvp in result.Locations)
            {
                if (kvp.Value is RegisterLocation regLoc && remap.TryGetValue(regLoc.Register, out var newReg))
                {
                    compacted.Locations[kvp.Key] = new RegisterLocation(newReg);
                    if (newReg > maxRegUsed) maxRegUsed = newReg;
                }
                else
                {
                    // Spill slots не меняем
                    compacted.Locations[kvp.Key] = kvp.Value;
                }
            }

            compacted.MaxRegisterIndex = maxRegUsed;

            _logger.WriteLine($"    [COMPACTION] Compacted from MaxReg {result.MaxRegisterIndex} → {compacted.MaxRegisterIndex} (params: {paramCount}, total regs: {remap.Count})");
            
            // Валидация: проверяем лимит 255 регистров
            if (compacted.MaxRegisterIndex > 254)
            {
                throw new InvalidOperationException($"Function '{function.Name}' uses too many registers: {compacted.MaxRegisterIndex + 1} (max 255)");
            }
            
            return (compacted, remap);
        }
        
        /// <summary>
        /// Returns the remapped register number if compaction was performed, otherwise original.
        /// Use this when emitting register operands to bytecode.
        /// </summary>
        public byte GetRemappedReg(byte originalReg)
        {
            if (_registerRemap != null && _registerRemap.TryGetValue(originalReg, out byte newReg))
            {
                return newReg;
            }
            return originalReg;
        }
        
        /// <summary>
        /// Applies register remapping to bytecode after compaction.
        /// Rewrites all register operands in MOV, LDC, arithmetic ops, etc.
        /// NOTE: This is complex and error-prone. Better to use GetRemappedReg() during emission.
        /// </summary>
        public void ApplyRemappingToBytecode_OLD(List<byte> bytecode)
        {
            if (_registerRemap == null || _registerRemap.Count == 0)
            {
                return; // No compaction or no remapping needed
            }
            
            _logger.WriteLine($"[REMAP] Applying register remapping to {bytecode.Count} bytes of bytecode...");
            
            int i = 0;
            while (i < bytecode.Count)
            {
                byte opcode = bytecode[i];
                i++;
                
                // For each opcode, identify register operand positions and remap them
                switch (opcode)
                {
                    // MOV opcodes: Rd, Rs
                    case 0x12: // MOV.I32/PTR/etc
                        if (i + 1 < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);     // Rd
                            bytecode[i+1] = RemapReg(bytecode[i+1]); // Rs
                            i += 2;
                        }
                        break;
                    
                    // LDC with register: Rd, ...
                    case 0x18: // LDC.I32.IMM: Rd, imm32
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 5; // Rd + 4 bytes imm
                        }
                        break;
                    
                    case 0x19: // LDC.I64.IMM: Rd, imm64
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 9; // Rd + 8 bytes imm
                        }
                        break;
                    
                    case 0x1A: // LDC.F32: Rd, f32
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 5;
                        }
                        break;
                    
                    case 0x1B: // LDC.F64: Rd, f64
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 9;
                        }
                        break;
                    
                    case 0x1C: // LDC.PTR: Rd, offset32
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 5;
                        }
                        break;
                    
                    case 0x1D: // LD_GLOBAL_ADDR: Rd, idx16
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 3;
                        }
                        break;
                    
                    // Arithmetic ops: Rd, Rs1, Rs2
                    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
                    case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
                    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36:
                    case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D:
                    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46:
                    case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D:
                    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56:
                    case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D:
                        if (i + 2 < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);     // Rd
                            bytecode[i+1] = RemapReg(bytecode[i+1]); // Rs1
                            bytecode[i+2] = RemapReg(bytecode[i+2]); // Rs2
                            i += 3;
                        }
                        break;
                    
                    // Unary ops: Rd, Rs
                    case 0x2E: // NOT
                    case 0x3E: case 0x3F: // I64 NOT, etc
                    case 0x4E: case 0x4F: // F32 ops
                    case 0x5E: case 0x5F: // F64 ops
                        if (i + 1 < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            bytecode[i+1] = RemapReg(bytecode[i+1]);
                            i += 2;
                        }
                        break;
                    
                    // Load/Store: Rd/Rs, Raddr, ...
                    case 0x80: case 0x81: case 0x82: case 0x83: // LOAD variants
                    case 0x84: case 0x85: case 0x86: case 0x87: // STORE variants
                        if (i + 1 < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);     // Rd or Rs
                            bytecode[i+1] = RemapReg(bytecode[i+1]); // Raddr
                            i += 6; // +4 for offset
                        }
                        break;
                    
                    // BR_IF: Rcond, offset16
                    case 0x03:
                        if (i < bytecode.Count)
                        {
                            bytecode[i] = RemapReg(bytecode[i]);
                            i += 3; // Rcond + 2 bytes offset
                        }
                        break;
                    
                    // RET: [Rret]
                    case 0x01:
                        // No operands for void return, 1 byte Rret for non-void
                        // We don't know here, so skip (handled elsewhere or no operand)
                        break;
                    
                    // CALL_IMPORT uses R0-R7 (already set by MOVs we just remapped)
                    case 0x09:
                        i += 2; // import_idx u16
                        if (i < bytecode.Count && bytecode[i-2] == 0xAA) // extended format marker
                        {
                            i++; // num_args
                            byte numArgs = bytecode[i-1];
                            i += numArgs; // arg types
                        }
                        break;
                    
                    // CALL: func_idx u16 (uses R0-R7, no remapping needed in opcode itself)
                    case 0x0A:
                        i += 2;
                        break;
                    
                    // Skip other opcodes (BR, BR_TABLE, etc.)
                    default:
                        // For unknown opcodes, we can't safely skip - log warning
                        _logger.WriteLine($"[REMAP] WARNING: Unknown opcode 0x{opcode:X2} at offset {i-1}, remapping may be incomplete");
                        // Try to continue anyway
                        break;
                }
            }
            
            _logger.WriteLine($"[REMAP] Register remapping applied successfully");
        }
        
        private byte RemapReg(byte oldReg)
        {
            if (_registerRemap != null && _registerRemap.TryGetValue(oldReg, out byte newReg))
            {
                return newReg;
            }
            return oldReg; // No mapping (e.g., temp regs or R0-R7 reserved)
        }
    }
}