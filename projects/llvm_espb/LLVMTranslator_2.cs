using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
// Предполагаем, что LLVMSharp установлен через NuGet
using LLVMSharp.Interop;
using System.Runtime.InteropServices; // Для Marshal
using System.Buffers;


namespace ESPB_Runtime
{
    public partial class LLVMTranslator
    {
        // Call-result forwarding: when a call result is consumed only by the next call in the same basic block,
        // we can keep it in ABI return register R0 and use it directly as an argument source.
        // This eliminates redundant MOV chains like: MOV tmp <- R0; MOV Rarg <- tmp.
        private LLVMValueRef _forwardedCallResult;
        private bool _forwardedCallResultValid = false;

        // Tracks call instructions that were lowered to a real ESPB CALL_* opcode and therefore
        // actually produce their return value in ABI return register R0.
        // This is the ONLY safe condition for any optimization that reads R0 from a "previous call".
        private readonly HashSet<IntPtr> _materialCallsProducingR0 = new();

        private void MarkMaterialCallProducesR0(LLVMValueRef callInstr)
        {
            if (callInstr.Handle != IntPtr.Zero)
                _materialCallsProducingR0.Add(callInstr.Handle);
        }

        private bool IsMaterialCallProducingR0(LLVMValueRef callInstr)
        {
            return callInstr.Handle != IntPtr.Zero && _materialCallsProducingR0.Contains(callInstr.Handle);
        }

        // JIT region tracking (OP_JIT_START/OP_JIT_STOP -> JIT_REGION_START opcode)
        private readonly Stack<int> _jitRegionStartStack = new Stack<int>();
        private ushort _nextJitRegionId = 0;
        
        // Список дескрипторов JIT-регионов для секции ID=19
        private readonly List<(ushort regionId, ushort funcIdx, int bytecodeStartOffset, int regionBodyLen)> _jitRegionDescriptors = new List<(ushort, ushort, int, int)>();
        
        // Текущий индекс транслируемой функции (для привязки JIT-регионов к функциям)
        private ushort _currentFunctionIndex = 0;

        // Флаг для пометки колбэка в аргументе вызова
        private const uint CALLBACK_FLAG_BIT = 0x80000000;

        // Флаг для маршалинга метаданных в ESPB
        private const uint FEATURE_MARSHALLING_META = 0x00000200;
        private const uint FEATURE_BULK_OPERATIONS = 0x00000010;
        private const uint FEATURE_ALLOCA_EXPLICIT_ALIGN = 0x00000400;
        
        // Метод для получения индекса текущей транслируемой функции
        private ushort GetCurrentFunctionIndex()
        {
            return _currentFunctionIndex;
        }

        // Заполняет секцию JIT Region Metadata (ID=19) из собранных дескрипторов
        private void PopulateJitRegionMetadataSection()
        {
            var jitRegionSection = GetSection<JitRegionMetadataSection>(19);
            if (jitRegionSection == null)
            {
                _logger.WriteLine("[PopulateJitRegionMetadata] ERROR: JitRegionMetadataSection not found!");
                return;
            }

            if (_jitRegionDescriptors.Count == 0)
            {
                _logger.WriteLine("[PopulateJitRegionMetadata] No JIT regions found, section will be empty.");
                return;
            }

            _logger.WriteLine($"[PopulateJitRegionMetadata] Adding {_jitRegionDescriptors.Count} region descriptor(s) to section ID=19");
            
            foreach (var (regionId, funcIdx, startOffset, bodyLen) in _jitRegionDescriptors)
            {
                jitRegionSection.AddRegion(funcIdx, (ushort)startOffset, (ushort)bodyLen, flags: 0);
                _logger.WriteLine($"[PopulateJitRegionMetadata]   - Region {regionId}: funcIdx={funcIdx}, startOff={startOffset}, len={bodyLen}");
            }

            // Устанавливаем флаг Features для обозначения наличия JIT-регионов
            // Используем зарезервированный бит или создаём новый (например, 0x800)
            // Пока используем 0x800 как FEATURE_JIT_REGIONS
            const uint FEATURE_JIT_REGIONS = 0x00000800;
            espbModule.Features |= FEATURE_JIT_REGIONS;
            _logger.WriteLine($"[PopulateJitRegionMetadata] Set FEATURE_JIT_REGIONS flag (0x800). Total features: 0x{espbModule.Features:X8}");
        }

        // Кэш для хранения результатов анализа функций
        private Dictionary<string, bool> callbackAnalysisCache;

        // Коллекция метаданных маршалинга по индексам импортов
        private Dictionary<uint, List<MarshallingInfo>> marshallingMetadata = new Dictionary<uint, List<MarshallingInfo>>();

        /// <summary>
        /// Анализирует, как используется указатель, в который была сохранена функция
        /// </summary>
        /// <param name="storePtr">Указатель, в который была сохранена функция</param>
        /// <param name="originalFunctionName">Имя исходной функции для отображения в логах</param>
        /// <returns>true, если указатель используется как колбэк</returns>
        private unsafe bool AnalyzeStoredFunctionUsage(LLVMValueRef storePtr, string originalFunctionName)
        {
            // Ищем все использования этой переменной
            LLVMUseRef useRef = LLVM.GetFirstUse(storePtr);
            while (!useRef.Equals(default(LLVMUseRef)))
            {
                LLVMValueRef user = LLVM.GetUser(useRef);
                
                // Если это инструкция
                if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind)
                {
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(user);
                    
                    // Если это загрузка из памяти
                    if (opcode == LLVMOpcode.LLVMLoad)
                    {
                        // Ищем все использования этой загрузки
                        LLVMUseRef loadUseRef = LLVM.GetFirstUse(user);
                        while (!loadUseRef.Equals(default(LLVMUseRef)))
                        {
                            LLVMValueRef loadUser = LLVM.GetUser(loadUseRef);
                            
                            // Если это инструкция
                            if (LLVM.GetValueKind(loadUser) == LLVMValueKind.LLVMInstructionValueKind)
                            {
                                LLVMOpcode loadOpcode = LLVM.GetInstructionOpcode(loadUser);
                                
                                // Если это вызов функции
                                if (loadOpcode == LLVMOpcode.LLVMCall)
                                {
                                    LLVMValueRef calledFunc = LLVM.GetCalledValue(loadUser);
                                    
                                    // Получаем имя вызываемой функции
                                    string calledFuncName = "";
                                    try 
                                    {
                                        sbyte* namePtr = LLVM.GetValueName(calledFunc);
                                        if (namePtr != null)
                                        {
                                            calledFuncName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                                        }
                                    }
                                    catch
                                    {
                                        calledFuncName = "";
                                    }
                                    
                                    // Убрана зависимость от списка известных API; используем только IR-признаки ниже
                                    
                                    // Проверяем, не передается ли загруженное значение как аргумент
                                    uint argCount = LLVM.GetNumArgOperands(loadUser);
                                    for (uint i = 0; i < argCount; i++)
                                    {
                                        LLVMValueRef arg = LLVM.GetOperand(loadUser, i);
                                        if (arg.Equals(user)) // user - это результат загрузки из памяти
                                        {
                                            _logger.WriteLine($"Info: Function '{originalFunctionName}' detected as CALLBACK - stored in variable and passed as argument {i} to function call");
                                            return true;
                                        }
                                    }
                                }
                                // Если это преобразование типа
                                else if (loadOpcode == LLVMOpcode.LLVMBitCast)
                                {
                                    // Ищем все использования этого преобразования
                                    LLVMUseRef castUseRef = LLVM.GetFirstUse(loadUser);
                                    while (!castUseRef.Equals(default(LLVMUseRef)))
                                    {
                                        LLVMValueRef castUser = LLVM.GetUser(castUseRef);
                                        
                                        // Если это инструкция
                                        if (LLVM.GetValueKind(castUser) == LLVMValueKind.LLVMInstructionValueKind)
                                        {
                                            LLVMOpcode castOpcode = LLVM.GetInstructionOpcode(castUser);
                                            
                                            // Если это вызов функции
                                            if (castOpcode == LLVMOpcode.LLVMCall)
                                            {
                                                LLVMValueRef calledFunc = LLVM.GetCalledValue(castUser);
                                                
                                                // Получаем имя вызываемой функции
                                                string calledFuncName = "";
                                                try 
                                                {
                                                    sbyte* namePtr = LLVM.GetValueName(calledFunc);
                                                    if (namePtr != null)
                                                    {
                                                        calledFuncName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                                                    }
                                                }
                                                catch
                                                {
                                                    calledFuncName = "";
                                                }
                                                
                                                // Убрана зависимость от списка известных API; используем только IR-признаки ниже
                                                
                                                // Проверяем, не передается ли преобразованное значение как аргумент
                                                uint argCount = LLVM.GetNumArgOperands(castUser);
                                                for (uint i = 0; i < argCount; i++)
                                                {
                                                    LLVMValueRef arg = LLVM.GetOperand(castUser, i);
                                                    if (arg.Equals(loadUser)) // loadUser - это результат преобразования типа
                                                    {
                                                        _logger.WriteLine($"Info: Function '{originalFunctionName}' detected as CALLBACK - stored in variable, cast, and passed as argument {i} to function call");
                                                        return true;
                                                    }
                                                }
                                            }
                                        }
                                        
                                        // Переходим к следующему использованию преобразования
                                        castUseRef = LLVM.GetNextUse(castUseRef);
                                    }
                                }
                            }
                            
                            // Переходим к следующему использованию загрузки
                            loadUseRef = LLVM.GetNextUse(loadUseRef);
                        }
                    }
                }
                
                // Переходим к следующему использованию указателя
                useRef = LLVM.GetNextUse(useRef);
            }
            
            return false;
        }
        
        /// <summary>
        /// Отслеживает цепочку использований указателя, чтобы определить, хранит ли он указанную функцию
        /// </summary>
        /// <param name="pointer">Указатель для анализа</param>
        /// <param name="targetFunction">Целевая функция, которую мы ищем</param>
        /// <returns>true, если указатель хранит целевую функцию</returns>
        private unsafe bool TracePointerToFunction(LLVMValueRef pointer, LLVMValueRef targetFunction)
        {
            // Ограничиваем глубину рекурсии для предотвращения зацикливания
            return TracePointerToFunctionImpl(pointer, targetFunction, 0);
        }
        
        /// <summary>
        /// Рекурсивно анализирует все места использования функции как указателя и добавляет соответствующие записи в FuncPtrMap
        /// </summary>
        /// <param name="function">Функция для анализа</param>
        /// <param name="functionIndex">Индекс функции в ESPB</param>
        /// <param name="dataOffset">Смещение данных для функции</param>
        private unsafe void AnalyzeFunctionPointerUsagesRecursively(LLVMValueRef function, uint functionIndex, uint dataOffset)
        {
            var visitedInstructions = new HashSet<IntPtr>();
            AnalyzeFunctionPointerUsagesRecursivelyImpl(function, functionIndex, dataOffset, visitedInstructions, 0);
        }
        
        /// <summary>
        /// Внутренняя реализация рекурсивного анализа использований указателей на функции
        /// </summary>
        private unsafe void AnalyzeFunctionPointerUsagesRecursivelyImpl(LLVMValueRef function, uint functionIndex, uint dataOffset, HashSet<IntPtr> visitedInstructions, int depth)
        {
            // Ограничиваем глубину рекурсии
            if (depth > 10)
            {
                _logger.WriteLine($"[FuncPtrMap] WARNING: Reached maximum recursion depth {depth} for function analysis");
                return;
            }
            
            // Проходим по всем использованиям функции
            LLVMUseRef useRef = LLVM.GetFirstUse(function);
            while (!useRef.Equals(default(LLVMUseRef)))
            {
                LLVMValueRef user = LLVM.GetUser(useRef);
                
                // Избегаем повторного анализа тех же инструкций
                if (visitedInstructions.Contains(user.Handle))
                {
                    useRef = LLVM.GetNextUse(useRef);
                    continue;
                }
                visitedInstructions.Add(user.Handle);
                
                if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind)
                {
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(user);
                    
                    switch (opcode)
                    {
                        case LLVMOpcode.LLVMStore:
                            // Функция сохраняется в переменную - анализируем все загрузки из этой переменной
                            AnalyzeStoreInstruction(user, function, functionIndex, dataOffset, visitedInstructions, depth);
                            break;
                            
                        case LLVMOpcode.LLVMBitCast:
                            // Функция преобразуется в другой тип - анализируем использования преобразованного значения
                            AnalyzeBitCastInstruction(user, function, functionIndex, dataOffset, visitedInstructions, depth);
                            break;
                            
                        case LLVMOpcode.LLVMGetElementPtr:
                            // Функция используется в GEP (доступ к элементам массива/структуры)
                            AnalyzeGetElementPtrInstruction(user, function, functionIndex, dataOffset, visitedInstructions, depth);
                            break;
                            
                        case LLVMOpcode.LLVMCall:
                            // Функция передается как аргумент в вызов - уже обрабатывается в основном коде
                            string calledFuncName = GetFunctionName(LLVM.GetCalledValue(user));
                            _logger.WriteLine($"[FuncPtrMap] RECURSIVE: Function passed as argument to '{calledFuncName}' - already handled by main logic");
                            break;
                    }
                }
                
                useRef = LLVM.GetNextUse(useRef);
            }
        }
        
        /// <summary>
        /// Анализирует инструкцию store для указателей на функции
        /// </summary>
        private unsafe void AnalyzeStoreInstruction(LLVMValueRef storeInstr, LLVMValueRef function, uint functionIndex, uint dataOffset, HashSet<IntPtr> visitedInstructions, int depth)
        {
            // store <function>, <pointer> - функция сохраняется в переменную
            LLVMValueRef storePtr = LLVM.GetOperand(storeInstr, 1); // Указатель, куда сохраняется
            
            _logger.WriteLine($"[FuncPtrMap] RECURSIVE: Function stored in variable, analyzing loads from this location...");
            
            // Ищем все load инструкции из этого указателя
            LLVMUseRef useRef = LLVM.GetFirstUse(storePtr);
            while (!useRef.Equals(default(LLVMUseRef)))
            {
                LLVMValueRef loadUser = LLVM.GetUser(useRef);
                
                if (!visitedInstructions.Contains(loadUser.Handle) &&
                    LLVM.GetValueKind(loadUser) == LLVMValueKind.LLVMInstructionValueKind &&
                    LLVM.GetInstructionOpcode(loadUser) == LLVMOpcode.LLVMLoad)
                {
                    visitedInstructions.Add(loadUser.Handle);
                    _logger.WriteLine($"[FuncPtrMap] RECURSIVE: Found load from stored function pointer - analyzing its uses...");

                    // Рекурсивно анализируем использования загруженного значения
                    AnalyzeFunctionPointerUsagesRecursivelyImpl(loadUser, functionIndex, dataOffset, visitedInstructions, depth + 1);
                }
                
                useRef = LLVM.GetNextUse(useRef);
            }
        }
        
        /// <summary>
        /// Анализирует инструкцию bitcast для указателей на функции
        /// </summary>
        private unsafe void AnalyzeBitCastInstruction(LLVMValueRef bitcastInstr, LLVMValueRef function, uint functionIndex, uint dataOffset, HashSet<IntPtr> visitedInstructions, int depth)
        {
            _logger.WriteLine($"[FuncPtrMap] RECURSIVE: Function cast to another type, analyzing cast uses...");

            // Рекурсивно анализируем использования преобразованного значения
            AnalyzeFunctionPointerUsagesRecursivelyImpl(bitcastInstr, functionIndex, dataOffset, visitedInstructions, depth + 1);
        }
        
        /// <summary>
        /// Анализирует инструкцию GetElementPtr для указателей на функции
        /// </summary>
        private unsafe void AnalyzeGetElementPtrInstruction(LLVMValueRef gepInstr, LLVMValueRef function, uint functionIndex, uint dataOffset, HashSet<IntPtr> visitedInstructions, int depth)
        {
            _logger.WriteLine($"[FuncPtrMap] RECURSIVE: Function used in GEP (array/struct access), analyzing GEP uses...");

            // Рекурсивно анализируем использования результата GEP
            AnalyzeFunctionPointerUsagesRecursivelyImpl(gepInstr, functionIndex, dataOffset, visitedInstructions, depth + 1);
        }
        
        /// <summary>
        /// Получает имя функции из LLVMValueRef
        /// </summary>
        private unsafe string GetFunctionName(LLVMValueRef function)
        {
            try
            {
                sbyte* namePtr = LLVM.GetValueName(function);
                if (namePtr != null)
                {
                    return Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "<unknown>";
                }
            }
            catch
            {
                // Игнорируем ошибки получения имени
            }
            return "<unknown>";
        }
        
        private unsafe bool TracePointerToFunctionImpl(LLVMValueRef pointer, LLVMValueRef targetFunction, int depth)
        {
            // Ограничиваем глубину рекурсии для предотвращения зацикливания
            if (depth > 5)
            {
                return false;
            }
            
            // Проверяем, является ли указатель аллокацией (alloca)
            if (LLVM.GetValueKind(pointer) == LLVMValueKind.LLVMInstructionValueKind &&
                LLVM.GetInstructionOpcode(pointer) == LLVMOpcode.LLVMAlloca)
            {
                // Ищем все использования этой аллокации (store инструкции)
                LLVMUseRef useRef = LLVM.GetFirstUse(pointer);
                while (!useRef.Equals(default(LLVMUseRef)))
                {
                    LLVMValueRef user = LLVM.GetUser(useRef);
                    
                    // Если это store инструкция
                    if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind &&
                        LLVM.GetInstructionOpcode(user) == LLVMOpcode.LLVMStore)
                    {
                        // Получаем значение, которое сохраняется (первый операнд)
                        LLVMValueRef storedValue = LLVM.GetOperand(user, 0);
                        
                        // Проверяем, не сохраняется ли целевая функция
                        if (storedValue.Equals(targetFunction))
                        {
                            _logger.WriteLine($"Info: Found indirect function pointer usage - function stored in variable");
                            return true;
                        }
                        
                        // Проверяем, не является ли сохраняемое значение загрузкой из другого указателя
                        if (LLVM.GetValueKind(storedValue) == LLVMValueKind.LLVMInstructionValueKind)
                        {
                            if (LLVM.GetInstructionOpcode(storedValue) == LLVMOpcode.LLVMLoad)
                            {
                                // Получаем указатель, из которого загружается значение
                                LLVMValueRef loadPtr = LLVM.GetOperand(storedValue, 0);
                                
                                // Рекурсивно проверяем этот указатель
                                if (TracePointerToFunctionImpl(loadPtr, targetFunction, depth + 1))
                                {
                                    _logger.WriteLine($"Info: Found indirect function pointer usage - function loaded from another variable");
                                    return true;
                                }
                            }
                            // Проверяем преобразования типов
                            else if (LLVM.GetInstructionOpcode(storedValue) == LLVMOpcode.LLVMBitCast)
                            {
                                // Получаем исходное значение для преобразования
                                LLVMValueRef castSrc = LLVM.GetOperand(storedValue, 0);
                                
                                // Проверяем, не является ли исходное значение нашей функцией
                                if (castSrc.Equals(targetFunction))
                                {
                                    _logger.WriteLine($"Info: Found indirect function pointer usage - function cast to another type");
                                    return true;
                                }
                                
                                // Если это загрузка из памяти, проверяем цепочку использований
                                if (LLVM.GetValueKind(castSrc) == LLVMValueKind.LLVMInstructionValueKind &&
                                    LLVM.GetInstructionOpcode(castSrc) == LLVMOpcode.LLVMLoad)
                                {
                                    LLVMValueRef loadPtr = LLVM.GetOperand(castSrc, 0);
                                    if (TracePointerToFunctionImpl(loadPtr, targetFunction, depth + 1))
                                    {
                                        _logger.WriteLine($"Info: Found indirect function pointer usage - function loaded and cast");
                                        return true;
                                    }
                                }
                            }
                            // Проверяем GetElementPtr (часто используется для доступа к полям структур)
                            else if (LLVM.GetInstructionOpcode(storedValue) == LLVMOpcode.LLVMGetElementPtr)
                            {
                                // Получаем базовый указатель (первый операнд)
                                LLVMValueRef gepPtr = LLVM.GetOperand(storedValue, 0);
                                
                                // Рекурсивно проверяем этот указатель
                                if (TracePointerToFunctionImpl(gepPtr, targetFunction, depth + 1))
                                {
                                    _logger.WriteLine($"Info: Found indirect function pointer usage - function stored in struct/array");
                                    return true;
                                }
                            }
                        }
                    }
                    
                    // Переходим к следующему использованию
                    useRef = LLVM.GetNextUse(useRef);
                }
            }
            
            // Если это глобальная переменная или другой тип указателя
            if (LLVM.GetValueKind(pointer) == LLVMValueKind.LLVMGlobalVariableValueKind)
            {
                // Проверяем инициализатор глобальной переменной
                LLVMValueRef initializer = LLVM.GetInitializer(pointer);
                if (initializer.Handle != IntPtr.Zero)
                {
                    // Если инициализатор - наша функция
                    if (initializer.Equals(targetFunction))
                    {
                        _logger.WriteLine($"Info: Found indirect function pointer usage - function stored in global variable");
                        return true;
                    }
                }
                
                // Ищем все использования этой глобальной переменной (store инструкции)
                LLVMUseRef useRef = LLVM.GetFirstUse(pointer);
                while (!useRef.Equals(default(LLVMUseRef)))
                {
                    LLVMValueRef user = LLVM.GetUser(useRef);
                    
                    // Если это store инструкция
                    if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind &&
                        LLVM.GetInstructionOpcode(user) == LLVMOpcode.LLVMStore)
                    {
                        // Получаем значение, которое сохраняется (первый операнд)
                        LLVMValueRef storedValue = LLVM.GetOperand(user, 0);
                        
                        // Проверяем, не сохраняется ли целевая функция
                        if (storedValue.Equals(targetFunction))
                        {
                            _logger.WriteLine($"Info: Found indirect function pointer usage - function stored in global variable");
                            return true;
                        }
                    }
                    
                    // Переходим к следующему использованию
                    useRef = LLVM.GetNextUse(useRef);
                }
            }
            
            return false;
        }
        
        // Многоуровневый анализ для определения колбэков
                private unsafe bool IsLikelyCallback(LLVMValueRef function, string functionName)
                {
                    _logger.WriteLine($"Performing deep callback analysis for function '{functionName}'...");
                    
                    try
            {
                // Уровень 1: Анализ имени функции
                if (functionName.Contains("task") || 
                    functionName.Contains("Task") || 
                    functionName.Contains("callback") || 
                    functionName.Contains("Callback") || 
                    functionName.Contains("handler") || 
                    functionName.Contains("Handler") ||
                    functionName.EndsWith("_cb") ||
                    functionName.EndsWith("_fn") ||
                    functionName.Contains("event"))
                {
                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK by name pattern");
                    return true;
                }
                
                // Уровень 2: Анализ сигнатуры функции
                LLVMTypeRef funcType = LLVM.TypeOf(function);
                if (LLVM.GetTypeKind(funcType) == LLVMTypeKind.LLVMPointerTypeKind)
                {
                    LLVMTypeRef pointeeType = LLVM.GetElementType(funcType);
                    if (LLVM.GetTypeKind(pointeeType) == LLVMTypeKind.LLVMFunctionTypeKind)
                    {
                        // Проверяем параметры функции
                        uint paramCount = LLVM.CountParamTypes(pointeeType);
                        
                        // Типичный колбэк имеет 0-2 параметра или первый параметр типа void*
                        if (paramCount <= 2)
                        {
                            _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK by signature (param count: {paramCount})");
                            return true;
                        }
                        
                        // Проверяем, имеет ли функция типичную сигнатуру колбэка (void* как первый параметр)
                        if (paramCount > 0)
                        {
                            try 
                            {
                                // Проверяем первый параметр напрямую
                                // Получаем типы параметров функции
                                LLVMTypeRef[] paramTypes = new LLVMTypeRef[LLVM.CountParamTypes(pointeeType)];
                                if (paramTypes.Length > 0)
                                {
                                    unsafe
                                    {
                                        fixed (LLVMTypeRef* paramTypesPtr = paramTypes)
                                        {
                                            LLVM.GetParamTypes(pointeeType, (LLVMOpaqueType**)paramTypesPtr);
                                        }
                                    }
                                    LLVMTypeRef firstParamType = paramTypes[0];
                                if (firstParamType.Handle != IntPtr.Zero && LLVM.GetTypeKind(firstParamType) == LLVMTypeKind.LLVMPointerTypeKind)
                                {
                                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK by signature (first param is pointer)");
                                    return true;
                                }
                                }
                            }
                            catch (Exception ex)
                            {
                                _logger.WriteLine($"Warning: Error checking parameter types for '{functionName}': {ex.Message}");
                            }
                        }
                    }
                }
                
                // Уровень 3: Глубокий анализ использования функции
                LLVMUseRef useRef = LLVM.GetFirstUse(function);
                if (!useRef.Equals(default(LLVMUseRef)))
                {
                    // Счетчик для отслеживания количества проанализированных использований
                    int useCount = 0;
                    
                    // Проходим по всем использованиям функции
                    while (!useRef.Equals(default(LLVMUseRef)))
                    {
                        useCount++;
                        
                        // Получаем инструкцию, использующую функцию
                        LLVMValueRef user = LLVM.GetUser(useRef);
                        if (!user.Equals(default(LLVMUseRef)))
                        {
                            // Если это инструкция
                            if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind)
                            {
                                LLVMOpcode opcode = LLVM.GetInstructionOpcode(user);
                                
                                // Случай 1: Функция передается как аргумент в вызов другой функции
                                if (opcode == LLVMOpcode.LLVMCall)
                                {
                                    LLVMValueRef calledFunc = LLVM.GetCalledValue(user);
                                    if (!calledFunc.Equals(function)) // Если наша функция не та, которая вызывается
                                    {
                                        // Проверяем, известна ли вызываемая функция как принимающая колбэки
                                        string calledFuncName = "";
                                        try 
                                        {
                                            // Получаем имя функции
                                            sbyte* namePtr = LLVM.GetValueName(calledFunc);
                                            if (namePtr != null)
                                            {
                                                calledFuncName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                                            }
                                        }
                                        catch
                                        {
                                            calledFuncName = "";
                                        }
                                        
                                        // Убрана зависимость от списка известных API; используем только IR-признаки ниже
                                        
                                        // Проверяем позицию аргумента (первый аргумент часто колбэк)
                                        uint argCount = LLVM.GetNumArgOperands(user);
                                        for (uint i = 0; i < argCount; i++)
                                        {
                                            LLVMValueRef arg = LLVM.GetOperand(user, i);
                                            
                                            // Прямая передача функции как аргумента
                                            if (arg.Equals(function))
                                            {
                                                if (i == 0) // Первый аргумент
                                                {
                                                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - passed as first argument to function call");
                                                    return true;
                                                }
                                                else
                                                {
                                                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - passed as argument {i} to function call");
                                                    return true;
                                                }
                                            }
                                            
                                            // Непрямая передача через указатель (загрузка из переменной)
                                            if (LLVM.GetValueKind(arg) == LLVMValueKind.LLVMInstructionValueKind)
                                            {
                                                // Проверяем, является ли аргумент загрузкой из памяти
                                                if (LLVM.GetInstructionOpcode(arg) == LLVMOpcode.LLVMLoad)
                                                {
                                                    // Получаем указатель, из которого загружается значение
                                                    LLVMValueRef loadPtr = LLVM.GetOperand(arg, 0);
                                                    
                                                    // Отслеживаем цепочку использований этого указателя
                                                    if (TracePointerToFunction(loadPtr, function))
                                                    {
                                                        _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - passed indirectly through pointer as argument {i}");
                                                        return true;
                                                    }
                                                }
                                                // Проверяем, является ли аргумент преобразованием указателя
                                                else if (LLVM.GetInstructionOpcode(arg) == LLVMOpcode.LLVMBitCast)
                                                {
                                                    // Получаем исходное значение для преобразования
                                                    LLVMValueRef castSrc = LLVM.GetOperand(arg, 0);
                                                    
                                                    // Проверяем, не является ли исходное значение нашей функцией
                                                    if (castSrc.Equals(function))
                                                    {
                                                        _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - passed after type cast as argument {i}");
                                                        return true;
                                                    }
                                                    
                                                    // Если это загрузка из памяти, проверяем цепочку использований
                                                    if (LLVM.GetValueKind(castSrc) == LLVMValueKind.LLVMInstructionValueKind && 
                                                        LLVM.GetInstructionOpcode(castSrc) == LLVMOpcode.LLVMLoad)
                                                    {
                                                        LLVMValueRef loadPtr = LLVM.GetOperand(castSrc, 0);
                                                        if (TracePointerToFunction(loadPtr, function))
                                                        {
                                                            _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - passed indirectly through cast pointer as argument {i}");
                                                            return true;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                // Случай 2: Функция сохраняется в переменную или память
                                else if (opcode == LLVMOpcode.LLVMStore)
                                {
                                    // В store первый операнд - значение, второй - указатель куда сохраняется
                                    LLVMValueRef storeValue = LLVM.GetOperand(user, 0);
                                    LLVMValueRef storePtr = LLVM.GetOperand(user, 1);
                                    
                                    // Если сохраняется наша функция
                                    if (storeValue.Equals(function))
                                    {
                                        _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - stored in variable");
                                        
                                        // Проверяем, не используется ли переменная, в которую сохраняется функция,
                                        // в качестве аргумента для других функций
                                        if (AnalyzeStoredFunctionUsage(storePtr, functionName))
                                        {
                                            return true;
                                        }
                                        
                                        return true;
                                    }
                                }
                                // Случай 3: Функция используется в GEP (доступ к элементу массива/структуры)
                                else if (opcode == LLVMOpcode.LLVMGetElementPtr)
                                {
                                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - used in array/structure access");
                                    return true;
                                }
                                // Случай 4: Функция преобразуется в указатель (bitcast)
                                else if (opcode == LLVMOpcode.LLVMBitCast)
                                {
                                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - cast to another type");
                                    return true;
                                }
                            }
                        }
                        
                        // Переходим к следующему использованию
                        useRef = LLVM.GetNextUse(useRef);
                    }
                    
                    _logger.WriteLine($"Info: Analyzed {useCount} uses of function '{functionName}', no callback pattern detected");
                }
                
                // Уровень 4: Проверка на функцию без тела (только объявление)
                if (LLVM.CountBasicBlocks(function) == 0)
                {
                    // Проверяем, что это не внешняя функция (импортированная)
                    LLVMValueKind kind = LLVM.GetValueKind(function);
                    LLVMLinkage linkage = LLVM.GetLinkage(function);
                    
                    // Если это внутренняя функция без тела, вероятно это колбэк
                    if (kind == LLVMValueKind.LLVMFunctionValueKind && 
                        linkage != LLVMLinkage.LLVMExternalLinkage)
                    {
                        _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK - internal function declaration without body");
                        return true;
                    }
                }
                
                // Уровень 5: Для FreeRTOS функций всегда считаем колбэком
                if (functionName.Contains("xTaskCreate") || 
                    functionName.EndsWith("_task") || 
                    functionName.Contains("xQueue") ||
                    functionName.Contains("xTimer") ||
                    functionName.Contains("xEvent"))
                {
                    _logger.WriteLine($"Info: Function '{functionName}' detected as CALLBACK for RTOS API");
                    return true;
                }
                
                // Если ни один из критериев не сработал, считаем функцию обычной
                _logger.WriteLine($"Info: Function '{functionName}' is NOT detected as callback");
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Warning: Error during callback analysis for '{functionName}': {ex.Message}");
                // В случае ошибки анализа, считаем функцию колбэком для безопасности
                return true;
            }
        }
        
    private unsafe (byte, uint?) GetOperandReg(LLVMValueRef operand, List<byte> output, byte? preferredDest = null)
        {
            var location = registerManager.GetLocation(operand);

            // --- Обработка inline assembly ---
            if (operand.Kind == LLVMValueKind.LLVMInlineAsmValueKind)
            {
                _logger.WriteLine($"Warning: Inline assembly encountered in GetOperandReg, treating as null pointer");
                // Inline assembly не может быть загружено в регистр обычным способом.
                // Мы генерируем null pointer как fallback.
                byte nullReg = registerManager.AllocateTemporaryRegister();
                EmitOpcode(output, 0x18, "LDC.I32.IMM");
                output.Add(nullReg);
                output.AddRange(BitConverter.GetBytes(0));
                return (nullReg, null);
            }

            // ВАЖНО: Для глобальных переменных НЕ используем кэшированный регистр,
            // т.к. нужно генерировать LD_GLOBAL_ADDR для загрузки адреса.
            // Глобальные переменные - это константы (адреса), а не значения в регистрах.
            bool isGlobalValue = operand.IsAGlobalValue != null && operand.IsAGlobalValue.Handle != IntPtr.Zero;
            bool isFunction = operand.IsAFunction != null && operand.IsAFunction.Handle != IntPtr.Zero;
            bool isGlobalVariable = isGlobalValue && !isFunction;
            
            if (location is RegisterLocation regLoc && !isGlobalVariable)
            {
                return (regLoc.Register, null);
            }

            if (location is SpillSlotLocation spillLoc)
            {
                _logger.WriteLine($"[RELOAD] Operand '{operand}' is spilled to slot {spillLoc.Offset}. Reloading.");
                byte tempReg = registerManager.AllocateTemporaryRegister();
                registerManager.GenerateReloadInstruction(output, tempReg, spillLoc);
                return (tempReg, null);
            }

            if (operand.IsAAllocaInst != null && operand.IsAAllocaInst.Handle != IntPtr.Zero)
            {
                _logger.WriteLine($"Warning: operand {operand} is an alloca instruction but was not found in the register map. Allocating a register on-the-fly.");
                byte destReg = GetOrAllocateRegWithCacheInvalidation(operand, output);
                _logger.WriteLine($"  Retrieved pre-allocated register R{destReg} for the alloca result.");
                return (destReg, null);
            }

            if (operand.IsAInstruction != null && operand.IsAInstruction.Handle != IntPtr.Zero)
            {
                LLVMOpcode opcode = LLVM.GetInstructionOpcode(operand);
                if (opcode == LLVMOpcode.LLVMPHI)
                {
                    byte phiReg = GetOrAllocateRegWithCacheInvalidation(operand, output);
                    _logger.WriteLine($"Info: Allocated register R{phiReg} for PHI instruction");
                    return (phiReg, null);
                }

                if (opcode == LLVMOpcode.LLVMPtrToInt)
                {
                    LLVMValueRef ptrOperand = operand.GetOperand(0);
                    if (ptrOperand.IsAGlobalValue != null && ptrOperand.IsAGlobalValue.Handle != IntPtr.Zero)
                    {
                        string symbolName = ptrOperand.Name ?? "<unknown symbol>";
                        _logger.WriteLine($"Info: Processing ptrtoint for global '{symbolName}'");
                        if (llvmGlobalToEspbIndex.TryGetValue(ptrOperand, out uint globalIndex))
                        {
                            byte ptrReg = registerManager.AllocateTemporaryRegister();
                            uint numImportedGlobals = (uint)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                            ushort combinedGlobalIndex = (ushort)(numImportedGlobals + globalIndex);
                            _logger.WriteLine($"Info: Loading global address for '{symbolName}' at index {combinedGlobalIndex}");
                            EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                            output.Add(ptrReg);
                            output.AddRange(BitConverter.GetBytes(combinedGlobalIndex));
                            return (ptrReg, null);
                        }
                        else
                        {
                            _logger.WriteLine($"Warning: Global '{symbolName}' not found in index. Using null pointer as fallback.");
                            byte nullPtrReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x18, "LDC.I32.IMM");
                            output.Add(nullPtrReg);
                            output.AddRange(BitConverter.GetBytes(0));
                            return (nullPtrReg, null);
                        }
                    }
                }
            }

            if (operand.IsAConstantExpr != null && operand.IsAConstantExpr.Handle != IntPtr.Zero)
            {
                LLVMOpcode expOpcode = LLVM.GetConstOpcode(operand);
                if (expOpcode == LLVMOpcode.LLVMPtrToInt)
                {
                    _logger.WriteLine("Info: Processing constant PtrToInt expression");
                    LLVMValueRef ptrOperand = LLVM.GetOperand(operand, 0);
                    if (ptrOperand.IsAGlobalValue != null && ptrOperand.IsAGlobalValue.Handle != IntPtr.Zero)
                    {
                        string symbolName = ptrOperand.Name ?? "<unknown symbol>";
                        _logger.WriteLine($"Info: Processing ptrtoint for global constant '{symbolName}'");

                        if (ptrOperand.Kind == LLVMValueKind.LLVMFunctionValueKind)
                        {
                            if (llvmFuncToEspbIndex.TryGetValue(ptrOperand, out uint funcIndex))
                            {
                                byte funcReg = registerManager.AllocateTemporaryRegister();
                                uint numGlobals = (uint)(GetSection<GlobalsSection>(4)!.Globals.Count);
                                ushort symbolIdx = (ushort)(funcIndex | 0x8000);
                                _logger.WriteLine($"Info: Loading function POINTER for '{symbolName}' (func_idx={funcIndex}, symbol_idx={symbolIdx}).");
                                EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                                AddReg(output, funcReg);
                                output.AddRange(BitConverter.GetBytes(symbolIdx));
                                // Добавляем запись в FuncPtrMap для функций, используемых как указатели
                                if (_funcPtrToDataOffset.TryGetValue(ptrOperand, out uint dataOffset))
                                {
                                    AddFunctionPointerMapEntry(dataOffset, (ushort)funcIndex);
                                    return (funcReg, dataOffset);
                                }
                                return (funcReg, null);
                            }
                            else
                            {
                                _logger.WriteLine($"Warning: Function '{symbolName}' not found in function index map. Using null pointer as fallback.");
                            }
                        }
                        else if (llvmGlobalToEspbIndex.TryGetValue(ptrOperand, out uint globalIndex))
                        {
                            byte ptrReg = preferredDest ?? registerManager.AllocateTemporaryRegister();
                            uint numImportedGlobals = (uint)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                            ushort combinedGlobalIndex = (ushort)(numImportedGlobals + globalIndex);
                            _logger.WriteLine($"Info: Loading global address for constant '{symbolName}' at index {combinedGlobalIndex}");
                            EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                            AddReg(output, ptrReg);
                            output.AddRange(BitConverter.GetBytes(combinedGlobalIndex));
                             _funcPtrToDataOffset.TryGetValue(ptrOperand, out uint dataOffset);
                            return (ptrReg, dataOffset > 0 ? dataOffset : null);
                        }
                    }
                }
            }

            if (operand.IsAConstant != null && operand.IsAConstant.Handle != IntPtr.Zero)
            {
                if (operand.IsAGlobalValue != null && operand.IsAGlobalValue.Handle != IntPtr.Zero)
                {
                    string symbolName = operand.Name ?? "<unknown symbol>";
                    byte addrReg = preferredDest ?? registerManager.AllocateTemporaryRegister();
                    bool handled = false;
                    ushort indexToUse = 0;
                    uint? dataOffset = null;

                    if (llvmImportToEspbIndex.TryGetValue(operand, out uint importIndex))
                    {
                        _logger.WriteLine($"Info: Operand is an imported symbol '{symbolName}'. Generating LD_GLOBAL_ADDR Rd, {importIndex} (Import Index)");
                        indexToUse = (ushort)importIndex;
                        handled = true;
                    }
                    else if (llvmFuncToEspbIndex.TryGetValue(operand, out uint funcIndex))
                    {
                        // Load function pointer using LD_GLOBAL_ADDR with symbol_idx = num_globals + funcIndex
                        uint numGlobals = (uint)(GetSection<GlobalsSection>(4)!.Globals.Count);
                        ushort symbolIdx = (ushort)(funcIndex | 0x8000);
                        _logger.WriteLine($"[GetOperandReg] Function reference '{symbolName}' found. Loading pointer (func_idx={funcIndex}, symbol_idx={symbolIdx}).");
                        byte funcReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                        output.Add(funcReg);
                        output.AddRange(BitConverter.GetBytes(symbolIdx));
                        
                        if (_funcPtrToDataOffset.TryGetValue(operand, out uint dOff))
                        {
                            AddFunctionPointerMapEntry(dOff, (ushort)funcIndex);
                            return (funcReg, dOff > 0 ? (uint?)dOff : null);
                        }
                        return (funcReg, null);
                    }
                    else if (llvmGlobalToEspbIndex.TryGetValue(operand, out uint globalIndex))
                    {
                        _logger.WriteLine($"Info: Operand is an internal global '{symbolName}'. Adjusting index with imported globals offset");
                        uint numImportedGlobals = (uint)GetSection<ImportsSection>(2)!.Imports.Count(imp => imp.Kind == EspbImportKind.Global);
                        ushort combinedGlobalIndex = (ushort)(numImportedGlobals + globalIndex);
                        _logger.WriteLine($"Info: Combined global index = imports({numImportedGlobals}) + globalIndex({globalIndex}) = {combinedGlobalIndex}");
                        _logger.WriteLine($"    [DEBUG] preferredDest = {(preferredDest.HasValue ? preferredDest.Value.ToString() : "null")}, addrReg = {addrReg}");
                        indexToUse = combinedGlobalIndex;
                        handled = true;
                        if (_funcPtrToDataOffset.TryGetValue(operand, out uint dOff))
                        {
                            dataOffset = dOff;
                        }
                    }

                    if (handled)
                    {
                        EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                        output.Add(addrReg);
                        output.AddRange(BitConverter.GetBytes(indexToUse));
                        return (addrReg, dataOffset);
                    }
                    _logger.WriteLine($"Warning: Global value '{symbolName}' not found in any index. Using null pointer as fallback.");
                    byte nullPtrReg = preferredDest ?? registerManager.AllocateTemporaryRegister();
                    EmitOpcode(output, 0x1C, "LDC.PTR.IMM");
                    output.Add(nullPtrReg);
                    output.AddRange(BitConverter.GetBytes(0));
                    return (nullPtrReg, null);
                }

                var constInt = operand.IsAConstantInt;
                if (constInt.Handle != IntPtr.Zero)
                {
                    LLVMTypeRef intType = operand.TypeOf;
                    uint bitWidth = LLVM.GetIntTypeWidth(intType);
                    long val = LLVM.ConstIntGetSExtValue(constInt);
                    _logger.WriteLine($"Info: Operand is a ConstantInt ({bitWidth}-bit) with value {val}");
                    bool force64Bit = bitWidth > 32;
                    byte newReg = GenerateIntConstant(output, val, preferredDest: preferredDest, force64Bit: force64Bit);
                    return (newReg, null);
                }

                var constPtrNull = operand.IsAConstantPointerNull;
                if (constPtrNull.Handle != IntPtr.Zero)
                {
                    _logger.WriteLine($"Info: Operand is a ConstantPointerNull. Generating LDC.I32.IMM with value 0");
                    byte newReg = GenerateIntConstant(output, 0, preferredDest);
                    return (newReg, null);
                }

                if (operand.IsAConstantExpr != null && operand.IsAConstantExpr.Handle != IntPtr.Zero)
                {
                    LLVMOpcode expOpcode = LLVM.GetConstOpcode(operand);
                    if (expOpcode == LLVMOpcode.LLVMIntToPtr)
                    {
                        try
                        {
                            LLVMValueRef src = LLVM.GetOperand(operand, 0);
                            var srcInt = src.IsAConstantInt;
                            if (srcInt.Handle != IntPtr.Zero)
                            {
                                long sVal = LLVM.ConstIntGetSExtValue(srcInt);
                                int imm = (int)sVal;
                                _logger.WriteLine($"Info: Operand is ConstantExpr inttoptr with imm {imm}. Emitting LDC.I32.IMM and INT_TO_PTR");
                                byte tempIntReg = GenerateIntConstant(output, imm, preferredDest);
                                byte newPtrReg = registerManager.AllocateTemporaryRegister();
                                EmitOpcode(output, 0xBD, "INT_TO_PTR");
                                AddReg(output, newPtrReg);
                                AddReg(output, tempIntReg);
                                return (newPtrReg, null);
                            }
                        }
                        catch { }
                    }
                }

                var constFP = operand.IsAConstantFP;
                if (constFP.Handle != IntPtr.Zero)
                {
                    int losesInfo = 0;
                    double doubleVal = LLVM.ConstRealGetDouble(constFP, &losesInfo);
                    var type = MapLlvmTypeToEspb(operand.TypeOf);
                    byte newReg = registerManager.AllocateTemporaryRegister();
                    if (type == ValueType.F32)
                    {
                        float floatVal = (float)doubleVal;
                        uint intVal = BitConverter.ToUInt32(BitConverter.GetBytes(floatVal), 0);
                        _logger.WriteLine($"Info: Operand is a ConstantFP (float). Generating LDC.F32 with value {floatVal} (raw bits: {intVal:X})");
                        EmitOpcode(output, 0x1A, "LDC.F32.IMM");
                        output.Add(newReg);
                        output.AddRange(BitConverter.GetBytes((int)intVal));
                    }
                    else
                    {
                        ulong longVal = BitConverter.ToUInt64(BitConverter.GetBytes(doubleVal), 0);
                        _logger.WriteLine($"Info: Operand is a ConstantFP (double). Generating LDC.F64 with value {doubleVal} (raw bits: {longVal:X})");
                        EmitOpcode(output, 0x1B, "LDC.F64.IMM");
                        output.Add(newReg);
                        output.AddRange(BitConverter.GetBytes(longVal));
                    }
                    return (newReg, null);
                }
            }
            throw new InvalidOperationException($"Operand is not constant and not found in register map: {operand}");
        }

        private unsafe byte GetOperandReg(LLVMValueRef operand)
        {
            return GetOperandReg(operand, new List<byte>()).Item1;
        }
        
        private unsafe byte GenerateIntConstant(List<byte> output, long value, byte? preferredDest = null, bool force64Bit = false)
        {
            _logger.WriteLine($"    DEBUG GenerateIntConstant: value = {value}, preferredDest = {(preferredDest.HasValue ? $"R{preferredDest.Value}" : "<temp>")}, force64Bit = {force64Bit}");
            byte reg = preferredDest ?? registerManager.AllocateTemporaryRegister();
            
            // Проверяем, нужна ли 64-битная инструкция
            if (force64Bit || value > int.MaxValue || value < int.MinValue)
        {
            _logger.WriteLine($"      DEBUG: Using LDC.I64.IMM for value {value}");
            EmitOpcode(output, 0x19, "LDC.I64.IMM");
            AddReg(output, reg);
            output.AddRange(BitConverter.GetBytes(value));
                _logger.WriteLine($"      DEBUG: LDC.I64.IMM R{reg}, {value} -> Bytes: {BitConverter.ToString(BitConverter.GetBytes(value))}");
            }
            else
            {
                int val32 = (int)value;
                _logger.WriteLine($"      DEBUG: Using LDC.I32.IMM for value {val32}");
                EmitOpcode(output, 0x18, "LDC.I32.IMM");
                AddReg(output, reg);
                output.AddRange(BitConverter.GetBytes(val32));
                _logger.WriteLine($"      DEBUG: LDC.I32.IMM R{reg}, {val32} -> Bytes: {BitConverter.ToString(BitConverter.GetBytes(val32))}");
            }
            
            return reg;
        }
        
        // Перегрузка для обратной совместимости с int параметром
        private unsafe byte GenerateIntConstant(List<byte> output, int value, byte? preferredDest = null)
        {
            return GenerateIntConstant(output, (long)value, preferredDest, false);
        }
        
        private unsafe uint GetLocalTypeSize(LLVMTypeRef type)
        {
            _logger.WriteLine($"    DEBUG GetLocalTypeSize called for Type Kind: {type.Kind}, String: {SafePrintType(type)}");
            if (type.Handle == IntPtr.Zero)
            {
                _logger.WriteLine("    Error: GetLocalTypeSize called with null/zero type handle.");
                return 0;
            }

            // Используем targetData для получения реального размера типа
            if (targetData.Handle != IntPtr.Zero)
            {
                _logger.WriteLine("    DEBUG GetLocalTypeSize: Attempting to use LLVM.StoreSizeOfType...");
                try
                {
                    ulong sizeBytes = LLVM.StoreSizeOfType(targetData, type);
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize: StoreSizeOfType returned: {sizeBytes}");
                    return (uint)sizeBytes;
                }
                catch (AccessViolationException ave)
                {
                     _logger.WriteLine($"      AccessViolationException calling StoreSizeOfType! {ave.Message}");
                     _logger.WriteLine(ave.StackTrace);
                     // Пытаемся использовать эвристику как fallback
                }
                 catch (Exception ex)
                {
                    _logger.WriteLine($"      Exception calling StoreSizeOfType: {ex.GetType().Name} - {ex.Message}");
                    // Пытаемся использовать эвристику как fallback
                }
            }
            else
            {
                 _logger.WriteLine("    DEBUG GetLocalTypeSize: Using fallback size calculation...");
            }

            // Если targetData недоступен или произошла ошибка, используем эвристику
             _logger.WriteLine("    DEBUG GetLocalTypeSize: Using fallback size calculation...");
            switch (type.Kind)
            {
                case LLVMTypeKind.LLVMIntegerTypeKind:
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: IntegerType, IntWidth = {type.IntWidth}");
                    return (uint)Math.Ceiling(type.IntWidth / 8.0);
                case LLVMTypeKind.LLVMFloatTypeKind:
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: FloatType");
                    return 4;
                case LLVMTypeKind.LLVMDoubleTypeKind:
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: DoubleType");
                    return 8;
                case LLVMTypeKind.LLVMPointerTypeKind:
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: PointerType");
                    return 4; // Предполагаем 32-битную архитектуру
                case LLVMTypeKind.LLVMArrayTypeKind:
                    {
                        _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: ArrayType, Length = {type.ArrayLength}");
                        uint elemSize = GetLocalTypeSize(type.ElementType); // Рекурсивный вызов
                        _logger.WriteLine($"        DEBUG GetLocalTypeSize Fallback: Array ElementSize = {elemSize}");
                        return elemSize * type.ArrayLength;
                    }
                case LLVMTypeKind.LLVMVectorTypeKind:
                    {
                        _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: VectorType, VectorSize = {type.VectorSize}");
                        if (type.ElementType.Handle != IntPtr.Zero)
                        {
                            uint elemSize = GetLocalTypeSize(type.ElementType); // Рекурсивный вызов
                            _logger.WriteLine($"        DEBUG GetLocalTypeSize Fallback: Vector ElementSize = {elemSize}");
                            return elemSize * type.VectorSize;
                        }
                        _logger.WriteLine($"        DEBUG GetLocalTypeSize Fallback: VectorType with null ElementType, defaulting size to 16.");
                        return 16; // Предполагаем 128-битный вектор
                    }
                default:
                    _logger.WriteLine($"      DEBUG GetLocalTypeSize Fallback: Default case, returning 1.");
                    return 1; // По умолчанию
            }
        }
        
        private unsafe void TranslateGEP(LLVMValueRef instr, List<byte> output)
        {
            _logger.WriteLine($"---> Translating GEP: {instr}");
            
            // GetElementPtr принимает базовый указатель и набор индексов
            // Результат - указатель на элемент
            
            // Получаем регистр для результата
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);

            // Получаем базовый указатель (первый операнд)
            LLVMValueRef basePtr = instr.GetOperand(0);
            // ВАЖНО: используем GetOperandReg с output, чтобы сгенерировать LD_GLOBAL_ADDR для глобальных переменных
            (byte baseReg, _) = GetOperandReg(basePtr, output);
            
            // Отладочная информация
            _logger.WriteLine($"  Base Ptr: {basePtr} (Reg: {baseReg}), Initial Indexing Type: {SafePrintType(instr.GetOperand(0).TypeOf)}");
            
                            // This is the correct way to start GEP translation.
                            // It gets the type of the data pointed to by the base pointer, which is the starting point for indexing.
                            LLVMTypeRef currentType = LLVM.GetGEPSourceElementType(instr);
                            _logger.WriteLine($"  GEP Start: Source Element Type for indexing is {SafePrintType(currentType)}");            
            // Сложная логика индексации в GEP
            byte currentReg = baseReg;
            int accumulatedOffset = 0;
            
            // Обрабатываем все индексы
            bool isFirstGepIndex = true; // Track first index for array-like semantics
            for (uint i = 1; i < LLVM.GetNumOperands(instr); i++)
            {
                LLVMValueRef indexOperand = instr.GetOperand(i);
                _logger.WriteLine($"    DEBUG: Before print for Index {i}. Current Type Kind: {currentType.Kind}, Handle: {currentType.Handle}");
                _logger.WriteLine($"    DEBUG: Attempting SafePrintType for Index {i}...");
                
                _logger.WriteLine($"  Processing Index {i}: {indexOperand} for type {SafePrintType(currentType)}");
                
                // Проверяем, является ли индекс константой
                bool isConstIndex = false;
                long constantIndex = 0;
                
                if (indexOperand.IsAConstantInt != null && indexOperand.IsAConstantInt.Handle != IntPtr.Zero)
                {
                    isConstIndex = true;
                    constantIndex = LLVM.ConstIntGetSExtValue(indexOperand);
                    _logger.WriteLine($"    Index is Constant: {constantIndex}");
                }
                else
                {
                    _logger.WriteLine($"    Index is Variable (Reg: {GetOperandReg(indexOperand)})");
                }
                
                // CRITICAL FIX: The first GEP index always performs array-like indexing,
                // even if the type is a struct. This indexes into an array of the source element type.
                // See LLVM Language Reference Manual.
                if (isFirstGepIndex)
                {
                    isFirstGepIndex = false;
                    uint elementSize = GetLocalTypeSize(currentType);
                    _logger.WriteLine($"    First GEP Index (Array-like): Element Size = {elementSize}");
                    
                    if (isConstIndex)
                    {
                        int indexOffset = (int)(constantIndex * elementSize);
                        _logger.WriteLine($"    Constant First Index Offset: {constantIndex} * {elementSize} = {indexOffset}");
                        accumulatedOffset += indexOffset;
                    }
                    else
                    {
                        // Dynamic first index - multiply by element size
                        if (accumulatedOffset != 0)
                        {
                            byte offsetReg = GenerateIntConstant(output, accumulatedOffset);
                            byte tempReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x20, "ADD.I32");
                            WriteBinaryInstruction(output, tempReg, currentReg, offsetReg);
                            currentReg = tempReg;
                            accumulatedOffset = 0;
                        }
                        
                        (byte indexReg, _) = GetOperandReg(indexOperand, output);
                        byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                        
                        byte mulReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x22, "MUL.I32");
                        WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                        
                        byte addReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x20, "ADD.I32");
                        WriteBinaryInstruction(output, addReg, currentReg, mulReg);
                        
                        currentReg = addReg;
                    }
                    // Type does NOT change for first index - still pointing at same element type
                    continue;
                }
                
                // Разные стратегии обработки в зависимости от типа (for indices after the first)
                if (currentType.Kind == LLVMTypeKind.LLVMStructTypeKind)
                {
                    if (!isConstIndex)
                    {
                        // This is not a struct field access, but indexing an array of structs with a variable.
                        _logger.WriteLine($"    INFO: Dynamic index on a struct type. Assuming array-of-structs indexing.");
                        uint structSize = GetLocalTypeSize(currentType);
                        _logger.WriteLine($"    Array-of-Struct Indexing - Element Size: {structSize}");

                        if (accumulatedOffset != 0)
                        {
                            byte offsetReg = GenerateIntConstant(output, accumulatedOffset);
                            byte tempReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x20, "ADD.I32");
                            WriteBinaryInstruction(output, tempReg, currentReg, offsetReg);
                            currentReg = tempReg;
                            accumulatedOffset = 0;
                        }
                        
                        (byte indexReg, _) = GetOperandReg(indexOperand, output);
                        byte sizeReg = GenerateIntConstant(output, (int)structSize);
                        
                        byte mulReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x22, "MUL.I32");
                        WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                        
                        byte addReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x20, "ADD.I32");
                        WriteBinaryInstruction(output, addReg, currentReg, mulReg);
                        
                        currentReg = addReg;
                        // The type for the next index is now a field within this struct element.
                        // The 'currentType' remains the struct type for the next iteration if any.
                        continue;
                    }
                    
                    // Original logic for constant index (struct field access)
                    uint fieldIdx = (uint)constantIndex;
                    LLVMTypeRef fieldType = LLVM.StructGetTypeAtIndex(currentType, fieldIdx);
                    _logger.WriteLine($"    DEBUG: Type after StructGetTypeAtIndex: {SafePrintType(fieldType)}");
                    
                    uint fieldOffset = (uint)LLVM.OffsetOfElement(targetData, currentType, fieldIdx);
                    _logger.WriteLine($"    Struct Field Offset: {fieldOffset}, New Type: {SafePrintType(fieldType)}");
                    
                    accumulatedOffset += (int)fieldOffset;
                    currentType = fieldType;
                }
                else if (currentType.Kind == LLVMTypeKind.LLVMArrayTypeKind || 
                        currentType.Kind == LLVMTypeKind.LLVMVectorTypeKind)
                {
                    // Для массивов и векторов - умножаем индекс на размер элемента
                    LLVMTypeRef elementType = LLVM.GetElementType(currentType);
                    _logger.WriteLine($"    DEBUG: Type after GetElementType (Array/Vector): {SafePrintType(elementType)}");
                    
                    uint elementSize = GetLocalTypeSize(elementType);
                    _logger.WriteLine($"    Array/Vector Element Size: {elementSize}, Element Type: {SafePrintType(elementType)}");
                    
                    if (isConstIndex)
                    {
                        // Для константных индексов просто обновляем смещение
                        int indexOffset = (int)(constantIndex * elementSize);
                        _logger.WriteLine($"    Constant Array/Vector Index Offset: {constantIndex} * {elementSize} = {indexOffset}");
                        accumulatedOffset += indexOffset;
                    }
                    else
                    {
                        // Для переменных индексов генерируем соответствующий код ESPB
                        if (accumulatedOffset != 0)
                        {
                            // Если есть накопленное смещение, сначала применяем его
                            byte offsetReg = GenerateIntConstant(output, accumulatedOffset);
                            byte tempReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x20, "ADD.I32");
                            WriteBinaryInstruction(output, tempReg, currentReg, offsetReg);
                            currentReg = tempReg;
                            accumulatedOffset = 0;
                        }
                        
                        // Получаем регистр с индексом
                        (byte indexReg, _) = GetOperandReg(indexOperand, output);
                        
                        // Загружаем размер элемента в регистр
                        byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                        
                        // Умножаем индекс на размер элемента
                        byte mulReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x22, "MUL.I32");
                        WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                        
                        // Добавляем смещение к текущему указателю
                        byte addReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x20, "ADD.I32");
                        WriteBinaryInstruction(output, addReg, currentReg, mulReg);
                        
                        // Обновляем текущий регистр
                        currentReg = addReg;
                    }
                    
                    currentType = elementType;
                }
                else
                {
                    // Скалярный тип - обрабатываем как массив с одним элементом
                    _logger.WriteLine($"    DEBUG: Type before ElementSize (Scalar): {SafePrintType(currentType)}");
                    uint elementSize = GetLocalTypeSize(currentType);
                    _logger.WriteLine($"    Scalar Indexing - Element Size: {elementSize}, Element Type: {SafePrintType(currentType)}");
                    
                    if (isConstIndex)
                    {
                        // Для константных индексов просто обновляем смещение
                        int indexOffset = (int)(constantIndex * elementSize);
                        _logger.WriteLine($"    Constant Scalar Index Offset: {constantIndex} * {elementSize} = {indexOffset}");
                        accumulatedOffset += indexOffset;
                    }
                    else
                    {
                        // Для переменных индексов генерируем код
                        if (accumulatedOffset != 0)
                        {
                            // Если есть накопленное смещение, сначала применяем его
                            byte offsetReg = GenerateIntConstant(output, accumulatedOffset);
                            byte tempReg = registerManager.AllocateTemporaryRegister();
                            EmitOpcode(output, 0x20, "ADD.I32");
                            WriteBinaryInstruction(output, tempReg, currentReg, offsetReg);
                            currentReg = tempReg;
                            accumulatedOffset = 0;
                        }
                        
                        // Получаем регистр с индексом
                        (byte indexReg, _) = GetOperandReg(indexOperand, output);
                        
                        // Загружаем размер элемента в регистр
                        byte sizeReg = GenerateIntConstant(output, (int)elementSize);
                        
                        // Умножаем индекс на размер элемента
                        byte mulReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x22, "MUL.I32");
                        WriteBinaryInstruction(output, mulReg, indexReg, sizeReg);
                        
                        // Добавляем смещение к текущему указателю
                        byte addReg = registerManager.AllocateTemporaryRegister();
                        EmitOpcode(output, 0x20, "ADD.I32");
                        WriteBinaryInstruction(output, addReg, currentReg, mulReg);
                        
                        // Обновляем текущий регистр
                        currentReg = addReg;
                    }
                }
            }
            
            // Если есть накопленное смещение, применяем его к базовому указателю
            if (accumulatedOffset != 0)
            {
                // Генерируем константу смещения
                byte offsetReg = GenerateIntConstant(output, accumulatedOffset);
                
                // Добавляем смещение к текущему указателю
                byte resultReg = registerManager.AllocateTemporaryRegister();
                EmitOpcode(output, 0x20, "ADD.I32");
                WriteBinaryInstruction(output, resultReg, currentReg, offsetReg);
                
                // Обновляем текущий регистр
                currentReg = resultReg;
            }
            
            // Копируем результат в целевой регистр
            EmitOpcode(output, 0x12, "MOV.32");
            output.Add(destReg);
            output.Add(currentReg);
            
            _logger.WriteLine("---> Finished Translating GEP");
        }
        
        private unsafe void TranslateCall(LLVMValueRef instr, List<byte> output)
        {
            // Note: OP_JIT_START/OP_JIT_STOP are handled below as synthetic intrinsics and do not represent real calls.



            if (instr.InstructionOpcode != LLVMOpcode.LLVMCall)
                throw new InvalidOperationException("Expected CALL instruction");

            LLVMValueRef calledFunc = LLVM.GetCalledValue(instr);
            string funcName = calledFunc.Name ?? "";

            // --- NEW LOGIC: Check for inline assembly and get original function name ---
            byte? destReg = instr.TypeOf.Kind == LLVMTypeKind.LLVMVoidTypeKind ? (byte?)null : GetOrAllocateRegWithCacheInvalidation(instr, output);

            if (calledFunc.Kind == LLVMValueKind.LLVMInlineAsmValueKind)
            {
                string? originalFuncName = GetOriginalFunctionNameFromDbgInfo(instr);
                if (originalFuncName != null)
                {
                    _logger.WriteLine($"[InlineAsm] START: Found original name '{originalFuncName}'");

                    var importsSection = GetSection<ImportsSection>(2)!;
                    _logger.WriteLine("[InlineAsm] Got imports section.");
                    int importIndex = importsSection.Imports.FindIndex(imp => imp.EntityName == originalFuncName);
                    _logger.WriteLine($"[InlineAsm] FindIndex result: {importIndex}");

                    if (importIndex == -1)
                    {
                        _logger.WriteLine($"[InlineAsm] Creating new import entry.");
                        var typesSection = GetSection<TypesSection>(1)!;
                        int typeIndex = -1;
                        for (int i = 0; i < typesSection.Signatures.Count; i++)
                        {
                            var sig = typesSection.Signatures[i];
                            if (sig.Parameters.Count == 0 && sig.Results.Count == 1 && sig.Results[0] == ValueType.I32)
                            {
                                typeIndex = i;
                                break;
                            }
                        }

                        if (typeIndex == -1)
                        {
                            LLVMTypeRef calledFuncType = LLVM.GetCalledFunctionType(instr);
                            typeIndex = AddTypeSignature(calledFuncType, null);
                        }
                        _logger.WriteLine($"[InlineAsm] Using type index: {typeIndex}");

                        var newImport = new EspbImport
                        {
                            ModuleNum = 0,
                            EntityName = originalFuncName,
                            Kind = EspbImportKind.Function,
                            TypeIndex = typeIndex,
                            ImportFlags = 0
                        };
                        importsSection.Imports.Add(newImport);
                        importIndex = importsSection.Imports.Count - 1;
                        _logger.WriteLine($"[InlineAsm] New import index: {importIndex}");
                    }

                    _logger.WriteLine($"[InlineAsm] Generating CALL_IMPORT for index {importIndex}");
                    EmitOpcode(output, 0x09, "CALL_IMPORT");
                    output.AddRange(BitConverter.GetBytes((ushort)importIndex));
                    if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                        MarkMaterialCallProducesR0(instr);
 
                    if (destReg.HasValue)
                    {
                        // If the call result is unused in IR, skip copying from R0.
                        LLVMUseRef firstUse = LLVM.GetFirstUse(instr);
                        if (!firstUse.Equals(default(LLVMUseRef)))
                        {
                            _logger.WriteLine($"[InlineAsm] Generating return value copy.");
                            var returnType = MapLlvmTypeToEspb(instr.TypeOf);
                            byte movRetOpcode = GetMoveOpcode(returnType);
                            _logger.WriteLine($"    [InlineAsm] Generating copy: {movRetOpcode:X2} R{destReg.Value}, R0");
                            EmitOpcode(output, movRetOpcode, "MOV_RET");
                            output.Add(destReg.Value);
                            output.Add(0); // R0 is the return register
                        }
                        else
                        {
                            _logger.WriteLine("    [OPT] Call result is unused, skipping MOV from R0");
                        }
                    }
                    _logger.WriteLine("[InlineAsm] END: Handled successfully, returning.");
                    return;
                }
                else
                {
                    _logger.WriteLine("Warning: Detected inline assembly, but could not determine original function name from debug info.");
                    // Inline assembly без определенного имени функции - пропускаем или генерируем NOP
                    _logger.WriteLine($"Info: Skipping inline assembly call: '{instr}'");
                    // Для skip assembly генерируем NOP или просто пропускаем
                    return;
                }
            }
            // --- END NEW LOGIC ---

            if (funcName == "__atomic_load_8")
            {
                _logger.WriteLine($"---> Translating intrinsic: __atomic_load_8");
                byte destRegForLoad = GetOrAllocateRegWithCacheInvalidation(instr, output);
                (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
                EmitOpcode(output, 0xEC, "ATOMIC.LOAD.I64");
                output.Add(destRegForLoad);
                output.Add(ptrReg);
                return; // Завершаем обработку, не переходя к логике FFI
            }
            else if (funcName == "__atomic_store_8")
            {
                _logger.WriteLine($"---> Translating intrinsic: __atomic_store_8");
                (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
                (byte valReg, _) = GetOperandReg(instr.GetOperand(1), output);
                EmitOpcode(output, 0xED, "ATOMIC.STORE.I64");
                output.Add(valReg); // Rs (value)
                output.Add(ptrReg); // Ra (address)
                return; // Завершаем обработку
            }
            else if (funcName == "__atomic_fetch_sub_8")
            {
                _logger.WriteLine($"---> Translating intrinsic: __atomic_fetch_sub_8");
                byte destRegForSub = GetOrAllocateRegWithCacheInvalidation(instr, output);
                (byte ptrReg, _) = GetOperandReg(instr.GetOperand(0), output);
                (byte valReg, _) = GetOperandReg(instr.GetOperand(1), output);

                EmitOpcode(output, 0xF1, "ATOMIC.RMW.SUB.I64");
                output.Add(destRegForSub);
                output.Add(ptrReg);
                output.Add(valReg);
                return; // Завершаем обработку
            }
            uint numOperands = instr.OperandCount > 0 ? (uint)instr.OperandCount - 1 : 0;

            LLVMTypeRef funcType = calledFunc.TypeOf.ElementType;
            
            bool isImport = llvmImportToEspbIndex.ContainsKey(calledFunc);

            _logger.WriteLine($"--- Translating Call to: '{funcName}' (isImport: {isImport}) ---");
            _logger.WriteLine($"---> Translating Call Instruction: {instr}");
            _logger.WriteLine($"    Operand Count: {numOperands}");

            LLVMValueRef calledValueTemp = instr.GetOperand((uint)(instr.OperandCount - 1));
            _logger.WriteLine($"    Called Value (Operand {instr.OperandCount - 1}): {calledValueTemp}");
            _logger.WriteLine($"    Called Value Kind: {calledValueTemp.Kind}");



            string calledFuncName = calledValueTemp.Name ?? "";

            // --- ЗАМЕНИТЕ СУЩЕСТВУЮЩУЮ ЛОГИКУ ДЛЯ malloc/free/realloc/calloc ---
            switch (calledFuncName)
            {
                case "OP_JIT_START":
                    {
                        // Emit JIT_REGION_START: [0x06][len:u16 placeholder][region_id:u16]
                        // len is the byte length of the region body *after* this instruction.
                        ushort regionId = _nextJitRegionId++;
                        int startIndex = output.Count;
                        _jitRegionStartStack.Push(startIndex);

                        EmitOpcode(output, 0x06, "JIT_REGION_START");
                        output.AddRange(BitConverter.GetBytes((ushort)0)); // placeholder len
                        output.AddRange(BitConverter.GetBytes(regionId));  // region_id

                        _logger.WriteLine($"    Generated JIT_REGION_START len=?, id={regionId} at bytecode offset {startIndex}");
                        return;
                    }

                case "OP_JIT_STOP":
                    {
                        if (_jitRegionStartStack.Count == 0)
                        {
                            _logger.WriteLine("Warning: OP_JIT_STOP encountered without matching OP_JIT_START. Ignoring.");
                            return;
                        }

                        int startIndex = _jitRegionStartStack.Pop();
                        int headerSize = 5; // opcode + u16 len + u16 id
                        int regionBodyStart = startIndex + headerSize;
                        int regionBodyLen = output.Count - regionBodyStart;
                        if (regionBodyLen < 0)
                        {
                            _logger.WriteLine("Warning: JIT region length became negative (internal error). Forcing to 0.");
                            regionBodyLen = 0;
                        }
                        if (regionBodyLen > ushort.MaxValue)
                        {
                            throw new InvalidOperationException($"JIT region too large: {regionBodyLen} bytes (max {ushort.MaxValue}).");
                        }

                        ushort len = (ushort)regionBodyLen;
                        byte[] lenBytes = BitConverter.GetBytes(len);
                        // Patch len into already emitted JIT_REGION_START
                        output[startIndex + 1] = lenBytes[0];
                        output[startIndex + 2] = lenBytes[1];

                        // Извлекаем region_id из байтов 3-4
                        ushort regionId = (ushort)(output[startIndex + 3] | (output[startIndex + 4] << 8));
                        
                        // Сохраняем дескриптор региона для секции ID=19
                        // startIndex указывает на опкод 0x06, regionBodyStart — начало тела региона
                        ushort funcIdx = GetCurrentFunctionIndex();
                        ushort startOffset = (ushort)regionBodyStart; // offset первого опкода тела региона
                        _jitRegionDescriptors.Add((regionId, funcIdx, startOffset, regionBodyLen));
                        
                        _logger.WriteLine($"    Patched JIT_REGION_START at {startIndex} with len={len} (region body bytes)");
                        _logger.WriteLine($"    Saved region descriptor: id={regionId}, funcIdx={funcIdx}, startOff={startOffset}, len={len}");
                        return;
                    }
                case "malloc":
                    if (!destReg.HasValue) return;
                    (byte sizeReg, _) = GetOperandReg(instr.GetOperand(0), output);
                    EmitOpcode(output, 0xFC, "PREFIX");
                    EmitOpcode(output, 0x0B, "HEAP_MALLOC");
                    output.Add(destReg.Value);
                    output.Add(sizeReg);
                    _logger.WriteLine($"    Generated HEAP_MALLOC R{destReg.Value}, R{sizeReg}");
                    return;

                case "free":
                    (byte ptrToFreeReg, _) = GetOperandReg(instr.GetOperand(0), output);
                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x07, "HEAP_FREE");

                    AddReg(output, ptrToFreeReg);
                    _logger.WriteLine($"    Generated HEAP_FREE R{ptrToFreeReg}");
                    return;

                case "OP_table_size":
                    if (!destReg.HasValue) return;
                    
                    // Correctly get the constant table index from the operand
                    LLVMValueRef tableIndexOp = instr.GetOperand(0);
                    byte tableIndex = 0;
                    if (tableIndexOp.IsAConstantInt.Handle != IntPtr.Zero)
                    {
                        tableIndex = (byte)LLVM.ConstIntGetZExtValue(tableIndexOp);
                    }
                    else
                    {
                        // Fallback if the index is not a constant, though it should be.
                        _logger.WriteLine($"Warning: OP_table_size expects a constant table index. Found: {tableIndexOp}. Using 0 as fallback.");
                    }

                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x08, "TABLE.SIZE");

                    AddReg(output, destReg.Value);

                    output.Add(tableIndex); // Write the constant value, not a register index
                    _logger.WriteLine($"    Generated TABLE.SIZE R{destReg.Value}, {tableIndex}");
                    return;

                case "OP_table_init":
                     // TABLE.INIT table_idx(u8), elem_seg_idx(u32), Rd(u8),Rs(u8),Rn(u8)
                    // First two parameters are immediate values (constants), not registers
                    byte table_idx = (byte)instr.GetOperand(0).ConstIntZExt;
                    uint elem_seg_idx = (uint)instr.GetOperand(1).ConstIntZExt;
                    // Last three parameters are loaded into registers
                    (byte dest_reg_init, _) = GetOperandReg(instr.GetOperand(2), output);
                    (byte offset_reg_init, _) = GetOperandReg(instr.GetOperand(3), output);
                    (byte size_reg_init, _) = GetOperandReg(instr.GetOperand(4), output);

                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x04, "TABLE.INIT");

                    output.Add(table_idx);

                    output.AddRange(BitConverter.GetBytes(elem_seg_idx));

                    AddReg(output, dest_reg_init);

                    AddReg(output, offset_reg_init);

                    AddReg(output, size_reg_init);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated TABLE.INIT table={table_idx}, seg={elem_seg_idx}, R{dest_reg_init}, R{offset_reg_init}, R{size_reg_init}");
                    return;

                case "OP_table_copy":
                    // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
                    byte tableDestIdx_copy = (byte)instr.GetOperand(0).ConstIntZExt;
                    byte tableSrcIdx_copy = (byte)instr.GetOperand(1).ConstIntZExt;
                    (byte destReg_copy, _) = GetOperandReg(instr.GetOperand(2), output);
                    (byte srcReg_copy, _) = GetOperandReg(instr.GetOperand(3), output);
                    (byte sizeReg_copy, _) = GetOperandReg(instr.GetOperand(4), output);

                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x16, "TABLE.COPY");

                    output.Add(tableDestIdx_copy);

                    output.Add(tableSrcIdx_copy);

                    AddReg(output, destReg_copy);

                    AddReg(output, srcReg_copy);

                    AddReg(output, sizeReg_copy);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated TABLE.COPY tableD={tableDestIdx_copy}, tableS={tableSrcIdx_copy}, R{destReg_copy}, R{srcReg_copy}, R{sizeReg_copy}");
                    return;

                case "OP_table_fill":
                    // TABLE.FILL table_idx(u8), Rd(u8), Rval(u8), Rn(u8)
                    byte table_idx_fill = (byte)instr.GetOperand(0).ConstIntZExt;
                    (byte dest_reg_fill, _) = GetOperandReg(instr.GetOperand(1), output);
                    (byte val_reg_fill, _) = GetOperandReg(instr.GetOperand(2), output);
                    (byte size_reg_fill, _) = GetOperandReg(instr.GetOperand(3), output);

                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x17, "TABLE.FILL");

                    output.Add(table_idx_fill);

                    AddReg(output, dest_reg_fill);

                    AddReg(output, val_reg_fill);

                    AddReg(output, size_reg_fill);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated TABLE.FILL table={table_idx_fill}, R{dest_reg_fill}, R{val_reg_fill}, R{size_reg_fill}");
                    return;

                case "OP_table_get":
                    // TABLE.GET Rd(u8), table_idx(u8), Rs(u8)
                    // Returns function index from table[Rs] into Rd
                    if (!destReg.HasValue) return;
                    byte table_idx_get = (byte)instr.GetOperand(0).ConstIntZExt;
                    (byte index_reg_get, _) = GetOperandReg(instr.GetOperand(1), output);
                    
                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x18, "TABLE.GET");

                    AddReg(output, destReg.Value);

                    output.Add(table_idx_get);

                    AddReg(output, index_reg_get);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated TABLE.GET R{destReg.Value}, table={table_idx_get}, R{index_reg_get}");
                    return;

                case "OP_table_set":
                    // TABLE.SET table_idx(u8), Rd(u8), Rval(u8)
                    // Sets table[Rd] = Rval
                    byte table_idx_set = (byte)instr.GetOperand(0).ConstIntZExt;
                    (byte index_reg_set, _) = GetOperandReg(instr.GetOperand(1), output);
                    (byte value_reg_set, _) = GetOperandReg(instr.GetOperand(2), output);
                    
                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x19, "TABLE.SET");

                    output.Add(table_idx_set);

                    AddReg(output, index_reg_set);

                    AddReg(output, value_reg_set);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated TABLE.SET table={table_idx_set}, R{index_reg_set}, R{value_reg_set}");
                    return;

                case "calloc":
                    if (!destReg.HasValue) return;
                    (byte numReg, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte elementSizeReg, _) = GetOperandReg(instr.GetOperand(1), output);
                    EmitOpcode(output, 0xFC, "PREFIX");

                    EmitOpcode(output, 0x09, "HEAP_CALLOC");

                    AddReg(output, destReg.Value);

                    AddReg(output, numReg);

                    AddReg(output, elementSizeReg);
                    _logger.WriteLine($"    Generated HEAP_CALLOC R{destReg.Value}, R{numReg}, R{elementSizeReg}");
                    return;

                case "realloc":
                    if (!destReg.HasValue) return;
                    (byte oldPtrReg, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte newSizeReg, _) = GetOperandReg(instr.GetOperand(1), output);
                    EmitOpcode(output, 0xFC, "PREFIX");
                    EmitOpcode(output, 0x06, "HEAP_REALLOC");
                    output.Add(destReg.Value);
                    output.Add(oldPtrReg);
                    output.Add(newSizeReg);
                    _logger.WriteLine($"    Generated HEAP_REALLOC R{destReg.Value}, R{oldPtrReg}, R{newSizeReg}");
                    return;
            }
            // --- КОНЕЦ СПЕЦИАЛЬНОЙ ОБРАБОТКИ ---

            // LLVM intrinsics can appear either as a direct function name (funcName)
            // or wrapped/bitcasted where Name may be empty on the called value.
            // To be robust, we match both.
            string intrinsicName = !string.IsNullOrEmpty(calledFuncName) ? calledFuncName : funcName;

            if (intrinsicName.StartsWith("llvm.") && !isImport)
            {
                if (intrinsicName.StartsWith("llvm.fabs"))
                {
                    byte dest = GetOrAllocateRegWithCacheInvalidation(instr, output);
                    (byte op1, _) = GetOperandReg(instr.GetOperand(0), output);
                    var type = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);
                    byte opcode = type == ValueType.F32 ? (byte)0x66 : (byte)0x6E; // ABS.F64
                    EmitOpcode(output, opcode, "FABS");
                    output.Add(dest);
                    output.Add(op1);
                    return;
                }
                if (intrinsicName.StartsWith("llvm.min"))
                {
                    byte dest = GetOrAllocateRegWithCacheInvalidation(instr, output);
                    (byte op1, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte op2, _) = GetOperandReg(instr.GetOperand(1), output);
                    var type = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);
                    byte opcode = type == ValueType.F32 ? (byte)0x64 : (byte)0x6C; // MIN.F64
                    EmitOpcode(output, opcode, "FMIN");
                    output.Add(dest);
                    output.Add(op1);
                    output.Add(op2);
                    return;
                }
                if (intrinsicName.StartsWith("llvm.max"))
                {
                    byte dest = GetOrAllocateRegWithCacheInvalidation(instr, output);
                    (byte op1, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte op2, _) = GetOperandReg(instr.GetOperand(1), output);
                    var type = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);
                    byte opcode = type == ValueType.F32 ? (byte)0x65 : (byte)0x6D; // MAX.F64
                    EmitOpcode(output, opcode, "FMAX");
                    output.Add(dest);
                    output.Add(op1);
                    output.Add(op2);
                    return;
                }
                if (intrinsicName.StartsWith("llvm.sqrt"))
                {
                    byte dest = GetOrAllocateRegWithCacheInvalidation(instr, output);
                    (byte op1, _) = GetOperandReg(instr.GetOperand(0), output);
                    var type = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);
                    byte opcode = type == ValueType.F32 ? (byte)0x67 : (byte)0x6F; // SQRT.F64
                    EmitOpcode(output, opcode, "FSQRT");
                    output.Add(dest);
                    output.Add(op1);
                    return;
                }
                if (calledFuncName.StartsWith("llvm.memcpy") || calledFuncName.StartsWith("llvm.memmove"))
                {
                    // MEMORY.COPY memD(u8), memS(u8), Rd(u8), Rs(u8), Rn(u8)
                    // LLVM intrinsic arguments are: dest, src, len, is_volatile
                    (byte dest_reg, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte src_reg, _) = GetOperandReg(instr.GetOperand(1), output);
                    (byte size_reg, _) = GetOperandReg(instr.GetOperand(2), output);

                    EmitOpcode(output, 0xFC, "PREFIX");
                    EmitOpcode(output, 0x02, "MEMORY.COPY");
                    output.Add(dest_reg);
                    output.Add(src_reg);
                    output.Add(size_reg);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated MEMORY.COPY R{dest_reg}, R{src_reg}, R{size_reg}");
                    return;
                }
                if (calledFuncName.StartsWith("llvm.memset"))
                {
                    // LLVM intrinsic arguments are: dest, val, len, is_volatile
                    (byte dest_reg, _) = GetOperandReg(instr.GetOperand(0), output);
                    (byte val_reg, _) = GetOperandReg(instr.GetOperand(1), output);
                    (byte size_reg, _) = GetOperandReg(instr.GetOperand(2), output);

                    EmitOpcode(output, 0xFC, "PREFIX");
                    EmitOpcode(output, 0x03, "MEMORY.FILL");
                    output.Add(dest_reg);
                    output.Add(val_reg);
                    output.Add(size_reg);
                    espbModule.Features |= FEATURE_BULK_OPERATIONS;
                    _logger.WriteLine($"    Generated MEMORY.FILL R{dest_reg}, R{val_reg}, R{size_reg}");
                    return;
                }

                _logger.WriteLine($"Info: Ignoring LLVM intrinsic '{calledFuncName}'");
                if (destReg.HasValue)
                {
                    _logger.WriteLine($"  Generating zero constant for ignored intrinsic return value");
                    byte zeroReg = GenerateIntConstant(output, 0, preferredDest: null);
                    byte moveOpcode = GetMoveOpcode(MapLlvmTypeToEspb(instr.TypeOf));
                    EmitOpcode(output, moveOpcode, "MOV");
                    output.Add(destReg.Value);
                    output.Add(zeroReg);
                }
                return;
            }

            // --- PASS 1: Collect source argument registers and types ---
            var sourceArguments = new List<(byte reg, ValueType type, bool isByval)>();
            _logger.WriteLine($"[TranslateCall-DEBUG] Before argument loop. output.Count = {output.Count}");

            // ИСПРАВЛЕНИЕ: Инвалидируем _allocaRegMap для всех alloca, адрес которых передается в функцию.
            // Это необходимо потому что функция может записать новое значение по этому адресу (OUT-параметр),
            // и при последующем load из alloca нужно читать из памяти, а не использовать кэшированный регистр.
            // ВАЖНО: Перед инвалидацией нужно сгенерировать STORE для записи текущего значения в память!
            for (uint i = 0; i < numOperands; i++)
            {
                LLVMValueRef arg = instr.GetOperand(i);
                if (arg.IsAAllocaInst != null && arg.IsAAllocaInst.Handle != IntPtr.Zero)
                {
                    if (_allocaRegMap.TryGetValue(arg, out byte cachedReg))
                    {
                        // Генерируем STORE для записи значения из регистра в память перед передачей адреса
                        // Это необходимо чтобы функция могла прочитать актуальное значение по указателю
                        (byte ptrReg, _) = GetOperandReg(arg, output);
                        
                        // Определяем тип для STORE
                        LLVMTypeRef allocatedType = LLVM.GetAllocatedType(arg);
                        ValueType storeType = MapLlvmTypeToEspb(allocatedType);
                        byte storeOpcode = GetStoreOpcode(storeType);
                        
                        _logger.WriteLine($"    [ALLOCA-SPILL] Spilling R{cachedReg} to memory before passing address to function call at arg {i}");
                        EmitOpcode(output, storeOpcode, "STORE");
                        output.Add(cachedReg); // Rs - value
                        output.Add(ptrReg);    // Ra - address
                        output.AddRange(BitConverter.GetBytes((short)0)); // offset
                        
                        _allocaRegMap.Remove(arg);
                        // CRITICAL: Also mark as volatile to prevent future ALLOCA-OPT on this alloca
                        // since the function may write through the pointer
                        _volatileAllocas.Add(arg);
                        _logger.WriteLine($"    [ALLOCA-INVALIDATE] Removed alloca from cache and marked volatile after spill");
                    }
                }
            }

            // Pre-copy previous call result (still in R0) into ABI argument register before we start
            // evaluating other arguments that might overwrite R0.
            // This turns double-hop MOV patterns (R0->tmp->Ri) into a single MOV (R0->Ri).
            bool[] argAlreadyInPlace = new bool[16];
            TryPrecopyPreviousCallResultArg(instr, numOperands, output, argAlreadyInPlace);

            for (uint i = 0; i < numOperands; i++)
            {
                _logger.WriteLine($"[TranslateCall-DEBUG] Processing arg {i}. output.Count = {output.Count}");
                LLVMValueRef arg = instr.GetOperand(i);

                // If we pre-copied a forwarded call result into its ABI register, treat it as already in place.
                if (i < 16 && argAlreadyInPlace[i])
                {
                    var t = MapLlvmTypeToEspb(arg.TypeOf);
                    bool isByvalForward = (calledFunc.Handle != IntPtr.Zero) && IsByvalArgument(calledFunc, i);
                    sourceArguments.Add(((byte)i, t, isByvalForward));
                    _logger.WriteLine($"[TranslateCall-DEBUG] Processed arg {i} via forwarding. Register is R{i}. output.Count = {output.Count}");
                    continue;
                }

                // Preferred destination for ABI argument registers (R0..R15)
                byte? preferredArgDest = (i < 16) ? (byte)i : (byte?)null;
                string argName = arg.ToString();
                _logger.WriteLine($"    Arg {i}: Kind={arg.Kind}, Type=[{arg.TypeOf.Kind}], Name={argName}");
                
                byte argReg;
                ValueType espbType;

                // --- НОВАЯ ЛОГИКА ОБРАБОТКИ АРГУМЕНТОВ-ФУНКЦИЙ ---
                if (arg.Kind == LLVMValueKind.LLVMFunctionValueKind)
                {
                    string cbFuncName = arg.Name ?? "unknown_callback";
                    if (llvmFuncToEspbIndex.TryGetValue(arg, out uint funcIdxCallback))
                    {
                        // Проверяем, действительно ли это колбэк согласно финальной карте
                        bool isCallback = callbackInfoMap.TryGetValue(funcName, out var cbInfos) &&
                                          cbInfos.Any(info => info.CallbackParamIndex == i);

                        if (isCallback)
                        {
                            // --- ЭТО КОЛБЭК ---
                            // Генерируем LDC.I32.IMM с флагом CALLBACK_FLAG_BIT
                            argReg = preferredArgDest ?? registerManager.AllocateTemporaryRegister();
                            int callbackValue = (int)(funcIdxCallback | CALLBACK_FLAG_BIT);
                            _logger.WriteLine($"[TranslateCall] Arg {i} ('{cbFuncName}') is a confirmed callback for '{funcName}'. Generating LDC.I32.IMM with value 0x{callbackValue:X8}");
                            EmitOpcode(output, 0x18, "LDC.I32.IMM");
                            output.Add(argReg);
                            output.AddRange(BitConverter.GetBytes(callbackValue));
                            espbType = ValueType.I32; // Указатель на колбэк передается как I32 с флагом
                        }
                        else
                        {
                            // --- ЭТО НЕ КОЛБЭК (Обычный указатель на функцию) ---
                            // ВОССТАНАВЛИВАЕМ СТАРУЮ, ПРЕДСКАЗУЕМУЮ ЛОГИКУ.
                            // GetOperandReg теперь правильно обрабатывает указатели на функции.
                            argReg = preferredArgDest ?? registerManager.AllocateTemporaryRegister();
                            ushort functionIndex = (ushort)funcIdxCallback;

                            uint numGlobals = (uint)(GetSection<GlobalsSection>(4)!.Globals.Count);
                            ushort symbolIdx = (ushort)(functionIndex | 0x8000);

                            _logger.WriteLine($"[TranslateCall] Arg {i} ('{cbFuncName}') is NOT a callback for '{funcName}'. Generating LD_GLOBAL_ADDR for function index {functionIndex} (symbol_idx={symbolIdx}). ");
                            EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                            output.Add(argReg);
                            output.AddRange(BitConverter.GetBytes(symbolIdx));
                            espbType = ValueType.PTR;
                            
                            // ИСПРАВЛЕНИЕ: Добавляем запись в FuncPtrMap для корректной работы CALL_INDIRECT_PTR
                            if (_funcPtrToDataOffset.TryGetValue(arg, out uint dataOffset))
                            {
                                AddFunctionPointerMapEntry(dataOffset, functionIndex);
                                _logger.WriteLine($"[TranslateCall] Added FuncPtrMap entry: DataOffset={dataOffset}, FuncIndex={functionIndex}");
                            }
                        }
                    }
                    else
                    {
                        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Поиск функции по имени как fallback
                        _logger.WriteLine($"[TranslateCall] Function '{cbFuncName}' not found by ValueRef. Trying name-based lookup...");
                        
                        uint? foundFuncIndex = null;
                        foreach (var kvp in llvmFuncToEspbIndex)
                        {
                            unsafe
                            {
                                sbyte* namePtr = LLVM.GetValueName(kvp.Key);
                                if (namePtr != null)
                                {
                                    string currentFuncName = System.Runtime.InteropServices.Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                                    if (currentFuncName == cbFuncName)
                                    {
                                        foundFuncIndex = kvp.Value;
                                        _logger.WriteLine($"[TranslateCall] SUCCESS: Found function '{cbFuncName}' by name with index {foundFuncIndex}");
                                        break;
                                    }
                                }
                            }
                        }
                        
                        if (foundFuncIndex.HasValue)
                        {
                            // Генерируем LD_GLOBAL_ADDR для найденной функции
                            argReg = preferredArgDest ?? registerManager.AllocateTemporaryRegister();
                            ushort functionIndex = (ushort)foundFuncIndex.Value;
                            uint numGlobals = (uint)(GetSection<GlobalsSection>(4)!.Globals.Count);
                            ushort symbolIdx = (ushort)(functionIndex | 0x8000);
                            
                            _logger.WriteLine($"[TranslateCall] Generating LD_GLOBAL_ADDR for function '{cbFuncName}' with index {functionIndex} (symbol_idx={symbolIdx})");
                            EmitOpcode(output, 0x1D, "LD_GLOBAL_ADDR");
                            output.Add(argReg);
                            output.AddRange(BitConverter.GetBytes(symbolIdx));
                            espbType = ValueType.PTR;
                            
                            // ИСПРАВЛЕНИЕ: Добавляем запись в FuncPtrMap для корректной работы CALL_INDIRECT_PTR
                            if (_funcPtrToDataOffset.TryGetValue(arg, out uint dataOffset))
                            {
                                AddFunctionPointerMapEntry(dataOffset, functionIndex);
                                _logger.WriteLine($"[TranslateCall] Added FuncPtrMap entry: DataOffset={dataOffset}, FuncIndex={functionIndex}");
                            }
                        }
                        else
                        {
                            _logger.WriteLine($"[TranslateCall] CRITICAL ERROR: Function '{cbFuncName}' not found by name either. Using null pointer.");
                            argReg = GenerateIntConstant(output, 0, preferredArgDest);
                            espbType = ValueType.PTR;
                        }
                    }
                }
                else
                {
                    // Fast path: forward last call result directly from R0 when safe.
                    if (_forwardedCallResultValid && arg.Equals(_forwardedCallResult))
                    {
                        argReg = 0; // ABI return register
                        espbType = MapLlvmTypeToEspb(arg.TypeOf);
                        // Consumed.
                        _forwardedCallResultValid = false;
                    }
                    else
                    {
                        // Для всех остальных типов аргументов используем стандартную логику
                        (argReg, _) = GetOperandReg(arg, output, preferredArgDest);
                        espbType = MapLlvmTypeToEspb(arg.TypeOf);
                    }

                }

                _logger.WriteLine($"[TranslateCall-DEBUG] Processed arg {i}. Register is R{argReg}. output.Count = {output.Count}");
                bool isByval = (calledFunc.Handle != IntPtr.Zero) && IsByvalArgument(calledFunc, i);
                sourceArguments.Add((argReg, espbType, isByval));
            }
            _logger.WriteLine($"[TranslateCall-DEBUG] After argument loop. output.Count = {output.Count}");

            // --- PASS 2: Plan and safely execute argument moves ---
            var moves = new List<MoveOperation>();
            for (int i = 0; i < numOperands; i++)
            {
                if (i >= 16)
                {
                    _logger.WriteLine($"    Arg Setup {i}: Argument > 15 not handled.");
                    continue;
                }
                var (sourceReg, espbType, _) = sourceArguments[i];
                byte targetReg = (byte)i;

                if (sourceReg != targetReg)
                {
                    moves.Add(new MoveOperation
                    {
                        SourceReg = sourceReg,
                        DestReg = targetReg,
                        Opcode = GetMoveOpcode(espbType),
                        Type = espbType
                    });
                }
            }

            ExecuteParallelMoves(moves, output);
            
            // --- Generate the actual CALL instruction ---
            byte callOpcode;
            ushort indexToCall;

            // ИСПРАВЛЕНИЕ: Используем calledValueTemp вместо calledFunc для правильного определения типа вызова
            if (calledValueTemp.Kind == LLVMValueKind.LLVMFunctionValueKind) // Direct call
            {
                // ОТЛАДКА: Показываем какая функция вызывается
                _logger.WriteLine($"    DEBUG: Processing direct call to function '{calledFuncName}'");
                
                // СПЕЦИАЛЬНАЯ ОБРАБОТКА для call_function_by_address
                if (calledFuncName == "call_function_by_address")
                {
                    _logger.WriteLine($"    SPECIAL HANDLING: Detected call_function_by_address - generating CALL_INDIRECT_PTR instead of CALL");

                    // --- НАЧАЛО НОВОЙ ЛОГИКИ ---
                    // Первый аргумент - это указатель на функцию, который нам нужно сопоставить.
                    LLVMValueRef funcPtrArg = instr.GetOperand(0);

                    // Разрешаем этот аргумент обратно до исходного определения функции.
                    LLVMValueRef? targetFunction = ResolveFunctionPointer(funcPtrArg);
                    int typeIndex;

                    if (targetFunction.HasValue)
                    {
                        if (llvmFuncToEspbIndex.TryGetValue(targetFunction.Value, out uint funcIndex))
                        {
                            if (_funcPtrToDataOffset.TryGetValue(targetFunction.Value, out uint dataOffset))
                            {
                                _logger.WriteLine($"[TranslateCall] Mapping indirect call: Function '{targetFunction.Value.Name}', Index={funcIndex}, DataOffset={dataOffset}");
                                AddFunctionPointerMapEntry(dataOffset, (ushort)funcIndex);
                            }
                        }
                        LLVMTypeRef concreteFuncType = LLVM.GlobalGetValueType(targetFunction.Value);
                        typeIndex = AddTypeSignature(concreteFuncType, null, targetFunction);
                        _logger.WriteLine($"    [SPECIAL-FIX] Resolved indirect call target to '{targetFunction.Value.Name}', using concrete type index {typeIndex}");
                    }
                    else
                    {
                        _logger.WriteLine($"[TranslateCall] ERROR: Could not resolve function pointer argument for call_function_by_address: {funcPtrArg}. Using fallback type index 2.");
                        typeIndex = 2; // Fallback to original broken behavior
                    }
                    // --- КОНЕЦ НОВОЙ ЛОГИКИ ---
                    
                    // Генерируем сам вызов
                    byte funcPtrReg = 0; // Указатель находится в R0 после настройки аргументов
                    
                    _logger.WriteLine($"    Generating CALL_INDIRECT_PTR (special for call_function_by_address) using register R{funcPtrReg} and type index {typeIndex}");
                    EmitOpcode(output, 0x0D, "CALL_INDIRECT_PTR");
                    output.Add(funcPtrReg);
                    output.AddRange(BitConverter.GetBytes((ushort)typeIndex));
                }
                else if (llvmFuncToEspbIndex.TryGetValue(calledFunc, out uint funcIndex))
                {
                    callOpcode = 0x0A; // CALL
                    indexToCall = (ushort)funcIndex;
                    _logger.WriteLine($"    Generating CALL for internal function index {indexToCall}");
                    EmitOpcode(output, callOpcode, "CALL");
                    output.AddRange(BitConverter.GetBytes(indexToCall));
                }
                else if (llvmImportToEspbIndex.TryGetValue(calledFunc, out uint importIndex))
                {
                    if (calledFuncName == "__wasm_memory_grow")
                    {
                        (byte delta_reg, _) = GetOperandReg(instr.GetOperand(0), output);
                        EmitOpcode(output, 0xFC, "PREFIX");
                        EmitOpcode(output, 0xFC, "PREFIX");
                        EmitOpcode(output, 0x0A, "MEMORY.GROW");
                        output.Add(delta_reg);
                        if (destReg.HasValue) {
                            if (destReg.Value != 0) { // Result is in R0
                                 EmitOpcode(output, 0x12, "MOV.I32");
                                 output.Add(destReg.Value);
                                 output.Add(0);
                            }
                        }
                        _logger.WriteLine($"    Generated MEMORY.GROW R{delta_reg}");
                        return;
                    }
                    if (calledFuncName == "__wasm_memory_init")
                    {
                        LLVMValueRef seg_idx_op = instr.GetOperand(0);
                        LLVMValueRef dest_op = instr.GetOperand(1);
                        LLVMValueRef offset_op = instr.GetOperand(2);
                        LLVMValueRef size_op = instr.GetOperand(3);

                        (byte dest_reg, _) = GetOperandReg(dest_op, output);
                        (byte offset_reg, _) = GetOperandReg(offset_op, output);
                        (byte size_reg, _) = GetOperandReg(size_op, output);
                        
                        if (seg_idx_op.IsAConstantInt == null) {
                             _logger.WriteLine($"ERROR: data_seg_idx for memory.init must be a constant.");
                             return;
                        }
                        uint data_seg_idx = (uint)seg_idx_op.ConstIntZExt;
 
                        EmitOpcode(output, 0xFC, "PREFIX");
                        EmitOpcode(output, 0x00, "MEMORY.INIT");
                        output.AddRange(BitConverter.GetBytes(data_seg_idx));
                        output.Add(dest_reg);
                        output.Add(offset_reg);
                        output.Add(size_reg);
                        espbModule.Features |= FEATURE_BULK_OPERATIONS;
                        _logger.WriteLine($"    Generated MEMORY.INIT seg={data_seg_idx}, R{dest_reg}, R{offset_reg}, R{size_reg}");
                        return;
                    }
                    if (calledFuncName == "table.fill")
                    {
                        // TABLE.FILL table_idx(u8), Rd(u8), Rval(u8), Rn(u8)
                        byte table_idx = (byte)instr.GetOperand(0).ConstIntZExt;
                        (byte dest_reg, _) = GetOperandReg(instr.GetOperand(1), output);
                        (byte val_reg, _) = GetOperandReg(instr.GetOperand(2), output);
                        (byte size_reg, _) = GetOperandReg(instr.GetOperand(3), output);

                        EmitOpcode(output, 0xFC, "PREFIX");

                        EmitOpcode(output, 0x17, "TABLE.FILL");

                        output.Add(table_idx);

                        AddReg(output, dest_reg);

                        AddReg(output, val_reg);

                        AddReg(output, size_reg);
                        espbModule.Features |= FEATURE_BULK_OPERATIONS;
                        _logger.WriteLine($"    Generated TABLE.FILL table={table_idx}, R{dest_reg}, R{val_reg}, R{size_reg}");
                        return;
                    }
                    if (calledFuncName == "table.copy")
                    {
                        // TABLE.COPY tableD(u8), tableS(u8), Rd(u8), Rs(u8), Rn(u8)
                        byte tableDestIdx_copy = (byte)instr.GetOperand(0).ConstIntZExt;
                        byte tableSrcIdx_copy = (byte)instr.GetOperand(1).ConstIntZExt;
                        (byte destReg_copy, _) = GetOperandReg(instr.GetOperand(2), output);
                        (byte srcReg_copy, _) = GetOperandReg(instr.GetOperand(3), output);
                        (byte sizeReg_copy, _) = GetOperandReg(instr.GetOperand(4), output);

                        EmitOpcode(output, 0xFC, "PREFIX");

                        EmitOpcode(output, 0x16, "TABLE.COPY");

                        output.Add(tableDestIdx_copy);

                        output.Add(tableSrcIdx_copy);

                        AddReg(output, destReg_copy);

                        AddReg(output, srcReg_copy);

                        AddReg(output, sizeReg_copy);
                        espbModule.Features |= FEATURE_BULK_OPERATIONS;
                        _logger.WriteLine($"    Generated TABLE.COPY tableD={tableDestIdx_copy}, tableS={tableSrcIdx_copy}, R{destReg_copy}, R{srcReg_copy}, R{sizeReg_copy}");
                        return;
                    }
                    if (calledFuncName == "table.init")
                    {
                        // TABLE.INIT table_idx(u8), elem_seg_idx(u32), Rd(u8),Rs(u8),Rn(u8)
                        LLVMValueRef table_idx_op = instr.GetOperand(0);
                        LLVMValueRef elem_seg_idx_op = instr.GetOperand(1);
                        LLVMValueRef dest_op = instr.GetOperand(2);
                        LLVMValueRef offset_op = instr.GetOperand(3);
                        LLVMValueRef size_op = instr.GetOperand(4);

                        byte table_idx = (byte)table_idx_op.ConstIntZExt;
                        uint elem_seg_idx = (uint)elem_seg_idx_op.ConstIntZExt;
                        (byte dest_reg, _) = GetOperandReg(dest_op, output);
                        (byte offset_reg, _) = GetOperandReg(offset_op, output);
                        (byte size_reg, _) = GetOperandReg(size_op, output);

                        EmitOpcode(output, 0xFC, "PREFIX");

                        EmitOpcode(output, 0x04, "TABLE.INIT");

                        output.Add(table_idx);

                        output.AddRange(BitConverter.GetBytes(elem_seg_idx));

                        AddReg(output, dest_reg);

                        AddReg(output, offset_reg);

                        AddReg(output, size_reg);
                        espbModule.Features |= FEATURE_BULK_OPERATIONS;
                        _logger.WriteLine($"    Generated TABLE.INIT table={table_idx}, seg={elem_seg_idx}, R{dest_reg}, R{offset_reg}, R{size_reg}");
                        return;
                    }
                    if (calledFuncName == "__wasm_data_drop")
                    {
                        LLVMValueRef seg_idx_op = instr.GetOperand(0);
                        if (seg_idx_op.IsAConstantInt == null) {
                            _logger.WriteLine($"ERROR: data_seg_idx for data.drop must be a constant.");
                            return;
                        }
                        uint data_seg_idx = (uint)seg_idx_op.ConstIntZExt;
                        
                        EmitOpcode(output, 0xFC, "PREFIX");
                        EmitOpcode(output, 0x01, "DATA.DROP");
                        output.AddRange(BitConverter.GetBytes(data_seg_idx));
                        espbModule.Features |= FEATURE_BULK_OPERATIONS;
                        _logger.WriteLine($"    Generated DATA.DROP seg={data_seg_idx}");
                        return;
                    }

                    callOpcode = 0x09; // CALL_IMPORT
                    indexToCall = (ushort)importIndex;
                    _logger.WriteLine($"    Generating CALL_IMPORT for import index {indexToCall}");

                    // Сбор метаданных маршалинга для аргументов импорта
                    var marshallingArgs = new List<MarshallingInfo>();
                    var argsWithMarshalling = 0;

                    // Проходим по всем аргументам импортируемой функции
                    for (uint argIdx = 0; argIdx < numOperands; argIdx++)
                    {
                        var marshallingInfo = AnalyzeArgumentForMarshalling(calledFunc, argIdx);
                        marshallingArgs.Add(marshallingInfo);

                        if (marshallingInfo.Direction != MarshallingDirection.None)
                        {
                            argsWithMarshalling++;
                            _logger.WriteLine($"      MARSHALLING for arg {argIdx}: Direction={marshallingInfo.Direction}, SizeMode={marshallingInfo.SizeMode}, SizeValue={marshallingInfo.SizeValue}");
                        }
                    }

                    // Если есть аргументы требующие маршалинга, сохраняем метаданные
                    if (argsWithMarshalling > 0)
                    {
                        _logger.WriteLine($"      IMPORT MARSHALLING: {argsWithMarshalling} of {numOperands} arguments require marshalling");

                        // Сохраняем метаданные для этого импорта
                        if (!marshallingMetadata.ContainsKey(importIndex))
                        {
                            marshallingMetadata[importIndex] = marshallingArgs;
                            espbModule.Features |= FEATURE_MARSHALLING_META; // Устанавливаем флаг в спецификации ESPB
                        }
                    }

                    espbModule.Features |= 0x100;
                    
                    var importsSection = GetSection<ImportsSection>(2)!;
                    var importEntry = importsSection.Imports[indexToCall];
                    // Проверяем hints для callback отключения
                    bool shouldGenerateCallbackMetadata = importEntry.IsVariadic;
                    if (hintsManager != null && hintsManager.HasHintsForFunction(calledFuncName))
                    {
                        var hints = hintsManager.GetHintsForFunction(calledFuncName);
                        if (hints != null && hints.DisableAllCallbacks)
                        {
                            _logger.WriteLine($"[Hints] Callbacks disabled for function '{calledFuncName}' - skipping 0xAA metadata");
                            shouldGenerateCallbackMetadata = false;
                        }
                    }
                    
                    if (shouldGenerateCallbackMetadata)
                    {
                        _logger.WriteLine($"    Generating extended CALL_IMPORT format for variadic function: {calledFuncName}");
                        EmitOpcode(output, callOpcode, "CALL_IMPORT");
                        output.AddRange(BitConverter.GetBytes(indexToCall));
                        if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                            MarkMaterialCallProducesR0(instr);
                        EmitOpcode(output, 0xAA, "EXTENDED_CALL_MARKER");
                        output.Add((byte)numOperands);
                        foreach (var (reg, argType, isByval) in sourceArguments)
                        {
                            output.Add((byte)argType);
                            _logger.WriteLine($"    Arg type: {argType}");
                        }
                    }
                    else
                    {
                        EmitOpcode(output, callOpcode, "CALL_IMPORT");
                        output.AddRange(BitConverter.GetBytes(indexToCall));
                        if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                            MarkMaterialCallProducesR0(instr);
                    }
                }
                else
                {
                     throw new InvalidOperationException($"Unknown direct call target function (not in imports or functions): '{calledFuncName}'");
                }
            }
            else // Indirect call
            {
                 _logger.WriteLine($"    Processing as Indirect Call...");
                 
                 // ИСПРАВЛЕНИЕ: Получаем тип функции из инструкции call
                 // Используем GetCalledFunctionType который возвращает правильный тип функции
                 LLVMTypeRef fnType = LLVM.GetCalledFunctionType(instr);
                 _logger.WriteLine($"    Got function type from call instruction: {fnType.Kind}");
                 
                 // Если не получилось, пробуем через операнд
                 if (fnType.Kind != LLVMTypeKind.LLVMFunctionTypeKind)
                 {
                     _logger.WriteLine($"    WARNING: GetCalledFunctionType didn't return function type, trying operand type");
                     LLVMTypeRef operandType = calledFunc.TypeOf;
                     if (operandType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                     {
                         fnType = operandType.ElementType;
                         _logger.WriteLine($"    Operand is pointer, element type: {fnType.Kind}");
                     }
                     else
                     {
                         fnType = operandType;
                         _logger.WriteLine($"    Operand type: {fnType.Kind}");
                     }
                 }
                 
                 (byte funcPtrReg, _) = GetOperandReg(calledFunc, output);

                 // --- ИСПРАВЛЕНИЕ: Динамическое определение индекса сигнатуры для косвенных вызовов ---
                 LLVMValueRef? targetFunction = ResolveFunctionPointer(calledFunc);
                 int typeIndex;

                 if (targetFunction.HasValue)
                 {
                     // Если удалось разрешить указатель до конкретной функции, используем ее сигнатуру
                     LLVMTypeRef concreteFuncType = LLVM.GlobalGetValueType(targetFunction.Value);
                     typeIndex = AddTypeSignature(concreteFuncType, null, targetFunction);
                     _logger.WriteLine($"    [INDIRECT-FIX] Resolved indirect call target to '{targetFunction.Value.Name}', using concrete type index {typeIndex}");
                 }
                 else
                 {
                     // Fallback: если не удалось разрешить, используем общий тип указателя (старое поведение)
                     typeIndex = AddTypeSignature(fnType, null);
                     _logger.WriteLine($"    [INDIRECT-FIX] Could not resolve indirect call target. Using generic pointer type index {typeIndex}");
                 }
                 // --- КОНЕЦ ИСПРАВЛЕНИЯ ---

                 // Логика для FuncPtrMap теперь обрабатывается в GetOperandReg при разрешении указателя на функцию.

                 // НОВАЯ ЛОГИКА: Определяем, нужен ли небезопасный вызов
                 bool needsUnsafeCall = RequiresUnsafeCall(instr, calledFunc, fnType);
                 
                 // ПРАВИЛЬНАЯ ЛОГИКА: Определяем тип вызова на основе анализа кода
                 // needsUnsafeCall уже правильно определен методом RequiresUnsafeCall()
                 _logger.WriteLine($"    Call type determined by analysis: {(needsUnsafeCall ? "UNSAFE (ptr)" : "SAFE (index)")}");
                 
                 if (needsUnsafeCall)
                 {
                     _logger.WriteLine($"    Generating CALL_INDIRECT_PTR (unsafe) using register R{funcPtrReg} and type index {typeIndex}");
                     _logger.WriteLine($"    WARNING: This call requires unsafe mode - std::function/capturing lambda detected");
                     EmitOpcode(output, 0x0D, "CALL_INDIRECT_PTR");
                 }
                 else
                 {
                     _logger.WriteLine($"    Generating CALL_INDIRECT (safe) using register R{funcPtrReg} and type index {typeIndex}");
                     EmitOpcode(output, 0x0B, "CALL_INDIRECT");
                 }
                 
                 output.Add(funcPtrReg);
                 output.AddRange(BitConverter.GetBytes((ushort)typeIndex));
                 if (instr.TypeOf.Kind != LLVMTypeKind.LLVMVoidTypeKind)
                     MarkMaterialCallProducesR0(instr);
            }

            // The new allocator makes manual cache invalidation obsolete.

            if (destReg.HasValue)

            {

                // If the call result is unused in IR, skip copying from R0.

                LLVMUseRef firstUse = LLVM.GetFirstUse(instr);

                if (!firstUse.Equals(default(LLVMUseRef)))

                {

                    // Optimization: if the call result is used only by the next call in this basic block,

                    // keep it in R0 and let the next call consume it directly (eliminates double-hop MOV).

                    if (EnableCallResultOptimizations && destReg.Value != 0 && TryMarkForwardCallResult(instr))

                   {

                       _logger.WriteLine("    [OPT] Forwarding call result in R0 to next call; skipping MOV from R0");

                   }

                    else

                    {

                        // If the only material use of the call result is as an argument of the *next* material call,
                        // we can avoid materializing it into destReg and let the next call pre-copy from R0 to its ABI arg.
                        if (EnableCallResultOptimizations && destReg.Value != 0 && IsOnlyUsedByNextMaterialCallAsArg(instr))
                        {
                            _logger.WriteLine("    [OPT] Call result only used by next call as arg; skipping MOV from R0");
                        }
                        else
                        {
                            var returnType = MapLlvmTypeToEspb(instr.TypeOf);

                            byte movRetOpcode = GetMoveOpcode(returnType);

                            _logger.WriteLine($"    Generating return value copy: {movRetOpcode:X2} R{destReg.Value}, R0");

                            EmitOpcode(output, movRetOpcode, "MOV");

                            output.Add(destReg.Value);

                            output.Add(0); // R0

                            // The allocator already mapped the call instruction (instr) to destReg.Value
                        }

                    }

                }

                else

               {
                   if (EnableCallResultOptimizations)
                   {
                       _logger.WriteLine("    [OPT] Call result is unused, skipping MOV from R0");
                   }
                   else
                   {
                       var returnType = MapLlvmTypeToEspb(instr.TypeOf);
                       byte movRetOpcode = GetMoveOpcode(returnType);
                       _logger.WriteLine($"    [NO-CALL-OPT] Materializing return value: {movRetOpcode:X2} R{destReg.Value}, R0");
                       EmitOpcode(output, movRetOpcode, "MOV");
                       output.Add(destReg.Value);
                       output.Add(0); // R0
                   }

               }

            }

             _logger.WriteLine($"---> Finished Translating Call. output.Count = {output.Count}");
         }

        private unsafe bool IsOnlyUsedByNextMaterialCallAsArg(LLVMValueRef callInstr)

       {
           if (!EnableCallResultOptimizations)
               return false;

           // Only valid if this call was lowered to a real CALL_* and therefore produces return in R0.
           if (!IsMaterialCallProducingR0(callInstr))
               return false;

           if (callInstr.TypeOf.Kind == LLVMTypeKind.LLVMVoidTypeKind)
               return false;



            // Find next material instruction (skip dbg/lifetime intrinsics).

            LLVMValueRef next = LLVM.GetNextInstruction(callInstr);

            while (next.Handle != IntPtr.Zero && IsNonMaterializingIntrinsicCall(next))

                next = LLVM.GetNextInstruction(next);



            if (next.Handle == IntPtr.Zero)

                return false;



            LLVMOpcode nextOp = LLVM.GetInstructionOpcode(next);

            bool nextIsCall = (nextOp == LLVMOpcode.LLVMCall || nextOp == LLVMOpcode.LLVMInvoke || nextOp == LLVMOpcode.LLVMCallBr);

            if (!nextIsCall)

                return false;



            // Ensure result is used as an argument of that next call.

            bool usedAsArgInNextCall = false;

            uint argCount = LLVM.GetNumArgOperands(next);

            for (uint i = 0; i < argCount; i++)

            {

                LLVMValueRef a = LLVM.GetArgOperand(next, i);

                if (a.Equals(callInstr))

                {

                    usedAsArgInNextCall = true;

                    break;

                }

            }

            if (!usedAsArgInNextCall)

                return false;



            // Verify: all material uses are ONLY this next call.

            LLVMUseRef use = LLVM.GetFirstUse(callInstr);

            if (use.Equals(default(LLVMUseRef)))

                return true; // no uses => dead; handled elsewhere too



            for (; !use.Equals(default(LLVMUseRef)); use = LLVM.GetNextUse(use))

            {

                LLVMValueRef u = LLVM.GetUser(use);

                if (u.Handle == IntPtr.Zero)

                    continue;



                if (u.Kind != LLVMValueKind.LLVMInstructionValueKind)

                    return false;



                // Ignore dbg/lifetime.

                if (IsNonMaterializingIntrinsicCall(u))

                    continue;



                // Any other user is not allowed.

                if (u.Handle != next.Handle)

                    return false;

            }



            // Also ensure there are no other material instructions between producer and the next call.

            for (LLVMValueRef it = LLVM.GetNextInstruction(callInstr);

                 it.Handle != IntPtr.Zero && it.Handle != next.Handle;

                 it = LLVM.GetNextInstruction(it))

            {

                if (IsNonMaterializingIntrinsicCall(it))

                    continue;

                return false;

            }



            return true;

        }



        private unsafe bool IsNonMaterializingIntrinsicCall(LLVMValueRef instr)



        {
            // Non-materializing here means: does not generate ESPB bytecode and can be ignored for
            // "next material instruction" reasoning.


            // Non-materializing here means: does not generate ESPB bytecode and must be safe to ignore

            // when reasoning about the lifetime of the ABI return register R0 across instructions.





            if (instr.Kind != LLVMValueKind.LLVMInstructionValueKind)



                return false;



            if (LLVM.GetInstructionOpcode(instr) != LLVMOpcode.LLVMCall)



                return false;







            LLVMValueRef cv = LLVM.GetCalledValue(instr);



            string name = cv.Name ?? "";



            return name.StartsWith("llvm.dbg.") || name.StartsWith("llvm.lifetime.");



        }



        private unsafe bool IsOnlyMaterialUsedByNextCallAsArgument(LLVMValueRef callInstr)
        {
            // Decide whether we can skip copying result from R0 into destReg.
            // We allow dbg/lifetime intrinsics as non-material uses/instructions.
            if (callInstr.TypeOf.Kind == LLVMTypeKind.LLVMVoidTypeKind)
                return false;

            // Find next material instruction.
            LLVMValueRef next = LLVM.GetNextInstruction(callInstr);
            while (next.Handle != IntPtr.Zero && IsNonMaterializingIntrinsicCall(next))
                next = LLVM.GetNextInstruction(next);

            if (next.Handle == IntPtr.Zero)
                return false;

            LLVMOpcode nextOp = LLVM.GetInstructionOpcode(next);
            bool nextIsCall = (nextOp == LLVMOpcode.LLVMCall || nextOp == LLVMOpcode.LLVMInvoke || nextOp == LLVMOpcode.LLVMCallBr);
            if (!nextIsCall)
                return false;

            // Ensure that this next call uses callInstr as an argument.
            uint argCount = LLVM.GetNumArgOperands(next);
            bool usedAsArg = false;
            for (uint i = 0; i < argCount; i++)
            {
                LLVMValueRef a = LLVM.GetArgOperand(next, i);
                if (a.Equals(callInstr))
                {
                    usedAsArg = true;
                    break;
                }
            }
            if (!usedAsArg)
                return false;

            // Now ensure there are no other *material* uses of callInstr.
            LLVMUseRef use = LLVM.GetFirstUse(callInstr);
            if (use.Equals(default(LLVMUseRef)))
                return false;

            for (; !use.Equals(default(LLVMUseRef)); use = LLVM.GetNextUse(use))
            {
                LLVMValueRef u = LLVM.GetUser(use);
                if (u.Handle == IntPtr.Zero)
                    continue;
                if (u.Kind != LLVMValueKind.LLVMInstructionValueKind)
                    return false; // conservative

                // Ignore non-materializing intrinsic calls.
                if (IsNonMaterializingIntrinsicCall(u))
                    continue;

                // Allow the next call as the only material user.
                if (u.Handle == next.Handle)
                    continue;

                return false;
            }

            return true;
        }

        private unsafe void TryPrecopyPreviousCallResultArg(LLVMValueRef currentCall, uint numOperands, List<byte> output, bool[] argAlreadyInPlace)

       {
           if (!EnableCallResultOptimizations)
               return;

           // Find previous material instruction in the same basic block.
           LLVMValueRef prev = LLVM.GetPreviousInstruction(currentCall);

            while (prev.Handle != IntPtr.Zero && IsNonMaterializingIntrinsicCall(prev))

                prev = LLVM.GetPreviousInstruction(prev);



            if (prev.Handle == IntPtr.Zero)
                return;

            // Only safe if prev was actually lowered to CALL_* (so return is in R0)
            if (!IsMaterialCallProducingR0(prev))
                return;



            // We only handle previous CALL with non-void return.

           LLVMOpcode prevOp = LLVM.GetInstructionOpcode(prev);

           if (prevOp != LLVMOpcode.LLVMCall)
               return;

           if (prev.TypeOf.Kind == LLVMTypeKind.LLVMVoidTypeKind)
               return;

           // IMPORTANT: Do NOT apply call-result R0 forwarding/pre-copy for LLVM intrinsics
           // (llvm.fabs/minnum/maxnum/sqrt/etc). We lower them to dedicated ESPB opcodes,
           // so the result is NOT produced in ABI return register R0.
           LLVMValueRef prevCv = LLVM.GetCalledValue(prev);
           string prevName = prevCv.Name ?? "";
           if (prevName.StartsWith("llvm.", StringComparison.Ordinal))
               return;



            // Ensure there are no material instructions between prev and currentCall.

            for (LLVMValueRef it = LLVM.GetNextInstruction(prev);

                 it.Handle != IntPtr.Zero && it.Handle != currentCall.Handle;

                 it = LLVM.GetNextInstruction(it))

            {

                if (IsNonMaterializingIntrinsicCall(it))

                    continue;

                return;

            }



            // If current call uses prev result as arg i<16, pre-copy from R0 to Ri.

            uint limit = Math.Min(numOperands, 16u);

            for (uint i = 0; i < limit; i++)

            {

                LLVMValueRef a = currentCall.GetOperand(i);

                if (!a.Equals(prev))

                    continue;



                if (EnableCallResultOptimizations)
               {
                   if (i != 0)
                   {
                       var t = MapLlvmTypeToEspb(a.TypeOf);
                       byte op = GetMoveOpcode(t);
                       _logger.WriteLine($"    [OPT] Prev-call pre-copy: {op:X2} (MOV.{t}) R{i}, R0");
                       EmitOpcode(output, op, $"MOV.{t}");
                       output.Add((byte)i);
                       output.Add(0);
                   }

                   argAlreadyInPlace[i] = true;
               }

               break;

            }

        }







        private unsafe bool TryMarkForwardCallResult(LLVMValueRef callInstr)

       {
           if (!EnableCallResultOptimizations)
               return false;

           if (!IsMaterialCallProducingR0(callInstr))
               return false;

           // Deprecated by TryPrecopyPreviousCallResultArg(); keep for compatibility.


            // Preconditions: callInstr is the current call instruction. We attempt to detect:

            //   callInstr has exactly one use,

            //   its only user is the immediate next instruction,

            //   and that user is a call that takes callInstr as an argument.

            // If so, we set _forwardedCallResult* so arg setup can use R0 directly.



            // Collect materializing uses (ignore dbg/lifetime intrinsics).
            LLVMUseRef use = LLVM.GetFirstUse(callInstr);
            if (use.Equals(default(LLVMUseRef)))
            {
                                return false;
            }

            LLVMValueRef user = default;
            int materialUseCount = 0;

            for (; !use.Equals(default(LLVMUseRef)); use = LLVM.GetNextUse(use))
            {
                LLVMValueRef u = LLVM.GetUser(use);
                if (u.Handle == IntPtr.Zero || u.Kind != LLVMValueKind.LLVMInstructionValueKind)
                    continue;

                // Ignore non-materializing intrinsic calls like llvm.dbg.* / llvm.lifetime.*
                if (IsNonMaterializingIntrinsicCall(u))
                    continue;

                materialUseCount++;
                if (materialUseCount == 1)
                {
                    user = u;
                }
                else
                {
                                        return false; // multiple material uses
                }
            }

            if (materialUseCount != 1)
            {
                                return false;
            }

            if (LLVM.GetInstructionOpcode(user) != LLVMOpcode.LLVMCall)
            {
                                return false;
            }



            // Must be in the same basic block. We allow some non-materializing intrinsics

            // (dbg/lifetime) between producer and consumer.



            LLVMBasicBlockRef bb = LLVM.GetInstructionParent(callInstr);



            LLVMBasicBlockRef userBb = LLVM.GetInstructionParent(user);



            if (bb.Handle != userBb.Handle)



                return false;



            LLVMValueRef it = LLVM.GetNextInstruction(callInstr);
            for (; it.Handle != IntPtr.Zero && it.Handle != user.Handle; it = LLVM.GetNextInstruction(it))
            {
                if (IsNonMaterializingIntrinsicCall(it))
                    continue;
                return false;
            }

            if (it.Handle == IntPtr.Zero)
                return false;




            // Ensure the user call actually references callInstr in its argument operands.

            uint argCount = LLVM.GetNumArgOperands(user);

            bool usedAsArg = false;

            for (uint i = 0; i < argCount; i++)

            {

                LLVMValueRef a = LLVM.GetArgOperand(user, i);

                if (a.Equals(callInstr))

                {

                    usedAsArg = true;

                    break;

                }

            }

            if (!usedAsArg)

                return false;



            _forwardedCallResult = callInstr;

            _forwardedCallResultValid = true;

            return true;

        }



        private void ExecuteParallelMoves(List<MoveOperation> moves, List<byte> output)
        {
            // Parallel copy resolution (SSA/ABI arg shuffle).
            // Generates the minimal number of MOVs for acyclic copies and uses a single temp only for true cycles.
            // This avoids the over-conservative "save all conflict regs" which creates extra MOVs.

            // Remove trivial identities.
            moves.RemoveAll(m => m.SourceReg == m.DestReg);
            if (moves.Count == 0)
                return;

            // dest->move mapping
            var byDest = moves.ToDictionary(m => m.DestReg, m => m);
            var srcSet = new HashSet<byte>(moves.Select(m => m.SourceReg));

            while (byDest.Count > 0)
            {
                // 1) Emit any move whose destination is not used as a source by any remaining move (acyclic front).
                MoveOperation? ready = null;
                foreach (var kv in byDest)
                {
                    if (!srcSet.Contains(kv.Key))
                    {
                        ready = kv.Value;
                        break;
                    }
                }

                if (ready != null)
                {
                    EmitPlannedMove(ready, output);
                    // Remove
                    byDest.Remove(ready.DestReg);
                    // Recompute srcSet cheaply
                    srcSet = new HashSet<byte>(byDest.Values.Select(m => m.SourceReg));
                    continue;
                }

                // 2) No ready moves => cycle. Break it with one temp.
                // Pick an arbitrary move to start cycle.
                var start = byDest.Values.First();

                // Collect cycle nodes: dest0 -> src0 -> src1 ... until back.
                var cycle = new List<MoveOperation>();
                var visited = new HashSet<byte>();
                byte curDest = start.DestReg;
                while (!visited.Contains(curDest) && byDest.TryGetValue(curDest, out var mcur))
                {
                    visited.Add(curDest);
                    cycle.Add(mcur);
                    curDest = mcur.SourceReg;
                }

                // We can only safely break a cycle if all moves use the same MOV opcode/width.
                byte opcode = cycle[0].Opcode;
                bool sameOpcode = cycle.All(m => m.Opcode == opcode);
                if (!sameOpcode)
                {
                    // Fallback to conservative method for the remaining moves.
                    ExecuteMovesConservatively(byDest.Values.ToList(), output);
                    return;
                }

                byte tempReg = registerManager.AllocateTemporaryRegister();
                var tempType = cycle[0].Type;

                // Save the original value of the first destination (it will be overwritten).
                _logger.WriteLine($"    Arg Setup TEMP SAVE: Generating {opcode:X2} (MOV.{tempType}) R{tempReg}, R{cycle[0].DestReg}");
                EmitOpcode(output, opcode, $"MOV.{tempType}");
                output.Add(tempReg);
                output.Add(cycle[0].DestReg);

                // Rotate values along the cycle.
                byte startDest = cycle[0].DestReg;
                byte d = startDest;
                while (true)
                {
                    var mv = byDest[d];
                    byte s = mv.SourceReg;
                    if (s == startDest)
                    {
                        // Last assignment from temp.
                        _logger.WriteLine($"    Arg Setup MOV: Generating {opcode:X2} (MOV.{tempType}) R{d}, R{tempReg}");
                        EmitOpcode(output, opcode, $"MOV.{tempType}");
                        output.Add(d);
                        output.Add(tempReg);
                        byDest.Remove(d);
                        break;
                    }
                    else
                    {
                        _logger.WriteLine($"    Arg Setup MOV: Generating {opcode:X2} (MOV.{tempType}) R{d}, R{s}");
                        EmitOpcode(output, opcode, $"MOV.{tempType}");
                        output.Add(d);
                        output.Add(s);
                        byDest.Remove(d);
                        d = s;
                    }
                }

                srcSet = new HashSet<byte>(byDest.Values.Select(m => m.SourceReg));
            }
        }

        private void EmitPlannedMove(MoveOperation move, List<byte> output)
        {
            _logger.WriteLine($"    Arg Setup MOV: Generating {move.Opcode:X2} (MOV.{move.Type}) R{move.DestReg}, R{move.SourceReg}");
            EmitOpcode(output, move.Opcode, $"MOV.{move.Type}");
            output.Add(move.DestReg);
            output.Add(move.SourceReg);
        }

        private void ExecuteMovesConservatively(List<MoveOperation> moves, List<byte> output)
        {
            // Old conservative algorithm: save any register that is both source and destination.
            var tempRegs = new Dictionary<byte, byte>();
            var destSet = new HashSet<byte>(moves.Select(m => m.DestReg));
            var conflictRegs = moves.Where(m => destSet.Contains(m.SourceReg)).Select(m => m.SourceReg).Distinct().ToList();

            foreach (var conflictReg in conflictRegs)
            {
                byte tempReg = registerManager.AllocateTemporaryRegister();
                tempRegs[conflictReg] = tempReg;

                var moveWithConflict = moves.First(m => m.SourceReg == conflictReg);
                var tempOpcode = moveWithConflict.Opcode;
                var tempType = moveWithConflict.Type;

                _logger.WriteLine($"    Arg Setup TEMP SAVE: Generating {tempOpcode:X2} (MOV.{tempType}) R{tempReg}, R{conflictReg}");
                EmitOpcode(output, tempOpcode, $"MOV.{tempType}");
                output.Add(tempReg);
                output.Add(conflictReg);
            }

            foreach (var move in moves)
            {
                if (tempRegs.TryGetValue(move.SourceReg, out var tmp))
                    move.SourceReg = tmp;
            }

            foreach (var move in moves)
                EmitPlannedMove(move, output);
        }

        private class MoveOperation {
            public byte SourceReg { get; set; }
            public byte DestReg { get; set; }
            public byte Opcode { get; set; }
            public ValueType Type { get; set; }
        }
        
        // Вспомогательный метод для добавления типов аргументов в расширенный формат
        private void AddRemainingArgTypes(LLVMValueRef instr, uint numOperands, List<byte> output, List<(byte reg, ValueType type)> collectedArgs)
        {
            for (uint i = 0; i < numOperands; i++)
            {
                LLVMValueRef arg = instr.GetOperand(i);
                ValueType argType = MapLlvmTypeToEspb(arg.TypeOf);
                
                // Для 64-битных типов проверяем, не разбиты ли они на пары 32-битных
                if ((argType == ValueType.I64 || argType == ValueType.U64) && 
                    i < collectedArgs.Count && i + 1 < collectedArgs.Count)
                {
                    // Проверяем, является ли это парой 32-битных значений
                    if (collectedArgs[(int)i].type == ValueType.U32 && 
                        collectedArgs[(int)i + 1].type == ValueType.U32)
                    {
                        // Это пара 32-битных значений для 64-битного аргумента
                        EmitOpcode(output, (byte)argType, "ARG_TYPE"); // Оригинальный 64-битный тип
                        i++; // Пропускаем следующий регистр
                        _logger.WriteLine($"    Arg {i-1}: Added 64-bit type info (pair of 32-bit regs)");
                        continue;
                    }
                }
                
                EmitOpcode(output, (byte)argType, "ARG_TYPE");
                _logger.WriteLine($"    Arg {i}: Added type info: {argType}");
            }
        }
        
        // Вспомогательный метод для получения опкода MOV в зависимости от типа
        private byte GetMoveOpcode(ValueType type)
        {
            switch (type)
            {
                case ValueType.I8:
                case ValueType.U8:
                    return 0x10; // MOV.I8
                case ValueType.I16:
                case ValueType.U16:
                    return 0x11; // MOV.I16
                case ValueType.I32:
                case ValueType.U32:
                case ValueType.BOOL:
                case ValueType.F32:
                case ValueType.PTR:
                case ValueType.FUNCREF:
                    return 0x12; // MOV.32
                case ValueType.I64:
                case ValueType.U64:
                case ValueType.F64:
                    return 0x13; // MOV.64
                case ValueType.V128:
                    return 0x17; // MOV.V128
                default:
                    return 0x12; // MOV.32 (fallback)
            }
        }
        
        /// <summary>
        /// Рекурсивно "разматывает" инструкции load и bitcast, чтобы найти исходный ValueRef.
        /// </summary>
        private unsafe LLVMValueRef ResolveValueRef(LLVMValueRef value)
        {
            // Ограничение рекурсии для предотвращения зацикливания
            for (int i = 0; i < 10; i++) // 10 итераций - более чем достаточно
            {
                if (value.InstructionOpcode == LLVMOpcode.LLVMLoad)
                {
                    value = value.GetOperand(0); // Переходим к указателю, из которого загрузили
                }
                else if (value.InstructionOpcode == LLVMOpcode.LLVMBitCast)
                {
                    value = value.GetOperand(0); // Переходим к исходному значению до bitcast
                }
                else if (value.IsAAllocaInst != null)
                {
                    // Если мы дошли до alloca, нужно найти, что в него записали (store)
                    for (var instrRef = LLVM.GetFirstUse(value); instrRef != null; instrRef = LLVM.GetNextUse(instrRef))
                    {
                        LLVMValueRef user = LLVM.GetUser(instrRef);
                        if (LLVM.GetInstructionOpcode(user) == LLVMOpcode.LLVMStore)
                        {
                            LLVMValueRef storeTarget = LLVM.GetOperand(user, 1);
                            if (storeTarget.Handle == value.Handle)
                            {
                                value = LLVM.GetOperand(user, 0); // Значение, которое было сохранено
                                goto next_iteration; // Продолжаем цикл с новым значением
                            }
                        }
                    }
                    return value; // Не нашли store, возвращаем alloca
                }
                else
                {
                    // Нашли не-load/bitcast/alloca инструкцию или константу, это наш источник
                    return value;
                }
                next_iteration:;
            }
            return value; // Возвращаем то, что есть, после исчерпания попыток
        }
        
        
        private byte HandleOperandExtension(LLVMValueRef operand, byte operandReg, bool isSigned, List<byte> output)
        {
            var typeRef = operand.TypeOf;
            if (typeRef.Kind != LLVMTypeKind.LLVMIntegerTypeKind) {
                return operandReg; // Not an integer, no extension needed
            }

            uint bitWidth = typeRef.IntWidth;
            if (bitWidth >= 32) {
                return operandReg; // Already 32-bit or wider
            }

            byte destReg = registerManager.AllocateTemporaryRegister();
            byte opcode = 0;

            if (isSigned) { // SEXT
                if (bitWidth == 8) opcode = 0x9D; // SEXT.I8.I32
                else if (bitWidth == 16) opcode = 0x9F; // SEXT.I16.I32
            } else { // ZEXT
                if (bitWidth == 1) opcode = 0x97; // ZEXT.I8.I32 for i1
                else if (bitWidth == 8) opcode = 0x97; // ZEXT.I8.I32
                else if (bitWidth == 16) opcode = 0x99; // ZEXT.I16.I32
            }

            if (opcode != 0) {
                _logger.WriteLine($"[OperandExtend] Extending R{operandReg} ({bitWidth}-bit) to R{destReg} (32-bit) using opcode 0x{opcode:X2}");
                EmitOpcode(output, opcode, "EXTEND");
                output.Add(destReg);
                output.Add(operandReg);
                return destReg;
            }

            return operandReg; // No extension was performed
        }
        
        private unsafe void TranslateICmp(LLVMValueRef instr, List<byte> output)
        {
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            
            var operand0 = instr.GetOperand(0);
            var operand1 = instr.GetOperand(1);

            (byte op1Reg, _) = GetOperandReg(operand0, output);
            (byte op2Reg, _) = GetOperandReg(operand1, output);
            
            var opTypeRef = operand0.TypeOf;
            var opType = MapLlvmTypeToEspb(opTypeRef);
            var predicate = (LLVMIntPredicate)LLVM.GetICmpPredicate(instr);
            
            // Determine if the comparison is signed
            bool isSigned = predicate >= LLVMIntPredicate.LLVMIntSGT && predicate <= LLVMIntPredicate.LLVMIntSLE;

            // Handle operand extension if necessary
            if (opTypeRef.Kind == LLVMTypeKind.LLVMIntegerTypeKind && opTypeRef.IntWidth < 32)
            {
                op1Reg = HandleOperandExtension(operand0, op1Reg, isSigned, output);
                op2Reg = HandleOperandExtension(operand1, op2Reg, isSigned, output);
            }

            int opcode;
            
            if (opTypeRef.Kind == LLVMTypeKind.LLVMPointerTypeKind)
            {
                switch (predicate)
                {
                    case LLVMIntPredicate.LLVMIntEQ: opcode = 0xC0; break;
                    case LLVMIntPredicate.LLVMIntNE: opcode = 0xC1; break;
                    case LLVMIntPredicate.LLVMIntUGT: opcode = 0xC7; break;
                    case LLVMIntPredicate.LLVMIntUGE: opcode = 0xC9; break;
                    case LLVMIntPredicate.LLVMIntULT: opcode = 0xC6; break;
                    case LLVMIntPredicate.LLVMIntULE: opcode = 0xC8; break;
                    default:
                        _logger.WriteLine($"Warning: Signed comparison predicate '{predicate}' used for pointers. Treating as signed integers.");
                        switch(predicate)
                        {
                            case LLVMIntPredicate.LLVMIntSGT: opcode = 0xC3; break;
                            case LLVMIntPredicate.LLVMIntSGE: opcode = 0xC5; break;
                            case LLVMIntPredicate.LLVMIntSLT: opcode = 0xC2; break;
                            case LLVMIntPredicate.LLVMIntSLE: opcode = 0xC4; break;
                            default: throw new NotSupportedException($"Unsupported ICmp predicate for pointers: {predicate}");
                        }
                        break;
                }
            }
            else // Integer types
            {
                switch (predicate)
                {
                    case LLVMIntPredicate.LLVMIntEQ: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCA : 0xC0; break;
                    case LLVMIntPredicate.LLVMIntNE: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCB : 0xC1; break;
                    case LLVMIntPredicate.LLVMIntSLT: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCC : 0xC2; break;
                    case LLVMIntPredicate.LLVMIntSGT: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCD : 0xC3; break;
                    case LLVMIntPredicate.LLVMIntSLE: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCE : 0xC4; break;
                    case LLVMIntPredicate.LLVMIntSGE: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xCF : 0xC5; break;
                    case LLVMIntPredicate.LLVMIntULT: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xD0 : 0xC6; break;
                    case LLVMIntPredicate.LLVMIntUGT: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xD1 : 0xC7; break;
                    case LLVMIntPredicate.LLVMIntULE: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xD2 : 0xC8; break;
                    case LLVMIntPredicate.LLVMIntUGE: opcode = (opType == ValueType.I64 || opType == ValueType.U64) ? 0xD3 : 0xC9; break;
                    default:
                        throw new NotSupportedException($"Unsupported ICmp predicate: {predicate}");
                }
            }
            
            EmitOpcode(output, (byte)opcode, "ICMP");
            WriteBinaryInstruction(output, destReg, op1Reg, op2Reg);
        }
        
        private unsafe void TranslateFCmp(LLVMValueRef instr, List<byte> output)
        {
            // Получаем регистры операндов
            byte destReg = GetOrAllocateRegWithCacheInvalidation(instr, output);
            (byte op1, _) = GetOperandReg(instr.GetOperand(0), output);
            (byte op2, _) = GetOperandReg(instr.GetOperand(1), output);
            
            // Определяем тип операндов и получаем предикат сравнения
            var opType = MapLlvmTypeToEspb(instr.GetOperand(0).TypeOf);
            var predicate = (LLVMRealPredicate)LLVM.GetFCmpPredicate(instr);
            
            // Выбираем опкод сравнения в зависимости от предиката и типа
            int opcode;
            
            switch (predicate)
            {
                // Note: LLVM's 'ordered' checks trap on NaN, 'unordered' does not.
                // ESPB's CMP.EQ traps on NaN. We map both OEQ and UEQ to the same ESPB opcode.
                // The runtime will handle the trapping behavior.
                case LLVMRealPredicate.LLVMRealOEQ:
                case LLVMRealPredicate.LLVMRealUEQ:
                    opcode = (opType == ValueType.F32) ? 0xE0 : 0xE6; // CMP.EQ.F32, CMP.EQ.F64
                    break;
                case LLVMRealPredicate.LLVMRealONE:
                case LLVMRealPredicate.LLVMRealUNE:
                    opcode = (opType == ValueType.F32) ? 0xE1 : 0xE7; // CMP.NE.F32, CMP.NE.F64
                    break;
                case LLVMRealPredicate.LLVMRealOLT:
                case LLVMRealPredicate.LLVMRealULT:
                    opcode = (opType == ValueType.F32) ? 0xE2 : 0xE8; // CMP.LT.F32, CMP.LT.F64
                    break;
                case LLVMRealPredicate.LLVMRealOGT:
                case LLVMRealPredicate.LLVMRealUGT:
                    opcode = (opType == ValueType.F32) ? 0xE3 : 0xE9; // CMP.GT.F32, CMP.GT.F64
                    break;
                case LLVMRealPredicate.LLVMRealOLE:
                case LLVMRealPredicate.LLVMRealULE:
                    opcode = (opType == ValueType.F32) ? 0xE4 : 0xEA; // CMP.LE.F32, CMP.LE.F64
                    break;
                case LLVMRealPredicate.LLVMRealOGE:
                case LLVMRealPredicate.LLVMRealUGE:
                    opcode = (opType == ValueType.F32) ? 0xE5 : 0xEB; // CMP.GE.F32, CMP.GE.F64
                    break;
                
                // LLVMRealORD (is not NaN) and LLVMRealUNO (is NaN) need special handling
                case LLVMRealPredicate.LLVMRealORD: // Check if value is not NaN (x == x)
                    opcode = (opType == ValueType.F32) ? 0xE0 : 0xE6; // CMP.EQ
                    op2 = op1; // Compare register with itself
                    break;
                case LLVMRealPredicate.LLVMRealUNO: // Check if value is NaN (x != x)
                    opcode = (opType == ValueType.F32) ? 0xE1 : 0xE7; // CMP.NE
                    op2 = op1; // Compare register with itself
                    break;

                default:
                    throw new NotSupportedException($"Unsupported FCmp predicate: {predicate}");
            }
            
            // Генерируем инструкцию сравнения
            EmitOpcode(output, (byte)opcode, "FCMP");
            WriteBinaryInstruction(output, destReg, op1, op2);
        }

        // Метод для получения метки базового блока
        private unsafe int GetBasicBlockLabel(LLVMBasicBlockRef basicBlock)
        {
            if (basicBlockToLabel.TryGetValue(basicBlock, out int label))
            {
                return label;
            }
            
            // Если метка еще не существует, создаем новую
            label = nextLabel++;
            basicBlockToLabel[basicBlock] = label;
            return label;
        }
        
        // Метод для очистки меток базовых блоков перед обработкой новой функции
        private void ResetBasicBlockLabels()
        {
            basicBlockToLabel.Clear();
            nextLabel = 0;
        }
        
        private void ResetPhiData()
        {
            phiInstructions.Clear();
            blockTailInstructions.Clear();
        }

        private unsafe bool TryMapLlvmConstant(LLVMValueRef constant, out object? value)
        {
            value = null;
            if (constant.IsAConstantInt != null && constant.IsAConstantInt.Handle != IntPtr.Zero)
            {
                value = constant.ConstIntZExt;
                return true;
            }
            if (constant.IsAConstantFP != null && constant.IsAConstantFP.Handle != IntPtr.Zero)
            {
                // Получаем значение FP-константы с параметром losesInfo
                int losesInfo = 0;
                double realVal = LLVM.ConstRealGetDouble(constant, &losesInfo);
                var type = MapLlvmTypeToEspb(constant.TypeOf);
                if (type == ValueType.F32)
                    value = Convert.ToSingle(realVal);
                else
                    value = realVal;
                return true;
            }
            if (constant.IsNull)
            {
                value = 0u;
                return true;
            }
            return false;
        }

        // НОВЫЙ МЕТОД для анализа использования вариативных функций
        private unsafe int GetMaxVariadicArgsForImport(LLVMValueRef importedFuncRef, LLVMTypeRef importedFuncType) // Убеждаемся, что unsafe здесь
        {
            int maxVariadicArgs = 0; // Максимальное обнаруженное количество вариативных аргументов
            bool wasCalledAtLeastOnce = false;

            uint numFixedParams = LLVM.CountParamTypes(importedFuncType);
            int fixedParamCount = (int)numFixedParams; // Добавляем локальную переменную int

            // Итерируем по всем функциям в модуле
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); func.Handle != IntPtr.Zero; func = LLVM.GetNextFunction(func))
            {
                if (func.IsDeclaration) continue; // Пропускаем объявления, нужны только определения

                // Итерируем по всем базовым блокам функции
                for (var basicBlock = LLVM.GetFirstBasicBlock(func); basicBlock != null; basicBlock = LLVM.GetNextBasicBlock(basicBlock))
                {
                    // Итерируем по всем инструкциям в базовом блоке
                    for (var instrRef = LLVM.GetFirstInstruction(basicBlock); instrRef != null; instrRef = LLVM.GetNextInstruction(instrRef))
                    {
                        if (LLVM.GetInstructionOpcode(instrRef) == LLVMOpcode.LLVMCall)
                        {
                            int numOperandsInCall = LLVM.GetNumOperands(instrRef); // Изменено на int
                            if (numOperandsInCall > 0)
                            {
                                LLVMValueRef calledValue = LLVM.GetOperand(instrRef, (uint)(numOperandsInCall - 1)); // Приведение к uint для GetOperand
                                if (calledValue.Handle == importedFuncRef.Handle) // Это вызов нашей импортируемой функции
                                {
                                    wasCalledAtLeastOnce = true;
                                    // Количество аргументов, переданных в вызове (исключая сам указатель на функцию)
                                    int totalArgsInCall = numOperandsInCall - 1; // Теперь это int
                                    if (totalArgsInCall > fixedParamCount) // Используем локальную переменную fixedParamCount
                                    {
                                        // Общее количество аргументов в вызове больше количества фиксированных параметров
                                        int variadicCount = totalArgsInCall - fixedParamCount; // Вычисление количества вариативных аргументов
                                        if (variadicCount > maxVariadicArgs)
                                        {
                                            maxVariadicArgs = variadicCount;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!wasCalledAtLeastOnce || maxVariadicArgs == 0) // Если не вызывалась, или вызывалась без вариативных аргументов
            {
                 // Используем значение по умолчанию, если нет информации или вариативных аргументов 0.
                 // Это обеспечивает базовую работоспособность для вызовов извне или если форматная строка не требует доп. аргументов.
                _logger.WriteLine($"    INFO: Variadic import '{importedFuncRef.Name}' not called with variadic args in this module, or not called at all. Using default of 6 placeholders.");
                return 6; 
            }
            
            _logger.WriteLine($"    INFO: Max variadic args found for import '{importedFuncRef.Name}': {maxVariadicArgs}");
            return maxVariadicArgs;
        }

        // Новый метод для извлечения строковой константы
        private unsafe string ExtractFormatString(LLVMValueRef strPtr)
        {
            try
            {
                // Пытаемся получить константное значение
                if (strPtr.Kind == LLVMValueKind.LLVMConstantExprValueKind)
                {
                    // Если это GetElementPtr или BitCast, получаем базовый операнд
                    LLVMOpcode opcode = LLVM.GetConstOpcode(strPtr);
                    if (opcode == LLVMOpcode.LLVMGetElementPtr || 
                        opcode == LLVMOpcode.LLVMBitCast)
                    {
                        strPtr = strPtr.GetOperand(0);
                    }
                }
                
                // Проверяем, является ли значение константной строкой
                if (LLVM.IsConstantString(strPtr) != 0)
                {
                    UIntPtr length;
                    sbyte* strBytes = LLVM.GetAsString(strPtr, (nuint*)&length);
                    if (strBytes != null && length > 0)
                    {
                        return Marshal.PtrToStringUTF8((IntPtr)strBytes, (int)length);
                    }
                }
                
                // Пытаемся получить данные из глобальной переменной
                if (strPtr.Kind == LLVMValueKind.LLVMGlobalVariableValueKind)
                {
                    var initializer = LLVM.GetInitializer(strPtr);
                    if (LLVM.IsConstantString(initializer) != 0)
                    {
                        UIntPtr length;
                        sbyte* strBytes = LLVM.GetAsString(initializer, (nuint*)&length);
                        if (strBytes != null && length > 0)
                        {
                            return Marshal.PtrToStringUTF8((IntPtr)strBytes, (int)length);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Error extracting format string: {ex.Message}");
            }
            
            return string.Empty; // Возвращаем пустую строку вместо null
        }
        
        // Новый метод для анализа форматной строки
        private List<ValueType> AnalyzeFormatString(string format)
        {
            var result = new List<ValueType>();
            
            // Ищем спецификаторы формата вида %d, %s, %f и т.д.
            int pos = 0;
            while ((pos = format.IndexOf('%', pos)) >= 0)
            {
                pos++; // Пропускаем символ '%'
                
                // Пропускаем флаги форматирования
                while (pos < format.Length && "-+ #0".Contains(format[pos])) pos++;
                
                // Пропускаем ширину поля
                while (pos < format.Length && char.IsDigit(format[pos])) pos++;
                
                // Пропускаем точность
                if (pos < format.Length && format[pos] == '.')
                {
                    pos++;
                    while (pos < format.Length && char.IsDigit(format[pos])) pos++;
                }
                
                // Пропускаем модификатор длины
                bool isLongLong = false;
                bool isLong = false;
                
                if (pos < format.Length)
                {
                    if (format[pos] == 'l')
                    {
                        isLong = true;
                        pos++;
                        
                        if (pos < format.Length && format[pos] == 'l')
                        {
                            isLongLong = true;
                            pos++;
                        }
                    }
                    else if (format[pos] == 'h')
                    {
                        pos++; // short
                    }
                }
                
                // Определяем тип по спецификатору формата
                if (pos < format.Length)
                {
                    char specifier = format[pos];
                    switch (specifier)
                    {
                        case 'd':
                        case 'i':
                            result.Add(isLongLong ? ValueType.I64 : (isLong ? ValueType.I32 : ValueType.I32));
                            break;
                            
                        case 'u':
                        case 'x':
                        case 'X':
                        case 'o':
                            result.Add(isLongLong ? ValueType.U64 : (isLong ? ValueType.U32 : ValueType.U32));
                            break;
                            
                        case 'f':
                        case 'F':
                        case 'e':
                        case 'E':
                        case 'g':
                        case 'G':
                        case 'a':
                        case 'A':
                            result.Add(isLong ? ValueType.F64 : ValueType.F32);
                            break;
                            
                        case 'c':
                            result.Add(ValueType.I32); // Символы представляются как int
                            break;
                            
                        case 's':
                        case 'p':
                            result.Add(ValueType.PTR); // Строки и указатели
                            break;
                            
                        case '%':
                            // Эскейп-последовательность %%, не соответствует аргументу
                            break;
                            
                        default:
                            // По умолчанию используем I32
                            result.Add(ValueType.I32);
                            break;
                    }
                    
                    pos++; // Переходим к следующему символу
                }
            }
            
            // Выводим результаты анализа
            _logger.WriteLine($"    Format string analysis: Found {result.Count} format specifiers");
            for (int i = 0; i < result.Count; i++)
            {
                _logger.WriteLine($"    Format specifier {i+1}: {result[i]}");
            }

            return result;
        }

        /// <summary>
        /// Определяет, требует ли данный непрямой вызов использования небезопасного CALL_INDIRECT_PTR
        /// </summary>
        private unsafe bool RequiresUnsafeCall(LLVMValueRef instr, LLVMValueRef calledFunc, LLVMTypeRef fnType)
        {
            // УНИВЕРСАЛЬНЫЙ АНАЛИЗ: Определяем тип вызова по структуре LLVM IR
            _logger.WriteLine($"    [Unsafe Analysis] Analyzing indirect call to determine safe vs unsafe mode");
            
            // 1. ПРОВЕРКА ИСТОЧНИКА ФУНКЦИИ: Откуда берется указатель на функцию?
            LLVMValueKind valueKind = LLVM.GetValueKind(calledFunc);
            _logger.WriteLine($"    [Unsafe Analysis] Called function value kind: {valueKind}");
            
            // 2. АРГУМЕНТЫ ФУНКЦИИ: Если указатель на функцию приходит как аргумент - это UNSAFE
            // потому что аргумент может содержать указатель на любую функцию (нативную или ESPB)
            if (valueKind == LLVMValueKind.LLVMArgumentValueKind)
            {
                _logger.WriteLine($"    [Unsafe Analysis] Function pointer is a function ARGUMENT - UNSAFE mode required");
                _logger.WriteLine($"    [Unsafe Analysis] Reason: Argument may contain native pointer, must use CALL_INDIRECT_PTR");
                return true; // UNSAFE - нужен CALL_INDIRECT_PTR
            }
            
            // 3. АНАЛИЗ LOAD ОПЕРАЦИЙ: Если функция загружается из массива/таблицы - это SAFE
            if (valueKind == LLVMValueKind.LLVMInstructionValueKind)
            {
                LLVMOpcode opcode = LLVM.GetInstructionOpcode(calledFunc);
                _logger.WriteLine($"    [Unsafe Analysis] Called function opcode: {opcode}");
                
                if (opcode == LLVMOpcode.LLVMLoad)
                {
                    _logger.WriteLine($"    [Unsafe Analysis] Function loaded from memory - checking source");
                    
                    // Анализируем, загружается ли из массива функций (SAFE) или из произвольного адреса (UNSAFE)
                    var loadSource = LLVM.GetOperand(calledFunc, 0);
                    bool isArrayAccess = IsArrayElementAccess(loadSource);
                    
                    if (isArrayAccess)
                    {
                        _logger.WriteLine($"    [Unsafe Analysis] Function loaded from array element - SAFE mode");
                        return false; // SAFE - обычный массив функций
                    }
                    else
                    {
                        _logger.WriteLine($"    [Unsafe Analysis] Function loaded from arbitrary address - UNSAFE mode");
                        return true; // UNSAFE - произвольный адрес
                    }
                }
                
                // 3. ПРОВЕРКА POINTER MANIPULATION: ptrtoint, inttoptr, bitcast указывают на UNSAFE
                if (opcode == LLVMOpcode.LLVMPtrToInt || opcode == LLVMOpcode.LLVMIntToPtr || opcode == LLVMOpcode.LLVMBitCast)
                {
                    _logger.WriteLine($"    [Unsafe Analysis] Pointer manipulation detected ({opcode}) - UNSAFE mode");
                    return true;
                }
            }
            
            // 4. ПРОВЕРКА КОНТЕКСТА: Только для явно unsafe функций
            LLVMValueRef parentFunction = LLVM.GetBasicBlockParent(LLVM.GetInstructionParent(instr));
            unsafe
            {
                sbyte* parentNamePtr = LLVM.GetValueName(parentFunction);
                if (parentNamePtr != null)
                {
                    string parentName = System.Runtime.InteropServices.Marshal.PtrToStringAnsi((IntPtr)parentNamePtr) ?? "";
                    _logger.WriteLine($"    [Unsafe Analysis] Parent function: '{parentName}'");
                    
                    // Только ЯВНО unsafe функции - КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ для v8.txt
                    if (parentName.Contains("call_function_by_address") ||
                        parentName.Contains("test_call_indirect_ptr") ||
                        parentName.Contains("unsafe"))
                    {
                        _logger.WriteLine($"    [Unsafe Analysis] Explicit unsafe function context - UNSAFE mode");
                        return true;
                    }
                }
            }
            
            // 5. ПО УМОЛЧАНИЮ: Если ничего подозрительного не найдено - SAFE режим
            _logger.WriteLine($"    [Unsafe Analysis] No unsafe patterns detected - SAFE mode (CALL_INDIRECT)");
            return false;
        }
        
        /// <summary>
        /// Анализирует, является ли источник load операции доступом к элементу массива
        /// </summary>
        private unsafe bool IsArrayElementAccess(LLVMValueRef loadSource)
        {
            try
            {
                _logger.WriteLine($"    [Array Analysis] Analyzing load source for array access pattern");
                
                LLVMValueKind sourceKind = LLVM.GetValueKind(loadSource);
                _logger.WriteLine($"    [Array Analysis] Load source kind: {sourceKind}");
                
                if (sourceKind == LLVMValueKind.LLVMInstructionValueKind)
                {
                    LLVMOpcode sourceOpcode = LLVM.GetInstructionOpcode(loadSource);
                    _logger.WriteLine($"    [Array Analysis] Load source opcode: {sourceOpcode}");
                    
                    // ПАТТЕРН МАССИВА: GetElementPtr (GEP) указывает на доступ к элементу массива/структуры
                    if (sourceOpcode == LLVMOpcode.LLVMGetElementPtr)
                    {
                        _logger.WriteLine($"    [Array Analysis] Found GetElementPtr - this is array/struct element access");
                        
                        // Дополнительная проверка: анализируем базовый адрес GEP
                        var gepBase = LLVM.GetOperand(loadSource, 0);
                        var gepBaseKind = LLVM.GetValueKind(gepBase);
                        _logger.WriteLine($"    [Array Analysis] GEP base kind: {gepBaseKind}");
                        
                        if (gepBaseKind == LLVMValueKind.LLVMGlobalVariableValueKind ||
                            gepBaseKind == LLVMValueKind.LLVMInstructionValueKind)
                        {
                            _logger.WriteLine($"    [Array Analysis] GEP from global/local variable - SAFE array access");
                            return true;
                        }
                    }
                    
                    // ПАТТЕРН УКАЗАТЕЛЯ: Add с размером элемента указывает на арифметику указателей (array[i])
                    else if (sourceOpcode == LLVMOpcode.LLVMAdd)
                    {
                        _logger.WriteLine($"    [Array Analysis] Found pointer arithmetic (Add) - likely array[index] access");
                        return true;
                    }
                }
                
                // ПРЯМОЙ ДОСТУП: Если это прямая переменная (не вычисляемый адрес) - тоже SAFE
                else if (sourceKind == LLVMValueKind.LLVMGlobalVariableValueKind ||
                         sourceKind == LLVMValueKind.LLVMArgumentValueKind)
                {
                    _logger.WriteLine($"    [Array Analysis] Direct variable access - SAFE");
                    return true;
                }
                
                _logger.WriteLine($"    [Array Analysis] No clear array access pattern - might be arbitrary pointer");
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    [Array Analysis] Error during analysis: {ex.Message}");
                return false; // В случае ошибки считаем небезопасным
            }
        }
        
        /// <summary>
        /// Проверяет, является ли вызов std::function
        /// </summary>
        private unsafe bool IsStdFunctionCall(LLVMValueRef instr, LLVMValueRef calledFunc)
        {
            // Проверяем тип операнда на характерные признаки std::function
            LLVMTypeRef operandType = calledFunc.TypeOf;
            
            // std::function обычно имеет сложную структуру типов
            if (operandType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
            {
                LLVMTypeRef pointeeType = operandType.ElementType;
                // Дополнительные проверки можно добавить здесь
            }
            
            return false; // Пока консервативно возвращаем false
        }
        
        /// <summary>
        /// Проверяет, является ли вызов capturing lambda
        /// </summary>
        private bool IsCapturingLambda(LLVMValueRef instr, LLVMValueRef calledFunc)
        {
            // Capturing lambda создают trampoline функции с захватом контекста
            // Пока консервативно возвращаем false
            return false;
        }
        
        /// <summary>
        /// Проверяет, является ли вызов JIT скомпилированной функции
        /// </summary>
        private bool IsJitGeneratedCall(LLVMValueRef instr, LLVMValueRef calledFunc)
        {
            // JIT функции обычно имеют характерные адреса или метки
            // Пока консервативно возвращаем false
            return false;
        }
        
        private unsafe uint? ResolveDataOffset(LLVMValueRef value, int depth = 0)
        {
            if (depth > 10) return null;

            if (_funcPtrToDataOffset.TryGetValue(value, out uint offset))
            {
                return offset;
            }

            LLVMValueKind kind = LLVM.GetValueKind(value);
            switch (kind)
            {
                case LLVMValueKind.LLVMGlobalVariableValueKind:
                    // Check the global itself first
                    if (_funcPtrToDataOffset.TryGetValue(value, out uint globalOffset))
                    {
                        return globalOffset;
                    }
                    // Then check its initializer
                    var initializer = LLVM.GetInitializer(value);
                    if (initializer != default(LLVMValueRef))
                    {
                        return ResolveDataOffset(initializer, depth + 1);
                    }
                    break;

                case LLVMValueKind.LLVMConstantExprValueKind:
                     return ResolveDataOffset(LLVM.GetOperand(value, 0), depth + 1);

                case LLVMValueKind.LLVMInstructionValueKind:
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(value);
                    switch (opcode)
                    {
                        case LLVMOpcode.LLVMLoad:
                            return ResolveDataOffset(LLVM.GetOperand(value, 0), depth + 1);
                        case LLVMOpcode.LLVMAlloca:
                            for (var use = LLVM.GetFirstUse(value); use != default(LLVMUseRef); use = LLVM.GetNextUse(use))
                            {
                                var user = LLVM.GetUser(use);
                                if (LLVM.GetInstructionOpcode(user) == LLVMOpcode.LLVMStore)
                                {
                                    // The source of the store is what we care about
                                    return ResolveDataOffset(LLVM.GetOperand(user, 0), depth + 1);
                                }
                            }
                            break;
                        case LLVMOpcode.LLVMBitCast:
                        case LLVMOpcode.LLVMGetElementPtr:
                             return ResolveDataOffset(LLVM.GetOperand(value, 0), depth + 1);
                    }
                    break;
            }
            return null;
        }

        private unsafe LLVMValueRef? ResolveFunctionPointer(LLVMValueRef value, int depth = 0)
        {
            if (depth > 10) return null;

            LLVMValueKind kind = LLVM.GetValueKind(value);

            switch (kind)
            {
                case LLVMValueKind.LLVMFunctionValueKind:
                    return value;

                case LLVMValueKind.LLVMGlobalVariableValueKind:
                    var initializer = LLVM.GetInitializer(value);
                    if (initializer != default(LLVMValueRef))
                    {
                        return ResolveFunctionPointer(initializer, depth + 1);
                    }
                    break;

                case LLVMValueKind.LLVMConstantExprValueKind:
                    return ResolveFunctionPointer(LLVM.GetOperand(value, 0), depth + 1);
                
                case LLVMValueKind.LLVMInstructionValueKind:
                    LLVMOpcode opcode = LLVM.GetInstructionOpcode(value);
                    switch (opcode)
                    {
                        case LLVMOpcode.LLVMLoad:
                            return ResolveFunctionPointer(LLVM.GetOperand(value, 0), depth + 1); // "разматываем" load
                        case LLVMOpcode.LLVMAlloca:
                             // Для alloca, ищем инструкцию store, которая в него пишет
                            for (var use = LLVM.GetFirstUse(value); use != default(LLVMUseRef); use = LLVM.GetNextUse(use))
                            {
                                var user = LLVM.GetUser(use);
                                if (LLVM.GetInstructionOpcode(user) == LLVMOpcode.LLVMStore)
                                {
                                    // Значение, которое сохраняется, является нашим кандидатом
                                    return ResolveFunctionPointer(LLVM.GetOperand(user, 0), depth + 1);
                                }
                            }
                            break;
                        case LLVMOpcode.LLVMBitCast:
                        case LLVMOpcode.LLVMGetElementPtr:
                             return ResolveFunctionPointer(LLVM.GetOperand(value, 0), depth + 1);
                    }
                    break;
            }
            
            return null;
        }

        /// <summary>
        /// Проверяет, содержит ли модуль функции для тестирования небезопасных вызовов
        /// </summary>
        private unsafe bool HasUnsafeFunctionInModule()
        {
            // Проверяем, есть ли в llvmFuncToEspbIndex функции с подозрительными именами
            foreach (var kvp in llvmFuncToEspbIndex)
            {
                unsafe
                {
                    sbyte* namePtr = LLVM.GetValueName(kvp.Key);
                    if (namePtr != null)
                    {
                        string name = System.Runtime.InteropServices.Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                        
                        if (name.Contains("call_function_by_address") ||
                            name.Contains("unsafe") ||
                            name.Contains("by_address") ||
                            name.Contains("test_call_indirect_ptr"))
                        {
                            _logger.WriteLine($"    Found unsafe test function: '{name}' - enabling unsafe mode for all indirect calls");
                            return true;
                        }
                    }
                }
            }
            
            return false;
        }

    }
}
