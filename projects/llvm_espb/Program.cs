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
  // --- Основной класс программы ---
    class Program
    {
        static void Main(string[] args)
        {
            // Для обратной совместимости при запуске из командной строки
            Run(args, Console.Out);
        }

        public static void Run(string[] args, TextWriter logger)
        {
            logger.WriteLine("=== ESPB LLVM Translator (Improved) ===");
            bool hexOutput = false;
            bool improvedAnalysis = false;
            bool enableCompaction = true;
            bool enableRegPatching = false;
            bool enablePeephole = true;
            bool enableLlvmOpt = true;
            bool enableCallResultOpt = true;
            bool enableAllocaOpt = true;
            string? hintsFilePath = null;
            string? customFastSymPath = null;
            string? idfFastSymPath = null;
            List<string> fileArgs = new List<string>();
            
            // Обработка аргументов командной строки
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i];
                if (arg.StartsWith("-"))
                {
                    // Обработка опций
                    if (arg == "-h")
                    {
                        hexOutput = true;
                        improvedAnalysis = true;
                        logger.WriteLine("Using improved parameter analysis with hex output");
                    }
                    else if (arg == "-v" || arg == "--improved")
                    {
                        improvedAnalysis = true;
                        logger.WriteLine("Using improved parameter analysis");
                    }
                    else if (arg == "--no-compaction")
                    {
                        enableCompaction = false;
                        logger.WriteLine("Register compaction disabled");
                    }
                    else if (arg == "--patch-regs")
                    {
                        enableRegPatching = true;
                        logger.WriteLine("Register patching enabled (experimental)");
                    }
                    else if (arg == "--no-peephole")
                    {
                        enablePeephole = false;
                        logger.WriteLine("Peephole bytecode optimization disabled");
                    }
                    else if (arg == "--no-llvm-opt")
                    {
                        enableLlvmOpt = false;
                        logger.WriteLine("LLVM IR optimization passes disabled");
                    }
                    else if (arg == "--no-call-result-opt")
                    {
                        enableCallResultOpt = false;
                        logger.WriteLine("Call-result optimizations disabled (always materialize R0 into dest reg)");
                    }
                    else if (arg == "--no-alloca-opt")
                    {
                        enableAllocaOpt = false;
                        logger.WriteLine("ALLOCA-OPT disabled (all allocas will use real memory operations)");
                    }
                    else if (arg == "-hi" && i + 1 < args.Length)
                    {
                        hintsFilePath = args[i + 1];
                        i++;
                        logger.WriteLine($"Using hints file: {hintsFilePath}");
                    }
                    else if (arg == "--custom-fast" && i + 1 < args.Length)
                    {
                        customFastSymPath = args[i + 1];
                        i++;
                        logger.WriteLine($"Using custom_fast symbols: {customFastSymPath}");
                    }
                    else if (arg == "--idf-fast" && i + 1 < args.Length)
                    {
                        idfFastSymPath = args[i + 1];
                        i++;
                        logger.WriteLine($"Using idf_fast symbols: {idfFastSymPath}");
                    }
                    // удалены устаревшие опции
                    // Здесь можно добавить другие опции при необходимости
                }
                else
                {
                    // Это файловый аргумент
                    fileArgs.Add(arg);
                }
            }
            
            if (fileArgs.Count < 2)
            {
                logger.WriteLine("Usage: LLVM_ESPB [-h] [-v|--improved] [--no-compaction] [--patch-regs] [--no-peephole] [--no-llvm-opt] [--no-call-result-opt] [--no-alloca-opt] [-hi <hints_file>] [--custom-fast <custom_fast.sym>] [--idf-fast <idf_fast.sym>] <input.bc> <output.espb>");
                logger.WriteLine("  -h: Output hex dump of the generated ESPB file");
                logger.WriteLine("  -v, --improved: Use improved parameter analysis with detailed logging");
                logger.WriteLine("  --no-compaction: Disable virtual register compaction");
                logger.WriteLine("  --no-peephole: Disable bytecode peephole optimization (MOV self-move removal)");
                logger.WriteLine("  --no-llvm-opt: Disable LLVM IR optimization passes (mem2reg/instcombine/sroa)");
                logger.WriteLine("  --no-call-result-opt: Disable call-result forwarding/skip-MOV optimizations");
                logger.WriteLine("  --no-alloca-opt: Disable ALLOCA-OPT (scalar alloca to register promotion)");
                logger.WriteLine("  --patch-regs: Enable experimental register patching mode");
                logger.WriteLine("  -hi <file>: Use specific hints file for manual FFI metadata control");
                return;
            }
            
            string inputFile = fileArgs[0];
            string outputFile = fileArgs[1];

            var translator = new LLVMTranslator(logger, enableCompaction);
            translator.EnableRegisterPatching = enableRegPatching;
            translator.EnablePeephole = enablePeephole;
            translator.EnableLlvmOptimizations = enableLlvmOpt;
            translator.EnableCallResultOptimizations = enableCallResultOpt;
            translator.EnableAllocaOpt = enableAllocaOpt;

            // Load optional fast symbol maps (name -> index)
            try
            {
                if (!string.IsNullOrEmpty(customFastSymPath))
                {
                    if (!File.Exists(customFastSymPath))
                    {
                        logger.WriteLine($"ERROR: custom_fast sym file not found: {customFastSymPath}");
                        return;
                    }
                    translator.CustomFastMap = FastSymbols.ParseSymFile(customFastSymPath);
                    logger.WriteLine($"Loaded custom_fast sym: {customFastSymPath} (entries={translator.CustomFastMap.Count})");
                    if (translator.CustomFastMap.TryGetValue("host_invoke_cb", out var idxCb))
                        logger.WriteLine($"custom_fast contains host_invoke_cb -> index {idxCb}");
                }

                if (!string.IsNullOrEmpty(idfFastSymPath))
                {
                    if (!File.Exists(idfFastSymPath))
                    {
                        logger.WriteLine($"ERROR: idf_fast sym file not found: {idfFastSymPath}");
                        return;
                    }
                    translator.IdfFastMap = FastSymbols.ParseSymFile(idfFastSymPath);
                    logger.WriteLine($"Loaded idf_fast sym: {idfFastSymPath} (entries={translator.IdfFastMap.Count})");
                    if (translator.IdfFastMap.TryGetValue("printf", out var idxPrintf))
                        logger.WriteLine($"idf_fast contains printf -> index {idxPrintf}");
                    if (translator.IdfFastMap.TryGetValue("puts", out var idxPuts))
                        logger.WriteLine($"idf_fast contains puts -> index {idxPuts}");
                    if (translator.IdfFastMap.TryGetValue("esp_cpu_get_cycle_count", out var idxCycles))
                        logger.WriteLine($"idf_fast contains esp_cpu_get_cycle_count -> index {idxCycles}");
                    else
                        logger.WriteLine("idf_fast does NOT contain esp_cpu_get_cycle_count (key mismatch)");
                }
            }
            catch (Exception ex)
            {
                logger.WriteLine($"Failed to parse fast symbols: {ex.Message}");
                return;
            }
            
            // Если hints файл не указан явно, используем автоматический путь
            if (hintsFilePath == null)
            {
                hintsFilePath = HintsManager.GetDefaultHintsPath(inputFile);
            }
            
            // Всегда используем TranslateFileWithImprovedAnalysis для одинакового поведения
            bool success = translator.TranslateFileWithImprovedAnalysis(inputFile, outputFile, hintsFilePath);

            if (success)
            {
                // PrintDetailedImportsReport() already called inside TranslateFileWithImprovedAnalysis
                
                logger.WriteLine($"Successfully translated {inputFile} to {outputFile}");
                logger.WriteLine($"Total ESPB opcodes generated (emitted): {translator.OpcodeCount}");
                logger.WriteLine($"Total ESPB opcodes generated (final): {translator.FinalOpcodeCount}");
                
                // Если указана опция -h, выводим шестнадцатеричный дамп
                if (hexOutput)
                {
                    PrintHexDump(outputFile, logger);
                }
            }
            else
            {
                logger.WriteLine("Translation failed.");
            }
        }
        
        /// <summary>
        /// Выводит содержимое файла в шестнадцатеричном формате
        /// </summary>
        /// <param name="filePath">Путь к файлу</param>
        static void PrintHexDump(string filePath, TextWriter logger)
        {
            try
            {
                byte[] fileBytes = File.ReadAllBytes(filePath);
                logger.WriteLine("\nHex dump of the generated ESPB file:");
                logger.WriteLine("----------------------------------");
                
                const int bytesPerLine = 16;
                for (int i = 0; i < fileBytes.Length; i += bytesPerLine)
                {
                    // Строка шестнадцатеричных значений
                    string hexLine = "";
                    
                    // Для каждой строки выводим до bytesPerLine байт, группируя по 2 байта
                    for (int j = 0; j < bytesPerLine && (i + j) < fileBytes.Length; j += 2)
                    {
                        if ((i + j + 1) < fileBytes.Length)
                        {
                            // Если доступны два последовательных байта, группируем их
                            hexLine += $"{fileBytes[i + j]:X2}{fileBytes[i + j + 1]:X2} ";
                        }
                        else
                        {
                            // Если остался только один байт, выводим его с заполнением
                            hexLine += $"{fileBytes[i + j]:X2}   ";
                        }
                    }
                    
                    // Выводим только шестнадцатеричные значения без адреса
                    logger.WriteLine(hexLine);
                }
                logger.WriteLine("----------------------------------");
            }
            catch (Exception ex)
            {
                logger.WriteLine($"Error generating hex dump: {ex.Message}");
            }
        }
    }
}
