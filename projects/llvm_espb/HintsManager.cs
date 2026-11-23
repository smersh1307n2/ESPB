using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace ESPB_Runtime
{
    /// <summary>
    /// Направления маршалинга для hints (отдельное от LLVMTranslator)
    /// </summary>
    public enum HintsMarshallingDirection
    {
        None = 0,   // Нет маршалинга
        In = 1,     // Копировать данные из ESPB в нативный буфер перед вызовом
        Out = 2,    // Копировать данные из нативного буфера в ESPB после вызова
        InOut = 3   // Двунаправленный маршалинг (IN | OUT)
    }

    /// <summary>
    /// Типы размера для hints (отдельное от LLVMTranslator)
    /// </summary>
    public enum HintsSizeKind
    {
        Constant = 0,        // Размер - константа в байтах
        FromArgument = 1     // Размер берется из другого аргумента функции
    }

    /// <summary>
    /// Информация о маршалинге для hints (отдельное от LLVMTranslator)
    /// </summary>
    public class HintsMarshallingInfo
    {
        public HintsMarshallingDirection Direction { get; set; } = HintsMarshallingDirection.None;
        public HintsSizeKind SizeMode { get; set; } = HintsSizeKind.Constant;
        public uint SizeValue { get; set; } = 0; // Константа или индекс аргумента
        public byte HandlerIndex { get; set; } = 0; // Новое поле для типа обработчика

        public HintsMarshallingInfo() { }

        public HintsMarshallingInfo(HintsMarshallingDirection dir, HintsSizeKind sizeMode, uint sizeValue)
        {
            Direction = dir;
            SizeMode = sizeMode;
            SizeValue = sizeValue;
            HandlerIndex = 0; // По умолчанию стандартный обработчик
        }

        public HintsMarshallingInfo(HintsMarshallingDirection dir, HintsSizeKind sizeMode, uint sizeValue, byte handlerIndex)
        {
            Direction = dir;
            SizeMode = sizeMode;
            SizeValue = sizeValue;
            HandlerIndex = handlerIndex;
        }
    }

    /// <summary>
    /// Информация о callback параметре для hints (отдельное от LLVMTranslator)
    /// </summary>
    public class HintsCallbackParameterInfo
    {
        public int CallbackParamIndex { get; set; }
        public int? UserDataParamIndex { get; set; }
        public int SignatureIndex { get; set; }
        public int EspbFuncIndex { get; set; } // ИНДЕКС ФУНКЦИИ-КОЛБЭКА
        public byte Flags { get; set; }

        public HintsCallbackParameterInfo(int callbackParamIndex, int? userDataParamIndex = null)
        {
            CallbackParamIndex = callbackParamIndex;
            UserDataParamIndex = userDataParamIndex;
            SignatureIndex = 0;
            Flags = 0;
        }
    }
    /// <summary>
    /// Управляет правилами из файла .hints для переопределения автоматического анализа параметров
    /// </summary>
    public class HintsManager
    {
        private Dictionary<string, FunctionHints> functionHints = new Dictionary<string, FunctionHints>();
        
        /// <summary>
        /// Загружает правила из файла .hints
        /// </summary>
        /// <param name="hintsFilePath">Путь к файлу .hints</param>
        /// <returns>True если файл успешно загружен, False в случае ошибки</returns>
        public bool LoadHintsFile(string hintsFilePath)
        {
            try
            {
                if (!File.Exists(hintsFilePath))
                {
                    // No logger here, this is a manager class.
                    // The caller should log this.
                    return true; // Not an error, just no hints.
                }

                string[] lines = File.ReadAllLines(hintsFilePath);
                // No logger here.
                
                int lineNumber = 0;
                foreach (string line in lines)
                {
                    lineNumber++;
                    ProcessHintsLine(line.Trim(), lineNumber);
                }
                
                // No logger here.
                return true;
            }
            catch (Exception)
            {
                // No logger here.
                return false;
            }
        }

        /// <summary>
        /// Создает файл .hints с примерами по умолчанию
        /// </summary>
        private void CreateDefaultHintsFile(string hintsFilePath)
        {
            string defaultContent = @"# ESPB Hints File - Template
# 
# This file allows manual control of FFI metadata generation (immeta and cbmeta).
# Rules defined here have highest priority and override automatic heuristic analysis.
#
# Syntax: function_name: rule1, rule2, ...
#
# Examples:
#
# FreeRTOS task creation with callback and async output
# xTaskCreatePinnedToCore: cb 0, ud 3 for 0, out 5 async
#
# Function reading data into buffer, size from parameter
# my_read_func: out 0 size from 1
#
# Complex INOUT parameter with fixed size
# update_struct: inout 2 size[12]
#
# Disable analysis for problematic function
# my_buggy_func: no im, no cb
#
# Add your function hints below:
";

            try
            {
                File.WriteAllText(hintsFilePath, defaultContent);
            }
            catch (Exception)
            {
                // No logger here.
            }
        }

        /// <summary>
        /// Обрабатывает одну строку файла .hints
        /// </summary>
        private void ProcessHintsLine(string line, int lineNumber)
        {
            // Пропускаем комментарии и пустые строки
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith("#"))
                return;

            // Ищем разделитель ':'
            int colonIndex = line.IndexOf(':');
            if (colonIndex == -1)
            {
                // No logger here.
                return;
            }

            string functionName = line.Substring(0, colonIndex).Trim();
            string rulesString = line.Substring(colonIndex + 1).Trim();

            // Debug: Function: '{functionName}', Rules: '{rulesString}'

            if (string.IsNullOrEmpty(functionName))
            {
                // No logger here.
                return;
            }

            // Парсим правила
            FunctionHints hints = ParseRules(rulesString, lineNumber);
            functionHints[functionName] = hints;
            
            // No logger here.
        }

        /// <summary>
        /// Парсит строку правил для функции
        /// </summary>
        private FunctionHints ParseRules(string rulesString, int lineNumber)
        {
            var hints = new FunctionHints();
            
            if (string.IsNullOrWhiteSpace(rulesString))
                return hints;

            // Разбиваем на отдельные правила по запятым
            string[] rules = rulesString.Split(',');
            
            foreach (string rule in rules)
            {
                ParseSingleRule(rule.Trim(), hints, lineNumber);
            }

            return hints;
        }

        /// <summary>
        /// Парсит одно правило
        /// </summary>
        private void ParseSingleRule(string rule, FunctionHints hints, int lineNumber)
        {
            if (string.IsNullOrWhiteSpace(rule))
                return;

            string[] parts = rule.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
                return;

            try
            {
                string command = parts[0].ToLower();
                
                switch (command)
                {
                    case "in":
                        if (parts.Length >= 2 && int.TryParse(parts[1], out int inIdx))
                        {
                            var marshalInfo = new HintsMarshallingInfo(HintsMarshallingDirection.In, HintsSizeKind.Constant, 0);
                            ParseMarshallingModifiers(parts, 2, marshalInfo);
                            hints.MarshallingOverrides[inIdx] = marshalInfo;
                        }
                        break;

                    case "out":
                        if (parts.Length >= 2 && int.TryParse(parts[1], out int outIdx))
                        {
                            var marshalInfo = new HintsMarshallingInfo(HintsMarshallingDirection.Out, HintsSizeKind.Constant, 0);
                            ParseMarshallingModifiers(parts, 2, marshalInfo);
                            hints.MarshallingOverrides[outIdx] = marshalInfo;
                        }
                        break;

                    case "inout":
                        if (parts.Length >= 2 && int.TryParse(parts[1], out int inoutIdx))
                        {
                            var marshalInfo = new HintsMarshallingInfo(HintsMarshallingDirection.InOut, HintsSizeKind.Constant, 0);
                            ParseMarshallingModifiers(parts, 2, marshalInfo);
                            hints.MarshallingOverrides[inoutIdx] = marshalInfo;
                        }
                        break;

                    case "cb":
                        if (parts.Length >= 2 && int.TryParse(parts[1], out int cbIdx))
                        {
                            var cbInfo = new HintsCallbackParameterInfo(cbIdx);
                            hints.CallbackOverrides[cbIdx] = cbInfo;
                        }
                        break;

                    case "ud":
                        // ud <idx> for <cb_idx>
                        if (parts.Length >= 4 && parts[2] == "for" && 
                            int.TryParse(parts[1], out int udIdx) && 
                            int.TryParse(parts[3], out int forCbIdx))
                        {
                            if (hints.CallbackOverrides.ContainsKey(forCbIdx))
                            {
                                hints.CallbackOverrides[forCbIdx].UserDataParamIndex = udIdx;
                            }
                            else
                            {
                                // Создаем callback info если еще нет
                                var cbInfo = new HintsCallbackParameterInfo(forCbIdx, udIdx);
                                hints.CallbackOverrides[forCbIdx] = cbInfo;
                            }
                        }
                        break;

                    case "no":
                        if (parts.Length >= 2)
                        {
                            string target = parts[1].ToLower();
                            if (target == "im")
                            {
                                if (parts.Length >= 3 && int.TryParse(parts[2], out int noImIdx))
                                {
                                    hints.DisabledImmeta.Add(noImIdx);
                                }
                                else
                                {
                                    hints.DisableAllImmeta = true;
                                }
                            }
                            else if (target == "cb")
                            {
                                if (parts.Length >= 3 && int.TryParse(parts[2], out int noCbIdx))
                                {
                                    hints.DisabledCallbacks.Add(noCbIdx);
                                }
                                else
                                {
                                    hints.DisableAllCallbacks = true;
                                }
                            }
                            else if (target == "ud")
                            {
                                // no ud <idx> for <cb_idx>
                                if (parts.Length >= 5 && parts[3] == "for" && 
                                    int.TryParse(parts[2], out int noUdIdx) && 
                                    int.TryParse(parts[4], out int noUdForCbIdx))
                                {
                                    // Добавляем в список отключенных user data для конкретного callback
                                    if (!hints.DisabledUserData.ContainsKey(noUdForCbIdx))
                                    {
                                        hints.DisabledUserData[noUdForCbIdx] = new HashSet<int>();
                                    }
                                    hints.DisabledUserData[noUdForCbIdx].Add(noUdIdx);
                                    // No logger here.
                                }
                            }
                        }
                        break;

                    default:
                        // No logger here.
                        break;
            }
            }
            catch (Exception)
            {
                // No logger here.
            }
        }

        /// <summary>
        /// Парсит модификаторы для marshalling правил (async, size[], size from)
        /// </summary>
        private void ParseMarshallingModifiers(string[] parts, int startIndex, HintsMarshallingInfo marshalInfo)
        {
            for (int i = startIndex; i < parts.Length; i++)
            {
                string part = parts[i];
                
                if (part.ToLower() == "async")
                {
                    marshalInfo.HandlerIndex = 1; // Асинхронный обработчик
                }
                else if (part.StartsWith("size[") && part.EndsWith("]"))
                {
                    // size[константа]
                    string sizeStr = part.Substring(5, part.Length - 6);
                    if (uint.TryParse(sizeStr, out uint sizeValue))
                    {
                        marshalInfo.SizeMode = HintsSizeKind.Constant;
                        marshalInfo.SizeValue = sizeValue;
                    }
                }
                else if (part.ToLower() == "size" && i + 2 < parts.Length && 
                         parts[i + 1].ToLower() == "from" && 
                         uint.TryParse(parts[i + 2], out uint fromIdx))
                {
                    // size from <idx>
                    marshalInfo.SizeMode = HintsSizeKind.FromArgument;
                    marshalInfo.SizeValue = fromIdx;
                    i += 2; // Пропускаем "from" и индекс
                }
            }
        }

        /// <summary>
        /// Получает hints для указанной функции
        /// </summary>
        /// <param name="functionName">Имя функции</param>
        /// <returns>Hints для функции или null если нет правил</returns>
        public FunctionHints? GetHintsForFunction(string functionName)
        {
            if (functionHints.TryGetValue(functionName, out var hints))
                return hints;
            return null;
        }

        /// <summary>
        /// Проверяет, есть ли hints для указанной функции
        /// </summary>
        public bool HasHintsForFunction(string functionName)
        {
            return functionHints.ContainsKey(functionName);
        }

        /// <summary>
        /// Возвращает автоматически определенный путь к файлу .hints для данного .bc файла
        /// </summary>
        public static string GetDefaultHintsPath(string bcFilePath)
        {
            string directory = Path.GetDirectoryName(bcFilePath) ?? "";
            string baseName = Path.GetFileNameWithoutExtension(bcFilePath);
            return Path.Combine(directory, baseName + ".hints");
        }
    }

    /// <summary>
    /// Содержит правила hints для одной функции
    /// </summary>
    public class FunctionHints
    {
        /// <summary>
        /// Переопределения marshalling для параметров (индекс параметра -> HintsMarshallingInfo)
        /// </summary>
        public Dictionary<int, HintsMarshallingInfo> MarshallingOverrides { get; } = new Dictionary<int, HintsMarshallingInfo>();

        /// <summary>
        /// Переопределения callback для параметров (индекс параметра -> HintsCallbackParameterInfo)
        /// </summary>
        public Dictionary<int, HintsCallbackParameterInfo> CallbackOverrides { get; } = new Dictionary<int, HintsCallbackParameterInfo>();

        /// <summary>
        /// Отключенные immeta для конкретных параметров
        /// </summary>
        public HashSet<int> DisabledImmeta { get; } = new HashSet<int>();

        /// <summary>
        /// Отключенные callbacks для конкретных параметров
        /// </summary>
        public HashSet<int> DisabledCallbacks { get; } = new HashSet<int>();

        /// <summary>
        /// Отключенные user data для конкретных callback параметров (callback_idx -> user_data_indices)
        /// </summary>
        public Dictionary<int, HashSet<int>> DisabledUserData { get; } = new Dictionary<int, HashSet<int>>();

        /// <summary>
        /// Отключить immeta для всей функции
        /// </summary>
        public bool DisableAllImmeta { get; set; } = false;

        /// <summary>
        /// Отключить callbacks для всей функции
        /// </summary>
        public bool DisableAllCallbacks { get; set; } = false;

        public override string ToString()
        {
            var parts = new List<string>();
            
            if (DisableAllImmeta) parts.Add("no immeta");
            if (DisableAllCallbacks) parts.Add("no callbacks");
            if (MarshallingOverrides.Count > 0) parts.Add($"{MarshallingOverrides.Count} marshalling rules");
            if (CallbackOverrides.Count > 0) parts.Add($"{CallbackOverrides.Count} callback rules");
            if (DisabledImmeta.Count > 0) parts.Add($"{DisabledImmeta.Count} disabled immeta");
            if (DisabledCallbacks.Count > 0) parts.Add($"{DisabledCallbacks.Count} disabled callbacks");
            if (DisabledUserData.Count > 0) parts.Add($"{DisabledUserData.Count} disabled userdata rules");
            
            return parts.Count > 0 ? string.Join(", ", parts) : "no rules";
        }
    }
}