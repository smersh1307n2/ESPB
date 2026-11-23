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
    /// Максимально продвинутая система анализа параметров с использованием всех доступных подсказок LLVM IR
    /// </summary>
    public class AdvancedParameterAnalysis
    {
        private readonly TextWriter _logger;
        private readonly SmartParameterAnalyzer _smartParameterAnalyzer;

        public AdvancedParameterAnalysis(TextWriter logger)
        {
            _logger = logger;
            _smartParameterAnalyzer = new SmartParameterAnalyzer(logger);
        }

        /// <summary>
        /// Продвинутый анализ параметра с использованием всех доступных техник
        /// Теперь использует SmartParameterAnalyzer для более точного анализа
        /// </summary>
        public unsafe MarshallingInfo AnalyzeParameterAdvanced(LLVMValueRef function, uint paramIndex, string functionName)
        {
            try
            {
                // ИСПРАВЛЕНИЕ: Добавляем специальную обработку для read-only функций
                if (functionName == "memcmp" || functionName == "strcmp")
                {
                    _logger.WriteLine($"    🎯 Special case for read-only function '{functionName}'. Forcing IN direction.");
                    return new MarshallingInfo(MarshallingDirection.In, SizeKind.Constant, 0);
                }

                _logger.WriteLine($"  🔍 ADVANCED IR Analysis for '{functionName}' parameter {paramIndex}:");

                // Используем новый умный анализатор
                var smartResult = _smartParameterAnalyzer.AnalyzeParameterSmart(function, paramIndex, functionName);

                // Конвертируем результат в старый формат для обратной совместимости
                var marshalling = _smartParameterAnalyzer.ConvertToMarshallingInfo(smartResult);

                // Выводим детали анализа
                _logger.WriteLine($"    📊 Smart analysis result: Direction={marshalling.Direction}, Handler={marshalling.HandlerIndex}, Confidence={smartResult.Confidence:F2}");
                foreach (var reason in smartResult.AnalysisReasons)
                {
                    _logger.WriteLine($"    📝 {reason}");
                }

                return marshalling;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"    ❌ Error during advanced analysis: {ex.Message}");
                // В случае ошибки возвращаем пустой результат, чтобы избежать неправильного маршалинга
                return new MarshallingInfo();
            }
        }

        /// <summary>
        /// Fallback на старый эвристический анализ в случае ошибки
        /// </summary>
        // This method is no longer needed as we rely on the universal analyzer
        
        /// <summary>
        /// Анализ семантики указателя на основе типа
        /// </summary>
        private unsafe MarshallingDirection AnalyzePointerSemantics(LLVMTypeRef paramType, string functionName, uint paramIndex, string paramName)
        {
            _logger.WriteLine($"    🔬 Analyzing pointer semantics...");
            
            try
            {
                // Анализ указателей
                if (paramType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                {
                    var elementType = paramType.ElementType;
                    
                    // УРОВЕНЬ 1: Double pointer (**) - обычно OUT параметры
                    if (elementType.Kind == LLVMTypeKind.LLVMPointerTypeKind)
                    {
                        _logger.WriteLine($"      🎯 Found double pointer (**) - likely OUT parameter");
                        return MarshallingDirection.Out;
                    }
                    
                    // УРОВЕНЬ 2: Указатель на функцию - обычно IN (callback)
                    if (elementType.Kind == LLVMTypeKind.LLVMFunctionTypeKind)
                    {
                        _logger.WriteLine($"      🎯 Found function pointer - likely IN parameter (callback)");
                        
                        // НОВОЕ: Проверяем, не является ли это callback-функцией
                        if (IsCallbackInvocationFunction(functionName))
                        {
                            _logger.WriteLine($"      ⚠️ Function '{functionName}' is callback invocation - should use cbmeta");
                            // Возвращаем None, чтобы исключить из immeta
                            return MarshallingDirection.None;
                        }
                        
                        return MarshallingDirection.In;
                    }
                    
                    // УРОВЕНЬ 3: Универсальный анализ позиции и имени для single pointer
                    if (elementType.Kind == LLVMTypeKind.LLVMIntegerTypeKind || 
                        elementType.Kind == LLVMTypeKind.LLVMStructTypeKind ||
                        elementType.Kind == LLVMTypeKind.LLVMVoidTypeKind || // Поддержка opaque типов
                        elementType.Handle == IntPtr.Zero)
                    {
                        // Анализ на основе позиции в списке параметров
                        // Последние параметры функций часто являются OUT параметрами
                        string lowerParamName = paramName.ToLower();
                        
                        // Сильные индикаторы OUT параметра
                        if (lowerParamName.Contains("out") || lowerParamName.Contains("result") ||
                            lowerParamName.Contains("handle") || lowerParamName.Contains("created") ||
                            lowerParamName.EndsWith("_out") || lowerParamName.StartsWith("out_"))
                        {
                            _logger.WriteLine($"      🎯 Parameter name strongly suggests OUT: '{paramName}'");
                            return MarshallingDirection.Out;
                        }
                        
                        // Универсальные эвристики на основе типов и позиций
                        // Убираем зависимость от имен функций для истинно универсального анализа
                        
                        // Эвристика 1: Единственный параметр-указатель часто является OUT
                        if (paramIndex == 0 &&
                            (elementType.Kind == LLVMTypeKind.LLVMIntegerTypeKind ||
                             elementType.Kind == LLVMTypeKind.LLVMFloatTypeKind ||
                             elementType.Kind == LLVMTypeKind.LLVMDoubleTypeKind ||
                             elementType.Kind == LLVMTypeKind.LLVMVoidTypeKind))
                        {
                            // Проверяем общее количество параметров функции
                            // Если у функции только один параметр-указатель, это часто OUT
                            _logger.WriteLine($"      🎯 Single pointer parameter detected - potential OUT pattern");
                            // Пока оставляем это как эвристику, не принуждая к OUT
                        }
                    }
                    
                    // УРОВЕНЬ 4: Анализ const квалификаторов (если доступны)
                    // TODO: Добавить анализ const квалификаторов через метаданные LLVM
                }
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      ❌ Error in pointer semantics: {ex.Message}");
            }
            
            return MarshallingDirection.None;
        }
        
        /// <summary>
        /// Анализ использования параметра в вызовах функций внутри модуля
        /// </summary>
        private unsafe MarshallingDirection AnalyzeParameterUsageInCalls(LLVMValueRef function, LLVMValueRef parameter, uint paramIndex, string functionName)
        {
            _logger.WriteLine($"    🔍 Analyzing parameter usage in calls...");
            
            try
            {
                // Поиск всех использований этого параметра в модуле
                // Это поможет понять, как параметр используется в реальных вызовах
                
                // TODO: Реализовать поиск по всем вызовам функции в модуле
                // и анализ того, что передается в этот параметр
                
                _logger.WriteLine($"      📝 Usage analysis not fully implemented yet");
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      ❌ Error in usage analysis: {ex.Message}");
            }
            
            return MarshallingDirection.None;
        }
        
        /// <summary>
        /// Анализ специфичных паттернов ESP-IDF и FreeRTOS
        /// </summary>
        private MarshallingDirection AnalyzeESPIDFPatterns(string functionName, uint paramIndex, string paramName, LLVMTypeRef paramType)
        {
            _logger.WriteLine($"    🏗️ Analyzing ESP-IDF/FreeRTOS patterns...");
            
            string lowerFuncName = functionName.ToLower();
            string lowerParamName = paramName.ToLower();
            
            // Специфичные паттерны ESP-IDF/FreeRTOS
            
            // 1. Функции создания задач
            if (lowerFuncName.Contains("taskcreate"))
            {
                _logger.WriteLine($"      🎯 Task creation function detected");
                
                if (paramIndex == 0) // TaskFunction_t - callback
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Task function callback - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 1) // Task name
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Task name string - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 3) // pvParameters - user data
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Task parameters - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 5) // TaskHandle_t *pxCreatedTask - output handle
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Created task handle - OUT");
                    return MarshallingDirection.Out;
                }
            }
            
            // 2. Функции создания таймеров
            if (lowerFuncName.Contains("timercreate"))
            {
                _logger.WriteLine($"      🎯 Timer creation function detected");
                
                if (paramIndex == 0) // Timer name
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Timer name string - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 3) // Callback function
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Timer callback - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 4) // pvTimerID - user data
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Timer user data - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 3. Функции получения данных
            if (lowerFuncName.StartsWith("pv") || lowerFuncName.Contains("getid") || lowerFuncName.Contains("gettimer"))
            {
                _logger.WriteLine($"      🎯 Data getter function detected");
                if (paramIndex == 0) // Handle для чтения
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Input handle - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 4. Функции команд
            if (lowerFuncName.Contains("command") || lowerFuncName.Contains("generic"))
            {
                _logger.WriteLine($"      🎯 Command function detected");
                if (paramIndex == 0) // Handle
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Command target handle - IN");
                    return MarshallingDirection.In;
                }
                if (paramIndex == 3) // Optional data pointer
                {
                    _logger.WriteLine($"        📌 Param {paramIndex}: Optional command data - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 5. Printf-подобные функции
            if (lowerFuncName == "printf" || lowerFuncName == "sprintf" || lowerFuncName == "snprintf")
            {
                if (paramIndex == 0)
                {
                    _logger.WriteLine($"        📌 Printf format string - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 6. Функции вывода строк
            if (lowerFuncName == "puts" || lowerFuncName == "fputs")
            {
                if (paramIndex == 0)
                {
                    _logger.WriteLine($"        📌 String output parameter - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 7. FreeRTOS специфичные функции - детальный анализ
            if (lowerFuncName == "vtaskdelete")
            {
                if (paramIndex == 0) // TaskHandle_t - handle для удаления
                {
                    _logger.WriteLine($"        📌 Task handle for deletion - IN");
                    return MarshallingDirection.In;
                }
            }
            
            if (lowerFuncName == "pvtimergettimereid")
            {
                if (paramIndex == 0) // TimerHandle_t - входной handle
                {
                    _logger.WriteLine($"        📌 Timer handle for reading ID - IN");
                    return MarshallingDirection.In;
                }
            }
            
            // 8. Функции создания с выходными handle
            if (lowerFuncName.Contains("create") && lowerFuncName.Contains("task"))
            {
                if (paramIndex >= 5) // В xTaskCreatePinnedToCore последние параметры
                {
                    _logger.WriteLine($"        📌 Late parameter in task creation - likely OUT handle");
                    return MarshallingDirection.Out;
                }
            }
            
            return MarshallingDirection.None;
        }
        
        /// <summary>
        /// Анализ позиционных паттернов в функции
        /// </summary>
        private unsafe MarshallingDirection AnalyzePositionalPatterns(LLVMValueRef function, uint paramIndex, string functionName, string paramName)
        {
            _logger.WriteLine($"    📍 Analyzing positional patterns...");
            
            try
            {
                uint totalParams = LLVM.CountParams(function);
                string lowerFuncName = functionName.ToLower();
                
                // Специальные паттерны для конкретных функций
                if (lowerFuncName == "xtaskcreatedpinnedtocore")
                {
                    // xTaskCreatePinnedToCore параметры:
                    // 0: TaskFunction_t (callback) - IN
                    // 1: name - IN  
                    // 2: stackDepth - value
                    // 3: parameters - IN
                    // 4: priority - value
                    // 5: handle** - OUT  ← ЭТО КЛЮЧЕВОЙ OUT ПАРАМЕТР
                    // 6: coreID - value
                    if (paramIndex == 5)
                    {
                        _logger.WriteLine($"      🎯 xTaskCreatePinnedToCore param 5 - task handle output - OUT");
                        return MarshallingDirection.Out;
                    }
                }
                
                // Общие паттерны API
                
                // Последний указатель в функциях создания - обычно OUT handle
                if (lowerFuncName.Contains("create"))
                {
                    if (paramIndex == totalParams - 1 || paramIndex == totalParams - 2)
                    {
                        _logger.WriteLine($"      🎯 Last pointer in creation function - likely OUT handle");
                        return MarshallingDirection.Out;
                    }
                }
                
                // Функции удаления - первый параметр handle (IN)
                if (lowerFuncName.Contains("delete") || lowerFuncName.Contains("destroy"))
                {
                    if (paramIndex == 0)
                    {
                        _logger.WriteLine($"      🎯 First parameter in delete function - handle IN");
                        return MarshallingDirection.In;
                    }
                }
                
                // Функции получения ID/данных - первый параметр handle (IN)
                if (lowerFuncName.Contains("get") && (lowerFuncName.Contains("id") || lowerFuncName.Contains("timer")))
                {
                    if (paramIndex == 0)
                    {
                        _logger.WriteLine($"      🎯 First parameter in getter function - handle IN");
                        return MarshallingDirection.In;
                    }
                }
                
                // Последний указатель часто является OUT параметром
                if (paramIndex == totalParams - 1 && totalParams >= 3)
                {
                    // Проверяем, является ли это выходным параметром по имени
                    string lowerParamName = paramName.ToLower();
                    if (lowerParamName.Contains("created") || lowerParamName.Contains("handle") || 
                        lowerParamName.Contains("result") || lowerParamName.Contains("out"))
                    {
                        _logger.WriteLine($"      🎯 Last parameter with output-like name - OUT");
                        return MarshallingDirection.Out;
                    }
                }
                
                // Первые параметры функций создания часто являются именами/настройками (IN)
                if (paramIndex <= 2)
                {
                    if (lowerFuncName.Contains("create") || lowerFuncName.Contains("init"))
                    {
                        _logger.WriteLine($"      🎯 Early parameter in creation function - IN");
                        return MarshallingDirection.In;
                    }
                }
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      ❌ Error in positional analysis: {ex.Message}");
            }
            
            return MarshallingDirection.None;
        }
        
        /// <summary>
        /// Эвристики по именам функций и параметров
        /// </summary>
        private MarshallingDirection AnalyzeNameHeuristics(string functionName, string paramName, uint paramIndex)
        {
            _logger.WriteLine($"    📝 Analyzing name heuristics...");
            
            string lowerFuncName = functionName.ToLower();
            string lowerParamName = paramName.ToLower();
            
            // Анализ имени параметра
            if (!string.IsNullOrEmpty(lowerParamName))
            {
                // OUT паттерны в именах параметров
                if (lowerParamName.Contains("out") || lowerParamName.Contains("result") || 
                    lowerParamName.Contains("buffer") && (lowerParamName.Contains("out") || paramIndex > 0) ||
                    lowerParamName.Contains("created") || lowerParamName.Contains("handle") ||
                    lowerParamName.StartsWith("p") && lowerParamName.Contains("created"))
                {
                    _logger.WriteLine($"      🎯 Parameter name suggests OUT: '{paramName}'");
                    return MarshallingDirection.Out;
                }
                
                // IN паттерны в именах параметров
                if (lowerParamName.Contains("name") || lowerParamName.Contains("str") ||
                    lowerParamName.Contains("src") || lowerParamName.Contains("input") ||
                    lowerParamName.Contains("config") || lowerParamName.Contains("param"))
                {
                    _logger.WriteLine($"      🎯 Parameter name suggests IN: '{paramName}'");
                    return MarshallingDirection.In;
                }
            }
            
            // Убираем жестко зашитые имена функций - анализ должен быть универсальным
            // Специфичные паттерны обрабатываются на более высоких уровнях анализа
            
            return MarshallingDirection.None;
        }
        
        /// <summary>
        /// Получает имя параметра из LLVM IR
        /// </summary>
        private static unsafe string GetParameterName(LLVMValueRef parameter)
        {
            try
            {
                sbyte* namePtr = LLVM.GetValueName(parameter);
                if (namePtr != null)
                {
                    return Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                }
            }
            catch { }
            return "";
        }
        
        /// <summary>
        /// Проверяет, является ли функция вызовом callback
        /// </summary>
        private bool IsCallbackInvocationFunction(string functionName)
        {
            string[] callbackPatterns = {
                "host_invoke_cb",
                "host_invoke_cb2",
                "invoke_callback",
                "call_callback"
            };
            
            string lowerFuncName = functionName.ToLower();
            
            foreach (var pattern in callbackPatterns)
            {
                if (lowerFuncName.Contains(pattern.ToLower()))
                {
                    return true;
                }
            }
            
            return false;
        }
    }
}