using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using LLVMSharp.Interop;
using System.Runtime.InteropServices;

namespace ESPB_Runtime
{
    /// <summary>
    /// Менеджер для работы с секцией Import Marshalling Metadata (immeta)
    /// Отвечает за создание и управление метаданными маршалинга
    /// </summary>
    public class ImmetaManager
    {
        private ImmetaSection immetaSection;
        private Dictionary<uint, List<MarshallingInfo>> marshallingData;
        private bool sectionCreated = false;
        private HintsManager? hintsManager = null;
        private readonly ParameterAnalysis _parameterAnalysis;
        private readonly TextWriter _logger;

        public ImmetaManager(HintsManager? hintsManager = null, TextWriter? logger = null)
        {
            immetaSection = new ImmetaSection();
            marshallingData = new Dictionary<uint, List<MarshallingInfo>>();
            this.hintsManager = hintsManager;
            _logger = logger ?? Console.Out;
            // Если логгер не предоставлен, используем Console.Out по умолчанию
            _parameterAnalysis = new ParameterAnalysis(_logger);
        }
        
        /// <summary>
        /// Анализирует импортированную функцию и создает метаданные маршалинга
        /// </summary>
        /// <param name="function">LLVM функция</param>
        /// <param name="functionName">Имя функции</param>
        /// <param name="importIndex">Индекс импорта в секции Imports</param>
        public unsafe void AnalyzeImportFunction(LLVMValueRef function, string functionName, uint importIndex)
        {
            _logger.WriteLine($"=== MARSHALLING ANALYSIS for Import #{importIndex}: '{functionName}' ===");
            
            try
            {
                // НОВОЕ: Проверяем, является ли функция callback-функцией
                if (IsCallbackFunction(functionName))
                {
                    _logger.WriteLine($"*** Function '{functionName}' is a CALLBACK function - excluding from immeta ***");
                    _logger.WriteLine($"*** This function should use cbmeta instead of immeta ***");
                    _logger.WriteLine($"=== END ANALYSIS for '{functionName}' (CALLBACK EXCLUDED) ===\n");
                    return; // Исключаем из immeta
                }
                
                // Получаем количество параметров
                uint paramCount = LLVM.CountParams(function);
                _logger.WriteLine($"Function has {paramCount} parameters to analyze");
                
                var functionMarshallingInfo = new List<MarshallingInfo>();
                bool requiresMarshalling = false;
                
                // Анализируем каждый параметр
                for (uint paramIndex = 0; paramIndex < paramCount; paramIndex++)
                {
                    _logger.WriteLine($"\n--- Analyzing Parameter {paramIndex} ---");
                    
                    var marshalling = _parameterAnalysis.AnalyzeArgumentForMarshalling(function, paramIndex, functionName);
                    
                    // Определяем размер буфера если требуется маршалинг
                    if (marshalling.Direction != MarshallingDirection.None)
                    {
                        LLVMValueRef parameter = LLVM.GetParam(function, paramIndex);
                        _parameterAnalysis.DetermineBufferSize(marshalling, parameter, paramIndex, functionName);
                        
                        // ТОЛЬКО OUT и INOUT требуют специального маршалинга в runtime
                        if (marshalling.Direction == MarshallingDirection.Out || 
                            marshalling.Direction == MarshallingDirection.InOut)
                        {
                            requiresMarshalling = true;
                        }
                        
                        _logger.WriteLine($"    Parameter {paramIndex} requires marshalling: {marshalling.Direction}");
                    }
                    else
                    {
                        _logger.WriteLine($"    Parameter {paramIndex}: No marshalling required");
                    }
                    
                    functionMarshallingInfo.Add(marshalling);
                }
                
                // Проверяем hints перед сохранением результатов анализа
                bool shouldCreateImmeta = requiresMarshalling;
                List<MarshallingInfo> finalMarshallingInfo = functionMarshallingInfo;
                
                if (hintsManager != null && hintsManager.HasHintsForFunction(functionName))
                {
                    var hints = hintsManager.GetHintsForFunction(functionName);
                    if (hints != null)
                    {
                        // Проверяем есть ли явные marshalling правила или отключения immeta
                        bool hasExplicitMarshallingRules = hints.MarshallingOverrides.Count > 0;
                        bool hasDisabledImmeta = hints.DisabledImmeta.Count > 0;
                        
                        if (hasExplicitMarshallingRules || hasDisabledImmeta)
                        {
                            _logger.WriteLine($"\n*** [Hints] Function '{functionName}' has hints rules - COMBINING with automatic analysis ***");
                            
                            // ВАЖНО: Комбинируем hints правила с автоматическим анализом
                            // Начинаем с результатов автоматического анализа
                            finalMarshallingInfo = new List<MarshallingInfo>(functionMarshallingInfo);
                            
                            // Расширяем список если нужно для hints правил
                            if (hasExplicitMarshallingRules)
                            {
                                int maxParamIndex = hints.MarshallingOverrides.Keys.Max();
                                while (finalMarshallingInfo.Count <= maxParamIndex)
                                {
                                    finalMarshallingInfo.Add(new MarshallingInfo()); // Пустой marshalling
                                }
                                
                                // Применяем hints правила (они переопределяют автоматические)
                                foreach (var overrideEntry in hints.MarshallingOverrides)
                                {
                                    int paramIndex = overrideEntry.Key;
                                    var hintsMarshal = overrideEntry.Value;
                                    
                                    // Конвертируем из hints типов в транслятор типы
                                    var direction = ConvertMarshallingDirection(hintsMarshal.Direction);
                                    var sizeKind = ConvertSizeKind(hintsMarshal.SizeMode);
                                    var marshalInfo = new MarshallingInfo(direction, sizeKind, hintsMarshal.SizeValue);
                                    
                                    // ВАЖНО: Устанавливаем HandlerIndex для async обработки
                                    marshalInfo.HandlerIndex = hintsMarshal.HandlerIndex;
                                    
                                    // Переопределяем marshalling для конкретного параметра
                                    finalMarshallingInfo[paramIndex] = marshalInfo;
                                    
                                    _logger.WriteLine($"[Hints] Overriding marshalling for param {paramIndex}: {direction}");
                                }
                            }
                            
                            // Применяем отключение immeta для конкретных параметров
                            if (hints.DisabledImmeta.Count > 0)
                            {
                                foreach (int disabledParamIndex in hints.DisabledImmeta)
                                {
                                    if (disabledParamIndex < finalMarshallingInfo.Count)
                                    {
                                        _logger.WriteLine($"[Hints] Disabling immeta for param {disabledParamIndex}");
                                        finalMarshallingInfo[disabledParamIndex] = new MarshallingInfo(); // Пустой marshalling
                                    }
                                }
                            }
                            
                            _logger.WriteLine($"[Hints] Final marshalling list has {finalMarshallingInfo.Count} entries (combined automatic + hints)");
                            
                            shouldCreateImmeta = finalMarshallingInfo.Count > 0;
                        }
                        else if (hints.DisableAllImmeta)
                        {
                            _logger.WriteLine($"\n*** [Hints] Function '{functionName}' immeta DISABLED by 'no im' rule ***");
                            shouldCreateImmeta = false;
                        }
                    }
                }
                
                // Сохраняем результаты анализа
                if (shouldCreateImmeta)
                {
                    marshallingData[importIndex] = finalMarshallingInfo;
                    _logger.WriteLine($"\n*** Function '{functionName}' REQUIRES MARSHALLING - adding to immeta section ***");
                }
                else
                {
                    _logger.WriteLine($"\n*** Function '{functionName}' does not require marshalling ***");
                }
                
                _logger.WriteLine($"=== END ANALYSIS for '{functionName}' ===\n");
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"ERROR: Failed to analyze function '{functionName}': {ex.Message}");
                _logger.WriteLine($"=== END ANALYSIS for '{functionName}' (ERROR) ===\n");
            }
        }
        
        /// <summary>
        /// Создает секцию immeta на основе собранных данных маршалинга
        /// </summary>
        /// <returns>Секция immeta или null, если маршалинг не требуется</returns>
        public ImmetaSection? CreateImmetaSection()
        {
            _logger.WriteLine("\n=== CREATING IMMETA SECTION ===");
            
            if (marshallingData.Count == 0)
            {
                _logger.WriteLine("No functions require marshalling - immeta section will NOT be created");
                sectionCreated = false;
                return null;
            }
            
            _logger.WriteLine($"Creating immeta section for {marshallingData.Count} functions requiring marshalling:");
            
            foreach (var kvp in marshallingData)
            {
                uint importIndex = kvp.Key;
                var marshallingList = kvp.Value;
                
                // Подсчитываем параметры, требующие маршалинга
                var marshalledArgs = marshallingList
                    .Select((m, index) => new { Marshalling = m, Index = index })
                    .Where(x => x.Marshalling.Direction != MarshallingDirection.None)
                    .ToList();
                
                if (marshalledArgs.Count > 0)
                {
                    _logger.WriteLine($"  Import #{importIndex}: {marshalledArgs.Count} parameters need marshalling");
                    
                    // Добавляем информацию в секцию immeta
                    immetaSection.AddMarshallingInfo((ushort)importIndex, marshallingList);
                    
                    // Детальное логирование для каждого параметра
                    foreach (var arg in marshalledArgs)
                    {
                        _logger.WriteLine($"    Param {arg.Index}: Direction={arg.Marshalling.Direction}, " +
                                        $"SizeMode={arg.Marshalling.SizeMode}, SizeValue={arg.Marshalling.SizeValue}");
                    }
                }
            }
            
            _logger.WriteLine($"Immeta section created successfully with {immetaSection.NumImportsWithMeta} imports");
            _logger.WriteLine($"Section size: {immetaSection.GetSize()} bytes");
            sectionCreated = true;
            
            _logger.WriteLine("=== IMMETA SECTION CREATED ===\n");
            return immetaSection!;
        }
        
        /// <summary>
        /// Возвращает информацию о том, была ли создана секция immeta
        /// </summary>
        public bool IsSectionCreated => sectionCreated;
        
        /// <summary>
        /// Возвращает количество функций, требующих маршалинга
        /// </summary>
        public int FunctionsRequiringMarshalling => marshallingData.Count;
        
        /// <summary>
        /// Выводит сводную информацию о маршалинге
        /// </summary>
        public void PrintMarshallingReport()
        {
            _logger.WriteLine("\n=== MARSHALLING SUMMARY REPORT ===");
            _logger.WriteLine($"Total imports analyzed: {marshallingData.Count}");
            _logger.WriteLine($"Immeta section created: {(sectionCreated ? "YES" : "NO")}");
            
            if (sectionCreated)
            {
                _logger.WriteLine($"Section size: {immetaSection.GetSize()} bytes");
                _logger.WriteLine($"Functions with marshalling: {immetaSection.NumImportsWithMeta}");
                
                _logger.WriteLine("\nDetailed breakdown:");
                foreach (var import in immetaSection.Imports)
                {
                    _logger.WriteLine($"  Import #{import.ImportIndex}: {import.NumMarshalledArgs} marshalled arguments");
                    foreach (var arg in import.Args)
                    {
                        string direction = arg.DirectionFlags switch
                        {
                            ImmetaArgEntry.DIRECTION_IN => "IN",
                            ImmetaArgEntry.DIRECTION_OUT => "OUT",
                            ImmetaArgEntry.DIRECTION_INOUT => "INOUT",
                            _ => "UNKNOWN"
                        };
                        
                        string sizeKind = arg.SizeKind switch
                        {
                            ImmetaArgEntry.SIZE_KIND_CONST => "CONST",
                            ImmetaArgEntry.SIZE_KIND_FROM_ARG => "FROM_ARG",
                            _ => "UNKNOWN"
                        };
                        
                        _logger.WriteLine($"    Arg {arg.ArgIndex}: {direction}, Size: {sizeKind}({arg.SizeValue})");
                    }
                }
            }
            
            _logger.WriteLine("=== END MARSHALLING REPORT ===\n");
        }
        
        /// <summary>
        /// Получает метаданные маршалинга для указанного импорта
        /// </summary>
        /// <param name="importIndex">Индекс импорта</param>
        /// <returns>Список метаданных маршалинга или null, если не найден</returns>
        public List<MarshallingInfo> GetMarshallingInfo(uint importIndex)
        {
            return marshallingData.TryGetValue(importIndex, out var info) ? info : null!;
        }
        
        /// <summary>
        /// Определяет, является ли функция callback-функцией
        /// </summary>
        private bool IsCallbackFunction(string functionName)
        {
            // Известные паттерны callback функций
            string[] callbackPatterns = {
                "host_invoke_cb",
                "host_invoke_cb2",
                "invoke_callback",
                "call_callback",
                "_cb_",
                "_callback_"
            };
            
            string lowerFuncName = functionName.ToLower();
            
            foreach (var pattern in callbackPatterns)
            {
                if (lowerFuncName.Contains(pattern.ToLower()))
                {
                    _logger.WriteLine($"    🎯 Function '{functionName}' matches callback pattern '{pattern}'");
                    return true;
                }
            }
            
            return false;
        }
        
        /// <summary>
        /// Проверяет, является ли параметр NULL в текущем контексте использования
        /// </summary>
        private unsafe bool IsParameterNullInCurrentContext(LLVMValueRef function, uint paramIndex, string functionName)
        {
            try
            {
                _logger.WriteLine($"    🔍 Analyzing if parameter {paramIndex} of '{functionName}' is passed as NULL...");
                
                // Анализируем все вызовы этой функции в модуле для определения NULL значений
                bool foundNullUsage = AnalyzeCallSitesForNullParameter(function, paramIndex, functionName);
                
                if (foundNullUsage)
                {
                    _logger.WriteLine($"    ✅ Found NULL usage for parameter {paramIndex} in function calls");
                    return true;
                }
                
                _logger.WriteLine($"    ❌ No NULL usage found for parameter {paramIndex}");
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    ⚠️ Error analyzing NULL parameter: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Анализирует места вызова функции для определения NULL параметров
        /// </summary>
        private unsafe bool AnalyzeCallSitesForNullParameter(LLVMValueRef function, uint paramIndex, string functionName)
        {
            try
            {
                // Проходим по всем функциям в модуле
                for (LLVMValueRef moduleFunc = LLVM.GetFirstFunction(LLVM.GetGlobalParent(function)); 
                     moduleFunc.Handle != IntPtr.Zero; 
                     moduleFunc = LLVM.GetNextFunction(moduleFunc))
                {
                    // Пропускаем функции без тела (импорты)
                    if (LLVM.CountBasicBlocks(moduleFunc) == 0)
                        continue;
                    
                    // Проходим по всем базовым блокам
                    for (LLVMBasicBlockRef bb = LLVM.GetFirstBasicBlock(moduleFunc);
                         bb.Handle != IntPtr.Zero;
                         bb = LLVM.GetNextBasicBlock(bb))
                    {
                        // Проходим по всем инструкциям в блоке
                        for (LLVMValueRef inst = LLVM.GetFirstInstruction(bb);
                             inst.Handle != IntPtr.Zero;
                             inst = LLVM.GetNextInstruction(inst))
                        {
                            // Ищем инструкции вызова
                            if (LLVM.GetInstructionOpcode(inst) == LLVMOpcode.LLVMCall)
                            {
                                // Получаем вызываемую функцию
                                LLVMValueRef calledFunc = LLVM.GetCalledValue(inst);
                                
                                // Проверяем, что это вызов нашей функции
                                if (IsSameFunction(calledFunc, function, functionName))
                                {
                                    _logger.WriteLine($"      📍 Found call to '{functionName}' - checking parameter {paramIndex}");
                                    
                                    // Проверяем конкретный параметр на NULL
                                    if (IsCallArgumentNull(inst, paramIndex))
                                    {
                                        _logger.WriteLine($"      ✅ Parameter {paramIndex} is NULL in this call");
                                        return true;
                                    }
                                    else
                                    {
                                        _logger.WriteLine($"      ❌ Parameter {paramIndex} is NOT NULL in this call");
                                    }
                                }
                            }
                        }
                    }
                }
                
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      ⚠️ Error in call site analysis: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Проверяет, является ли функция той же самой
        /// </summary>
        private unsafe bool IsSameFunction(LLVMValueRef calledFunc, LLVMValueRef targetFunc, string functionName)
        {
            try
            {
                // Сравниваем указатели функций
                if (calledFunc.Handle == targetFunc.Handle)
                    return true;
                
                // Сравниваем имена функций
                sbyte* calledNamePtr = LLVM.GetValueName(calledFunc);
                if (calledNamePtr != null)
                {
                    string calledName = Marshal.PtrToStringAnsi((IntPtr)calledNamePtr) ?? "";
                    return calledName == functionName;
                }
                
                return false;
            }
            catch
            {
                return false;
            }
        }
        
        /// <summary>
        /// Проверяет, является ли аргумент вызова NULL
        /// </summary>
        private unsafe bool IsCallArgumentNull(LLVMValueRef callInst, uint paramIndex)
        {
            try
            {
                // Получаем количество аргументов
                uint numArgs = LLVM.GetNumArgOperands(callInst);
                
                if (paramIndex >= numArgs)
                    return false;
                
                // Получаем аргумент
                LLVMValueRef arg = LLVM.GetArgOperand(callInst, paramIndex);
                
                // Проверяем, является ли аргумент константным нулевым указателем
                if (LLVM.IsConstant(arg) != 0)
                {
                    if (LLVM.IsNull(arg) != 0)
                    {
                        _logger.WriteLine($"        🎯 Argument {paramIndex} is a constant NULL pointer");
                        return true;
                    }
                    
                    // Дополнительная проверка для константных выражений
                    LLVMValueKind argKind = LLVM.GetValueKind(arg);
                    if (argKind == LLVMValueKind.LLVMConstantPointerNullValueKind)
                    {
                        _logger.WriteLine($"        🎯 Argument {paramIndex} is ConstantPointerNull");
                        return true;
                    }
                }
                
                // Получаем имя аргумента для логирования
                sbyte* argNamePtr = LLVM.GetValueName(arg);
                string argName = "";
                if (argNamePtr != null)
                {
                    argName = Marshal.PtrToStringAnsi((IntPtr)argNamePtr) ?? "";
                }
                
                _logger.WriteLine($"        📋 Argument {paramIndex}: Kind={LLVM.GetValueKind(arg)}, Name='{argName}', IsConstant={LLVM.IsConstant(arg)}, IsNull={LLVM.IsNull(arg)}");
                
                return false;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"        ⚠️ Error checking argument: {ex.Message}");
                return false;
            }
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
    }
}