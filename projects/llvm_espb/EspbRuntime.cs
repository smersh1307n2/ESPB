using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices; // Для Marshal
using System.Buffers;

namespace ESPB_Runtime
{
    // Типы данных и структуры из спецификации ESPB v1.6, используемые во время выполнения
    public enum BlockType { BLOCK, LOOP, IF, TRY } // Управление потоком

    // Фрейм стека управления
    public class ControlFrame
    {
        public BlockType Type { get; set; }
        public int ResultType { get; set; } // Индекс в Types (0 для void)
        public int StartPC { get; set; } // Адрес начала блока (для LOOP)
        public int EndPC { get; set; } // Адрес после END
        public int ElsePC { get; set; } // Адрес ELSE (только для IF)
        // Могут понадобиться другие поля, например, для хранения высоты стека операндов
    }

    // Фрейм выполнения (для стека вызовов)
    public class RuntimeFrame
    {
        public int ReturnPC { get; set; } // Адрес возврата
        public int SavedFP { get; set; } // Сохраненный указатель предыдущего фрейма
        public object SavedSP { get; set; } = null!; // Сохраненный стековый указатель SP (регистр R7)
        public object[] SavedRegs { get; set; } = Array.Empty<object>(); // Сохраненные регистры (весь массив R)
        public byte[] SavedCode { get; set; } = Array.Empty<byte>(); // Сохраненный код текущей функции
        public int SavedCodeLength { get; set; } = 0; // Сохраненная длина кода
    }

    // Основная виртуальная машина ESPB (очень упрощенная)
    public class ESPB_VM
    {
        private readonly TextWriter _logger;
        // Регистры (ESPB v1.6 имеет 8 регистров R0-R7)
        public object[] R = new object[8]; // Регистры общего назначения (тип object для простоты)
        public int PC; // Счетчик команд (указатель на текущую инструкцию в коде функции)
        private int FP; // Указатель фрейма (индекс текущего RuntimeFrame в RuntimeStack)

        #pragma warning disable CS0169 // Поле не используется, но оставлено для будущего использования
        private int SP; // Указатель стека (может эмулировать стековый автомат WASM/ESPB)
        #pragma warning restore CS0169

        // Стеки
        private Stack<ControlFrame> ControlStack = new Stack<ControlFrame>(); // Для управления блоками (IF/LOOP/BLOCK)
        private Stack<RuntimeFrame> RuntimeStack = new Stack<RuntimeFrame>(); // Стек вызовов функций

        // Память (линейная память)
        private byte[] Memory = new byte[1024 * 64]; // 64KB памяти (стандартный размер страницы WASM)

        // Таблицы из модуля (загружаются из EspbModule)
        private List<TypeSignature> Types = new List<TypeSignature>();
        private List<Function> Functions = new List<Function>(); // Описание внутренних функций (индекс типа)
        private List<Import> Imports = new List<Import>(); // Описание импортов
        private List<Tag> Tags = new List<Tag>(); // Для обработки исключений
        private byte[][] FunctionCode = Array.Empty<byte[]>(); // Массив с кодом каждой функции
        private object[] Globals = Array.Empty<object>(); // Значения глобальных переменных
        private Dictionary<string, Delegate> HostImports = new Dictionary<string, Delegate>(); // Внешние функции хоста


        public ESPB_VM(EspbModule module, Dictionary<string, Delegate> hostImports, TextWriter? logger = null)
        {
            _logger = logger ?? Console.Out;
            HostImports = hostImports;
            LoadModule(module);
        }

        private void LoadModule(EspbModule module)
        {
            // 1. Загрузка секций
            var typesSection = module.Sections.OfType<TypesSection>().FirstOrDefault();
            var funcSection = module.Sections.OfType<FunctionSection>().FirstOrDefault();
            var codeSection = module.Sections.OfType<CodeSection>().FirstOrDefault();
            var importSection = module.Sections.OfType<ImportsSection>().FirstOrDefault();
            var globalSection = module.Sections.OfType<GlobalsSection>().FirstOrDefault();
            // TODO: Загрузка других секций (Exports, Data, etc.)

            if (typesSection != null) Types = typesSection.Signatures;
            // TODO: Загрузка Functions, Imports, Tags из соответствующих секций

            // Связываем индексы функций с их кодом
            if (funcSection != null && codeSection != null && funcSection.TypeIndices.Count == codeSection.Bodies.Count)
            {
                FunctionCode = new byte[codeSection.Bodies.Count][];
                Functions = new List<Function>(funcSection.TypeIndices.Count);
                for (int i = 0; i < codeSection.Bodies.Count; i++)
                {
                    Functions.Add(new Function { TypeIdx = funcSection.TypeIndices[i] });
                    FunctionCode[i] = codeSection.Bodies[i].Instructions;
                }
            }

            // Инициализация импортов (упрощенно)
            if (importSection != null)
            {
                // Process imports, potentially linking to HostImports
            }

            // Инициализация глобальных переменных
            if (globalSection != null)
            {
                Globals = new object[globalSection.Globals.Count];
                // TODO: Инициализировать значения Globals на основе globalSection.Globals
                // (Zero, Constant, DataSegmentOffset)
            }

             // TODO: Обработка секции Data для инициализации памяти

            _logger.WriteLine("ESPB Module Loaded (partially).");
        }


        // TODO: Метод для вызова экспортируемой функции
        public object? CallExportedFunction(string name, params object[] args)
        {
            // Найти экспорт функции по имени
            // Настроить начальное состояние VM (аргументы в регистрах/стеке)
            // Установить PC на начало функции
            // Запустить цикл выполнения Run()
            // Вернуть результат из R0 (или другого соглашения)
            throw new NotImplementedException();
        }

        public void Run()
        {
            // Основной цикл выполнения
            while (PC < currentCodeLength) // currentCodeLength - длина текущего массива байт-кода
            {
                byte opcode = ReadOpcode();
                ExecuteInstruction(opcode);
                // TODO: Обработка прерываний, лимитов выполнения и т.д.
            }
        }

        private int currentCodeLength = 0; // Placeholder
        private byte[] currentCode = Array.Empty<byte>(); // Placeholder

        private byte ReadOpcode()
        {
            if (PC >= currentCode.Length) throw new IndexOutOfRangeException("PC out of bounds");
            return currentCode[PC++];
        }

        // Реализация инструкций ESPB (некоторые примеры)
        public void ExecuteInstruction(byte opcode)
        {
            // Реализация будет здесь, используя методы Handle*
             _logger.WriteLine($"Executing Opcode: 0x{opcode:X2} at PC={PC-1}");
            switch (opcode)
            {
                // Базовые инструкции
                case 0x01: HandleReturn(); break;
                case 0x05: throw new InvalidOperationException("Unreachable code executed");
                case 0x06: StartBlock(); break;
                case 0x07: StartLoop(); break;
                case 0x0B: HandleIf(); break;
                case 0x0C: HandleElse(); break;
                case 0x0F: EndBlock(); break;

                // Инструкции MOV (пример)
                case 0x12: // MOV.I32 Rd, Rs
                    {
                        ushort rd = ReadU16();
                        ushort rs = ReadU16();
                        R[rd] = R[rs]; // Прямое копирование для I32
                        break;
                    }
                case 0x8F: // ALLOCA Rd, Rs
                    {
                        byte rd = ReadU8();
                        byte rs = ReadU8();
                        // Rd = old SP; SP = SP + Rs
                        int oldSP = Convert.ToInt32(R[7]);
                        R[rd] = oldSP;
                        int bump = Convert.ToInt32(R[rs]);
                        R[7] = oldSP + bump;
                        break;
                    }
                // TODO: Другие MOV инструкции

                // Арифметические операции (пример)
                case 0x20: HandleAddI32(); break; // ADD.I32 Rd, R1, R2
                // TODO: Другие арифметические инструкции

                 // Операции сравнения (пример)
                case 0x40: HandleEqI32(); break; // EQ.I32 Rd, R1, R2
                // TODO: Другие инструкции сравнения

                // Операции управления
                case 0x02: HandleBr(); break; // BR target
                case 0x03: HandleBrIf(); break; // BR_IF cond, target
                case 0x08: HandleCall(); break; // CALL func_idx
                case 0x09: HandleCallImport(); break; // CALL_IMPORT import_idx
                case 0x0A: HandleCallIndirect(); break; // CALL_INDIRECT Rfunc, type_idx
                case 0x0D: HandleCallIndirectPtr(); break; // CALL_INDIRECT_PTR Rfunc_ptr, type_idx

                // Операции с памятью (пример)
                case 0x72: HandleLoadI32(); break; // LOAD.I32 Rd, Ra, offset
                case 0x82: HandleStoreI32(); break; // STORE.I32 Rs, Ra, offset
                // TODO: Другие инструкции памяти (включая GEP - его нет в опкодах ESPB v1.6, эмулируется через ADD?)

                // Константы (пример)
                case 0x18: HandleLdcI32Imm(); break; // LDC.I32.IMM Rd, value
                // TODO: Другие LDC инструкции

                default:
                    throw new InvalidOperationException($"Unknown or unimplemented opcode: 0x{opcode:X2} at PC={PC-1}");
            }
        }

        // --- Реализации обработчиков инструкций (Примеры) ---

         private void HandleLdcI32Imm()
        {
            ushort rd = ReadU16();
            int value = ReadI32();
            R[rd] = value;
        }

        private void HandleAddI32()
        {
            ushort rd = ReadU16();
            ushort r1 = ReadU16();
            ushort r2 = ReadU16();
            // TODO: Проверка типов операндов
            R[rd] = Convert.ToInt32(R[r1]) + Convert.ToInt32(R[r2]);
        }

        private void HandleEqI32()
        {
            ushort rd = ReadU16();
            ushort r1 = ReadU16();
            ushort r2 = ReadU16();
            // TODO: Проверка типов операндов
            R[rd] = Convert.ToInt32(R[r1]) == Convert.ToInt32(R[r2]); // Результат BOOL (или I32 0/1)
        }

        private void HandleLoadI32()
        {
            ushort rd = ReadU16();
            ushort ra = ReadU16(); // Регистр с адресом
            byte memIdx = ReadU8(); // Индекс памяти (0 для стандартной)
            short offset = ReadI16(); // Смещение

            if (memIdx != 0) throw new NotSupportedException("Only memory index 0 is supported.");
            // TODO: Проверка типа адреса
            int baseAddress = Convert.ToInt32(R[ra]);
            int effectiveAddress = baseAddress + offset;

            // TODO: Проверка границ памяти
            if (effectiveAddress < 0 || effectiveAddress + 4 > Memory.Length)
                throw new IndexOutOfRangeException("Memory access out of bounds during LOAD.I32");

            R[rd] = BitConverter.ToInt32(Memory, effectiveAddress);
        }

        private void HandleStoreI32()
        {
            ushort rs = ReadU16(); // Регистр со значением
            ushort ra = ReadU16(); // Регистр с адресом
            byte memIdx = ReadU8();
            short offset = ReadI16();

             if (memIdx != 0) throw new NotSupportedException("Only memory index 0 is supported.");
            // TODO: Проверка типов
            int baseAddress = Convert.ToInt32(R[ra]);
            int effectiveAddress = baseAddress + offset;
            int valueToStore = Convert.ToInt32(R[rs]);

            // TODO: Проверка границ памяти
            if (effectiveAddress < 0 || effectiveAddress + 4 > Memory.Length)
                throw new IndexOutOfRangeException("Memory access out of bounds during STORE.I32");

            BitConverter.GetBytes(valueToStore).CopyTo(Memory, effectiveAddress);
        }


        private void HandleBr()
        {
            ushort targetLabel = ReadU16(); // ESPB использует метки/смещения
            // TODO: Преобразовать метку/смещение в абсолютный PC
            PC = targetLabel; // Упрощенно - предполагаем, что targetLabel это PC
        }

        private void HandleBrIf()
        {
            ushort condReg = ReadU16();
            ushort targetLabel = ReadU16();
            // TODO: Проверка типа условия (ожидается BOOL или I32)
            if (Convert.ToBoolean(R[condReg])) // Упрощенно
            {
                 PC = targetLabel; // Упрощенно
            }
        }

        private void HandleCall()
        {
            ushort funcIndex = ReadU16();
            if (funcIndex >= Functions.Count) throw new InvalidOperationException($"Invalid function index: {funcIndex}");

            // Получение сигнатуры функции
            var funcSignature = Types[Functions[funcIndex].TypeIdx];

            // Сохраняем текущее состояние в фрейме
            var frame = new RuntimeFrame
            {
                ReturnPC = PC, // Адрес после инструкции CALL
                SavedFP = FP,
                SavedSP = R[7], // Сохраняем текущий SP (регистр R7)
                SavedRegs = R,  // Сохраняем весь массив регистров
                SavedCode = currentCode,        
                SavedCodeLength = currentCodeLength
            };

            RuntimeStack.Push(frame);
            FP = RuntimeStack.Count - 1; // Новый FP

            // Создаем новый контекст регистров для вызываемой функции, копируя аргументы R0-R7
            var newRegs = new object[R.Length];
            for (int i = 0; i < R.Length; i++) newRegs[i] = R[i];
            R = newRegs;

            // Установка PC и кода на начало вызываемой функции
            currentCode = FunctionCode[funcIndex];
            currentCodeLength = currentCode.Length;
            PC = 0;
        }

         private void HandleCallImport()
        {
            ushort importIndex = ReadU16();
            // TODO: Найти импортированную функцию по индексу
            // TODO: Вызвать функцию хоста (HostImports)
            // TODO: Обработать аргументы и результат
            throw new NotImplementedException("CALL_IMPORT not fully implemented");
        }

        private void HandleCallIndirect()
        {
             byte rFunc = ReadU8(); // Регистр с указателем/индексом функции
             ushort typeIndex = ReadU16(); // Ожидаемый индекс типа функции

            _logger.WriteLine($"[CALL_INDIRECT] rFunc=R{rFunc}, typeIndex={typeIndex}");

            // Получаем значение из регистра - это должен быть индекс функции с флагом CALLBACK_FLAG_BIT
            uint funcValue = (uint)Convert.ToInt32(R[rFunc]);
            _logger.WriteLine($"[CALL_INDIRECT] Function value from R{rFunc}: 0x{funcValue:X8}");

            // Проверяем флаг колбэка
            const uint CALLBACK_FLAG_BIT = 0x80000000;
            if ((funcValue & CALLBACK_FLAG_BIT) == 0)
            {
                throw new InvalidOperationException($"CALL_INDIRECT: Function value 0x{funcValue:X8} does not have callback flag set");
            }

            // Извлекаем индекс функции
            uint funcIndex = funcValue & ~CALLBACK_FLAG_BIT;
            _logger.WriteLine($"[CALL_INDIRECT] Extracted function index: {funcIndex}");

            // Проверяем, что индекс функции валиден
            if (funcIndex >= Functions.Count)
            {
                throw new InvalidOperationException($"CALL_INDIRECT: Function index {funcIndex} out of range (max: {Functions.Count})");
            }

            // Получаем тип функции и проверяем соответствие
            ushort actualTypeIndex = (ushort)Functions[(int)funcIndex].TypeIdx;
            if (actualTypeIndex != typeIndex)
            {
                _logger.WriteLine($"[CALL_INDIRECT] Warning: Type mismatch. Expected {typeIndex}, got {actualTypeIndex}");
                // В реальной реализации здесь может быть более строгая проверка
            }

            _logger.WriteLine($"[CALL_INDIRECT] Calling internal function at index {funcIndex}");

            // Вызываем внутреннюю функцию (используем тот же код, что и в HandleCall)
            // Получение сигнатуры функции
            var funcSignature = Types[Functions[(int)funcIndex].TypeIdx];

            // Сохраняем текущее состояние в фрейме
            var frame = new RuntimeFrame
            {
                ReturnPC = PC, // Адрес после инструкции CALL_INDIRECT
                SavedFP = FP,
                SavedSP = R[7], // Сохраняем текущий SP (регистр R7)
                SavedRegs = R,  // Сохраняем весь массив регистров
                SavedCode = currentCode,        
                SavedCodeLength = currentCodeLength
            };

            RuntimeStack.Push(frame);
            FP = RuntimeStack.Count - 1; // Новый FP

            // Создаем новый контекст регистров для вызываемой функции, копируя аргументы R0-R7
            var newRegs = new object[R.Length];
            for (int i = 0; i < R.Length; i++) newRegs[i] = R[i];
            R = newRegs;

            // Установка PC и кода на начало вызываемой функции
            currentCode = FunctionCode[funcIndex];
            currentCodeLength = currentCode.Length;
            PC = 0;
        }

        private void HandleCallIndirectPtr()
        {
            // Реализация для CALL_INDIRECT_PTR - аналогично HandleCallIndirect, но для небезопасных вызовов
            byte rFunc = ReadU8(); // Регистр с указателем/индексом функции
            ushort typeIndex = ReadU16(); // Ожидаемый индекс типа функции

            _logger.WriteLine($"[CALL_INDIRECT_PTR] rFunc=R{rFunc}, typeIndex={typeIndex}");

            // Получаем значение из регистра - это должен быть прямой адрес функции
            uint funcValue = (uint)Convert.ToInt32(R[rFunc]);
            _logger.WriteLine($"[CALL_INDIRECT_PTR] Function pointer value from R{rFunc}: 0x{funcValue:X8}");

            // Для небезопасного режима - интерпретируем как прямой адрес функции
            // В реальной реализации здесь должен быть вызов функции по адресу
            // Пока что просто логируем и возвращаем ошибку
            _logger.WriteLine($"[CALL_INDIRECT_PTR] WARNING: Unsafe function call not implemented in C# runtime");
            throw new NotImplementedException("CALL_INDIRECT_PTR not implemented in C# runtime - use ESP-IDF runtime");
        }

        private void HandleReturn()
        {
            // 1. Проверяем, есть ли что-то в стеке. Если нет, значит, это возврат из самой первой функции.
            if (RuntimeStack.Count == 0)
            {
                // Выполнение действительно завершено.
                currentCodeLength = 0;
                _logger.WriteLine("DEBUG: Top-level function returned. Terminating execution.");
                return;
            }

            // 2. Стек НЕ пуст, значит, это возврат из вложенного вызова.
            // Только теперь мы извлекаем фрейм.
            var frame = RuntimeStack.Pop();

            // 3. Сохраняем возвращаемое значение (результат) из R0 текущей (завершившейся) функции.
            object retVal = R[0];

            // 4. Восстанавливаем ПОЛНЫЙ контекст вызывающей функции.
            R = frame.SavedRegs;
            R[0] = retVal; // Помещаем результат в R0 вызывающей функции.
            R[7] = frame.SavedSP;

            PC = frame.ReturnPC;
            FP = frame.SavedFP;

            // 5. Восстанавливаем байт-код и его длину для вызывающей функции, чтобы она могла продолжить работу.
            // Этот блок теперь выполняется всегда при возврате из вложенной функции.
            currentCode = frame.SavedCode;
            currentCodeLength = frame.SavedCodeLength;
        }

        private void StartBlock() // BLOCK type_idx
        {
            int type_idx = ReadU16();
            // TODO: Найти соответствующий END и вычислить end_pc
            int end_pc_placeholder = PC + 10; // Placeholder

            ControlStack.Push(new ControlFrame
            {
                Type = BlockType.BLOCK,
                ResultType = type_idx,
                EndPC = end_pc_placeholder
            });
        }

        private void StartLoop() // LOOP type_idx
        {
             int type_idx = ReadU16();
            // TODO: Найти соответствующий END и вычислить end_pc
            int end_pc_placeholder = PC + 10; // Placeholder

            ControlStack.Push(new ControlFrame
            {
                Type = BlockType.LOOP,
                ResultType = type_idx,
                StartPC = PC - 1, // Адрес самой инструкции LOOP для BR
                EndPC = end_pc_placeholder
            });
        }

        private void HandleIf() // IF type_idx
        {
            int type_idx = ReadU16();
            // TODO: Найти соответствующие ELSE/END и вычислить else_pc, end_pc
            int else_pc_placeholder = PC + 5; // Placeholder
            int end_pc_placeholder = PC + 10; // Placeholder

            var frame = new ControlFrame
            {
                Type = BlockType.IF,
                ResultType = type_idx,
                EndPC = end_pc_placeholder,
                ElsePC = else_pc_placeholder
            };
            ControlStack.Push(frame);

            // Проверка условия из R0 (упрощенно)
            // TODO: Соглашение, где лежит условие (регистр или стек операндов)
            ushort condReg = 0; // Предположим R0
            if (!Convert.ToBoolean(R[condReg]))
            {
                // Если условие ложно, переходим к ELSE (или END, если ELSE нет)
                PC = frame.ElsePC != 0 ? frame.ElsePC : frame.EndPC;
            }
            // Если истинно, просто продолжаем выполнение
        }

        private void HandleElse() // ELSE
        {
            if (ControlStack.Count == 0 || ControlStack.Peek().Type != BlockType.IF)
                throw new InvalidOperationException("ELSE without matching IF");

            var frame = ControlStack.Peek(); // Не снимаем фрейм, он нужен для END
            // Безусловно переходим к END блока IF
            PC = frame.EndPC;
        }

        private void EndBlock() // END
        {
            if (ControlStack.Count == 0) throw new InvalidOperationException("END without matching block");

            var frame = ControlStack.Pop();
            // Ничего не делаем с PC, просто снимаем фрейм
            // Переход после END происходит естественным образом
            // (Кроме случая, когда END завершает функцию - это обрабатывается в RET?)

             // TODO: Обработка передачи результатов блока на стек операндов, если необходимо
        }

        // --- Вспомогательные методы для чтения данных из currentCode --- 
        private ushort ReadU16()
        {
            if (PC + 2 > currentCode.Length) throw new IndexOutOfRangeException("ReadU16 out of bounds");
            ushort value = BitConverter.ToUInt16(currentCode, PC);
            PC += 2;
            return value;
        }
        private short ReadI16()
        {
            if (PC + 2 > currentCode.Length) throw new IndexOutOfRangeException("ReadI16 out of bounds");
            short value = BitConverter.ToInt16(currentCode, PC);
            PC += 2;
            return value;
        }
         private int ReadI32()
        {
            if (PC + 4 > currentCode.Length) throw new IndexOutOfRangeException("ReadI32 out of bounds");
            int value = BitConverter.ToInt32(currentCode, PC);
            PC += 4;
            return value;
        }
         private byte ReadU8()
        {
             if (PC + 1 > currentCode.Length) throw new IndexOutOfRangeException("ReadU8 out of bounds");
             byte value = currentCode[PC];
             PC += 1;
             return value;
        }

        // TODO: Добавить другие методы Read* для F32, F64, I64 и т.д.

        // Вспомогательный метод для получения размера типа данных (упрощенный)
        private int GetValueTypeSize(ValueType type)
        {
            switch (type)
            {
                case ValueType.I8: case ValueType.U8: case ValueType.BOOL: return 1;
                case ValueType.I16: case ValueType.U16: return 2;
                case ValueType.I32: case ValueType.U32: case ValueType.F32: case ValueType.PTR: return 4; // Assuming 32-bit pointers
                case ValueType.I64: case ValueType.U64: case ValueType.F64: return 8;
                case ValueType.V128: return 16;
                case ValueType.VOID: return 0;
                default: throw new InvalidOperationException($"Unknown type size: {type}");
            }
        }
    }
}
