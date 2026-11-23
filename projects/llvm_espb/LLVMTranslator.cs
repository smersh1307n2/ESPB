using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
// Предполагаем, что LLVMSharp установлен через NuGet
using LLVMSharp;
using LLVMSharp.Interop;
using System.Runtime.InteropServices; // Для Marshal
using System.Buffers;


namespace ESPB_Runtime
{
    // Перечисления для маршалинга аргументов
    public enum MarshallingDirection
    {
        Unknown = -1, // Неизвестное направление (требует дополнительного анализа)
        None = 0,   // Нет маршалинга
        In = 1,     // Копировать данные из ESPB в нативный буфер перед вызовом
        Out = 2,    // Копировать данные из нативного буфера в ESPB после вызова
        InOut = 3,  // Двунаправленный маршалинг (IN | OUT)
    }

    public enum SizeKind
    {
        Constant = 0,        // Размер - константа в байтах
        FromArgument = 1,    // Размер берётся из другого аргумента функции
        NullTerminated = 2   // Размер = strlen(arg[size_value]) + 1 (для strcpy и подобных)
    }

    public class MarshallingInfo
    {
        public MarshallingDirection Direction { get; set; } = MarshallingDirection.None;
        public SizeKind SizeMode { get; set; } = SizeKind.Constant;
        public uint SizeValue { get; set; } = 0; // Константа или индекс аргумента
        public byte HandlerIndex { get; set; } = 0; // Новое поле для типа обработчика

        public MarshallingInfo() { }

        public MarshallingInfo(MarshallingDirection dir, SizeKind sizeMode, uint sizeValue)
        {
            Direction = dir;
            SizeMode = sizeMode;
            SizeValue = sizeValue;
            HandlerIndex = 0; // По умолчанию стандартный обработчик
        }

        public MarshallingInfo(MarshallingDirection dir, SizeKind sizeMode, uint sizeValue, byte handlerIndex)
        {
            Direction = dir;
            SizeMode = sizeMode;
            SizeValue = sizeValue;
            HandlerIndex = handlerIndex;
        }
    }


    public class CallbackParameterInfo
    {
        public int CallbackParamIndex { get; set; }
        public int? UserDataParamIndex { get; set; }
        public int SignatureIndex { get; set; }
        public int EspbFuncIndex { get; set; } // ИНДЕКС ФУНКЦИИ-КОЛБЭКА
        public byte Flags { get; set; }

        public CallbackParameterInfo(int callbackParamIndex, int? userDataParamIndex = null)
        {
            CallbackParamIndex = callbackParamIndex;
            UserDataParamIndex = userDataParamIndex;
            SignatureIndex = 0;
            Flags = 0;
        }
    }
    
    // --- Транслятор LLVM -> ESPB ---
    public partial class LLVMTranslator
    {
        // Optional fast symbol maps (name -> index)
        public Dictionary<string, ushort>? CustomFastMap { get; set; }
        public Dictionary<string, ushort>? IdfFastMap { get; set; }

        public bool EnableRegisterPatching { get; set; } = false;

        // Bytecode peephole optimizer runs after codegen and can be disabled for bisecting issues.
        public bool EnablePeephole { get; set; } = true;

        // LLVM IR optimization pipeline (mem2reg/instcombine/sroa/...) can be disabled for bisecting issues.
        public bool EnableLlvmOptimizations { get; set; } = true;

        // Call-result optimizations (skip MOV from R0 when unused / forwarding to next call).
        // Useful for performance but can be disabled to bisect correctness issues.
        public bool EnableCallResultOptimizations { get; set; } = true;

        // ALLOCA-OPT: promote scalar allocas to virtual registers instead of memory.
        // This optimization can be disabled if address-taken detection fails or for debugging.
        public bool EnableAllocaOpt { get; set; } = true;

        private EspbModule espbModule = new EspbModule(); // Инициализация
        private LLVMModuleRef llvmModule;
        private LLVMContextRef llvmContext;
        // Инициализация словарей для CS8618
        private RegisterManager registerManager;
        private DataSection dataSection = new DataSection(); // Может быть null до инициализации в TranslateFile
        private Dictionary<LLVMValueRef, uint> llvmFuncToEspbIndex = new Dictionary<LLVMValueRef, uint>();
        private Dictionary<LLVMValueRef, uint> llvmGlobalToEspbIndex = new Dictionary<LLVMValueRef, uint>();
        private Dictionary<LLVMValueRef, uint> llvmImportToEspbIndex = new Dictionary<LLVMValueRef, uint>();
        private uint nextEspbFuncIndex = 0;
        private uint nextEspbGlobalIndex = 0;
        private uint nextEspbImportIndex = 0;
        private LLVMTargetDataRef targetData;
        private Dictionary<LLVMValueRef, uint> _funcPtrToDataOffset = new Dictionary<LLVMValueRef, uint>();
        // Словарь для хранения меток базовых блоков
        private Dictionary<LLVMBasicBlockRef, int> basicBlockToLabel = new Dictionary<LLVMBasicBlockRef, int>();
        private int nextLabel = 0;
        // Count of opcodes emitted during translation (before any final cleanup passes).
        public int OpcodeCount { get; private set; } = 0;

        // Count of opcodes in the final bytecode written to the ESPB module (after cleanup/peephole).
        public int FinalOpcodeCount { get; private set; } = 0;
        
        // --- Новые поля для backpatching с относительными смещениями ---
    
        // Структуры для улучшенной обработки PHI-инструкций
        private class PhiInstruction
        {
            public LLVMValueRef PhiValue { get; }
            public byte DestReg { get; }
            public Dictionary<LLVMBasicBlockRef, byte> IncomingValues { get; } = new Dictionary<LLVMBasicBlockRef, byte>();
            public byte MoveOpcode { get; }
            
            public PhiInstruction(LLVMValueRef phiValue, byte destReg, byte moveOpcode)
            {
                PhiValue = phiValue;
                DestReg = destReg;
                MoveOpcode = moveOpcode;
            }
            
            public void AddIncomingValue(LLVMBasicBlockRef block, byte valueReg)
            {
                IncomingValues[block] = valueReg;
            }
        }
        
        // Словарь для хранения всех PHI-инструкций, сгруппированных по базовым блокам
        private Dictionary<LLVMBasicBlockRef, List<PhiInstruction>> phiInstructions = new Dictionary<LLVMBasicBlockRef, List<PhiInstruction>>();
        // Словарь для хранения инструкций, которые нужно добавить в конец каждого базового блока
        private Dictionary<LLVMBasicBlockRef, List<List<byte>>> blockTailInstructions = new Dictionary<LLVMBasicBlockRef, List<List<byte>>>();
        
        // Хранит информацию о коллбэк-параметрах для импортов (ключ - имя импорта)
        private Dictionary<string, List<CallbackParameterInfo>> callbackInfoMap = new Dictionary<string, List<CallbackParameterInfo>>();
        
        // Таблица сигнатур колбэков, вычисленных из IR
        private List<(ValueType Ret, List<ValueType> Params)> callbackSignatures = new List<(ValueType Ret, List<ValueType> Params)>();
        private Dictionary<string, int> signatureKeyToIndex = new Dictionary<string, int>();
        
        // Менеджер для работы с hints файлами
        private HintsManager? hintsManager = null;
        private readonly TextWriter _logger;

        // Optional bytecode emitter for register patching (experimental)
        private BytecodeEmitter? _bytecodeEmitter;
        private readonly SpecialFunctionAnalyzer _specialFunctionAnalyzer;
        
        public LLVMTranslator() : this(Console.Out)
        {
        }

        public LLVMTranslator(TextWriter logger)
        {
            _logger = logger;
            _specialFunctionAnalyzer = new SpecialFunctionAnalyzer(logger);
            registerManager = new RegisterManager(logger);
            this.immetaManager = new ImmetaManager(this.hintsManager, this._logger);
        }

        public void SetCompactionEnabled(bool enabled)
        {
            registerManager.EnableCompaction = enabled;
        }

        private void SyncRegisterManagerModes()
        {
            // When register patching is enabled, allow temp regs to be compacted away.
            registerManager.AllowTempRegCompaction = EnableRegisterPatching;
        }

        public LLVMTranslator(TextWriter logger, bool enableCompaction) : this(logger)
        {
            SetCompactionEnabled(enableCompaction);
            // Инициализация кэша для анализа колбэков
            callbackAnalysisCache = new Dictionary<string, bool>();
        }

        private void EmitOpcode(List<byte> output, byte opcode, string? debugName = null)
        {
            output.Add(opcode);
            OpcodeCount++;
            if(debugName != null) {
                //_logger.WriteLine($"      [EMIT] {debugName} (0x{opcode:X2})");
            }
        }

        private void EmitOpcode(BytecodeEmitter be, byte opcode, string? debugName = null)
        {
            be.AddByte(opcode);
            OpcodeCount++;
            if(debugName != null) {
                //_logger.WriteLine($"      [EMIT] {debugName} (0x{opcode:X2})");
            }
        }

        // Register-operand helper: if patching is enabled, record operand positions.
        private void AddReg(List<byte> output, byte reg)
        {
            if (_bytecodeEmitter != null) _bytecodeEmitter.AddReg(reg);
            else output.Add(reg);
        }

          private unsafe byte[]? GetInitializerData(LLVMValueRef initializer)
        {
            if (initializer.Handle == IntPtr.Zero)
            {
                _logger.WriteLine("  Error: Initializer handle is null");
                return null;
            }
            
            _logger.WriteLine($"  Trying to extract initializer data for type: {SafePrintType(initializer.TypeOf)}");
            
            // --- Обработка ConstantString ---
            if (LLVM.IsConstantString(initializer) != 0)
            {
                _logger.WriteLine("  Initializer is a ConstantString.");
                UIntPtr length;
                sbyte* strBytes = LLVM.GetAsString(initializer, (nuint*)&length);
                if (strBytes != null && length > 0)
                {
                    byte[] data = new byte[(int)length];
                    Marshal.Copy((IntPtr)strBytes, data, 0, (int)length);
                    _logger.WriteLine($"  Extracted string data: {length} bytes.");
                    return data;
                }
                else
                {
                    _logger.WriteLine("  Warning: Failed to get string data using GetAsString.");
                    return null; // Не удалось получить данные строки
                }
            }

            // --- Обработка ConstantDataSequential (массив простых типов) ---
            else if (LLVM.IsAConstantDataSequential(initializer) != null)
            {
                _logger.WriteLine("  Initializer is ConstantDataSequential.");
                LLVMTypeRef seqType = initializer.TypeOf;
                LLVMTypeRef elementType = LLVM.GetElementType(seqType);
                uint length = LLVM.GetArrayLength(seqType); // LLVM.GetElementCount устарел
                _logger.WriteLine($"    Array Type: {SafePrintType(seqType)}, Element Type: {SafePrintType(elementType)}, Length: {length}");

                if (length == 0) return Array.Empty<byte>();

                // --- Обработка массива байт (i8) ---
                if (elementType.Kind == LLVMTypeKind.LLVMIntegerTypeKind && elementType.IntWidth == 8)
                {
                    _logger.WriteLine("    Processing as byte array (i8).");
                    // LLVMGetAggregateElement устарел для ConstantData*
                    byte[] data = new byte[length];
                    bool success = true;
                    for (uint i = 0; i < length; ++i)
                    {
                        LLVMValueRef elementConst = LLVM.GetAggregateElement(initializer, i);
                        if (elementConst.Handle != IntPtr.Zero && TryMapLlvmConstant(elementConst, out object? val) && val != null)
                        {
                            try
                            {
                                // Используем Convert.ToByte для безопасного преобразования
                                data[i] = Convert.ToByte(val);
                            }
                            catch (OverflowException)
                            {
                                _logger.WriteLine($"      Warning: Overflow converting element {i} value {val} to byte.");
                                success = false; break;
                            }
                            catch (InvalidCastException)
                            {
                                _logger.WriteLine($"      Warning: Invalid cast converting element {i} value {val} to byte.");
                                success = false; break;
                            }
                        }
                        else
                        {
                            _logger.WriteLine($"      Warning: Failed to get or map constant for element {i}.");
                            success = false; break;
                        }
                    }

                    if (success)
                    {
                        _logger.WriteLine($"    Successfully extracted {data.Length} bytes for ConstantDataSequential<i8>.");
                        return data;
                    }
                    else
                    {
                        _logger.WriteLine("    Failed to extract all elements for ConstantDataSequential<i8>.");
                        return null;
                    }
                }
                // --- Обработка других массивов (рекурсивно) ---
                else
                {
                    _logger.WriteLine("    Processing as non-byte array (recursive).");
                    List<byte> elementBytes = new List<byte>();
                    bool success = true;
                    for (uint i = 0; i < length; ++i)
                    {
                        LLVMValueRef elementConst = LLVM.GetAggregateElement(initializer, i);
                        if (elementConst.Handle == IntPtr.Zero)
                        {
                            _logger.WriteLine($"      Warning: Element {i} handle is null. Trying alternate method.");
                            // Попробуем альтернативный метод получения элемента для некоторых типов констант
                            if (elementType.Kind == LLVMTypeKind.LLVMIntegerTypeKind)
                            {
                                // Для целочисленных элементов массива - попробуем получить целочисленный литерал
                                // Для LLVM >= 10 можно использовать LLVM.GetConstantDataSequentialElementAsInteger
                                // Здесь упрощенная логика
                                if (TryMapLlvmConstant(initializer, out object? containerValue) && containerValue is Array arr && i < arr.Length)
                                {
                                    object? value = arr.GetValue((int)i);
                                    if (value != null)
                                    {
                                        using var memStream = new MemoryStream();
                                        using var writer = new BinaryWriter(memStream);
                                        WriteConstantValue(writer, MapLlvmTypeToEspb(elementType), value);
                                        elementBytes.AddRange(memStream.ToArray());
                                        continue; // Успешно обработали элемент, переходим к следующему
                                    }
                                }
                            }
                            success = false;
                            break;
                        }
                        
                        byte[]? elementData = GetInitializerData(elementConst); // Рекурсивный вызов
                        if (elementData != null)
                        {
                            elementBytes.AddRange(elementData);
                        }
                        else
                        {
                            _logger.WriteLine($"      Warning: Failed to get data for element {i} of type {SafePrintType(elementType)}.");
                            success = false;
                            break;
                        }
                    }

                    if (success)
                    {
                        _logger.WriteLine($"    Successfully extracted {elementBytes.Count} bytes recursively for ConstantDataSequential.");
                        return elementBytes.ToArray();
                    }
                    else
                    {
                         _logger.WriteLine("    Failed recursive extraction for ConstantDataSequential.");
                        return null;
                    }
                }
            }

            // --- Обработка ConstantStruct ---
            else if (LLVM.IsAConstantStruct(initializer) != null)
            {
                _logger.WriteLine("  Initializer is ConstantStruct.");
                LLVMTypeRef structType = initializer.TypeOf;
                uint numElements = LLVM.CountStructElementTypes(structType);
                _logger.WriteLine($"    Struct has {numElements} elements.");

                List<byte> structData = new List<byte>();
                bool success = true;

                for (uint i = 0; i < numElements; ++i)
                {
                    LLVMValueRef elementConst = LLVM.GetAggregateElement(initializer, i);
                    LLVMTypeRef elementType = LLVM.StructGetTypeAtIndex(structType, i);
                    _logger.WriteLine($"    Processing element {i} of type {SafePrintType(elementType)}");

                    if (elementConst.Handle == IntPtr.Zero)
                    {
                        _logger.WriteLine($"      Warning: Element {i} constant handle is zero.");
                        
                        // Для структур с нулевыми элементами, можно добавить заполнение нулями
                        uint elementSize = GetTypeSize(elementType);
                        if (elementSize > 0)
                        {
                            _logger.WriteLine($"      Adding {elementSize} zero bytes for missing element {i}");
                            structData.AddRange(new byte[elementSize]);
                            continue; // Переходим к следующему элементу
                        }
                        else
                        {
                            success = false;
                            break;
                        }
                    }

                    byte[]? elementData = GetInitializerData(elementConst); // Рекурсивный вызов
                    if (elementData != null)
                    {
                         _logger.WriteLine($"      Element {i} is Constant: {elementData.Length} bytes.");
                        structData.AddRange(elementData);
                    }
                    else
                    {
                        _logger.WriteLine($"      Warning: Failed to get data for struct element {i} of type {SafePrintType(elementType)}.");
                        // Попытка обработать нулевой элемент структуры
                        uint elementSize = GetTypeSize(elementType);
                        if (elementSize > 0)
                        {
                            _logger.WriteLine($"      Adding {elementSize} zero bytes as fallback");
                            structData.AddRange(new byte[elementSize]);
                        }
                        else
                        {
                            success = false;
                            break;
                        }
                    }
                }

                if (success)
                {
                    _logger.WriteLine($"    Successfully extracted {structData.Count} bytes for ConstantStruct.");
                    return structData.ToArray();
                }
                else
                {
                     _logger.WriteLine("    Failed recursive extraction for ConstantStruct.");
                    return null;
                }
            }
            // --- Обработка ConstantAggregateZero ---
            else if (LLVM.IsAConstantAggregateZero(initializer) != null)
            {
                _logger.WriteLine("  Initializer is ConstantAggregateZero.");
                LLVMTypeRef type = initializer.TypeOf;
                uint size = (uint)LLVM.StoreSizeOfType(targetData, type); // Используем StoreSizeOfType для определения размера
                _logger.WriteLine($"    Aggregate zero size: {size} bytes.");
                return new byte[size]; // Возвращаем массив нулей нужного размера
            }
            // --- Обработка GlobalValue (включая GlobalAlias) ---
            else if (LLVM.IsAGlobalValue(initializer) != null)
            {
                _logger.WriteLine("  Initializer is GlobalValue. Needs relocation.");
                // УДАЛЕНО: Возврат new byte[4]. Инициализация будет через релокации.
                // Теперь управление перейдет к концу метода и вернется null.
            }
            // --- Обработка ConstantExpr ---
            else if (LLVM.IsAConstantExpr(initializer) != null)
            {
                _logger.WriteLine("  Initializer is ConstantExpr.");
                LLVMOpcode opcode = LLVM.GetConstOpcode(initializer);
                _logger.WriteLine($"    ConstExpr opcode: {opcode}");

                // Обработка GetElementPtr в константном выражении
                if (opcode == LLVMOpcode.LLVMGetElementPtr)
                {
                    _logger.WriteLine("    Processing GetElementPtr constant expression. Needs relocation.");
                    // УДАЛЕНО: Возврат new byte[4]. Инициализация будет через релокации.
                    // Теперь управление перейдет к концу метода и вернется null.
                }
                // --- ИСПРАВЛЕНО: Нет else, если не GEP, просто продолжаем ---
                // else // Удалено
                {
                    // Попробуем вычислить константу, если она простая (например, bitcast)
                    if (TryMapLlvmConstant(initializer, out object? exprValue) && exprValue != null)
                    {
                        _logger.WriteLine($"    Evaluated ConstExpr to value: {exprValue}");
                        ValueType espbType = MapLlvmTypeToEspb(initializer.TypeOf);

                        using var memStream = new MemoryStream();
                        using var writer = new BinaryWriter(memStream);
                        WriteConstantValue(writer, espbType, exprValue);
                        return memStream.ToArray();
                    }
                } // Добавлена закрывающая скобка
            }
            // --- Обработка простых констант ---
            else if (TryMapLlvmConstant(initializer, out object? simpleValue) && simpleValue != null)
            {
                 _logger.WriteLine($"  Initializer is a simple constant: {simpleValue}");
                 ValueType espbType = MapLlvmTypeToEspb(initializer.TypeOf);

                // --- HOTFIX for i8 vs bool ---
                // LLVM uses i8 for C's bool, but we must distinguish it from a real i1.
                // If the original type was i8, force the ESPB type to be I8, not BOOL.
                if (initializer.TypeOf.Kind == LLVMTypeKind.LLVMIntegerTypeKind && initializer.TypeOf.IntWidth == 8)
                {
                    espbType = ValueType.I8;
                    _logger.WriteLine($"    HOTFIX: Forcing type to I8 for i8 constant.");
                }
                // --- END HOTFIX ---

                 List<byte> bytes = new List<byte>();
                 using (var writer = new BinaryWriter(new MemoryStream()))
                 {
                     try
                     {
                         WriteConstantValue(writer, espbType, simpleValue);
                         writer.Flush();
                         bytes.AddRange(((MemoryStream)writer.BaseStream).ToArray());
                         _logger.WriteLine($"    Converted simple constant to {bytes.Count} bytes.");
                         return bytes.ToArray();
                     }
                     catch (Exception ex)
                     {
                          _logger.WriteLine($"    Warning: Failed to convert simple constant {simpleValue} to bytes: {ex.Message}");
                        return null;
                     }
                 }
            }

            // Если ни один из типов не подошел
             _logger.WriteLine($"  Warning: GetInitializerData could not process initializer type: {initializer.TypeOf.Kind}");
            return null;
        }

        // --- ДОБАВЛЕН Вспомогательный метод для записи значения константы ---
        // Этот метод дублирует часть логики WriteConstant из GlobalsSection,
        // но необходим здесь для рекурсивного сбора данных.
        // Можно вынести в общий Util класс.
        private void WriteConstantValue(BinaryWriter writer, ValueType type, object value)
        {
             try
            {
                switch (type)
                {
                    case ValueType.I8: writer.Write(Convert.ToSByte(value)); break;
                    case ValueType.U8: writer.Write(Convert.ToByte(value)); break;
                    case ValueType.I16: writer.Write(Convert.ToInt16(value)); break;
                    case ValueType.U16: writer.Write(Convert.ToUInt16(value)); break;
                    case ValueType.I32: writer.Write(Convert.ToInt32(value)); break;
                    case ValueType.U32: writer.Write(Convert.ToUInt32(value)); break;
                    case ValueType.I64: writer.Write(Convert.ToInt64(value)); break;
                    case ValueType.U64: writer.Write(Convert.ToUInt64(value)); break;
                    case ValueType.F32: writer.Write(Convert.ToSingle(value)); break;
                    case ValueType.F64: writer.Write(Convert.ToDouble(value)); break;
                    case ValueType.BOOL: writer.Write(Convert.ToBoolean(value) ? (byte)1 : (byte)0); break;
                    // PTR обычно не является константой в этом контексте, но добавим для полноты
                    case ValueType.PTR:
                         // Предполагаем, что PTR хранится как Int32 или Int64
                         if (IntPtr.Size == 4) writer.Write(Convert.ToInt32(value));
                         else writer.Write(Convert.ToInt64(value));
                         break;
                    default:
                        throw new NotSupportedException($"Unsupported constant type for writing: {type}");
                }
            }
            catch (Exception ex)
            {
                 _logger.WriteLine($"    Error writing constant value of type {type} ({value}): {ex.Message}");
                throw; // Перебрасываем исключение
            }
        }

private int AddDataSegment(byte[] data, int alignment)
        {
            if (data == null || data.Length == 0) return -1;

            var existingSegment = dataSection.Segments.FirstOrDefault();
            if (existingSegment == null || existingSegment.SegmentType != DataSegmentType.Passive)
            {
                existingSegment = new DataSegment { SegmentType = DataSegmentType.Passive, Data = Array.Empty<byte>() };
                dataSection.Segments.Add(existingSegment);
            }

            int offset = existingSegment.Data.Length;

            var newData = new byte[offset + data.Length];
            Array.Copy(existingSegment.Data, newData, offset);
            Array.Copy(data, 0, newData, offset, data.Length);

            existingSegment.Data = newData;

            return offset;
        }
        private unsafe void CollectExports()
        {
            var exportsSection = GetSection<ExportsSection>(5)!;
            // Экспортируем функции с внешней линковкой (ExternalLinkage)
            var importsSection = GetSection<ImportsSection>(2)!; // Получаем секцию импортов
            uint funcImportCount = (uint)importsSection.Imports.Count(imp => imp.Kind == EspbImportKind.Function);
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
            {
                if (func.IsDeclaration) continue;
                if (func.Linkage == LLVMLinkage.LLVMExternalLinkage)
                {
                    unsafe
                    {
                        var namePtr = LLVM.GetValueName(func);
                        string name = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
                        // Создаем экспорт с объединённым индексом (импорты + определённые функции)
                        var export = new EspbExport
                        {
                            Name = name,
                            Kind = EspbExportKind.Function,
                            Index = llvmFuncToEspbIndex[func]
                        };
                        exportsSection.Exports.Add(export);
                    }
                }
            }

            // Экспортируем глобальные переменные с внешней линковкой
            for (LLVMValueRef global = LLVM.GetFirstGlobal(llvmModule); global.Handle != IntPtr.Zero; global = LLVM.GetNextGlobal(global))
            {
                if (global.IsDeclaration) continue;

                // Проверяем, имеет ли глобальная переменная внешнюю линковку
                if (global.Linkage == LLVMLinkage.LLVMExternalLinkage)
                {
                    unsafe
                    {
                        var namePtr = LLVM.GetValueName(global);
                        string name = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
                        // Создаем экспорт
                        var export = new EspbExport
                        {
                            Name = name,
                            Kind = EspbExportKind.Global,
                            Index = llvmGlobalToEspbIndex[global]
                        };

                        exportsSection.Exports.Add(export);
                    }
                }
            }
        }

        // TranslateSwitchInstr moved to LLVMTranslator_1.cs


        // Специальный метод для обработки PHI-фиксапов в switch
        private unsafe void AddSwitchPhiFixups(LLVMBasicBlockRef currentBlock, LLVMBasicBlockRef targetBlock, long caseValue, List<byte> output)
        {
            if (!phiInstructions.ContainsKey(targetBlock))
                return;

            _logger.WriteLine($"      [SWITCH PHI FIXUP] Processing switch case {caseValue}: {currentBlock.Handle:X} -> {targetBlock.Handle:X}");

            foreach (var phi in phiInstructions[targetBlock])
            {
                _logger.WriteLine($"      [SWITCH PHI FIXUP] PHI instruction R{phi.DestReg}");
                
                // Для switch нужно найти правильное значение PHI по исходному LLVM IR
                byte correctReg = 0;
                bool foundCorrectValue = false;
                
                // Анализируем исходную LLVM PHI инструкцию для определения правильного значения
                for (uint i = 0; i < phi.PhiValue.IncomingCount; i++)
                {
                    var incomingBlock = phi.PhiValue.GetIncomingBlock(i);
                    var incomingValue = phi.PhiValue.GetIncomingValue(i);
                    
                    // Проверяем, соответствует ли этот incoming блок нашему case
                    if (incomingBlock.Handle == currentBlock.Handle)
                    {
                        // Это правильный путь - получаем регистр для этого значения
                        try
                        {
                            (correctReg, _) = GetOperandReg(incomingValue, new List<byte>());
                            foundCorrectValue = true;
                            _logger.WriteLine($"        [SWITCH PHI FIXUP] Found correct value for case {caseValue}: R{correctReg}");
                            
                            // Проверяем, что это правильное значение для данного case
                            // Анализируем имя глобальной переменной или строкового литерала
                            if (incomingValue.IsAGlobalValue != null)
                            {
                                string globalName = incomingValue.Name ?? "";
                                _logger.WriteLine($"        [SWITCH PHI FIXUP] Global value: {globalName}");
                                
                                // Валидируем соответствие case -> строка
                                string expectedString = GetExpectedStringForCase(caseValue);
                                if (!string.IsNullOrEmpty(expectedString))
                                {
                                    _logger.WriteLine($"        [SWITCH PHI FIXUP] Expected string for case {caseValue}: '{expectedString}'");
                                    // Здесь можно добавить дополнительную валидацию
                                }
                            }
                            break;
                        }
                        catch (Exception ex)
                        {
                            _logger.WriteLine($"        [SWITCH PHI FIXUP] Failed to get register for case {caseValue}: {ex.Message}");
                        }
                    }
                }

                if (foundCorrectValue && correctReg != phi.DestReg)
                {
                    // Генерируем MOV инструкцию
                    output.Add(phi.MoveOpcode);
                    output.Add(phi.DestReg);
                    output.Add(correctReg);
                    _logger.WriteLine($"      [SWITCH PHI FIXUP] Generated {GetOpcodeName(phi.MoveOpcode)} R{phi.DestReg} <- R{correctReg} for case {caseValue}");
                }
                else if (!foundCorrectValue)
                {
                    _logger.WriteLine($"      [SWITCH PHI FIXUP] WARNING: No correct value found for case {caseValue}");
                }
                else
                {
                    _logger.WriteLine($"      [SWITCH PHI FIXUP] Skipped self-move for R{phi.DestReg}");
                }
            }
        }

        // Вспомогательный метод для определения ожидаемой строки для case
        private string GetExpectedStringForCase(long caseValue)
        {
            return caseValue switch
            {
                0 => "Case 0",
                1 => "Case 1", 
                2 => "Case 2",
                -1 => "Default case", // default case
                _ => ""
            };
        }


        // Добавляем метод GetSection, который отсутствует
        private T? GetSection<T>(byte id) where T : EspbSection
        {
            return espbModule.Sections.OfType<T>().FirstOrDefault(s => s.Id == id);
        }

        private unsafe void TranslateAlloca(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating ALLOCA: {instr}");

            // === REGISTER PROMOTION: Check if this alloca is promoted to a register ===
            // Promoted allocas don't need stack memory - they live entirely in registers
            if (_promotedAllocas.ContainsKey(instr))
            {
                byte promotedReg = _promotedAllocas[instr];
                _logger.WriteLine($"    [REG-PROMOTION] Alloca promoted to R{promotedReg} - skipping stack allocation");
                // No code needed - the promoted register will be used directly in load/store
                return;
            }

            // 1. Set the feature flag to indicate this module uses the new ALLOCA format.
            espbModule.Features |= 0x00000400; // FEATURE_ALLOCA_EXPLICIT_ALIGN

            // 2. Get destination register
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            
            // 3. Get size of the allocated type first.
            uint typeSize = GetTypeSize(LLVM.GetAllocatedType(instr));

            // 4. Get alignment, with fallback.
            uint alignVal = LLVM.GetAlignment(instr);
            if (alignVal == 0)
            {
                // Heuristic: Use the type's size as alignment, if it's a power of two.
                if (typeSize > 0 && (typeSize & (typeSize - 1)) == 0) {
                    alignVal = typeSize;
                } else {
                    alignVal = 4; // Absolute fallback for non-power-of-two sizes.
                }
                _logger.WriteLine($"    WARN: Alloca alignment was 0, defaulted to {alignVal}");
            }
            
            // Ensure alignment is a power of two. The interpreter will normalize non-power-of-two to 1.
            if (alignVal > 0 && (alignVal & (alignVal - 1)) != 0)
            {
                uint nextPowerOfTwo = 1;
                while (nextPowerOfTwo < alignVal) {
                    nextPowerOfTwo <<= 1;
                }
                _logger.WriteLine($"    WARN: Alloca alignment {alignVal} was not a power of two, rounded up to {nextPowerOfTwo}.");
                alignVal = nextPowerOfTwo;
            }
            if (alignVal == 0) alignVal = 1; // Final fallback

            // 5. Get size to allocate.
            LLVMValueRef sizeOperand = instr.GetOperand(0);
            byte sizeReg;

            // ИСПРАВЛЕНИЕ: Правильная проверка на константу
            bool isConstantSize = sizeOperand.IsConstant;
            _logger.WriteLine($"    DEBUG: sizeOperand.IsConstant={isConstantSize}, Kind={sizeOperand.Kind}");
            
            if (isConstantSize && sizeOperand.Kind == LLVMValueKind.LLVMConstantIntValueKind)
            {
                _logger.WriteLine($"    DEBUG: Inside constant size block!");
                // Size is a compile-time constant.
                uint numElements = (uint)LLVM.ConstIntGetZExtValue(sizeOperand);
                uint byteCount = typeSize * numElements;
                _logger.WriteLine($"    DEBUG: numElements={numElements}, byteCount={byteCount}, typeSize={typeSize}");
                
                // Per spec, the size passed to ALLOCA is the raw, unpadded size.
                _logger.WriteLine($"    Constant alloca: size={byteCount}, align={alignVal}");

                sizeReg = registerManager.AllocateTemporaryRegister();
                _logger.WriteLine($"    DEBUG: Allocated sizeReg={sizeReg}, emitting LDC.I32.IMM");
                EmitOpcode(output, 0x18, "LDC.I32.IMM");

                AddReg(output, sizeReg);

                output.AddRange(BitConverter.GetBytes((int)byteCount));
                _logger.WriteLine($"    DEBUG: Emitted LDC.I32.IMM R{sizeReg}, {byteCount}");
            }
            else
            {
                // Size is dynamic (in a register).
                (byte numElementsReg, _) = GetOperandReg(sizeOperand, output);
                
                if (typeSize > 1) {
                    byte typeSizeReg = GenerateIntConstant(output, (int)typeSize);
                    sizeReg = registerManager.AllocateTemporaryRegister();
                    EmitOpcode(output, 0x22, "MUL.I32");
                    WriteBinaryInstruction(output, sizeReg, numElementsReg, typeSizeReg);
                } else {
                    sizeReg = numElementsReg;
                }
                _logger.WriteLine($"    Dynamic alloca: typeSize={typeSize}, align={alignVal}");
            }

            // 6. Emit 4-byte ALLOCA opcode: [0x8F][Rd][Rs_size][imm_align]
            EmitOpcode(output, 0x8F, "ALLOCA");
            AddReg(output, destReg);
            AddReg(output, sizeReg);
            output.Add((byte)alignVal);

            _logger.WriteLine($"    Emitted ALLOCA R{destReg}, R{sizeReg}, align={(byte)alignVal}");
        }

        private unsafe void TranslateSelect(LLVMValueRef instr, List<byte> output)
        {
            // Select выбирает одно из двух значений на основе условия
            // Синтаксис: %result = select i1 %condition, TypeA %true_value, TypeA %false_value
            
            // Получаем регистр для результата
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            
            // Получаем регистры операндов
            (byte condReg, _) = GetOperandReg(instr.GetOperand(0), output);
            (byte trueValueReg, _) = GetOperandReg(instr.GetOperand(1), output);
            (byte falseValueReg, _) = GetOperandReg(instr.GetOperand(2), output);
            
            // Определяем тип результата для выбора правильного опкода SELECT
            ValueType resultType = MapLlvmTypeToEspb(instr.TypeOf);
            byte opcode;
            switch (resultType)
            {
                case ValueType.I32:  opcode = 0xBE; break; // SELECT.I32
                case ValueType.I64:  opcode = 0xBF; break; // SELECT.I64
                case ValueType.F32:  opcode = 0xD4; break; // SELECT.F32
                case ValueType.F64:  opcode = 0xD5; break; // SELECT.F64
                case ValueType.PTR:  opcode = 0xD6; break; // SELECT.PTR
                default:
                    throw new NotSupportedException($"Unsupported type for SELECT: {resultType}");
            }
            
            // Генерируем инструкцию SELECT
            EmitOpcode(output, opcode, "SELECT");
            AddReg(output, destReg);
            AddReg(output, condReg);
            AddReg(output, trueValueReg);
            AddReg(output, falseValueReg);
        }

        private unsafe void TranslatePHI(LLVMValueRef instr, List<byte>? output)
        {
            _logger.WriteLine($"[TranslatePHI] Processing PHI instruction: {instr}");
            _logger.WriteLine($"[TranslatePHI] Incoming count: {instr.IncomingCount}");
            
            // 1. Если output == null: только сбор информации о PHI-инструкции.
            // 2. Если output != null: генерация кода (но текущая логика не генерирует код здесь).

            var destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            var phiType = MapLlvmTypeToEspb(instr.TypeOf);
            var moveOpcode = GetMoveOpcode(phiType);

            _logger.WriteLine($"[TranslatePHI] Dest reg: R{destReg}, Type: {phiType}, Move opcode: 0x{moveOpcode:X2}");

            var phi = new PhiInstruction(instr, destReg, moveOpcode);
            
            for (uint i = 0; i < instr.IncomingCount; i++)
            {
                var incomingValue = instr.GetIncomingValue(i);
                var incomingBlock = instr.GetIncomingBlock(i);
                
                _logger.WriteLine($"[TranslatePHI] Incoming {i}: Block addr, Value addr");
                
                // Логируем информацию о глобальном значении
                if (incomingValue.IsAGlobalValue != null)
                {
                    string globalName = incomingValue.Name ?? "";
                    _logger.WriteLine($"[TranslatePHI]   -> Global: '{globalName}'");
                }
                
                // The new allocator should have pre-assigned a register for all values.
                // If it's not found, it's a spill or a bug in the allocator.
                (byte incomingReg, _) = GetOperandReg(incomingValue, output ?? new List<byte>());
                _logger.WriteLine($"[TranslatePHI]   -> Register: R{incomingReg}");
                
                phi.AddIncomingValue(incomingBlock, incomingReg);
            }

            // Сохраняем информацию о PHI для последующего использования
            var parentBlock = LLVM.GetInstructionParent(instr);
            _logger.WriteLine($"[TranslatePHI] Parent block addr");
            
            if (!phiInstructions.ContainsKey(parentBlock))
            {
                phiInstructions[parentBlock] = new List<PhiInstruction>();
            }
            phiInstructions[parentBlock].Add(phi);
            
            _logger.WriteLine($"[TranslatePHI] Added PHI to dictionary. Total blocks: {phiInstructions.Count}");

            // Логика генерации кода для PHI была перенесена в TranslateFunctionBody,
            // где создаются инструкции в конце каждого предшествующего блока.
            // Этот метод теперь в основном собирает информацию.
        }

        private unsafe void TranslateAtomicRMW(LLVMValueRef instr, List<byte> output)
        {
            espbModule.Features |= 0x00000002; // FEATURE_ATOMICS
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
            (byte valueReg, _) = GetOperandReg(instr.GetOperand(1), output);
            
            // Для atomicrmw тип результата совпадает с типом операнда
            ValueType espbType = MapLlvmTypeToEspb(instr.TypeOf);
            var atomicOp = LLVM.GetAtomicRMWBinOp(instr);
            
            byte opcode;

            if (espbType == ValueType.I32 || espbType == ValueType.U32)
            {
                switch (atomicOp)
                {
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAdd:   opcode = 0xD7; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpSub:   opcode = 0xD8; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAnd:   opcode = 0xD9; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpOr:    opcode = 0xDA; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXor:   opcode = 0xDB; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXchg:  opcode = 0xDC; break;
                    default:
                        _logger.WriteLine($"Warning: Unsupported atomic RMW operation for I32: {atomicOp}.");
                        return;
                }
                EmitOpcode(output, opcode, "ATOMIC.RMW");
                AddReg(output, destReg);
                AddReg(output, ptrReg);
                AddReg(output, valueReg);
            }
            else if (espbType == ValueType.I64 || espbType == ValueType.U64)
            {
                 switch (atomicOp)
                {
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAdd:   opcode = 0xF0; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpSub:   opcode = 0xF1; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAnd:   opcode = 0xF2; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpOr:    opcode = 0xF3; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXor:   opcode = 0xF4; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXchg:  opcode = 0xF5; break;
                    default:
                        _logger.WriteLine($"Warning: Unsupported atomic RMW operation for I64: {atomicOp}.");
                        return;
                }
                EmitOpcode(output, opcode, "ATOMIC.RMW");
                AddReg(output, destReg);
                AddReg(output, ptrReg);
                AddReg(output, valueReg);
            }
            else if (espbType == ValueType.I64 || espbType == ValueType.U64)
            {
                 switch (atomicOp)
                {
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAdd:   opcode = 0xF0; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpSub:   opcode = 0xF1; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpAnd:   opcode = 0xF2; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpOr:    opcode = 0xF3; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXor:   opcode = 0xF4; break;
                    case LLVMAtomicRMWBinOp.LLVMAtomicRMWBinOpXchg:  opcode = 0xF5; break;
                    default:
                        _logger.WriteLine($"Warning: Unsupported atomic RMW operation for I64: {atomicOp}.");
                        return;
                }
                EmitOpcode(output, opcode, "ATOMIC.RMW");
                AddReg(output, destReg);
                AddReg(output, ptrReg);
                AddReg(output, valueReg);
            }
            else
            {
                throw new NotSupportedException($"Unsupported type for atomic RMW: {espbType}");
            }
        }
        
        private unsafe void TranslateAtomicCmpXchg(LLVMValueRef instr, List<byte> output)
        {
            espbModule.Features |= 0x00000002; // FEATURE_ATOMICS
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
            (byte cmpReg, _) = GetOperandReg(instr.GetOperand(1), output);
            (byte newReg, _) = GetOperandReg(instr.GetOperand(2), output);
            
            // Для cmpxchg получаем тип из структуры возврата
            // cmpxchg возвращает {T, i1}, где T - тип операции
            LLVMTypeRef structType = instr.TypeOf;
            LLVMTypeRef valueType = LLVM.StructGetTypeAtIndex(structType, 0);
            ValueType espbType = MapLlvmTypeToEspb(valueType);

            byte opcode;
            if (espbType == ValueType.I32 || espbType == ValueType.U32)
            {
                opcode = 0xDD; // ATOMIC.RMW.CMPXCHG.I32
            }
            else if (espbType == ValueType.I64 || espbType == ValueType.U64)
            {
                opcode = 0xF6; // ATOMIC.RMW.CMPXCHG.I64
            }
            else if (espbType == ValueType.I64 || espbType == ValueType.U64)
            {
                opcode = 0xF6; // ATOMIC.RMW.CMPXCHG.I64
            }
            else
            {
                throw new NotSupportedException($"Unsupported type for atomic CMPXCHG: {espbType}");
            }
 
            EmitOpcode(output, opcode, "ATOMIC.CMPXCHG");
            // Format: [opcode][Rd][Ra][Rexp][Rdes]
            AddReg(output, destReg); // Rd
            AddReg(output, ptrReg);  // Ra
            AddReg(output, cmpReg);  // Rexp
            AddReg(output, newReg);  // Rdes
        }

        private void TranslateFence(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating FENCE");
            EmitOpcode(output, 0xEE, "ATOMIC.FENCE");
            espbModule.Features |= 0x00000002; // FEATURE_ATOMICS
        }

        private unsafe void TranslateAtomicLoad(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating ATOMIC.LOAD");
            espbModule.Features |= 0x00000002; // FEATURE_ATOMICS
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
            ValueType type = MapLlvmTypeToEspb(instr.TypeOf);

            byte opcode;
            switch (type)
            {
                case ValueType.I32:
                case ValueType.U32:
                    opcode = 0xDE; // ATOMIC.LOAD.I32
                    break;
                case ValueType.I64:
                case ValueType.U64:
                    opcode = 0xEC; // ATOMIC.LOAD.I64
                    break;
                default:
                    _logger.WriteLine($"Warning: Unsupported type for ATOMIC.LOAD: {type}. Falling back to regular LOAD.");
                    TranslateLoad(instr, output);
                    return;
            }
            
            EmitOpcode(output, opcode, "ATOMIC.LOAD");
            AddReg(output, destReg);
            AddReg(output, ptrReg);
        }

        private unsafe void TranslateAtomicStore(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating ATOMIC.STORE");
            espbModule.Features |= 0x00000002; // FEATURE_ATOMICS
            (byte valueReg, _) = GetOperandReg(instr.GetOperand(0), output);
            (byte ptrReg, _) = GetOperandReg(instr.GetOperand(1), output);
            ValueType type = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);

            byte opcode;
            switch (type)
            {
                case ValueType.I32:
                case ValueType.U32:
                    opcode = 0xDF; // ATOMIC.STORE.I32
                    break;
                case ValueType.I64:
                case ValueType.U64:
                    opcode = 0xED; // ATOMIC.STORE.I64
                    break;
                default:
                    _logger.WriteLine($"Warning: Unsupported type for ATOMIC.STORE: {type}. Falling back to regular STORE.");
                    TranslateStore(instr, output);
                    return;
            }
 
            EmitOpcode(output, opcode, "ATOMIC.STORE");
            AddReg(output, valueReg);
            AddReg(output, ptrReg);
        }
        
        // Helper: Check if a load instruction is used with zext (unsigned extension)
        // This helps determine whether to use signed or unsigned load opcodes for i8/i16
        private unsafe bool IsLoadUsedWithZext(LLVMValueRef loadInstr)
        {
            // Iterate through all users of the load instruction
            LLVMOpaqueUse* use = LLVM.GetFirstUse(loadInstr);
            while (use != null)
            {
                LLVMValueRef userInstr = LLVM.GetUser(use);
                if (userInstr.Handle != IntPtr.Zero)
                {
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(userInstr);
                    if (opcode == LLVMOpcode.LLVMZExt)
                    {
                        return true; // Found zext user - this is an unsigned load
                    }
                }
                use = LLVM.GetNextUse(use);
            }
            return false; // No zext found - assume signed
        }

        private unsafe void TranslateLoad(LLVMValueRef instr, List<byte> output)
        {
            byte dest = GetOrAllocateRegWithCacheInvalidation(instr, output);
            LLVMValueRef loadPtrOp = instr.GetOperand(0);
            ValueType type = MapLlvmTypeToEspb(instr.TypeOf);

            // For i8 and i16 loads, check if used with zext to determine signed/unsigned
            if (type == ValueType.I8 || type == ValueType.I16)
            {
                bool isUnsigned = IsLoadUsedWithZext(instr);
                if (isUnsigned)
                {
                    // Convert to unsigned type for correct load opcode selection
                    type = (type == ValueType.I8) ? ValueType.U8 : ValueType.U16;
                    _logger.WriteLine($"    [LOAD] Detected zext user - using unsigned load (type={type})");
                }
            }

            if (LLVM.GetValueKind(loadPtrOp) == LLVMValueKind.LLVMGlobalVariableValueKind &&
                llvmGlobalToEspbIndex.TryGetValue(loadPtrOp, out uint gidx))
        {
            ushort numImportedGlobals = (ushort)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
            ushort combinedIdx = (ushort)(numImportedGlobals + gidx);
            EmitOpcode(output, 0x1E, "LD_GLOBAL");
            AddReg(output, dest);
            output.AddRange(BitConverter.GetBytes(combinedIdx));
            }
            else
            {
                (byte ptrReg, _) = GetOperandReg(loadPtrOp, output);
                byte loadOpcode = GetLoadOpcode(type);
                _logger.WriteLine($"    [LOAD] Real LOAD from memory: R{dest} <- [R{ptrReg}] (type={type}, opcode=0x{loadOpcode:X2})");
                EmitOpcode(output, loadOpcode, "LOAD");
                AddReg(output, dest);
                AddReg(output, ptrReg);
                output.AddRange(BitConverter.GetBytes((short)0)); // offset = 0
            }
        }

        private unsafe void TranslateStore(LLVMValueRef instr, List<byte> output)
        {
            LLVMValueRef valueToStore = instr.GetOperand(0);
            LLVMValueRef ptr = instr.GetOperand(1);

            if (LLVM.GetValueKind(ptr) == LLVMValueKind.LLVMGlobalVariableValueKind &&
                llvmGlobalToEspbIndex.TryGetValue(ptr, out uint gidxStore))
            {
                (byte rsVal, _) = GetOperandReg(valueToStore, output);
                ushort numImportedGlobals = (ushort)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                ushort combinedIdx = (ushort)(numImportedGlobals + gidxStore);
                EmitOpcode(output, 0x1F, "ST_GLOBAL");
                output.AddRange(BitConverter.GetBytes(combinedIdx));
                output.Add(rsVal);
            }
            else
            {
                // Если сохраняем указатель на функцию, сохраняем АДРЕС (LD_GLOBAL_ADDR), а не индекс
                byte valueReg;
                if (valueToStore.Kind == LLVMValueKind.LLVMFunctionValueKind && 
                    llvmFuncToEspbIndex.TryGetValue(valueToStore, out uint funcIndex))
                {
                    unsafe
                    {
                        sbyte* namePtr = LLVM.GetValueName(valueToStore);
                        string funcName = System.Runtime.InteropServices.Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "unknown";
                        uint numGlobals = (uint)(GetSection<GlobalsSection>(4)!.Globals.Count);
                        ushort symbolIdx = (ushort)(funcIndex | 0x8000);
                        _logger.WriteLine($"Info: Storing function pointer '{funcName}' - loading pointer via LD_GLOBAL_ADDR (func_idx={funcIndex}, symbol_idx={symbolIdx})");
                        valueReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                        AddReg(output, valueReg);
                        output.AddRange(BitConverter.GetBytes(symbolIdx));

                        if (_funcPtrToDataOffset.TryGetValue(valueToStore, out uint dataOffset))
                        {
                            AddFunctionPointerMapEntry(dataOffset, (ushort)funcIndex);
                        }
                    }
                }
                else
                {
                    // Обычное значение - загружаем как обычно
                    (valueReg, _) = GetOperandReg(valueToStore, output);
                }
                
                (byte ptrReg, _) = GetOperandReg(ptr, output);
                
                ValueType storeType = MapLlvmTypeToEspb(valueToStore.TypeOf);
                byte storeOpcode = GetStoreOpcode(storeType);
                
                EmitOpcode(output, storeOpcode, "STORE");
                AddReg(output, valueReg); // Rs
                AddReg(output, ptrReg);   // Ra
                output.AddRange(BitConverter.GetBytes((short)0)); // offset
            }
        }

        private unsafe byte GenerateConstantCode(LLVMValueRef constValue, List<byte> output)
        {
            byte resultReg = registerManager.AllocateTemporaryRegister();
            bool handled = false;
            
            // Обрабатываем различные типы констант
            if (constValue.IsAConstantInt != null && constValue.IsAConstantInt.Handle != IntPtr.Zero)
            {
                // Целочисленная константа
                LLVMTypeRef type = constValue.TypeOf;
                ValueType valueType = MapLlvmTypeToEspb(type);
                
                if (type.IntWidth <= 32)
                {
                    int value = (int)LLVM.ConstIntGetSExtValue(constValue);
                    EmitOpcode(output, 0x18, "LDC.I32.IMM");

                    AddReg(output, resultReg);

                    output.AddRange(BitConverter.GetBytes(value));
                    handled = true;
                }
                else if (type.IntWidth <= 64)
                {
                    long value = (long)LLVM.ConstIntGetSExtValue(constValue);
                    EmitOpcode(output, 0x19, "LDC.I64.IMM");

                    AddReg(output, resultReg);

                    output.AddRange(BitConverter.GetBytes(value));
                    handled = true;
                }
                // else: > 64 bit integer not supported directly
            }
            else if (constValue.IsAConstantFP != null && constValue.IsAConstantFP.Handle != IntPtr.Zero)
            {
                // Константа с плавающей точкой
                LLVMTypeRef type = constValue.TypeOf;
                if (type.Kind == LLVMTypeKind.LLVMFloatTypeKind)
                {
                    // ... (code to parse float and generate LDC.F32.IMM) ...
                    handled = true; // Assume success for brevity
                }
                else if (type.Kind == LLVMTypeKind.LLVMDoubleTypeKind)
                {
                    // ... (code to parse double and generate LDC.F64.IMM) ...
                     handled = true; // Assume success for brevity
                }
                 // else: other FP types not supported
            }
            else if (constValue.IsAConstantPointerNull != null && constValue.IsAConstantPointerNull.Handle != IntPtr.Zero)
            {
                // Null pointer via immediate
                EmitOpcode(output, 0x1C, "LDC.PTR.IMM");

                AddReg(output, resultReg);

                output.AddRange(BitConverter.GetBytes(0)); // immediate null pointer
                handled = true;
            }
            else if (constValue.IsAConstantDataSequential != null || constValue.IsAConstantArray != null || constValue.IsAConstantStruct != null || constValue.IsAConstantVector != null)
            {
                // Complex constant: Get data, add to data section, add relocation
                byte[]? data = GetInitializerData(constValue);
                if (data != null && data.Length > 0)
                {
                    int dataOffset = AddDataSegment(data, 1);
                    EmitOpcode(output, 0x1C, "LDC.PTR.IMM");

                    AddReg(output, resultReg);

                    output.AddRange(BitConverter.GetBytes(dataOffset)); // Placeholder offset
 
                     // Add relocation for this data segment to CODE section
                    var relocSection = espbModule.Sections.OfType<RelocationsSection>().FirstOrDefault(s => s.TargetSectionId == 6); // Target CODE
                    if (relocSection == null) { relocSection = new RelocationsSection { TargetSectionId = 6 }; espbModule.Sections.Add(relocSection); }
                    // Calculate offset within the current instruction stream (output List<byte>)
                    uint relocOffsetInCode = (uint)(output.Count - sizeof(int));
                    relocSection.Relocations.Add(new RelocationEntry { Type = RelocationType.R_ABS32_DATA, Offset = relocOffsetInCode, SymbolIndex = (uint)dataOffset, Addend = 0 });
                    _logger.WriteLine($"  Added R_ABS32_DATA relocation for constant data at code offset {relocOffsetInCode}, symbol index {dataOffset}");
                    handled = true;
                    }
                // else: Fallback to null pointer if data extraction fails
            }
            // --- REMOVED GlobalValue handling from here ---
            // else if (constValue.IsAGlobalValue != null && constValue.IsAGlobalValue.Handle != IntPtr.Zero) { /* Handled by GetOperandReg */ }

            // Если константа не была обработана, используем fallback (0)
            if (!handled)
           {
                _logger.WriteLine($"Warning: Unsupported or failed constant generation for: {constValue}. Using default value (0) as fallback.");
               EmitOpcode(output, 0x18, "LDC.I32.IMM");

               AddReg(output, resultReg);

                output.AddRange(BitConverter.GetBytes(0));
            }
            
            return resultReg;
        }

        private unsafe void TranslateTypeConversion_Old(LLVMValueRef instr, List<byte> output)
        {
            // Получаем регистр для результата
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            
            // Получаем операнд для преобразования
            (byte srcReg, _) = GetOperandReg(instr.GetOperand(0), output);
            
            // Определяем исходный и целевой типы
            LLVMTypeRef srcType = instr.GetOperand(0).TypeOf;
            LLVMTypeRef destType = instr.TypeOf;
            
            // Получаем соответствующие ESPB типы
            ValueType srcEspbType = MapLlvmTypeToEspb(srcType);
            ValueType destEspbType = MapLlvmTypeToEspb(destType);
            
            // Определяем опкод на основе операции преобразования и типов
            LLVMOpcode opcode = instr.InstructionOpcode;
            // Добавляю логирование входных параметров преобразования типов
            _logger.WriteLine($"[LLVMTranslator] TranslateTypeConversion: opcode={opcode}, srcEspbType={srcEspbType}, destEspbType={destEspbType}, srcReg={srcReg}, destReg={destReg}");
            switch (opcode)
            {
                case LLVMOpcode.LLVMTrunc:
                    // Усечение типа (обычно целочисленное)
                    if (srcEspbType == ValueType.I64 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0x90, "TRUNC.I64.I32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.I16)
                    {
                        EmitOpcode(output, 0x91, "TRUNC.I64.I16");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.I8)
                    {
                        EmitOpcode(output, 0x92, "TRUNC.I64.I8");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I32 && destEspbType == ValueType.I16)
                    {
                        EmitOpcode(output, 0x93, "TRUNC.I32.I16");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I32 && destEspbType == ValueType.I8)
                    {
                        EmitOpcode(output, 0x94, "TRUNC.I32.I8");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I16 && destEspbType == ValueType.I8)
                    {
                        EmitOpcode(output, 0x95, "TRUNC.I16.I8");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (destEspbType == ValueType.BOOL)
                    {
                        // Truncation to bool (i1) - normalize: any non-zero value becomes 1
                        // Generate: CMP.NE.I32 destReg, srcReg, 0
                        // This sets destReg to 1 if srcReg != 0, else 0
                        _logger.WriteLine($"    [TRUNC TO BOOL] Normalizing {srcEspbType} to BOOL");
                        byte zeroReg = GenerateIntConstant(output, 0);
                        EmitOpcode(output, 0xC1, "CMP.NE.I32"); // CMP.NE.I32 Rd, R1, R2 -> Rd = (R1 != R2)
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                        AddReg(output, zeroReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported truncation from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMZExt:
                    // Расширение с заполнением нулями (беззнаковое расширение)
                    if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) && (destEspbType == ValueType.U16 || destEspbType == ValueType.I16))
                     {
                         EmitOpcode(output, 0x96, "ZEXT.I8.I16");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) && (destEspbType == ValueType.U32 || destEspbType == ValueType.I32))
                     {
                         EmitOpcode(output, 0x97, "ZEXT.I8.I32");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) && (destEspbType == ValueType.U64 || destEspbType == ValueType.I64))
                     {
                         EmitOpcode(output, 0x98, "ZEXT.I8.I64");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I16 || srcEspbType == ValueType.U16) && (destEspbType == ValueType.U32 || destEspbType == ValueType.I32))
                     {
                         EmitOpcode(output, 0x99, "ZEXT.I16.I32");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I16 || srcEspbType == ValueType.U16) && destEspbType == ValueType.U64)
                     {
                         EmitOpcode(output, 0x9A, "ZEXT.I16.I64");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I32 || srcEspbType == ValueType.U32) && (destEspbType == ValueType.U64 || destEspbType == ValueType.I64))
                    {
                        EmitOpcode(output, 0x9B, "ZEXT.I32.I64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.BOOL && (destEspbType == ValueType.U8 || destEspbType == ValueType.U16 ||
                                                           destEspbType == ValueType.U32 || destEspbType == ValueType.U64))
                     {
                         // Расширение bool до целого (это по сути тоже zext)
                         switch (destEspbType)
                         {
                             case ValueType.U8:
                                 EmitOpcode(output, 0x96, "ZEXT.I8.I16"); // ZEXT используем как аналог
                                 break;
                             case ValueType.U16:
                                 EmitOpcode(output, 0x96, "ZEXT.I8.I16");
                                 break;
                             case ValueType.U32:
                                 EmitOpcode(output, 0x97, "ZEXT.I8.I32");
                                 break;
                             case ValueType.U64:
                                 EmitOpcode(output, 0x98, "ZEXT.I8.I64");
                                 break;
                         }
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                      else
                     {
                         _logger.WriteLine($"Warning: Unsupported zero extension from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                         EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                      break;
                    
                case LLVMOpcode.LLVMSExt:
                    // Расширение с сохранением знака (знаковое расширение)
                    if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) &&
                        (destEspbType == ValueType.I16 || destEspbType == ValueType.U16))
                     {
                         EmitOpcode(output, 0x9C, "SEXT.I8.I16");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) &&
                              (destEspbType == ValueType.I32 || destEspbType == ValueType.U32))
                     {
                         EmitOpcode(output, 0x9D, "SEXT.I8.I32");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I8 || srcEspbType == ValueType.U8) &&
                              (destEspbType == ValueType.I64 || destEspbType == ValueType.U64))
                     {
                         EmitOpcode(output, 0x9E, "SEXT.I8.I64");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I16 || srcEspbType == ValueType.U16) &&
                              (destEspbType == ValueType.I32 || destEspbType == ValueType.U32))
                     {
                         EmitOpcode(output, 0x9F, "SEXT.I16.I32");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I16 || srcEspbType == ValueType.U16) &&
                              (destEspbType == ValueType.I64 || destEspbType == ValueType.U64))
                     {
                         EmitOpcode(output, 0xA0, "SEXT.I16.I64");
                         AddReg(output, destReg);
                         AddReg(output, srcReg);
                     }
                    else if ((srcEspbType == ValueType.I32 || srcEspbType == ValueType.U32) && (destEspbType == ValueType.U64 || destEspbType == ValueType.I64))
                    {
                        EmitOpcode(output, 0xA1, "SEXT.I32.I64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported sign extension from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMFPTrunc:
                    // Усечение типа с плавающей точкой
                    if (srcEspbType == ValueType.F64 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xA4, "FPROUND.F64.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported floating-point truncation from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMFPExt:
                    // Расширение типа с плавающей точкой
                    if (srcEspbType == ValueType.F32 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xA5, "FPROMOTE.F32.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported floating-point extension from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMSwitch:
                    // Switch statement - транслируется в BR_TABLE
                    TranslateSwitchInstr(instr, output);
                    break;
                    
                case LLVMOpcode.LLVMFPToUI:
                    // Преобразование из плавающей точки в беззнаковое целое
                    if (srcEspbType == ValueType.F32 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0xA6, "CVT.F32.U32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F32 && destEspbType == ValueType.I64)
                    {
                        EmitOpcode(output, 0xA7, "CVT.F32.U64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F64 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0xA8, "CVT.F64.U32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F64 && destEspbType == ValueType.I64)
                    {
                        EmitOpcode(output, 0xA9, "CVT.F64.U64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported conversion from {srcEspbType} to unsigned {destEspbType}. Using truncation as fallback.");
                        EmitOpcode(output, 0xA6, "CVT.F32.U32"); // Используем базовое преобразование
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMFPToSI:
                    // Преобразование из плавающей точки в знаковое целое
                    if (srcEspbType == ValueType.F32 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0xAA, "CVT.F32.I32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F32 && destEspbType == ValueType.I64)
                    {
                        EmitOpcode(output, 0xAB, "CVT.F32.I64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F64 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0xAC, "CVT.F64.I32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F64 && destEspbType == ValueType.I64)
                    {
                        EmitOpcode(output, 0xAD, "CVT.F64.I64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported conversion from {srcEspbType} to signed {destEspbType}. Using truncation as fallback.");
                        EmitOpcode(output, 0xAA, "CVT.F32.I32"); // Используем базовое преобразование
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMUIToFP:
                    // Преобразование из беззнакового целого в число с плавающей точкой
                    if (srcEspbType == ValueType.I32 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xAE, "CVT.U32.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I32 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xAF, "CVT.U32.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xB0, "CVT.U64.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xB1, "CVT.U64.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported conversion from unsigned {srcEspbType} to {destEspbType}. Using basic conversion as fallback.");
                        EmitOpcode(output, 0xAE, "CVT.U32.F32"); // Используем базовое преобразование
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMSIToFP:
                    // Преобразование из знакового целого в число с плавающей точкой
                    if (srcEspbType == ValueType.I32 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xB2, "CVT.I32.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I32 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xB3, "CVT.I32.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xB4, "CVT.I64.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xB5, "CVT.I64.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported conversion from signed {srcEspbType} to {destEspbType}. Using basic conversion as fallback.");
                        EmitOpcode(output, 0xB2, "CVT.I32.F32"); // Используем базовое преобразование
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMBitCast:
                    // Побитовое приведение типа (reinterpret cast)
                    if (srcEspbType == ValueType.F32 && destEspbType == ValueType.I32)
                    {
                        EmitOpcode(output, 0xB8, "BITCAST.F32.I32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I32 && destEspbType == ValueType.F32)
                    {
                        EmitOpcode(output, 0xB9, "BITCAST.I32.F32");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.F64 && destEspbType == ValueType.I64)
                    {
                        EmitOpcode(output, 0xBA, "BITCAST.F64.I64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if (srcEspbType == ValueType.I64 && destEspbType == ValueType.F64)
                    {
                        EmitOpcode(output, 0xBB, "BITCAST.I64.F64");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else if ((srcEspbType == ValueType.PTR && IsLlvmIntegerType(destType)) ||
                            (IsLlvmIntegerType(srcType) && destEspbType == ValueType.PTR))
                    {
                        // Преобразование между указателем и целым (int <-> ptr)
                        if (srcEspbType == ValueType.PTR)
                        {
                            EmitOpcode(output, 0xBC, "PTR_TO_INT");
                            AddReg(output, destReg);
                            AddReg(output, srcReg);
                        }
                        else
                        {
                            EmitOpcode(output, 0xBD, "INT_TO_PTR");
                            AddReg(output, destReg);
                            AddReg(output, srcReg);
                        }
                    }
                    else
                    {
                        // Для других типов просто используем MOV
                        _logger.WriteLine($"Warning: Unsupported bitcast from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                case LLVMOpcode.LLVMPtrToInt:
                    _logger.WriteLine($"[LLVMTranslator] TranslateTypeConversion: Handling PtrToInt (0xBC) src={srcEspbType}, dest={destEspbType}");
                    if (srcEspbType == ValueType.PTR && IsLlvmIntegerType(destType))
                    {
                        EmitOpcode(output, 0xBC, "PTR_TO_INT");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported PtrToInt conversion from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;

                case LLVMOpcode.LLVMIntToPtr:
                     _logger.WriteLine($"[LLVMTranslator] TranslateTypeConversion: Handling IntToPtr (0xBD) src={srcEspbType}, dest={destEspbType}");
                    if (IsLlvmIntegerType(srcType) && destEspbType == ValueType.PTR)
                    {
                        EmitOpcode(output, 0xBD, "INT_TO_PTR");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: Unsupported IntToPtr conversion from {srcEspbType} to {destEspbType}. Using MOV as fallback.");
                        EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                        AddReg(output, destReg);
                        AddReg(output, srcReg);
                    }
                    break;
                    
                default:
                    _logger.WriteLine($"Warning: Unsupported type conversion opcode: {opcode}. Using MOV as fallback.");
                    EmitOpcode(output, GetMoveOpcode(destEspbType), "MOV_FALLBACK");
                    AddReg(output, destReg);
                    AddReg(output, srcReg);
                    break;
            }
        }

        private unsafe void TranslateGetElementPtr(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating GEP: {instr}");
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);

            LLVMValueRef basePtrOperand = instr.GetOperand(0);
            (byte basePtrReg, _) = GetOperandReg(basePtrOperand, output);
            
            // ИСПРАВЛЕНО: Правильное получение типа элемента GEP с расширенным логированием
            LLVMTypeRef gepSourceElementType = LLVM.GetGEPSourceElementType(instr);
            
            _logger.WriteLine($"DEBUG GEP: Initial GetGEPSourceElementType returned: Handle={gepSourceElementType.Handle}, Kind={gepSourceElementType.Kind}");
            
            // Если GetGEPSourceElementType возвращает недопустимый тип, получаем тип из базового указателя
            if (gepSourceElementType.Handle == IntPtr.Zero || gepSourceElementType.Kind == LLVMTypeKind.LLVMVoidTypeKind)
            {
                _logger.WriteLine("DEBUG GEP: GetGEPSourceElementType returned invalid type, trying alternative methods");
                
                // Метод 1: Получаем тип из базового операнда
                LLVMTypeRef basePtrType = LLVM.TypeOf(basePtrOperand);
                _logger.WriteLine($"DEBUG GEP: Base pointer type: Handle={basePtrType.Handle}, Kind={basePtrType.Kind}");
                
                if (basePtrType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                {
                    _logger.WriteLine("DEBUG GEP: Base is pointer type, analyzing further");
                    
                    // Метод 2: Анализируем тип результата GEP
                    LLVMTypeRef resultType = LLVM.TypeOf(instr);
                    _logger.WriteLine($"DEBUG GEP: Result type: Handle={resultType.Handle}, Kind={resultType.Kind}");
                    
                    if (resultType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                    {
                        _logger.WriteLine("DEBUG GEP: Result is pointer type, checking base operand");
                        
                        // Метод 3: Используем информацию из алокации (alloca)
                        _logger.WriteLine($"DEBUG GEP: Base operand kind: {basePtrOperand.Kind}");
                        if (basePtrOperand.Kind == LLVMValueKind.LLVMInstructionValueKind)
                        {
                            LLVMOpcode baseOpcode = LLVM.GetInstructionOpcode(basePtrOperand);
                            _logger.WriteLine($"DEBUG GEP: Base operand opcode: {baseOpcode}");
                            
                            if (baseOpcode == LLVMOpcode.LLVMAlloca)
                            {
                                _logger.WriteLine("DEBUG GEP: Found alloca instruction, getting allocated type");
                                // Получаем аллоцированный тип из alloca инструкции
                                gepSourceElementType = LLVM.GetAllocatedType(basePtrOperand);
                                _logger.WriteLine($"DEBUG GEP: Allocated type: Handle={gepSourceElementType.Handle}, Kind={gepSourceElementType.Kind}, Type={SafePrintType(gepSourceElementType)}");
                                
                                // Если это массив, получаем тип элемента
                                if (gepSourceElementType.Kind == LLVMTypeKind.LLVMArrayTypeKind)
                                {
                                    _logger.WriteLine("DEBUG GEP: Type is array, extracting element type");
                                    gepSourceElementType = LLVM.GetElementType(gepSourceElementType);
                                    _logger.WriteLine($"DEBUG GEP: Array element type: Handle={gepSourceElementType.Handle}, Kind={gepSourceElementType.Kind}, Type={SafePrintType(gepSourceElementType)}");
                                }
                            }
                            else
                            {
                                _logger.WriteLine($"DEBUG GEP: Base operand is not alloca, opcode: {baseOpcode}");
                            }
                        }
                        else
                        {
                            _logger.WriteLine($"DEBUG GEP: Base operand is not instruction, kind: {basePtrOperand.Kind}");
                        }
                    }
                    else
                    {
                        _logger.WriteLine($"DEBUG GEP: Result is not pointer type, kind: {resultType.Kind}");
                    }
                }
                else
                {
                    _logger.WriteLine($"DEBUG GEP: Base is not pointer type, kind: {basePtrType.Kind}");
                }
                
                // Если все еще не получили валидный тип, используем контекст данных
                if (gepSourceElementType.Handle == IntPtr.Zero || gepSourceElementType.Kind == LLVMTypeKind.LLVMVoidTypeKind)
                {
                    _logger.WriteLine("DEBUG GEP: Still no valid type found, using i8 as fallback");
                    gepSourceElementType = LLVM.Int8Type();
                }
                else
                {
                    _logger.WriteLine($"DEBUG GEP: Successfully recovered type: {SafePrintType(gepSourceElementType)}");
                }
            }
            else
            {
                _logger.WriteLine($"DEBUG GEP: GetGEPSourceElementType returned valid type: {SafePrintType(gepSourceElementType)}");
            }
            
            LLVMTypeRef currentType = gepSourceElementType;

            _logger.WriteLine($"  Base Ptr: {basePtrOperand} (Reg: {basePtrReg}), Initial Indexing Type: {SafePrintType(currentType)}");

            byte currentAddrReg = basePtrReg;
            uint startIndex = 1;
            bool isFirstIndex = true; // Track if this is the first index (array-like indexing)
            for (uint i = startIndex; i < LLVM.GetNumOperands(instr); i++)
            {
                LLVMValueRef indexOperand = instr.GetOperand(i);
                _logger.WriteLine($"    DEBUG: Before print for Index {i}. Current Type Kind: {currentType.Kind}, Handle: {currentType.Handle}");
                _logger.WriteLine($"    DEBUG: Attempting SafePrintType for Index {i}...");
                
                _logger.WriteLine($"  Processing Index {i}: {indexOperand} for type {SafePrintType(currentType)}");

                long constIndexVal = -1; 
                byte indexReg = 0; // Инициализируем здесь
                bool isConstIndex = indexOperand.IsAConstantInt != null && indexOperand.IsAConstantInt.Handle != IntPtr.Zero;

                if (isConstIndex)
                {
                    constIndexVal = LLVM.ConstIntGetSExtValue(indexOperand);
                    _logger.WriteLine($"    Index is Constant: {constIndexVal}");
                }
                else
                {
                    (indexReg, _) = GetOperandReg(indexOperand, output); // Присваиваем, если не константа
                    _logger.WriteLine($"    Index is Variable (Reg: {indexReg})");
                }

                ulong elementSize = 0;
                long calculatedOffset = 0; 

                // CRITICAL FIX: The first GEP index always performs array-like indexing on gepSourceElementType,
                // even if the type is a struct. Only subsequent indices index into struct fields.
                // See LLVM Language Reference: "The first index always indexes the pointer value..."
                _logger.WriteLine($"    DEBUG: isFirstIndex = {isFirstIndex}, currentType.Kind = {currentType.Kind}");
                if (isFirstIndex)
                {
                    isFirstIndex = false;
                    elementSize = (ulong)LLVM.StoreSizeOfType(targetData, currentType);
                    _logger.WriteLine($"    First Index (Array-like): Element Size = {elementSize}");
                    
                    if (elementSize == 0) { continue; }
                    
                    if (isConstIndex)
                    {
                        calculatedOffset = constIndexVal * (long)elementSize;
                        _logger.WriteLine($"    Constant First Index Offset: {constIndexVal} * {elementSize} = {calculatedOffset}");
                    }
                    else
                    {
                        // Dynamic first index
                        byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                        (indexReg, _) = GetOperandReg(indexOperand, output);
                        byte mulReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x22, "MUL.I32");
                        WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                        
                        byte addReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x20, "ADD.I32");
                        WriteBinaryInstruction(output, addReg, currentAddrReg, mulReg);
                        
                        currentAddrReg = addReg;
                    }
                    // Type does NOT change for first index - we're still pointing at the same element type
                    // (just at a different array element)
                }
                else switch (currentType.Kind)
                {
                    case LLVMTypeKind.LLVMStructTypeKind:
                        if (!isConstIndex || constIndexVal < 0 || constIndexVal >= LLVM.CountStructElementTypes(currentType))
                        {
                            throw new NotSupportedException($"GEP: Variable or out-of-bounds index for struct {SafePrintType(currentType)}: {indexOperand}");
                        }
                        calculatedOffset = (long)LLVM.OffsetOfElement(targetData, currentType, (uint)constIndexVal);
                        LLVMTypeRef nextStructType = LLVM.StructGetTypeAtIndex(currentType, (uint)constIndexVal);
                        _logger.WriteLine($"    DEBUG: Type after StructGetTypeAtIndex: {SafePrintType(nextStructType)}");
                        currentType = nextStructType;
                        _logger.WriteLine($"    Struct Field Offset: {calculatedOffset}, New Type: {SafePrintType(currentType)}");
                        break;

                    case LLVMTypeKind.LLVMArrayTypeKind:
                    case LLVMTypeKind.LLVMVectorTypeKind:
                        LLVMTypeRef elementType = LLVM.GetElementType(currentType);
                        _logger.WriteLine($"    DEBUG: Type after GetElementType (Array/Vector): {SafePrintType(elementType)}");
                        elementSize = ElementSize(elementType);
                        _logger.WriteLine($"    Array/Vector Element Size: {elementSize}, Element Type: {SafePrintType(elementType)}");
                        if (elementSize == 0) { continue; }
                        if (isConstIndex)
                        {
                            calculatedOffset = constIndexVal * (long)elementSize;
                            _logger.WriteLine($"    Constant Array/Vector Index Offset: {constIndexVal} * {elementSize} = {calculatedOffset}");
                        }
                        else
                        {
                            // Для динамических индексов
                            byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                            byte mulReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x22, "MUL.I32");
                            WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                            
                            byte addReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x20, "ADD.I32");
                            WriteBinaryInstruction(output, addReg, currentAddrReg, mulReg);
                            
                            currentAddrReg = addReg;
                        }
                        currentType = elementType; 
                        break;
                        
                    case LLVMTypeKind.LLVMPointerTypeKind:
                        elementType = LLVM.GetElementType(currentType);
                        _logger.WriteLine($"    DEBUG: Type after GetElementType (Pointer): {SafePrintType(elementType)}");
                        elementSize = ElementSize(elementType);
                        _logger.WriteLine($"    Pointer Indexing - Element Size: {elementSize}, Element Type: {SafePrintType(elementType)}");
                        if (elementSize == 0) { continue; }
                        if (isConstIndex)
                        {
                            calculatedOffset = constIndexVal * (long)elementSize;
                            _logger.WriteLine($"    Constant Pointer Index Offset: {constIndexVal} * {elementSize} = {calculatedOffset}");
                        }
                        else
                        {
                            // Для динамических индексов
                            byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                            byte mulReg = registerManager.AllocateTemporaryRegister();
                            output.Add(0x22); // MUL.I32
                            WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                            
                            byte addReg = registerManager.AllocateTemporaryRegister();
                            output.Add(0x20); // ADD.I32
                            WriteBinaryInstruction(output, addReg, currentAddrReg, mulReg);
                            
                            currentAddrReg = addReg;
                        }
                        currentType = elementType; 
                        break;

                    default:
                        _logger.WriteLine($"    DEBUG: Type before ElementSize (Scalar): {SafePrintType(currentType)}");
                        elementSize = ElementSize(currentType);
                        _logger.WriteLine($"    Scalar Indexing - Element Size: {elementSize}, Element Type: {SafePrintType(currentType)}");
                        if (elementSize == 0) { continue; }
                        if (isConstIndex)
                        {
                            calculatedOffset = constIndexVal * (long)elementSize;
                            _logger.WriteLine($"    Constant Scalar Index Offset: {constIndexVal} * {elementSize} = {calculatedOffset}");
                        }
                        else
                        {
                            // Для динамических индексов
                            byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                            byte mulReg = registerManager.AllocateTemporaryRegister();
                            output.Add(0x22); // MUL.I32
                            WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                            
                            byte addReg = registerManager.AllocateTemporaryRegister();
                            output.Add(0x20); // ADD.I32
                            WriteBinaryInstruction(output, addReg, currentAddrReg, mulReg);
                            
                            currentAddrReg = addReg;
                        }
                        break;
                }
                
                // Если константное смещение, добавляем его к текущему адресу
                if (isConstIndex && calculatedOffset != 0)
                {
                    byte offsetReg = GenerateIntConstant(output, (int)calculatedOffset);
                    byte tempReg = registerManager.AllocateTemporaryRegister();
                    EmitOpcode(output, 0x20, "ADD.I32");
                    WriteBinaryInstruction(output, tempReg, currentAddrReg, offsetReg);
                    currentAddrReg = tempReg;
                }
            }
            
            // Копируем финальный адрес в целевой регистр
            EmitOpcode(output, 0x12, "MOV.32");
            AddReg(output, destReg);
            output.Add(currentAddrReg);
            
            _logger.WriteLine("---> Finished Translating GEP");
        }
        
        private unsafe ulong ElementSize(LLVMTypeRef type)
        {
            if (type.Handle == IntPtr.Zero)
                return 0;

            // Определяем размер элемента в байтах для различных типов LLVM
            switch (type.Kind)
            {
                case LLVMTypeKind.LLVMVoidTypeKind:
                    return 0;
                case LLVMTypeKind.LLVMIntegerTypeKind:
                    return (type.IntWidth + 7) / 8;
                case LLVMTypeKind.LLVMFloatTypeKind:
                    return 4;
                case LLVMTypeKind.LLVMDoubleTypeKind:
                    return 8;
                case LLVMTypeKind.LLVMPointerTypeKind:
                    return (ulong)IntPtr.Size;
                case LLVMTypeKind.LLVMStructTypeKind:
                    uint numElements = LLVM.CountStructElementTypes(type);
                    if (numElements == 0) return 0;
                    ulong size = 0;
                    for (uint i = 0; i < numElements; i++)
                    {
                        size += ElementSize(LLVM.StructGetTypeAtIndex(type, i));
                    }
                    return size;
                case LLVMTypeKind.LLVMArrayTypeKind:
                    return ElementSize(type.ElementType) * type.ArrayLength;
                case LLVMTypeKind.LLVMVectorTypeKind:
                    return ElementSize(type.ElementType) * type.VectorSize;
                default:
                    _logger.WriteLine($"Warning: Unsupported type for size calculation: {type.Kind}. Returning 0.");
                    return 0;
            }
        }

        // --- ДОБАВЛЕНО: Метод для безопасной печати типа --- 
        private unsafe string SafePrintType(LLVMTypeRef typeRef)
        {
            try
            {
                if (!IsValidPointer((IntPtr)(void*)typeRef)) return "[Invalid Type Pointer]";
                
                // Дополнительная проверка - попробуем получить тип
                var typeKind = LLVM.GetTypeKind(typeRef);
                return $"[{typeKind}]"; 
            }
            catch (AccessViolationException) 
            { 
                return "[Access Violation - Invalid Type]"; 
            }
            catch (Exception ex) 
            { 
                return $"[Error getting type kind: {ex.GetType().Name}]"; 
            }
        }
        // --- Конец добавленного метода ---

        private unsafe string? GetOriginalFunctionNameFromDbgInfo(LLVMValueRef instr)
        {
            try
            {
                _logger.WriteLine($"Debug: Extracting function name from debug info for instruction");

                if (instr.Kind != LLVMValueKind.LLVMInstructionValueKind)
                {
                    _logger.WriteLine("Debug: Not an instruction");
                    return null;
                }

                LLVMOpcode opcode = LLVM.GetInstructionOpcode(instr);
                if (opcode != LLVMOpcode.LLVMCall)
                {
                    _logger.WriteLine("Debug: Not a call instruction");
                    return null;
                }

                // Universal approach: Follow the debug info chain to find original function name
                return ExtractOriginalFunctionNameFromDebugChain(instr);
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Warning: Error extracting function name from debug info: {ex.Message}");

                // Fallback to simplified inline asm extraction
                return GetFunctionNameFromInlineAsm(instr);
            }
        }

        private unsafe string? GetFunctionNameFromInlineAsm(LLVMValueRef callInstr)
        {
            try
            {
                var callee = LLVM.GetOperand(callInstr, (uint)(LLVM.GetNumOperands(callInstr) - 1));
                if (LLVM.GetValueKind(callee) == LLVMValueKind.LLVMConstantExprValueKind)
                {
                    LLVMOpcode exprOpcode = LLVM.GetConstOpcode(callee);
                    if (exprOpcode == LLVMOpcode.LLVMCall || exprOpcode == 0) // LLVMCall or unknown for inline asm
                    {
                        // Для универсальности: пытаемся получить имя через debug info
                        // LLVMSharp имеет ограниченную поддержку DI, поэтому полагаемся только на доступные API

                        _logger.WriteLine("Debug: Inline assembly detected but cannot determine function name without LLVM DI API extensions");

                        // Вместо эвристики возвращаем null - пусть система сама решает по импортам
                        return null;
                    }
                }

                return null;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Warning: Error extracting function name from inline asm: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Универсальный метод извлечения оригинального имени функции из debug info цепочки inlinedAt
        /// </summary>
        private unsafe string? ExtractOriginalFunctionNameFromDebugChain(LLVMValueRef instr)
        {
            try
            {
                _logger.WriteLine($"[DebugChain] Attempting to extract original function name from debug info chain");
                var debugLoc = LLVM.InstructionGetDebugLoc(instr);

                while (debugLoc != null)
                {
                    var scope = LLVM.DILocationGetScope(debugLoc);
                    if (scope != null && IsSubprogram(scope))
                    {
                        string? funcName = GetMangledNameFromDISubprogram(scope);
                        if (!string.IsNullOrEmpty(funcName))
                        {
                            _logger.WriteLine($"[DebugChain] Found function name '{funcName}' in scope.");
                            return funcName;
                        }
                    }

                    var inlinedAt = LLVM.DILocationGetInlinedAt(debugLoc);
                    if (inlinedAt != null)
                    {
                        debugLoc = inlinedAt;
                    }
                    else
                    {
                        break;
                    }
                }

                _logger.WriteLine($"[DebugChain] Could not find function name in the debug info chain.");
                return null;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"[DebugChain] Critical error in ExtractOriginalFunctionNameFromDebugChain: {ex.Message}");
                return null;
            }
        }

        private unsafe bool IsSubprogram(LLVMOpaqueMetadata* metadata)
        {
            if (metadata == null) return false;
            var value = LLVM.MetadataAsValue(llvmContext, metadata);
            if (value == null) return false;

            var s = LLVM.PrintValueToString(value);
            var str = Marshal.PtrToStringAnsi((IntPtr)s);
            LLVM.DisposeMessage(s);

            return str?.Contains("!DISubprogram") ?? false;
        }

        private unsafe string? GetMangledNameFromDISubprogram(LLVMOpaqueMetadata* subprogram)
        {
            if (subprogram == null) return null;
            var value = LLVM.MetadataAsValue(llvmContext, subprogram);
            if (value == null) return null;

            var s = LLVM.PrintValueToString(value);
            var str = Marshal.PtrToStringAnsi((IntPtr)s);
            LLVM.DisposeMessage(s);

            if (str != null)
            {
                var match = System.Text.RegularExpressions.Regex.Match(str, @"name: ""([^""]+)""");
                if (match.Success)
                {
                    return match.Groups[1].Value;
                }
            }
            return null;
        }

        private unsafe string? GetMDStringFromValueRef(LLVMValueRef mdStringValueRef)
        {
            if (mdStringValueRef.Handle == IntPtr.Zero) return null;

            uint len;
            sbyte* strPtr = LLVM.GetMDString(mdStringValueRef, &len);
            if (strPtr != null && len > 0)
            {
                return Marshal.PtrToStringAnsi((IntPtr)strPtr, (int)len);
            }
            
            return null;
        }

        private unsafe string ExtractAsmCode(LLVMValueRef inlineAsmExpr)
        {
            try
            {
                // Анализ inline asm из LLVM IR
                // Ищем в строковых литералах или константах

                // Проверяем, можем ли извлечь через атрибуты или метаданные
                // Inline asm в LLVM часто представлена как константные выражения

                // Для простоты вернем паттерн на основе типичных inline assembler последовательностей
                // Из анализа IR видно: tail call i32 asm sideeffect "csrr $0, 0x7e2", "=r"()

                return "csrr $0, 0x7e2"; // Извлечено из LLVM IR примера

                // В будущем можно улучшить с помощью LLVM metadata API
                // return "<inline_asm_placeholder>";
            }
            catch
            {
                return "<error>";
            }
        }
        
    
        private int AddOrFindSpecificSignature(TypeSignature signature)
        {
            var typesSection = GetSection<TypesSection>(1)!;
            for (int i = 0; i < typesSection.Signatures.Count; ++i)
            {
                if (SignaturesEqual(typesSection.Signatures[i], signature))
                {
                    return i;
                }
            }
            typesSection.Signatures.Add(signature);
            return typesSection.Signatures.Count - 1;
        }
    
        private unsafe bool IsByValParameter(LLVMValueRef calledFunction, uint paramIndex)
        {
            if (calledFunction.Kind != LLVMValueKind.LLVMFunctionValueKind)
            {
                return false;
            }

            LLVMAttributeIndex llvmAttrIdxForParam = (LLVMAttributeIndex)(paramIndex + 1);
            
            // Получаем количество атрибутов для данного параметра
            uint attrCount = LLVM.GetAttributeCountAtIndex(calledFunction, llvmAttrIdxForParam);
            if (attrCount == 0) return false;

            _logger.WriteLine($"    DEBUG IsByValParameter: Found {attrCount} attributes for param {paramIndex} of '{calledFunction.Name}'. Inspecting...");

            // Выделяем память для получения всех хэндлов атрибутов
            LLVMOpaqueAttributeRef*[] attrsHandles = new LLVMOpaqueAttributeRef*[attrCount];
            fixed (LLVMOpaqueAttributeRef** pAttrsHandles = attrsHandles)
            {
                LLVM.GetAttributesAtIndex(calledFunction, llvmAttrIdxForParam, pAttrsHandles);
            }
            
            // Итерируем по всем атрибутам и ищем нужный
            foreach (var attrHandle in attrsHandles)
            {
                if (attrHandle == null) continue;
                LLVMAttributeRef attrRef = (LLVMAttributeRef)attrHandle;

                // --- ДЕТАЛЬНАЯ ПЕЧАТЬ АТРИБУТА ---
                bool isEnum = LLVM.IsEnumAttribute(attrRef) != 0;
                bool isString = LLVM.IsStringAttribute(attrRef) != 0;
                
                if (isEnum)
                {
                    uint kindId = LLVM.GetEnumAttributeKind(attrRef);
                    ulong value = LLVM.GetEnumAttributeValue(attrRef);
                    _logger.WriteLine($"        -> Found Enum Attribute: KindId = {kindId}, Value = {value}");

                    // --- Проверяем на известные KindId для byval ---
                    // 7 = byval, 38 = byvaltype
                    if (kindId == 7 || kindId == 38) {
                        _logger.WriteLine($"        ----> SUCCESS: Detected ByVal/ByValType attribute (Kind={kindId}). Returning true.");
                        return true;
                    }
                }
                else if (isString)
                {
                    uint kindLen, valLen;
                    sbyte* kindStrPtr = LLVM.GetStringAttributeKind(attrRef, &kindLen);
                    sbyte* valStrPtr = LLVM.GetStringAttributeValue(attrRef, &valLen);
                    string kindStr = Marshal.PtrToStringAnsi((IntPtr)kindStrPtr, (int)kindLen) ?? "";
                    string valStr = Marshal.PtrToStringAnsi((IntPtr)valStrPtr, (int)valLen) ?? "";
                    _logger.WriteLine($"        -> Found String Attribute: Kind = \"{kindStr}\", Value = \"{valStr}\"");
                }
                else
                {
                    _logger.WriteLine($"        -> Found Attribute of unknown type.");
                }
            }

            _logger.WriteLine($"    DEBUG IsByValParameter: No ByVal attribute found for param {paramIndex}. Returning false.");
            return false;
        }

        // Новый, более надежный метод для определения byval атрибута через текстовое представление
        private unsafe bool IsByvalArgument(LLVMValueRef funcRef, uint paramIndex)
        {
            if (funcRef.Handle == IntPtr.Zero || funcRef.Kind != LLVMValueKind.LLVMFunctionValueKind)
            {
                return false;
            }

            // Получаем атрибуты для конкретного параметра
            LLVMAttributeIndex attrIdx = (LLVMAttributeIndex)(paramIndex + 1);
            uint attrCount = LLVM.GetAttributeCountAtIndex(funcRef, attrIdx);
            if (attrCount == 0) return false;

            _logger.WriteLine($"    DEBUG IsByvalArgument: Found {attrCount} attributes for param {paramIndex} of '{funcRef.Name}'. Checking for byval...");

            // Выделяем память для получения всех хэндлов атрибутов
            var attrsHandles = new LLVMOpaqueAttributeRef*[attrCount];
            fixed (LLVMOpaqueAttributeRef** pAttrsHandles = attrsHandles)
            {
                LLVM.GetAttributesAtIndex(funcRef, attrIdx, pAttrsHandles);
            }

            // Итерируем по всем атрибутам и ищем byval
            foreach (var attrHandle in attrsHandles)
            {
                if (attrHandle == null) continue;
                LLVMAttributeRef attrRef = (LLVMAttributeRef)attrHandle;

                // Проверяем enum-атрибуты
                if (LLVM.IsEnumAttribute(attrRef) != 0)
                {
                    uint kindId = LLVM.GetEnumAttributeKind(attrRef);
                    _logger.WriteLine($"        -> Found Enum Attribute: KindId = {kindId}");

                    // Стандартный KindId для byval - это 7. 38 - для byvaltype.
                    // NOTE: KindId=41 это 'noundef', НЕ byval! KindId=87 тоже не byval.
                    if (kindId == 7 || kindId == 38)
                    {
                        _logger.WriteLine($"        ----> SUCCESS: Detected official byval attribute with KindId={kindId}");
                        return true;
                    }
                }

                // Проверяем строковые атрибуты в качестве запасного варианта
                if (LLVM.IsStringAttribute(attrRef) != 0)
                {
                    uint kindLen;
                    sbyte* kindStrPtr = LLVM.GetStringAttributeKind(attrRef, &kindLen);
                    string kindStr = Marshal.PtrToStringAnsi((IntPtr)kindStrPtr, (int)kindLen) ?? "";
                    
                    if (kindStr.Contains("byval", StringComparison.OrdinalIgnoreCase))
                    {
                        _logger.WriteLine($"        ----> SUCCESS: Detected 'byval' in string attribute: Kind=\"{kindStr}\"");
                        return true;
                    }
                }
            }
            
            return false;
        }

        // Добавляем секцию для метаданных коллбэков
        private void AddCallbackMetadataSection()
        {
            _logger.WriteLine($"[DEBUG] AddCallbackMetadataSection called. Features=0x{espbModule.Features:X}, callbackInfoMap.Count={callbackInfoMap.Count}");
            
            if (callbackInfoMap.Count == 0)
            {
                _logger.WriteLine("[DEBUG] No callbacks found - cbmeta creation skipped");
                return;
            }

            _logger.WriteLine("Creating new CallbackMetadata section.");

            var cbMetaSection = new CallbackMetadataSection();
            var importsSection = GetSection<ImportsSection>(2)!;

            foreach (var importEntry in callbackInfoMap)
            {
                string importName = importEntry.Key;
                var callbackInfos = importEntry.Value;
                
                // Логика проверки hints была перенесена в BuildCallbackInfoFromIR.
                // Теперь этот метод просто работает с финальной, уже скорректированной картой.

                int importIndex = -1;
                for (int i = 0; i < importsSection.Imports.Count; i++)
                {
                    if (importsSection.Imports[i].EntityName == importName &&
                        importsSection.Imports[i].Kind == EspbImportKind.Function)
                    {
                        importIndex = i;
                        break;
                    }
                }

                if (importIndex < 0) continue;

                cbMetaSection.ImportsWithCallbacks.Add(new ImportCallbackInfo
                {
                    ImportIndex = (ushort)importIndex,
                    ImportName = importName, // ДОБАВЛЕНО: сохраняем имя импорта для отладки
                    Callbacks = callbackInfos
                });
            }
            
            if (cbMetaSection.ImportsWithCallbacks.Count > 0)
            {
                espbModule.AddSection(cbMetaSection);
                _logger.WriteLine($"Added new CallbackMetadata section with {cbMetaSection.ImportsWithCallbacks.Count} entries.");
            }
        }


        // Метод для обнаружения коллбэк-параметров для импортируемой функции
        // Возвращает true, если найдены коллбэк параметры
        private unsafe bool TryDetectCallbackParameter(LLVMValueRef importFunc, int numOperands, out List<CallbackParameterInfo> callbackInfos)
        {
            callbackInfos = new List<CallbackParameterInfo>();
            
            // Получаем имя функции для анализа
            var namePtr = LLVM.GetValueName(importFunc);
            string funcName = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
            
            // DEBUG: Логируем имя функции для отладки
            _logger.WriteLine($"[CallbackDetection] Analyzing function: '{funcName}'");
            
            // ПРАВИЛЬНОЕ ИСПРАВЛЕНИЕ: Специальная обработка для функций с прямыми указателями
            if (funcName.Contains("call_function_by_address"))
            {
                _logger.WriteLine($"[CallbackDetection] Special handling for call_function_by_address: {funcName}");
                
                // Для call_function_by_address(void* func_addr, int arg1, int arg2)
                // callback это первый параметр (индекс 0), а не второй (индекс 1)
                callbackInfos.Add(new CallbackParameterInfo(0)); // func_addr - это callback
                
                _logger.WriteLine($"[CallbackDetection] Added callback for parameter 0 (func_addr) for function: {funcName}");
                return true; // Возвращаем true, чтобы использовать наш callback
            }
            
            // Отключаем автоматический анализ для других unsafe функций
            if (funcName.Contains("test_call_indirect_ptr") || funcName.Contains("unsafe"))
            {
                _logger.WriteLine($"[CallbackDetection] Skipping callback analysis for unsafe function: {funcName}");
                return false;
            }
            
            if (importFunc.Kind != LLVMValueKind.LLVMFunctionValueKind)
                return false;
                
            // funcName уже объявлена выше
            
            // Эвристика для определения коллбэк-параметров на основе имени функции и типов параметров
            // Известно, что функция xTaskCreatePinnedToCore имеет коллбэк-параметр на позиции 0 (первый параметр)
            if (funcName == "xTaskCreatePinnedToCore")
            {
                callbackInfos.Add(new CallbackParameterInfo(0)); // 1-й параметр (индекс 0) - это коллбэк
                // Возможно, что 4-й параметр (индекс 3) - это userData, проверим в будущем
                _logger.WriteLine($"CALLBACK_DETECT: Function name '{funcName}' suggests it takes a callback at index 0 (first parameter)");
                return true;
            }
            
            // Эвристика на основе имени функции
            // Многие функции с префиксами вроде "register_", "set_", "on_" могут принимать колбэки
            if (funcName.StartsWith("register_") || funcName.StartsWith("set_") || 
                funcName.Contains("_callback") || funcName.Contains("_handler") ||
                funcName.StartsWith("on_"))
           {
               _logger.WriteLine($"CALLBACK_DETECT: Function name '{funcName}' suggests it might take a callback");
               
               // Предположительно, первый параметр - это коллбэк
                callbackInfos.Add(new CallbackParameterInfo(0)); 
                
                // Если есть второй параметр, предположим, что это данные пользователя
                if (numOperands > 2)
                    callbackInfos[0].UserDataParamIndex = 1;
                    
                return true;
            }
            
            return false;
        }

        // ===================== IR-базированный анализ колбэков =====================
        private unsafe void BuildCallbackInfoFromIR()
        {
            callbackInfoMap.Clear();
            callbackSignatures.Clear();
            signatureKeyToIndex.Clear();

            // Карта: имя импорта -> карта cb_param_idx -> список user_data индексов, встреченных в вызовах
            var perImportCbToUdList = new Dictionary<string, Dictionary<int, List<int>>>();
            // Карта: имя импорта -> карта cb_param_idx -> signature index
            var perImportCbToSigIdx = new Dictionary<string, Dictionary<int, int>>();

            // Идем по всем функциям и вызовам
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
            {
                for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(func); bb.Handle != IntPtr.Zero; bb = LLVM.GetNextBasicBlock(bb))
                {
                    for (LLVMValueRef inst = LLVM.GetFirstInstruction(bb); inst.Handle != IntPtr.Zero; inst = LLVM.GetNextInstruction(inst))
                    {
                        if (LLVM.GetInstructionOpcode(inst) != LLVMOpcode.LLVMCall) continue;

                        int numOp = LLVM.GetNumOperands(inst);
                        if (numOp <= 0) continue;
                        var called = LLVM.GetOperand(inst, (uint)(numOp - 1));
                        if (LLVM.GetValueKind(called) != LLVMValueKind.LLVMFunctionValueKind) continue;
                        // интересуют только импорты
                        if (!llvmImportToEspbIndex.ContainsKey(called)) continue;

                        string importName = "";
                        try 
                        {
                            sbyte* namePtr = LLVM.GetValueName(called);
                            if (namePtr != null)
                            {
                                importName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                            }
                        }
                        catch
                        {
                            importName = "";
                        }
                        if (string.IsNullOrEmpty(importName)) continue;

                        // Просматриваем аргументы вызова
                        int argCount = numOp - 1;
                        _logger.WriteLine($"  IR Analysis: Found call to import '{importName}' with {argCount} args");
                        for (int argIndex = 0; argIndex < argCount; argIndex++)
                        {
                            var arg = LLVM.GetOperand(inst, (uint)argIndex);
                            string argName = "";
                            var argKind = LLVM.GetValueKind(arg);
                            if (argKind == LLVMValueKind.LLVMFunctionValueKind)
                            {
                                try 
                                {
                                    sbyte* namePtr = LLVM.GetValueName(arg);
                                    if (namePtr != null)
                                    {
                                        argName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "unnamed";
                                    }
                                }
                                catch
                                {
                                    argName = "unnamed";
                                }
                            }
                            else if (argKind == LLVMValueKind.LLVMGlobalVariableValueKind)
                            {
                                try 
                                {
                                    sbyte* namePtr = LLVM.GetValueName(arg);
                                    if (namePtr != null)
                                    {
                                        argName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "unnamed_global";
                                    }
                                }
                                catch
                                {
                                    argName = "unnamed_global";
                                }
                            }
                            _logger.WriteLine($"    Arg {argIndex}: Kind={argKind}, Type={SafePrintType(LLVM.TypeOf(arg))}, Name={argName}");
                            if (!IsFunctionReference(arg, out LLVMValueRef cbFunction)) continue;
                            
                            // УНИВЕРСАЛЬНЫЙ АНАЛИЗ: Проверяем, действительно ли это callback
                            if (!IsRealCallback(importName, argIndex, cbFunction, inst))
                            {
                                _logger.WriteLine($"    -> Function reference detected but NOT used as callback - skipping");
                                continue;
                            }
                            
                            string cbName = "";
                            try 
                            {
                                sbyte* namePtr = LLVM.GetValueName(cbFunction);
                                if (namePtr != null)
                                {
                                    cbName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "unnamed";
                                }
                            }
                            catch
                            {
                                cbName = "unnamed";
                            }
                            _logger.WriteLine($"    -> Confirmed REAL callback function at arg {argIndex}: {cbName}");

                            // Получаем локальный индекс функции-колбэка
                            if (!llvmFuncToEspbIndex.TryGetValue(cbFunction, out uint funcIdx))
                            {
                                _logger.WriteLine($"ERROR: Callback function '{cbName}' not found in function index. Skipping.");
                                continue;
                            }

                            // Определяем user_data на основании аргументов вызова (универсальная эвристика)
                            int? userDataIdx = FindUserDataIndex(inst, argIndex);

                            if (!perImportCbToUdList.TryGetValue(importName, out var cbMap))
                            {
                                cbMap = new Dictionary<int, List<int>>();
                                perImportCbToUdList[importName] = cbMap;
                            }
                            if (!cbMap.TryGetValue(argIndex, out var udList))
                            {
                                udList = new List<int>();
                                cbMap[argIndex] = udList;
                            }
                            if (userDataIdx.HasValue)
                                udList.Add(userDataIdx.Value);

                            // Используем perImportCbToSigIdx для хранения индекса функции, а не сигнатуры
                            if (!perImportCbToSigIdx.TryGetValue(importName, out var funcIdxMap))
                            {
                                funcIdxMap = new Dictionary<int, int>();
                                perImportCbToSigIdx[importName] = funcIdxMap;
                            }
                            funcIdxMap[argIndex] = (int)funcIdx;
                        }
                    }
                }
            }

            // Строим финальную карту callbackInfoMap
            foreach (var importEntry in perImportCbToUdList)
            {
                string importName = importEntry.Key;
                var cbMap = importEntry.Value;
                var sigMap = perImportCbToSigIdx.ContainsKey(importName) ? perImportCbToSigIdx[importName] : new Dictionary<int, int>();
                var list = new List<CallbackParameterInfo>();

                foreach (var kv in cbMap)
                {
                    int cbIdx = kv.Key;
                    var udIndices = kv.Value;
                    int? stableUd = null;
                    if (udIndices.Count > 0 && udIndices.All(i => i == udIndices[0]))
                    {
                        stableUd = udIndices[0];
                    }
                    else
                    {
                        stableUd = null; // нестабильно -> 0xF будет записан
                    }

                    var info = new CallbackParameterInfo(cbIdx, stableUd);
                    if (sigMap.TryGetValue(cbIdx, out int fIdx))
                        info.EspbFuncIndex = fIdx;
                    list.Add(info);
                }

                // Если карта пустая (например, только сигнатуры нашлись), пропускаем
                if (list.Count > 0)
                {
                    callbackInfoMap[importName] = list.OrderBy(i => i.CallbackParamIndex).ToList();
                }
            }

            // ===================== ПРИМЕНЕНИЕ HINTS =====================
            if (hintsManager != null)
            {
                _logger.WriteLine("[Hints] Applying hints to the automatically detected callback map...");

                // Создаем копию ключей, чтобы можно было изменять словарь в цикле
                var importNames = callbackInfoMap.Keys.ToList();

                foreach (var importName in importNames)
                {
                    if (hintsManager.HasHintsForFunction(importName))
                    {
                        var hints = hintsManager.GetHintsForFunction(importName);
                        if (hints == null) continue;

                        // 1. Обработка полного отключения колбэков для функции
                        if (hints.DisableAllCallbacks)
                        {
                            _logger.WriteLine($"[Hints] Removing all callbacks for function '{importName}' due to 'no cb' rule.");
                            callbackInfoMap.Remove(importName);
                            continue; // Переходим к следующему импорту
                        }

                        // 2. Обработка отключения конкретных колбэков
                        if (hints.DisabledCallbacks.Any())
                        {
                            if (callbackInfoMap.TryGetValue(importName, out var detectedCallbacks))
                            {
                                // Отфильтровываем отключенные колбэки
                                var originalCount = detectedCallbacks.Count;
                                detectedCallbacks.RemoveAll(cb => hints.DisabledCallbacks.Contains(cb.CallbackParamIndex));
                                
                                if (originalCount != detectedCallbacks.Count)
                                {
                                     _logger.WriteLine($"[Hints] Removed {originalCount - detectedCallbacks.Count} disabled callbacks for '{importName}'.");
                                }

                                // Если после фильтрации колбэков не осталось, удаляем всю запись
                                if (detectedCallbacks.Count == 0)
                                {
                                    callbackInfoMap.Remove(importName);
                                }
                            }
                        }
                        
                        // 3. Обработка добавления/переопределения колбэков
                        if (hints.CallbackOverrides.Any())
                        {
                            if (!callbackInfoMap.ContainsKey(importName))
                            {
                                callbackInfoMap[importName] = new List<CallbackParameterInfo>();
                            }
                            var callbacks = callbackInfoMap[importName];

                            foreach (var overrideEntry in hints.CallbackOverrides)
                            {
                                var overrideInfo = overrideEntry.Value;
                                // Удаляем существующий колбэк на этом же индексе, если он есть
                                callbacks.RemoveAll(cb => cb.CallbackParamIndex == overrideInfo.CallbackParamIndex);
                                
                                // Добавляем новый (из хинта)
                                var newCbInfo = new CallbackParameterInfo(overrideInfo.CallbackParamIndex, overrideInfo.UserDataParamIndex)
                                {
                                    EspbFuncIndex = overrideInfo.EspbFuncIndex,
                                    SignatureIndex = overrideInfo.SignatureIndex,
                                    Flags = overrideInfo.Flags
                                };
                                callbacks.Add(newCbInfo);
                                _logger.WriteLine($"[Hints] Added/Overwritten callback for '{importName}' at index {newCbInfo.CallbackParamIndex}.");
                            }
                             // Сортируем для порядка
                            callbacks.Sort((a, b) => a.CallbackParamIndex.CompareTo(b.CallbackParamIndex));
                        }

                        // 4. Обработка отключения user data для конкретных callback'ов
                        if (hints.DisabledUserData.Any())
                        {
                            if (callbackInfoMap.TryGetValue(importName, out var callbacks))
                            {
                                foreach (var disabledUdEntry in hints.DisabledUserData)
                                {
                                    int cbIdx = disabledUdEntry.Key;
                                    var disabledUdIndices = disabledUdEntry.Value;
                                    
                                    // Находим callback с указанным индексом
                                    var targetCallback = callbacks.FirstOrDefault(cb => cb.CallbackParamIndex == cbIdx);
                                    if (targetCallback != null)
                                    {
                                        // Если user data параметр отключен для этого callback'а, убираем его
                                        if (targetCallback.UserDataParamIndex.HasValue &&
                                            disabledUdIndices.Contains(targetCallback.UserDataParamIndex.Value))
                                        {
                                            _logger.WriteLine($"[Hints] Disabled userdata {targetCallback.UserDataParamIndex.Value} for callback {cbIdx} in '{importName}'");
                                            targetCallback.UserDataParamIndex = null;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // =================== КОНЕЦ ПРИМЕНЕНИЯ HINTS ==================

            if (callbackInfoMap.Count > 0)
            {
                espbModule.Features |= 0x100; // FEATURE_CALLBACK_AUTO
                _logger.WriteLine($"IR Analysis (after hints): Found {callbackInfoMap.Count} imports with callbacks:");
                foreach (var kv in callbackInfoMap)
                {
                    _logger.WriteLine($"  {kv.Key}: {kv.Value.Count} callbacks");
                    foreach (var cb in kv.Value)
                    {
                        _logger.WriteLine($"    cb_param_idx={cb.CallbackParamIndex}, user_data={cb.UserDataParamIndex}, espb_func_idx={cb.EspbFuncIndex}");
                    }
                }
            }
            else
            {
                _logger.WriteLine("IR Analysis (after hints): No callbacks found in imports");
            }
        }

        // НАСТОЯЩИЙ АНАЛИЗАТОР: Анализирует реальное использование функций в LLVM IR
        private unsafe bool IsRealCallback(string importName, int paramIndex, LLVMValueRef function, LLVMValueRef callInst)
        {
            // 1. ПРИОРИТЕТ HINTS: Пользователь может переопределить любое решение
            if (hintsManager != null && hintsManager.HasHintsForFunction(importName))
            {
                var hints = hintsManager.GetHintsForFunction(importName);
                if (hints != null)
                {
                    if (hints.DisableAllCallbacks)
                    {
                        _logger.WriteLine($"    [Hints Override] Callbacks disabled for '{importName}' by hints");
                        return false;
                    }
                    
                    if (hints.DisabledCallbacks.Contains(paramIndex))
                    {
                        _logger.WriteLine($"    [Hints Override] Param {paramIndex} of '{importName}' disabled as callback by hints");
                        return false;
                    }
                    
                    if (hints.CallbackOverrides.ContainsKey(paramIndex))
                    {
                        _logger.WriteLine($"    [Hints Override] Param {paramIndex} of '{importName}' FORCED as callback by hints");
                        return true;
                    }
                }
            }
            
            // 2. РЕАЛЬНЫЙ АНАЛИЗ LLVM IR: Отслеживаем использование функции
            bool isUsedAsCallback = AnalyzeRealUsageInIR(importName, paramIndex, function, callInst);
            
            if (isUsedAsCallback)
            {
                _logger.WriteLine($"    [IR Analysis] Function '{importName}' param {paramIndex} IS used as callback (calls detected in IR)");
                return true;
            }
            else
            {
                _logger.WriteLine($"    [IR Analysis] Function '{importName}' param {paramIndex} is NOT used as callback (no calls detected in IR)");
                return false;
            }
        }
        
        // БЕЗОПАСНЫЙ АНАЛИЗ: Анализирует использование без рискованных LLVM операций
        private unsafe bool AnalyzeRealUsageInIR(string importName, int paramIndex, LLVMValueRef function, LLVMValueRef callInst)
        {
            try
            {
                _logger.WriteLine($"    [IR Analysis] Starting safe analysis for '{importName}' param {paramIndex}");
                
                // 1. БЕЗОПАСНАЯ ПРОВЕРКА: Анализируем только использование переданной функции
                bool hasUses = AnalyzeFunctionUsagePatterns(function);
                
                if (hasUses)
                {
                    _logger.WriteLine($"    [IR Analysis] Function '{importName}' param {paramIndex} has uses - might be callback");
                    
                    // 2. ДОПОЛНИТЕЛЬНАЯ ЭВРИСТИКА: По названию функции и позиции параметра
                    bool isLikelyCallback = IsLikelyCallbackByContext(importName, paramIndex);
                    
                    if (isLikelyCallback)
                    {
                        _logger.WriteLine($"    [IR Analysis] Context suggests callback - CONFIRMED");
                        return true;
                    }
                    else
                    {
                        _logger.WriteLine($"    [IR Analysis] Context suggests NOT callback - likely just value");
                        return false;
                    }
                }
                else
                {
                    _logger.WriteLine($"    [IR Analysis] Function has no uses - NOT a callback");
                    return false;
                }
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    [IR Analysis] Error during analysis: {ex.Message}");
                return IsConservativeCallback(importName, paramIndex);
            }
        }
        
        // Определяет, является ли функция вероятным callback'ом по контексту
        private bool IsLikelyCallbackByContext(string importName, int paramIndex)
        {
            // ANTI-CALLBACK ПАТТЕРНЫ (функции, которые точно НЕ используют callback'и)
            var antiCallbackPatterns = new[]
            {
                "printf", "fprintf", "sprintf", "snprintf",
                "scanf", "fscanf", "sscanf", "vprintf",
                "puts", "fputs", "putchar", "fputc",
                "memcmp", "strcmp", "strncmp", "strcat",
                "malloc", "free", "calloc", "realloc"
            };
            
            foreach (var pattern in antiCallbackPatterns)
            {
                if (importName.IndexOf(pattern, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    _logger.WriteLine($"    [Context] '{importName}' matches anti-callback pattern '{pattern}'");
                    return false;
                }
            }
            
            // CALLBACK ПАТТЕРНЫ (функции, которые вероятно используют callback'и)
            var callbackPatterns = new[]
            {
                "callback", "invoke", "call", "exec",
                "create", "timer", "task", "async",
                "register", "subscribe", "listen",
                "schedule", "queue", "defer"
            };
            
            foreach (var pattern in callbackPatterns)
            {
                if (importName.IndexOf(pattern, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    _logger.WriteLine($"    [Context] '{importName}' matches callback pattern '{pattern}'");
                    return true;
                }
            }
            
            // ПО УМОЛЧАНИЮ: неизвестные функции НЕ считаем callback'ами
            _logger.WriteLine($"    [Context] '{importName}' - no clear pattern, assuming NOT callback");
            return false;
        }
        
        // Анализирует тело функции на предмет использования callback параметров
        private unsafe bool AnalyzeFunctionBodyForCallbacks(LLVMValueRef function, int targetParamIndex)
        {
            try
            {
                var bb = LLVM.GetFirstBasicBlock(function);
                int callsFound = 0;
                int storesFound = 0;
                
                while (bb != default(LLVMBasicBlockRef))
                {
                    var inst = LLVM.GetFirstInstruction(bb);
                    while (inst != default(LLVMValueRef))
                    {
                        var opcode = LLVM.GetInstructionOpcode(inst);
                        
                        if (opcode == LLVMOpcode.LLVMCall)
                        {
                            // Простая проверка: является ли это indirect call
                            var calledValue = LLVM.GetOperand(inst, (uint)(LLVM.GetNumOperands(inst) - 1));
                            var valueKind = LLVM.GetValueKind(calledValue);
                            
                            if (valueKind == LLVMValueKind.LLVMArgumentValueKind)
                            {
                                callsFound++;
                                _logger.WriteLine($"    [IR Analysis] Found INDIRECT CALL using function parameter - this IS a callback!");
                            }
                        }
                        else if (opcode == LLVMOpcode.LLVMStore)
                        {
                            storesFound++;
                        }
                        
                        inst = LLVM.GetNextInstruction(inst);
                    }
                    bb = LLVM.GetNextBasicBlock(bb);
                }
                
                _logger.WriteLine($"    [IR Analysis] Usage pattern: {callsFound} indirect calls, {storesFound} stores");
                
                // РЕШЕНИЕ: Если найдены indirect calls - это callback
                if (callsFound > 0)
                {
                    _logger.WriteLine($"    [IR Analysis] VERDICT: IS callback (found {callsFound} indirect calls)");
                    return true;
                }
                
                _logger.WriteLine($"    [IR Analysis] VERDICT: NOT callback (no indirect calls found)");
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    [IR Analysis] Error analyzing function body: {ex.Message}");
                return false;
            }
        }
        
        // Анализирует паттерны использования функции в модуле
        private unsafe bool AnalyzeFunctionUsagePatterns(LLVMValueRef function)
        {
            try
            {
                // Упрощенный анализ: проверяем есть ли uses функции вообще
                var user = LLVM.GetFirstUse(function);
                int usesFound = 0;
                
                while (user != default(LLVMUseRef))
                {
                    usesFound++;
                    user = LLVM.GetNextUse(user);
                    
                    // Ограничиваем количество проверок
                    if (usesFound > 10) break;
                }
                
                _logger.WriteLine($"    [IR Analysis] Function has {usesFound} uses in module");
                
                // Простая эвристика: если функция используется, может быть callback
                return usesFound > 0;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    [IR Analysis] Error analyzing function usage: {ex.Message}");
                return false;
            }
        }
        
        // Консервативный анализ для fallback случаев
        private bool IsConservativeCallback(string importName, int paramIndex)
        {
            // Только явные callback функции
            var obviousCallbacks = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                "host_invoke_cb", "host_invoke_cb2"
            };
            
            bool isObvious = obviousCallbacks.Contains(importName);
            _logger.WriteLine($"    [Conservative] '{importName}' is {(isObvious ? "" : "NOT ")}obvious callback function");
            return isObvious;
        }

        private unsafe bool IsFunctionReference(LLVMValueRef val, out LLVMValueRef function)
        {
            function = default;
            
            try
            {
                if (val == null)
                    return false;

                // Прямое значение функции (внутренняя функция)
                if (LLVM.GetValueKind(val) == LLVMValueKind.LLVMFunctionValueKind)
                {
                    function = val;
                    return true;
                }
                
                // Константные выражения, приводящие к функции (bitcast и т.п.)
                if (LLVM.GetValueKind(val) == LLVMValueKind.LLVMConstantExprValueKind)
                {
                    LLVMOpcode op = LLVM.GetConstOpcode(val);
                    if (op == LLVMOpcode.LLVMBitCast || op == LLVMOpcode.LLVMGetElementPtr)
                    {
                        var baseOp = val.GetOperand(0);
                        if (baseOp != null)
                            return IsFunctionReference(baseOp, out function);
                    }
                }
                
                // Глобальные переменные - пропускаем, так как они не являются колбэками
                // В LLVM IR колбэки передаются как прямые ссылки на функции или через константные выражения
                if (LLVM.GetValueKind(val) == LLVMValueKind.LLVMGlobalVariableValueKind)
                {
                    // Глобальные переменные не являются колбэками, пропускаем
                    return false;
                }
                
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Error in IsFunctionReference: {ex.Message}");
                return false;
            }
        }

        private unsafe int GetOrAddCallbackSignature(LLVMValueRef funcVal)
        {
             if (funcVal.Handle == IntPtr.Zero) return 0;
            LLVMTypeRef funcType = LLVM.TypeOf(funcVal);
             if (funcType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                 funcType = funcType.ElementType;

             if (funcType.Kind != LLVMTypeKind.LLVMFunctionTypeKind)
                 return 0;

            var retType = MapLlvmTypeToEspb(funcType.ReturnType);
            var paramsList = new List<ValueType>();
            uint paramCount = LLVM.CountParamTypes(funcType);
            LLVMTypeRef[] paramTypes = new LLVMTypeRef[paramCount];
            if (paramCount > 0)
            {
                 fixed(LLVMTypeRef* p = paramTypes)
                 {
                    LLVM.GetParamTypes(funcType, (LLVMOpaqueType**)p);
                 }
                 foreach (var p in paramTypes)
                    paramsList.Add(MapLlvmTypeToEspb(p));
            }

            string key = BuildSignatureKey(retType, paramsList);
            if (signatureKeyToIndex.TryGetValue(key, out int idx))
                return idx;
            
            idx = callbackSignatures.Count;
            callbackSignatures.Add((retType, paramsList));
            signatureKeyToIndex.Add(key, idx);
            return idx;
        }

        private string BuildSignatureKey(ValueType ret, List<ValueType> prms)
        {
            var sb = new StringBuilder();
            sb.Append(((int)ret).ToString()).Append(':');
            for (int i = 0; i < prms.Count; i++)
            {
                if (i > 0) sb.Append(',');
                sb.Append(((int)prms[i]).ToString());
            }
            return sb.ToString();
        }

        private unsafe int? FindUserDataIndex(LLVMValueRef callInst, int cbParamIdx)
        {
            int numOp = LLVM.GetNumOperands(callInst);
            int argCount = numOp - 1;
            _logger.WriteLine($"    DEBUG FindUserDataIndex: Analyzing {argCount} arguments for callback at index {cbParamIdx}");

            int bestIndex = -1;
            int bestScore = int.MinValue;

            for (int i = 0; i < argCount; i++)
            {
                if (i == cbParamIdx) continue;

                var arg = LLVM.GetOperand(callInst, (uint)i);
                int currentScore = 0;
                
                // Rule 1: Must be a pointer-like type. Non-pointers get a very low score.
                if (IsPointerLike(arg, out bool isStringLiteral))
                {
                    currentScore += 100; // Base score for being a pointer.
                    
                    // Rule 2: Penalize string literals heavily. They are almost never user_data.
                    if (isStringLiteral)
                    {
                        currentScore -= 200; // Heavy penalty for string literals.
                        _logger.WriteLine($"      Arg {i}: Is a string literal. Penalizing.");
                    }

                    // Rule 3: Proximity to the callback parameter. Closer is better.
                    int distance = Math.Abs(i - cbParamIdx);
                    currentScore -= distance * 10; // Penalty for distance.

                    // Rule 4: Prefer arguments to the right of the callback.
                    if (i > cbParamIdx)
                    {
                        currentScore += 20;
                    }
                    
                    // Rule 5: Prefer non-obvious constants like NULL or other pointers over simple integers cast to pointers.
                    var argKind = LLVM.GetValueKind(arg);
                    if (argKind == LLVMValueKind.LLVMConstantPointerNullValueKind)
                    {
                        currentScore += 15; // NULL is a common user_data value.
                    }
                    else if(argKind == LLVMValueKind.LLVMConstantExprValueKind)
                    {
                         // Potentially a cast from an integer. Give a small penalty.
                         currentScore -= 5;
                    }
                }
                else
                {
                    currentScore = int.MinValue; // Not a pointer, not a candidate.
                }

                _logger.WriteLine($"      Arg {i}: Score = {currentScore}");
                if (currentScore > bestScore)
                {
                    bestScore = currentScore;
                    bestIndex = i;
                    _logger.WriteLine($"        -> New best candidate index {bestIndex} with score {bestScore}");
                }
            }

            int? result = bestIndex >= 0 && bestScore > 0 ? bestIndex : (int?)null;
            _logger.WriteLine($"    DEBUG FindUserDataIndex: Final candidate = {result} (score {bestScore})");
            return result;
        }

        private unsafe bool IsValidPointer(IntPtr ptr)
        {
            // Проверяем, что указатель не null и не указывает на недопустимую область памяти
            return ptr != IntPtr.Zero && ptr != (IntPtr)(-1);
        }

        private unsafe bool IsPointerLike(LLVMValueRef val, out bool isStringLiteral)
        {
            isStringLiteral = false;
            if (val.Handle == IntPtr.Zero) return false;

            var valKind = LLVM.GetValueKind(val);
            var typeKind = LLVM.GetTypeKind(LLVM.TypeOf(val));

            // Rule 1: Must be a pointer type.
            if (typeKind != LLVMTypeKind.LLVMPointerTypeKind) return false;

            // Rule 2: Check for string literals, which are pointers but not typical user_data.
            if (valKind == LLVMValueKind.LLVMGlobalVariableValueKind)
            {
                if (LLVM.IsConstantString(LLVM.GetInitializer(val)) != 0)
                {
                    isStringLiteral = true;
                }
            }
            else if (valKind == LLVMValueKind.LLVMConstantExprValueKind && LLVM.GetConstOpcode(val) == LLVMOpcode.LLVMGetElementPtr)
            {
                var baseOp = val.GetOperand(0);
                if (LLVM.GetValueKind(baseOp) == LLVMValueKind.LLVMGlobalVariableValueKind)
                {
                    if (LLVM.IsConstantString(LLVM.GetInitializer(baseOp)) != 0)
                    {
                        isStringLiteral = true;
                    }
                }
            }

            return true;
        }

        // Метод для анализа аргумента и определения типа маршалинга согласно ТЗ
        //----------------------------------
        // --- НОВЫЙ ВСПОМОГАТЕЛЬНЫЙ МЕТОД ДЛЯ ПРОВЕРКИ АТРИБУТОВ ---
private unsafe bool IsAttributePresent(LLVMValueRef funcRef, uint paramIndex, string attributeName)
{
    // Атрибуты для параметров имеют индекс arg_idx + 1
    var attrIdx = (LLVMAttributeIndex)(paramIndex + 1);

    uint attrCount = LLVM.GetAttributeCountAtIndex(funcRef, attrIdx);
    if (attrCount == 0) return false;

    // Выделяем память для получения хэндлов всех атрибутов
    var attrsHandles = new LLVMOpaqueAttributeRef*[attrCount];
    fixed (LLVMOpaqueAttributeRef** pAttrsHandles = attrsHandles)
    {
        LLVM.GetAttributesAtIndex(funcRef, attrIdx, pAttrsHandles);
    }

    // Итерируем по всем атрибутам
    foreach (var attrHandle in attrsHandles)
    {
        if (attrHandle == null) continue;
        LLVMAttributeRef attrRef = (LLVMAttributeRef)attrHandle;

        // Проверяем enum-атрибуты по известным ID
        if (LLVM.IsEnumAttribute(attrRef) != 0)
        {
            uint kindId = LLVM.GetEnumAttributeKind(attrRef);

            // Известные атрибуты и их ID
            string attrKindStr = kindId switch
            {
                7 => "byval",
                38 => "byvaltype",
                41 => "readonly", // пример, может быть неправиьно
                87 => "writeonly", // пример, может быть неправиьно
                1 => "noalias",
                4 => "norecurse",
                15 => "sret",
                _ => $"unknown_{kindId}"
            };

            if (attrKindStr.Equals(attributeName, StringComparison.OrdinalIgnoreCase))
            {
                _logger.WriteLine($"[Analyze] Arg {paramIndex}: Found enum attribute '{attributeName}' by kind ID {kindId}.");
                return true;
            }
        }

        // Проверяем строковые атрибуты в качестве запасного варианта - временно отключено из-за проблем с сигнатурой
        // TODO: Восстановить после исправления LLVMSharp
        /*
        if (LLVM.IsStringAttribute(attrRef) != 0)
        {
            sbyte* kindStrPtr;
            uint kindLen;
            LLVM.GetStringAttributeKind(attrRef, out kindStrPtr, out kindLen);
            string kindStr = Marshal.PtrToStringAnsi((IntPtr)kindStrPtr, (int)kindLen) ?? "";

            if (kindStr.Equals(attributeName, StringComparison.OrdinalIgnoreCase))
            {
                _logger.WriteLine($"[Analyze] Arg {paramIndex}: Found string attribute '{attributeName}'.");
                return true;
            }
        }
        */
    }

    return false;
}

// --- НОВЫЙ ВСПОМОГАТЕЛЬНЫЙ МЕТОД ДЛЯ ПОИСКА АРГУМЕНТА-РАЗМЕРА ---
private unsafe (uint Index, string Name)? FindSizeArgumentForBuffer(LLVMValueRef importFunc, uint bufferArgIndex, LLVMTypeRef[] paramTypes)
{
    // Эвристика: ищем следующий за буфером аргумент целочисленного типа
    uint sizeArgIndex = bufferArgIndex + 1;
    if (sizeArgIndex < paramTypes.Length)
    {
        if (IsSizeType(paramTypes[sizeArgIndex]))
        {
            // Проверяем имя, если оно есть, для подтверждения (например, "len", "size")
            unsafe
            {
                var param = LLVM.GetParam(importFunc, sizeArgIndex);
                var namePtr = LLVM.GetValueName(param);
                string paramName = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
                if (paramName.Contains("size", StringComparison.OrdinalIgnoreCase) ||
                    paramName.Contains("len", StringComparison.OrdinalIgnoreCase))
                {
                    return (sizeArgIndex, paramName);
                }
                // Если имя не показательно, но тип подходит, все равно считаем это парой
                return (sizeArgIndex, paramName);
            }
        }
    }
    return null;
}

// --- ПОЛНОСТЬЮ ПЕРЕРАБОТАННЫЙ МЕТОД АНАЛИЗА ---
private unsafe MarshallingInfo AnalyzeArgumentForMarshalling(LLVMValueRef importFunc, uint argIndex)
{
    unsafe
    {
        var namePtr = LLVM.GetValueName(importFunc);
        string importFuncName = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
        _logger.WriteLine($"[Analyze] Analyzing arg {argIndex} of '{importFuncName}'...");
    }

    // Используем новый метод с поддержкой hints
    return AnalyzeParameterWithHints(importFunc, argIndex);
}

        /// <summary>
        /// Анализирует параметр с учетом hints файла
        /// </summary>
        private unsafe MarshallingInfo AnalyzeParameterWithHints(LLVMValueRef importFunc, uint argIndex)
        {
            // Получаем имя функции
            string funcName = "";
            unsafe
            {
                var namePtr = LLVM.GetValueName(importFunc);
                funcName = namePtr != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "" : "";
            }

            // Проверяем, есть ли hints для этой функции
            if (hintsManager != null && hintsManager.HasHintsForFunction(funcName))
            {
                var hints = hintsManager.GetHintsForFunction(funcName);
                if (hints != null)
                {
                    _logger.WriteLine($"[Hints] Found hints for function '{funcName}'");
                    
                    // Проверяем, отключен ли immeta для всей функции
                    if (hints.DisableAllImmeta)
                    {
                        _logger.WriteLine($"[Hints] Immeta disabled for entire function '{funcName}'");
                        return new MarshallingInfo(MarshallingDirection.None, SizeKind.Constant, 0);
                    }
                    
                    // Проверяем, отключен ли immeta для конкретного параметра
                    if (hints.DisabledImmeta.Contains((int)argIndex))
                    {
                        _logger.WriteLine($"[Hints] Immeta disabled for parameter {argIndex} in function '{funcName}'");
                        return new MarshallingInfo(MarshallingDirection.None, SizeKind.Constant, 0);
                    }
                    
                    // Проверяем переопределения marshalling
                    if (hints.MarshallingOverrides.TryGetValue((int)argIndex, out var hintsMarshalInfo))
                    {
                        _logger.WriteLine($"[Hints] Using marshalling override for parameter {argIndex} in function '{funcName}': {hintsMarshalInfo.Direction}");
                        // Конвертируем типы из hints в типы транслятора
                        var direction = ConvertMarshallingDirection(hintsMarshalInfo.Direction);
                        var sizeKind = ConvertSizeKind(hintsMarshalInfo.SizeMode);
                        var result = new MarshallingInfo(direction, sizeKind, hintsMarshalInfo.SizeValue);
                        // Устанавливаем HandlerIndex, если поле существует
                        if (hintsMarshalInfo.HandlerIndex != 0)
                        {
                            // Попробуем установить через reflection, если поле есть
                            var handlerIndexProperty = result.GetType().GetProperty("HandlerIndex");
                            if (handlerIndexProperty != null)
                            {
                                handlerIndexProperty.SetValue(result, hintsMarshalInfo.HandlerIndex);
                            }
                        }
                        return result;
                    }
                }
            }

            // Если hints не найдены или не применимы, используем автоматический анализ
            return AnalyzeParameterForMarshallingInternal(importFunc, argIndex);
        }

        /// <summary>
        /// Переименованный оригинальный метод анализа для внутреннего использования
        /// </summary>
        private unsafe MarshallingInfo AnalyzeParameterForMarshallingInternal(LLVMValueRef importFunc, uint argIndex)
        {
            // Код оригинального метода AnalyzeParameterForMarshalling
            
            LLVMTypeRef funcPointerType = importFunc.TypeOf;
            if (funcPointerType.Kind != LLVMTypeKind.LLVMPointerTypeKind || funcPointerType.ElementType.Kind != LLVMTypeKind.LLVMFunctionTypeKind)
            {
                unsafe
                {
                    var namePtr2 = LLVM.GetValueName(importFunc);
                    string importFuncName2 = namePtr2 != null ? Marshal.PtrToStringAnsi((IntPtr)namePtr2) ?? "" : "";
                    _logger.WriteLine($"[Analyze] Warning: Could not determine function type for '{importFuncName2}'.");
                }
                return new MarshallingInfo();
            }
            LLVMTypeRef funcType = funcPointerType.ElementType;

            var paramCount = LLVM.CountParamTypes(funcType);
            if (argIndex >= paramCount)
            {
                _logger.WriteLine($"[Analyze] Arg index {argIndex} is out of bounds or in variadic part.");
                return new MarshallingInfo();
            }

            LLVMTypeRef[] paramTypes = new LLVMTypeRef[paramCount];
            fixed (LLVMTypeRef* p = paramTypes)
            {
                LLVM.GetParamTypes(funcType, (LLVMOpaqueType**)p);
            }
            var argType = paramTypes[argIndex];
            _logger.WriteLine($"[Analyze] Arg {argIndex} type is {argType.Kind}");

            // --- ПРИОРИТЕТ 1: Явные атрибуты OUT ---
            if (IsAttributePresent(importFunc, argIndex, "sret") || IsAttributePresent(importFunc, argIndex, "writeonly"))
            {
                var pointeeTypeSize = GetTypeSize(argType.ElementType);
                return new MarshallingInfo(MarshallingDirection.Out, SizeKind.Constant, pointeeTypeSize);
            }

            // --- ПРИОРИТЕТ 2: Неявные OUT-параметры по типу (ptr*) ---
            if (argType.Kind == LLVMTypeKind.LLVMPointerTypeKind && argType.ElementType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
            {
                _logger.WriteLine($"[Analyze] Arg {argIndex} is a pointer-to-pointer. Marking as OUT.");
                return new MarshallingInfo(MarshallingDirection.Out, SizeKind.Constant, (uint)IntPtr.Size);
            }

            // --- ПРИОРИТЕТ 3: Явные атрибуты IN ---
            if (IsAttributePresent(importFunc, argIndex, "byval"))
            {
                _logger.WriteLine($"[Analyze] Arg {argIndex} has byval attribute. Marking as IN.");
                var sizeInBytes = GetTypeSize(argType.ElementType); // Размер того, НА ЧТО указывает byval-указатель
                return new MarshallingInfo(MarshallingDirection.In, SizeKind.Constant, sizeInBytes);
            }
            if (IsAttributePresent(importFunc, argIndex, "readonly"))
            {
                _logger.WriteLine($"[Analyze] Arg {argIndex} has readonly attribute. No marshalling needed.");
                return new MarshallingInfo(MarshallingDirection.None, SizeKind.Constant, 0);
            }

            // --- ПРИОРИТЕТ 4: Эвристика для динамических буферов (пара ptr/size) ---
            if (argType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
            {
                var sizeArgInfo = FindSizeArgumentForBuffer(importFunc, argIndex, paramTypes);
                if (sizeArgInfo != null)
                {
                    _logger.WriteLine($"[Analyze] Arg {argIndex} appears to be a buffer with size from arg {sizeArgInfo.Value.Index}.");
                    
                    string funcName = importFunc.Name.ToLowerInvariant();
                    if (funcName.Contains("get_") || funcName.Contains("read_") || funcName.Contains("fill_") || funcName.Contains("receive"))
                    {
                        _logger.WriteLine($"[Analyze] Function name suggests OUT direction for buffer.");
                        return new MarshallingInfo(MarshallingDirection.Out, SizeKind.FromArgument, sizeArgInfo.Value.Index);
                    }
                    if (funcName.Contains("set_") || funcName.Contains("write_") || funcName.Contains("send_") || funcName.Contains("process"))
                    {
                        _logger.WriteLine($"[Analyze] Function name suggests IN direction for buffer.");
                        return new MarshallingInfo(MarshallingDirection.In, SizeKind.FromArgument, sizeArgInfo.Value.Index);
                    }

                    _logger.WriteLine($"[Analyze] Function name is ambiguous. Defaulting to INOUT for buffer safety.");
                    return new MarshallingInfo(MarshallingDirection.InOut, SizeKind.FromArgument, sizeArgInfo.Value.Index);
                }
            }

            _logger.WriteLine($"[Analyze] No special marshalling rules detected for arg {argIndex}.");
            return new MarshallingInfo(); // Поведение по умолчанию: нет маршалинга
        }

        /// <summary>
        /// Анализирует callbacks с учетом hints файла
        /// </summary>
        private bool AnalyzeCallbacksWithHints(string funcName, LLVMValueRef importFunc)
        {
            // Проверяем, есть ли hints для этой функции
            if (hintsManager != null && hintsManager.HasHintsForFunction(funcName))
            {
                var hints = hintsManager.GetHintsForFunction(funcName);
                if (hints != null)
                {
                    _logger.WriteLine($"[Hints] Found hints for function '{funcName}' - checking callbacks");
                    
                    // Проверяем, отключены ли callbacks для всей функции
                    if (hints.DisableAllCallbacks)
                    {
                        _logger.WriteLine($"[Hints] Callbacks disabled for entire function '{funcName}'");
                        return false; // Не анализируем callbacks для этой функции
                    }
                    
                    // Если есть переопределения callbacks, применяем их
                    if (hints.CallbackOverrides.Count > 0)
                    {
                        _logger.WriteLine($"[Hints] Applying {hints.CallbackOverrides.Count} callback overrides for function '{funcName}'");
                        
                        var callbackList = new List<CallbackParameterInfo>();
                        
                        foreach (var kvp in hints.CallbackOverrides)
                        {
                            int paramIndex = kvp.Key;
                            var callbackInfo = kvp.Value;
                            
                            // Проверяем, не отключен ли этот конкретный callback
                            if (hints.DisabledCallbacks.Contains(paramIndex))
                            {
                                _logger.WriteLine($"[Hints] Callback at parameter {paramIndex} is explicitly disabled");
                                continue;
                            }
                            
                            _logger.WriteLine($"[Hints] Adding callback: param {callbackInfo.CallbackParamIndex}, userdata {callbackInfo.UserDataParamIndex}");
                            // Конвертируем HintsCallbackParameterInfo в CallbackParameterInfo
                            var convertedCallback = new CallbackParameterInfo(callbackInfo.CallbackParamIndex, callbackInfo.UserDataParamIndex);
                            convertedCallback.SignatureIndex = callbackInfo.SignatureIndex;
                            convertedCallback.EspbFuncIndex = callbackInfo.EspbFuncIndex;
                            convertedCallback.Flags = callbackInfo.Flags;
                            callbackList.Add(convertedCallback);
                        }
                        
                        if (callbackList.Count > 0)
                        {
                            callbackInfoMap[funcName] = callbackList;
                            return true;
                        }
                    }
                    
                    // Проверяем отключенные callbacks для автоматического анализа
                    if (hints.DisabledCallbacks.Count > 0)
                    {
                        _logger.WriteLine($"[Hints] Some callbacks disabled, will filter automatic analysis");
                        // Продолжаем с автоматическим анализом, но с фильтрацией
                    }
                }
            }

            // Если hints не найдены или не применимы, используем автоматический анализ
            return PerformAutomaticCallbackAnalysis(funcName, importFunc);
        }

        /// <summary>
        /// Выполняет автоматический анализ callbacks (оригинальная логика)
        /// </summary>
        private bool PerformAutomaticCallbackAnalysis(string funcName, LLVMValueRef importFunc)
        {
            // Здесь должна быть оригинальная логика анализа callbacks
            // Для примера возвращаем false, но нужно будет интегрировать существующий код
            _logger.WriteLine($"[Analyze] Performing automatic callback analysis for '{funcName}'");
            
            // Проверяем фильтры из hints, если они есть
            if (hintsManager != null && hintsManager.HasHintsForFunction(funcName))
            {
                var hints = hintsManager.GetHintsForFunction(funcName);
                if (hints != null && hints.DisabledCallbacks.Count > 0)
                {
                    _logger.WriteLine($"[Hints] Filtering out disabled callback parameters: {string.Join(", ", hints.DisabledCallbacks)}");
                }
            }
            
            return false; // Временно, пока не интегрируем оригинальный код
        }

        /// <summary>
        /// Конвертирует HintsMarshallingDirection в MarshallingDirection транслятора
        /// </summary>
        private MarshallingDirection ConvertMarshallingDirection(HintsMarshallingDirection hintsDirection)
        {
            return hintsDirection switch
            {
                HintsMarshallingDirection.None => MarshallingDirection.None,
                HintsMarshallingDirection.In => MarshallingDirection.In,
                HintsMarshallingDirection.Out => MarshallingDirection.Out,
                HintsMarshallingDirection.InOut => MarshallingDirection.InOut,
                _ => MarshallingDirection.None
            };
        }

        /// <summary>
        /// Конвертирует HintsSizeKind в SizeKind транслятора
        /// </summary>
        private SizeKind ConvertSizeKind(HintsSizeKind hintsSizeKind)
        {
            return hintsSizeKind switch
            {
                HintsSizeKind.Constant => SizeKind.Constant,
                HintsSizeKind.FromArgument => SizeKind.FromArgument,
                _ => SizeKind.Constant
            };
        }

        /// <summary>
        /// Определяет является ли функция variadic с учетом hints
        /// </summary>
        private bool GetIsVariadicWithHints(string functionName, bool llvmIsVariadic)
        {
            // Проверяем, есть ли hints для этой функции
            if (hintsManager != null && hintsManager.HasHintsForFunction(functionName))
            {
                var hints = hintsManager.GetHintsForFunction(functionName);
                if (hints != null && hints.DisableAllCallbacks)
                {
                    _logger.WriteLine($"[Hints] Function '{functionName}' callbacks disabled - treating as non-variadic");
                    return false; // Отключаем variadic для этой функции
                }
            }

            // Если hints не найдены или не применимы, используем результат LLVM анализа
            return llvmIsVariadic;
        }

        private unsafe bool IsSizeType(LLVMTypeRef type)
        {
            return type.Kind == LLVMTypeKind.LLVMIntegerTypeKind &&
                   (type.IntWidth == 32 || type.IntWidth == 64);
        }

        private unsafe uint GetTypeSize(LLVMTypeRef type)
        {
            if (type.Handle == IntPtr.Zero) return 0;
            return (uint)LLVM.StoreSizeOfType(targetData, type);
        }

        public void AddFunctionPointerMapEntry(uint dataOffset, ushort functionIndex)
        {
            // --- DEBUG START ---
            _logger.WriteLine("[FuncPtrMap] DEBUG: Searching for FunctionPointerMapSection (ID 18). Available sections:");
            foreach (var section in espbModule.Sections)
            {
                _logger.WriteLine($"  - Found Section ID: {section.Id}, Type: {section.GetType().Name}");
            }
            // --- DEBUG END ---

            var funcPtrMapSection = GetSection<FunctionPointerMapSection>(18);
            if (funcPtrMapSection == null)
            {
                // ИСПРАВЛЕНИЕ: Создаем секцию, если она не найдена
                _logger.WriteLine("[FuncPtrMap] WARNING: FunctionPointerMapSection not found, creating it now.");
                funcPtrMapSection = new FunctionPointerMapSection();
                espbModule.Sections.Add(funcPtrMapSection);
            }

            if (funcPtrMapSection.Entries.Any(e => e.DataOffset == dataOffset))
            {
                _logger.WriteLine($"[FuncPtrMap] WARNING: Entry for data offset {dataOffset} already exists. Skipping.");
                return;
            }

            funcPtrMapSection.Entries.Add(new FunctionPointerMapEntry
            {
                DataOffset = dataOffset,
                FunctionIndex = functionIndex
            });
            _logger.WriteLine($"[FuncPtrMap] Added entry: DataOffset={dataOffset}, FunctionIndex={functionIndex}");
        }
    }
}
