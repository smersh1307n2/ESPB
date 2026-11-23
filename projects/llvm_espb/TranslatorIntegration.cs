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
    /// Интеграция улучшенной системы анализа в основной транслятор
    /// </summary>
    public unsafe partial class LLVMTranslator
    {
        /// <summary>
        /// Обновленный метод TranslateFile с интеграцией улучшенного анализа
        /// </summary>
        private void ApplyFastImportMapping()
        {
            var importsSection = GetSection<ImportsSection>(2);
            if (importsSection == null) return;
            if (CustomFastMap == null && IdfFastMap == null) return;

            int mapped = 0;
            _logger.WriteLine($"[ApplyFastImportMapping] Starting. Total imports: {importsSection.Imports.Count}");
            _logger.WriteLine($"[ApplyFastImportMapping] CustomFastMap: {(CustomFastMap != null ? CustomFastMap.Count.ToString() : "null")}");
            _logger.WriteLine($"[ApplyFastImportMapping] IdfFastMap: {(IdfFastMap != null ? IdfFastMap.Count.ToString() : "null")}");

            for (int i = 0; i < importsSection.Imports.Count; i++)
            {
                var imp = importsSection.Imports[i];
                if (imp.Kind != EspbImportKind.Function) continue;

                _logger.WriteLine($"[ApplyFastImportMapping] Import #{i}: '{imp.EntityName}' Flags=0x{imp.ImportFlags:X2}");

                // If already indexed, keep it
                if ((imp.ImportFlags & 0x10) != 0)
                {
                    _logger.WriteLine($"  -> Already indexed, skipping");
                    continue;
                }

                if (CustomFastMap != null)
                {
                    foreach (var cand in GetFastNameCandidates(imp.EntityName))
                    {
                        _logger.WriteLine($"  -> Trying custom_fast candidate: '{cand}'");
                        if (CustomFastMap.TryGetValue(cand, out var symIdx))
                        {
                            imp.ImportFlags = (byte)(0x10 | 0x20);
                            imp.SymbolIndex = symIdx;
                            mapped++;
                            _logger.WriteLine($"  -> MAPPED to custom_fast index {symIdx}, flags=0x{imp.ImportFlags:X2}");
                            goto nextImport;
                        }
                    }
                }

                if (IdfFastMap != null)
                {
                    foreach (var cand in GetFastNameCandidates(imp.EntityName))
                    {
                        _logger.WriteLine($"  -> Trying idf_fast candidate: '{cand}'");
                        if (IdfFastMap.TryGetValue(cand, out var symIdx))
                        {
                            imp.ImportFlags = (byte)(0x10 | 0x40);
                            imp.SymbolIndex = symIdx;
                            mapped++;
                            _logger.WriteLine($"  -> MAPPED to idf_fast index {symIdx}, flags=0x{imp.ImportFlags:X2}");
                            goto nextImport;
                        }
                    }
                }

                _logger.WriteLine($"  -> NOT MAPPED (no match found)");

            nextImport:
                ;
            }

            _logger.WriteLine($"Fast import mapping applied: {mapped} function imports mapped to indexed.");
        }

        public unsafe bool TranslateFileWithImprovedAnalysis(string bcFilePath, string espbFilePath, string? hintsFilePath = null)
        {
            _logger.WriteLine($"=== TRANSLATING WITH IMPROVED ANALYSIS ===");
            _logger.WriteLine($"Input: {bcFilePath}");
            _logger.WriteLine($"Output: {espbFilePath}");
            
            // Инициализация HintsManager
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
                // 1. Инициализация LLVM и парсинг .bc
                if (!File.Exists(bcFilePath))
                {
                    _logger.WriteLine($"Error: Input file not found: {bcFilePath}");
                    return false;
                }
                
                _logger.WriteLine("Initializing LLVM context...");
                llvmContext = LLVMContextRef.Create();

                byte[] fileBytes = File.ReadAllBytes(bcFilePath);
                fixed (byte* pBytes = fileBytes)
                {
                    byte[] moduleNameBytes = Encoding.UTF8.GetBytes("module\0");
                    fixed (byte* pModuleName = moduleNameBytes)
                    {
                         memBuf = LLVM.CreateMemoryBufferWithMemoryRangeCopy((sbyte*)pBytes, (UIntPtr)fileBytes.LongLength, (sbyte*)pModuleName);
                    }
                }

                if (memBuf.Handle == IntPtr.Zero)
                {
                     _logger.WriteLine($"Error: Failed to create LLVM memory buffer");
                     llvmContext.Dispose();
                     return false;
                }

                _logger.WriteLine("Parsing LLVM bitcode...");
                llvmModule = llvmContext.ParseBitcode(memBuf);

                if (llvmModule.Handle == IntPtr.Zero)
                {
                     _logger.WriteLine($"Error: Failed to parse LLVM bitcode");
                     LLVM.DisposeMemoryBuffer(memBuf);
                     llvmContext.Dispose();
                     return false;
                }

                // Инициализация TargetData
                string dataLayoutStr = "e-p:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v128:128:128-a:0:64-n32-S128";
                targetData = LLVMTargetDataRef.FromStringRepresentation(dataLayoutStr);
                if (targetData.Handle == IntPtr.Zero)
                {
                    _logger.WriteLine("Error: Failed to create TargetData.");
                    llvmModule.Dispose();
                    llvmContext.Dispose();
                    return false;
                }

                // 2. Инициализация ESPB модуля
                _logger.WriteLine("Initializing ESPB module...");
                espbModule = new EspbModule(this._logger);
                espbModule.Sections.Clear();
                
                // Очищаем все словари
                llvmFuncToEspbIndex.Clear();
                llvmGlobalToEspbIndex.Clear();
                llvmImportToEspbIndex.Clear();
                // llvmValueToEspbReg удален - используется RegisterManager
                nextEspbFuncIndex = 0;
                nextEspbGlobalIndex = 0;
                nextEspbImportIndex = 0;
                registerManager.ResetForNewFile();

                // Инициализируем менеджер immeta с логгером
                immetaManager = new ImmetaManager(this.hintsManager, this._logger);

                // Добавляем базовые секции
                espbModule.Sections.Add(new TypesSection());
                espbModule.Sections.Add(new ImportsSection());
                espbModule.Sections.Add(new FunctionSection());
                dataSection = new DataSection();
                espbModule.Sections.Add(dataSection);
                espbModule.Sections.Add(new GlobalsSection());
                espbModule.Sections.Add(new ExportsSection());
                espbModule.Sections.Add(new CodeSection());
                espbModule.Sections.Add(new FunctionPointerMapSection()); // Секция ID=18
                espbModule.Sections.Add(new JitRegionMetadataSection()); // Секция ID=19

                // Добавляем секцию памяти
                var memorySection = new MemorySection();
                var memoryDefinition = new MemoryDefinition
                {
                    LimitsFlags = 0x00,
                    InitialPages = 1
                };
                memorySection.Memories.Add(memoryDefinition);
                espbModule.Sections.Add(memorySection);
                
                // Добавляем секцию Table (ID=11) для поддержки TABLE операций
                var tableSection = new TableSection();
                var tableDesc = new EspbTableDesc
                {
                    element_type = EspbRefType.FUNCREF,
                    limits = new EspbMemoryLimits
                    {
                        flags = 0x01, // has_max = true
                        initial_size = 10, // Начальный размер таблицы (10 элементов)
                        max_size = 100 // Максимальный размер таблицы
                    }
                };
                tableSection.Tables.Add(tableDesc);
                espbModule.Sections.Add(tableSection);
                
                // Добавляем секцию Element (ID=12) для инициализации таблиц
                var elementSection = new ElementSection();
                // Создаем пустой пассивный сегмент элементов (будет заполнен через TABLE.INIT)
                var elementSegment = new ElementSection.ElementSegment
                {
                    Flags = 1, // Passive segment (не привязан к таблице напрямую)
                    ElementType = EspbRefType.FUNCREF,
                    FunctionIndices = new List<uint> { 0, 0 }, // Два элемента, указывающие на функцию 0
                    OffsetExpression = Array.Empty<byte>() // Пустое выражение для пассивного сегмента
                };
                elementSection.Segments.Add(elementSegment);
                espbModule.Sections.Add(elementSection);
                
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 4 }); // Globals
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 6 }); // Code
                espbModule.Sections.Add(new RelocationsSection { TargetSectionId = 8 }); // Data

                // ===================================================================
                // ЭТАП 1: СБОР МЕТА-ИНФОРМАЦИИ С УЛУЧШЕННЫМ АНАЛИЗОМ
                // ===================================================================
                _logger.WriteLine("\n=== STAGE 1: Collecting Metadata with Improved Analysis ===");
                
                CollectTypes();
                
                // ИСПОЛЬЗУЕМ УЛУЧШЕННУЮ ВЕРСИЮ СБОРА ИМПОРТОВ
                CollectImportsImproved();

                // Apply fast import mapping (name -> index) as a finalization step.
                // This makes fast mapping robust even if some earlier import-collection path did not set flags.
                ApplyFastImportMapping();
                
                CollectGlobals();
                CollectFunctionDeclarations();
                
                // КРИТИЧЕСКИ ВАЖНО: Pre-scan для создания записей указателей на функции
                PreScanForFunctionPointers();
                
                CollectExports();
                
                _logger.WriteLine("=== STAGE 1: Metadata Collection Complete ===\n");

                // ===================================================================
                // ЭТАП 1.5: АНАЛИЗ CALLBACK МЕТАДАННЫХ
                // ===================================================================
                _logger.WriteLine("=== STAGE 1.5: Callback Analysis ===");
                
                // Построим метаданные колбэков из IR и добавим секцию при необходимости
                BuildCallbackInfoFromIR();
                // Всегда пытаемся создать cbmeta секцию (флаг 0x100 устанавливается внутри)
                AddCallbackMetadataSection();
                
                _logger.WriteLine("=== STAGE 1.5: Callback Analysis Complete ===\n");

                // ===================================================================
                // ЭТАП 2: ОПТИМИЗАЦИЯ LLVM-IR
                // ===================================================================
                _logger.WriteLine("=== STAGE 2: LLVM Optimization ===");

                if (EnableLlvmOptimizations)
                {
                    string customPasses = "mem2reg,instcombine,sroa,mem2reg";
                    _logger.WriteLine($"Running optimization pipeline: {customPasses}");

                    var options = LLVM.CreatePassBuilderOptions();
                    byte[] passesBytes = Encoding.ASCII.GetBytes(customPasses + "\0");
                    fixed (byte* pPasses = passesBytes)
                    {
                        LLVMOpaqueError* errPtr = LLVM.RunPasses(llvmModule, (sbyte*)pPasses, (LLVMOpaqueTargetMachine*)0, options);
                        if (errPtr != null)
                        {
                            _logger.WriteLine("Warning: LLVM optimization failed");
                        }
                        else
                        {
                            _logger.WriteLine("LLVM optimization completed successfully");
                        }
                    }
                }
                else
                {
                    _logger.WriteLine("LLVM optimization passes disabled (--no-llvm-opt)");
                }

                _logger.WriteLine("=== STAGE 2: Optimization Complete ===\n");

                // ===================================================================
                // ЭТАП 3: ТРАНСЛЯЦИЯ ФУНКЦИЙ
                // ===================================================================
                _logger.WriteLine("=== STAGE 3: Function Translation ===");
                
                                TranslateAllFunctionBodies();
                
                _logger.WriteLine("=== STAGE 3: Translation Complete ===\n");
                
                // Заполняем секцию JIT Region Metadata (ID=19) из собранных дескрипторов
                // ВАЖНО: вызываем после TranslateAllFunctionBodies, когда все OP_JIT_START/STOP уже обработаны
                PopulateJitRegionMetadataSection();

                // ===================================================================
                // ЭТАП 4: ГЕНЕРАЦИЯ ESPB ФАЙЛА
                // ===================================================================
                _logger.WriteLine("=== STAGE 4: ESPB File Generation ===");

                // Finalize fast import mapping right before serialization.
                // Some imports are added later than Stage 1 (e.g., during callback analysis / integration),
                // so we must apply mapping at the very end to guarantee all function imports are indexed.
                ApplyFastImportMapping();
                
                bool success = espbModule.WriteToFile(espbFilePath);
                if (success)
                {
                    _logger.WriteLine($"ESPB file successfully written to: {espbFilePath}");
                    
                    // Выводим детальный отчет
                    PrintDetailedImportsReport();
                    
                    // Выводим статистику файла
                    var fileInfo = new FileInfo(espbFilePath);
                    _logger.WriteLine($"File size: {fileInfo.Length} bytes");
                    _logger.WriteLine($"Sections count: {espbModule.Sections.Count}");
                    _logger.WriteLine($"Features: 0x{espbModule.Features:X8}");
                }
                else
                {
                    _logger.WriteLine("Failed to write ESPB file");
                }
                
                _logger.WriteLine("=== STAGE 4: File Generation Complete ===\n");
                
                return success;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Translation failed with error: {ex.Message}");
                _logger.WriteLine($"Stack trace: {ex.StackTrace}");
                return false;
            }
            finally
            {
                // Очистка ресурсов
                if (llvmModule.Handle != IntPtr.Zero) llvmModule.Dispose();
                if (llvmContext != null && llvmContext.Handle != IntPtr.Zero) llvmContext.Dispose();
                
                _logger.WriteLine("=== TRANSLATION COMPLETE ===\n");
            }
        }
        
        /// <summary>
        /// Демонстрационный метод для тестирования анализа конкретных функций
        /// </summary>
        public void TestSpecificFunctionAnalysis(string bcFilePath, string[] functionNames)
        {
            _logger.WriteLine($"=== TESTING SPECIFIC FUNCTION ANALYSIS ===");
            _logger.WriteLine($"Input file: {bcFilePath}");
            _logger.WriteLine($"Functions to analyze: {string.Join(", ", functionNames)}");
            
            // Инициализация только для анализа
            if (!InitializeForAnalysis(bcFilePath))
            {
                _logger.WriteLine("Failed to initialize for analysis");
                return;
            }
            
            try
            {
                // Анализируем каждую указанную функцию
                foreach (string funcName in functionNames)
                {
                    AnalyzeSingleImport(funcName);
                }
                
                // Выводим общий отчет
                immetaManager.PrintMarshallingReport();
            }
            finally
            {
                // Очистка
                if (llvmModule.Handle != IntPtr.Zero) llvmModule.Dispose();
                if (llvmContext != null && llvmContext.Handle != IntPtr.Zero) llvmContext.Dispose();
            }
        }
        
        /// <summary>
        /// Вспомогательный метод для инициализации только для анализа
        /// </summary>
        private bool InitializeForAnalysis(string bcFilePath)
        {
            try
            {
                if (!File.Exists(bcFilePath))
                    return false;
                
                llvmContext = LLVMContextRef.Create();
                byte[] fileBytes = File.ReadAllBytes(bcFilePath);
                
                LLVMMemoryBufferRef memBuf;
                fixed (byte* pBytes = fileBytes)
                {
                    byte[] moduleNameBytes = Encoding.UTF8.GetBytes("module\0");
                    fixed (byte* pModuleName = moduleNameBytes)
                    {
                         memBuf = LLVM.CreateMemoryBufferWithMemoryRangeCopy((sbyte*)pBytes, (UIntPtr)fileBytes.LongLength, (sbyte*)pModuleName);
                    }
                }
                
                llvmModule = llvmContext.ParseBitcode(memBuf);
                
                string dataLayoutStr = "e-p:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v128:128:128-a:0:64-n32-S128";
                targetData = LLVMTargetDataRef.FromStringRepresentation(dataLayoutStr);
                
                immetaManager = new ImmetaManager(this.hintsManager, this._logger);
                
                return llvmModule.Handle != IntPtr.Zero && targetData.Handle != IntPtr.Zero;
            }
            catch
            {
                return false;
            }
        }
    }
}