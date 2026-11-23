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
    /// Улучшенная реализация сбора импортов с анализом параметров для маршалинга
    /// </summary>
    public unsafe partial class LLVMTranslator
    {
        // Менеджер для работы с секцией immeta
        private ImmetaManager immetaManager;
        
        /// <summary>
        /// Улучшенный метод сбора импортов с анализом маршалинга
        /// </summary>
        private unsafe void CollectImportsImproved()
        {
            _logger.WriteLine("\n=== COLLECTING IMPORTS WITH MARSHALLING ANALYSIS ===");
            
            // Инициализируем immetaManager с hintsManager и логгером
            immetaManager = new ImmetaManager(this.hintsManager, this._logger);
            
            var importsSection = GetSection<ImportsSection>(2)!;
            var typesSection = GetSection<TypesSection>(1)!;
            
            // Сначала используем оригинальную логику сбора импортов
            _logger.WriteLine("  Running original imports collection first...");
            CollectImports(); // Вызываем оригинальный метод
            
            _logger.WriteLine($"  Found {importsSection.Imports.Count} imports, now analyzing for marshalling...");
            
            // Теперь анализируем уже собранные импорты
            for (int i = 0; i < importsSection.Imports.Count; i++)
            {
                var import = importsSection.Imports[i];
                if (import.Kind != EspbImportKind.Function)
                    continue;
                
                _logger.WriteLine($"\n--- Analyzing Import #{i}: '{import.EntityName}' ---");
                
                // Ищем соответствующую LLVM функцию
                LLVMValueRef llvmFunc = default;
                for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); 
                     func.Handle != IntPtr.Zero; 
                     func = LLVM.GetNextFunction(func))
                {
                    // Ищем функции БЕЗ тела (импорты)
                    if (LLVM.CountBasicBlocks(func) > 0)
                        continue;
                    
                    // Получаем имя функции
                    string funcName = "";
                    try
                    {
                        sbyte* namePtr = LLVM.GetValueName(func);
                        if (namePtr != null)
                        {
                            funcName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                        }
                    }
                    catch { }
                    
                    if (funcName == import.EntityName)
                    {
                        llvmFunc = func;
                        break;
                    }
                }
                
                if (llvmFunc.Handle == IntPtr.Zero)
                {
                    _logger.WriteLine($"  Warning: Could not find LLVM function for import '{import.EntityName}'");
                    continue;
                }
                
                // АНАЛИЗ ПАРАМЕТРОВ ДЛЯ МАРШАЛИНГА
                immetaManager.AnalyzeImportFunction(llvmFunc, import.EntityName, (uint)i);
            }
            
            _logger.WriteLine($"\n=== IMPORT COLLECTION COMPLETE ===");
            _logger.WriteLine($"Total imports collected: {importsSection.Imports.Count}");
            
            // Создаем секцию immeta если необходимо
            var immetaSection = immetaManager.CreateImmetaSection();
            if (immetaSection != null)
            {
                _logger.WriteLine("Adding immeta section to ESPB module");
                espbModule.Sections.Add(immetaSection);
                
                // Устанавливаем флаг поддержки маршалинга
                espbModule.Features |= FEATURE_MARSHALLING_META;
                _logger.WriteLine($"Set FEATURE_MARSHALLING_META flag (0x{FEATURE_MARSHALLING_META:X8})");
            }
            
            // Выводим итоговый отчет
            immetaManager.PrintMarshallingReport();
            
            _logger.WriteLine("=== IMPORTS WITH MARSHALLING ANALYSIS COMPLETE ===\n");
        }
        
        /// <summary>
        /// Альтернативный метод для анализа отдельной импортированной функции
        /// Используется для тестирования и отладки
        /// </summary>
        public void AnalyzeSingleImport(string functionName)
        {
            _logger.WriteLine($"\n=== ANALYZING SINGLE IMPORT: '{functionName}' ===");
            
            // Ищем функцию по имени
            LLVMValueRef targetFunc = default;
            bool found = false;
            
            for (LLVMValueRef func = LLVM.GetFirstFunction(llvmModule); 
                 func.Handle != IntPtr.Zero; 
                 func = LLVM.GetNextFunction(func))
            {
                // Пропускаем функции с телом
                if (LLVM.CountBasicBlocks(func) > 0)
                    continue;
                
                string funcName = "";
                try
                {
                    sbyte* namePtr = LLVM.GetValueName(func);
                    if (namePtr != null)
                    {
                        funcName = Marshal.PtrToStringAnsi((IntPtr)namePtr) ?? "";
                    }
                }
                catch { }
                
                if (funcName == functionName)
                {
                    targetFunc = func;
                    found = true;
                    break;
                }
            }
            
            if (!found)
            {
                _logger.WriteLine($"Function '{functionName}' not found in imports");
                return;
            }
            
            // Анализируем найденную функцию
            immetaManager.AnalyzeImportFunction(targetFunc, functionName, 999); // Используем фиктивный индекс
            
            _logger.WriteLine($"=== ANALYSIS COMPLETE FOR '{functionName}' ===\n");
        }
        
        /// <summary>
        /// Выводит подробную информацию о всех импортах с их анализом маршалинга
        /// </summary>
        public void PrintDetailedImportsReport()
        {
            _logger.WriteLine("\n=== DETAILED IMPORTS REPORT ===");
            
            var importsSection = GetSection<ImportsSection>(2);
            if (importsSection == null)
            {
                _logger.WriteLine("No imports section found");
                return;
            }
            
            _logger.WriteLine($"Total imports: {importsSection.Imports.Count}");
            _logger.WriteLine($"Functions requiring marshalling: {immetaManager.FunctionsRequiringMarshalling}");
            _logger.WriteLine($"Immeta section created: {(immetaManager.IsSectionCreated ? "YES" : "NO")}");
            
            for (int i = 0; i < importsSection.Imports.Count; i++)
            {
                var import = importsSection.Imports[i];
                _logger.WriteLine($"\nImport #{i}: {import.EntityName}");
                _logger.WriteLine($"  ModuleNum: {import.ModuleNum}");
                _logger.WriteLine($"  Type Index: {import.TypeIndex}");
                _logger.WriteLine($"  ImportFlags: 0x{import.ImportFlags:X2}");
                if ((import.ImportFlags & 0x10) != 0)
                {
                    _logger.WriteLine($"  SymbolIndex: {import.SymbolIndex}");
                    string tableType = "unknown";
                    if ((import.ImportFlags & 0x20) != 0) tableType = "custom_fast";
                    else if ((import.ImportFlags & 0x40) != 0) tableType = "idf_fast";
                    _logger.WriteLine($"  Table: {tableType}");
                }
                
                var marshallingInfo = immetaManager.GetMarshallingInfo((uint)i);
                if (marshallingInfo != null)
                {
                    _logger.WriteLine("  Marshalling Info:");
                    for (int j = 0; j < marshallingInfo.Count; j++)
                    {
                        var info = marshallingInfo[j];
                        if (info.Direction != ESPB_Runtime.MarshallingDirection.None)
                        {
                            _logger.WriteLine($"    Param {j}: {info.Direction}, Size: {info.SizeMode}({info.SizeValue}), Handler: {info.HandlerIndex}");
                        }
                    }
                }
                else
                {
                    _logger.WriteLine("  No marshalling required");
                }
            }
            
            _logger.WriteLine("=== END DETAILED REPORT ===\n");
        }
    }
}