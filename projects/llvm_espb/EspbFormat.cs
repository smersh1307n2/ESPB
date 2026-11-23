using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;


namespace ESPB_Runtime
{
    // Типы данных и структуры из спецификации ESPB v1.7
    public enum ValueType : byte // Указываем базовый тип byte для сериализации
    {
        VOID = 0x00, I8 = 0x01, U8 = 0x02, I16 = 0x03, U16 = 0x04,
        I32 = 0x05, U32 = 0x06, I64 = 0x07, U64 = 0x08, F32 = 0x09,
        F64 = 0x0A, PTR = 0x0B, BOOL = 0x0C, V128 = 0x0D,
        // Дополнительные типы, упомянутые в спецификации или необходимые для LLVM
        FUNCREF = 0x11, // 0x01 уже занят, используем другие значения
        EXTERNREF = 0x12,
        EXNREF = 0x13, // Пока не используется активно
        ANY = 0x1F, // Пока не используется активно
        // --- Дополнительные коды для удобства маппинга ---
        F16 = 0x10 // Пока нет в ESPB v1.6 опкодах, но есть в LLVM
    }

    public enum EspbRefType : byte
    {
        FUNCREF = 0x01,
        EXTERNREF = 0x02
    }

    // --- Структуры данных для ESPB модуля ---
    public class EspbModule
    {
        private readonly TextWriter _logger;

        public EspbModule(TextWriter? logger = null)
        {
            _logger = logger ?? Console.Out;
            foreach (var section in sections)
            {
                section.SetLogger(_logger);
            }
        }
        public const ushort MAGIC = 0x4553; // 'ES'
        public const ushort MAGIC_EXT = 0x5042; // 'PB'
        public const uint MAGIC_FULL = 0x42505345; // 'ESPB'
        
        public const uint FEATURE_DATA_SYMBOLS = 0x80;
        public const uint FEATURE_MARSHALLING_META = 0x00000200;
        
        // ===== Версия формата =====
        // Основные изменения:
        // 1.0 - Базовый формат
        // 1.1 - Добавление секции SymbolMap и поддержка релокаций
        // 1.2 - Реструктуризация блоков, типов, сигнатур
        // 1.3 - Добавление плоской модели памяти (flat memory), изменения в sections
        // 1.4 - Расширение инструкций, новые секции
        // 1.5 - Обновление набора инструкций, улучшенная обработка ошибок
        // 1.6 - Добавление секции импортов, стека вызовов
        // 1.7 - Новые инструкции, stack unwinding, улучшенные диагностические возможности
        public const uint VERSION = 0x00000107; // v1.7
        
        public uint Features { get; set; } = 0; // По умолчанию без особых возможностей
        
        // Максимально допустимый размер модуля (64 МБ)
        private const uint MAX_MODULE_SIZE = 64 * 1024 * 1024;
        
        private List<EspbSection> sections = new List<EspbSection>();
        
        // Добавляем свойство Sections для обратной совместимости
        public List<EspbSection> Sections => sections;
        
        // Получение секции по ID
        public T? GetSection<T>(byte id) where T : EspbSection
        {
            return sections.FirstOrDefault(s => s.Id == id) as T;
        }
        
        // Добавление секции
        public void AddSection(EspbSection section)
        {
            // Проверка на дублирование ID
            if (sections.Any(s => s.Id == section.Id))
            {
                 throw new InvalidOperationException($"Section with ID {section.Id} already exists in the module.");
            }
            // Устанавливаем логгер для секции
            section.SetLogger(_logger);
            sections.Add(section);
        }
        
        // Метод для автоматической установки флага FEATURE_DATA_SYMBOLS,
        // если в модуле есть секции DataSection или RelocationsSection
        private void UpdateFeatures()
        {
            // Проверяем наличие данных в DataSection и/или RelocationsSection
            bool hasDataSections = false;
            
            DataSection? dataSection = sections.OfType<DataSection>().FirstOrDefault();
            if (dataSection != null && dataSection.Segments.Count > 0)
            {
                hasDataSections = true;
                _logger.WriteLine($"Module has data segments: {dataSection.Segments.Count}");
            }
            
            RelocationsSection? relocSection = sections.OfType<RelocationsSection>().FirstOrDefault();
            if (relocSection != null && relocSection.Relocations.Count > 0)
            {
                hasDataSections = true;
                _logger.WriteLine($"Module has relocations: {relocSection.Relocations.Count}");
            }
            
            // Обновляем флаг только если действительно есть данные
            if (hasDataSections)
            {
                _logger.WriteLine("Setting FEATURE_DATA_SYMBOLS flag (0x80)");
                Features |= FEATURE_DATA_SYMBOLS; // Добавляем бит 0x80
            }
            else
            {
                _logger.WriteLine("Clearing FEATURE_DATA_SYMBOLS flag (0x80) - no data sections with content");
                Features &= ~FEATURE_DATA_SYMBOLS; // Снимаем ТОЛЬКО бит 0x80, остальные флаги сохраняем
            }
        }
        
        // Запись модуля в файл
        public bool WriteToFile(string filePath)
        {
            Console.WriteLine($"╔════════════════════════════════════════════════════════");
            Console.WriteLine($"║ WriteToFile ENTRY");
            Console.WriteLine($"║ Path: {filePath}");
            Console.WriteLine($"║ Sections.Count = {Sections.Count}");
            Console.WriteLine($"║ Features = 0x{Features:X8}");
            Console.WriteLine($"╚════════════════════════════════════════════════════════");
            try
            {
                // Обновляем флаги на основе содержимого секций
                UpdateFeatures();

                using (FileStream fs = new FileStream(filePath, FileMode.Create))
                using (BinaryWriter writer = new BinaryWriter(fs))
            {
                    // --- ИСПРАВЛЕНИЕ: Собираем данные секций ОДИН РАЗ ---
                    var sectionDataList = sections
                        .Select(s => new { Section = s, Data = s.GetData() })
                        .Where(s => s.Data.Length > 0)
                        .OrderBy(s => s.Section.Id)
                        .ToList();

                    ushort numSections = (ushort)sectionDataList.Count;

                    // Записываем заголовок (16 байт) - старый формат для совместимости с рантаймом
                    writer.Write(MAGIC_FULL);                        // Magic (4 байта) = 0x42505345 "ESPB"
                    writer.Write(VERSION);                           // Version (4 байта) = 0x00000107
                    writer.Write((uint)0);                           // Reserved (4 байта)
                    writer.Write(Features);                          // Features (4 байта)
                    
                    // Записываем количество секций (u16)
                    writer.Write(numSections);                       // NumSections (2 байта)

                    // Вычисляем начало таблицы секций
                    long tableStartPos = writer.BaseStream.Position;
                    // Размер одной записи таблицы секций: Id(u8) + reserved(u8) + reserved(u16) + offset(u32) + size(u32) = 12 байт
                    const int SectionHeaderSize = 12;
                    // Вычисляем смещение начала данных секций
                    uint sectionDataOffset = (uint)(tableStartPos + numSections * SectionHeaderSize);
                    
                    // Записываем таблицу секций, используя собранные размеры
                    uint currentOffset = sectionDataOffset;
                foreach (var entry in sectionDataList)
                {
                        // Записываем запись в таблице секций (12 байт)
                        // Формат: ID, Type, Reserved(u16), Offset(u32), Size(u32)
                        writer.Write((byte)entry.Section.Id);                    // 1 байт - ID
                        writer.Write((byte)entry.Section.SectionSpecificType);   // 1 байт - Type
                        writer.Write((ushort)0);                                 // 2 байта - Reserved
                        writer.Write((uint)currentOffset);                       // 4 байта - Offset
                        writer.Write((uint)entry.Data.Length);                   // 4 байта - Size
                        // Итого: 1+1+4+4+2 = 12 байт

                        // Обновляем смещение для следующей секции БЕЗ PADDING
                        currentOffset += (uint)entry.Data.Length;
                    }
                    
                    // Перемещаемся к началу данных секций (на всякий случай, если позиция сбилась)
                    writer.BaseStream.Seek(sectionDataOffset, SeekOrigin.Begin);

                    // Записываем данные секций БЕЗ PADDING
                foreach (var entry in sectionDataList)
                {
                    writer.Write(entry.Data);
                    }
                }
                
                return true;
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"Error writing ESPB module to file: {ex.Message}");
                return false;
            }
        }
    }

    public abstract class EspbSection
    {
        protected TextWriter _logger = Console.Out;

        public void SetLogger(TextWriter logger)
        {
            _logger = logger;
        }
        public abstract byte Id { get; }
        public abstract byte SectionSpecificType { get; }
        public abstract void WriteData(BinaryWriter writer);
        public abstract uint GetSize(); // Добавлено для расчета размера секции
        
        // Добавляем метод GetData для получения байтового представления секции
        public byte[] GetData()
        {
            using (MemoryStream ms = new MemoryStream())
            using (BinaryWriter writer = new BinaryWriter(ms))
            {
                WriteData(writer);
                return ms.ToArray();
            }
        }
    }

    public class TypesSection : EspbSection
    {
        public override byte Id => 1;
        public override byte SectionSpecificType => 1;
        public List<TypeSignature> Signatures { get; set; } = new List<TypeSignature>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            _logger.WriteLine($"DEBUG TypesSection.WriteData: Writing {Signatures.Count} signatures."); // Отладочный вывод
            // Записываем количество сигнатур как uint (4 байта) - ПЕРВОЕ ДЕЙСТВИЕ
            writer.Write((uint)Signatures.Count);
            
            for (int idx = 0; idx < Signatures.Count; idx++) // Используем for для индекса в логе
            {
                var sig = Signatures[idx];
                // --- DETAILED SIGNATURE DEBUGGING ---
                _logger.WriteLine($"DEBUG Sig[{idx}]: Parameters.Count = {sig.Parameters.Count}");
                _logger.Write($"DEBUG Sig[{idx}] ParamTypes (hex): ");
                foreach (var pType in sig.Parameters) { _logger.Write($"{(byte)pType:X2} "); }
                _logger.WriteLine();
                _logger.Write($"DEBUG Sig[{idx}] ParamTypes (enum): ");
                foreach (var pType in sig.Parameters) { _logger.Write($"{pType} "); }
                _logger.WriteLine();

                _logger.WriteLine($"DEBUG Sig[{idx}]: Results.Count = {sig.Results.Count}");
                _logger.Write($"DEBUG Sig[{idx}] ResultTypes (hex): ");
                foreach (var rType in sig.Results) { _logger.Write($"{(byte)rType:X2} "); }
                _logger.WriteLine();
                _logger.Write($"DEBUG Sig[{idx}] ResultTypes (enum): ");
                foreach (var rType in sig.Results) { _logger.Write($"{rType} "); }
                _logger.WriteLine();
                // --- END DETAILED SIGNATURE DEBUGGING ---

                writer.Write((byte)sig.Parameters.Count);
                foreach (var paramType in sig.Parameters)
                {
                    writer.Write((byte)paramType);
                }
                writer.Write((byte)sig.Results.Count);
                foreach (var resultType in sig.Results)
                {
                    writer.Write((byte)resultType);
                }
            }
        }

        public override uint GetSize()
        {
            // Implementation of GetSize method for TypesSection
            throw new NotImplementedException();
        }
    }

    // Секция Imports (ID=2)
    public class ImportsSection : EspbSection
    {
        public override byte Id => 2;
        public override byte SectionSpecificType => 2;
        public List<EspbImport> Imports { get; set; } = new List<EspbImport>();

        public override void WriteData(BinaryWriter writer)
        {
            writer.Write((uint)Imports.Count);
            foreach (var import in Imports)
            {
                // module_num (u8)
                writer.Write(import.ModuleNum);

                // kind (u8)
                writer.Write((byte)import.Kind);

                switch (import.Kind)
                {
                    case EspbImportKind.Function:
                        writer.Write((ushort)(import.TypeIndex ?? 0));
                        writer.Write(import.ImportFlags);

                        // If indexed: write symbol_index (u16). Otherwise: write name string.
                        if ((import.ImportFlags & 0x10) != 0)
                        {
                            writer.Write(import.SymbolIndex);
                        }
                        else
                        {
                            byte[] entityNameBytes = Encoding.UTF8.GetBytes(import.EntityName);
                            writer.Write((ushort)entityNameBytes.Length);
                            writer.Write(entityNameBytes);
                        }
                        break;

                    case EspbImportKind.Global:
                        // Global keeps entity_name string (needed for resolve)
                        {
                            byte[] entityNameBytes = Encoding.UTF8.GetBytes(import.EntityName);
                            writer.Write((ushort)entityNameBytes.Length);
                            writer.Write(entityNameBytes);
                        }
                        writer.Write((byte)import.GlobalType);
                        writer.Write(import.IsMutable ? (byte)1 : (byte)0);
                        writer.Write(import.IsShared ? (byte)1 : (byte)0);
                        break;

                    case EspbImportKind.Table:
                    case EspbImportKind.Memory:
                        // Keep old behavior for now: write entity_name string then kind-specific fields (not implemented)
                        {
                            byte[] entityNameBytes = Encoding.UTF8.GetBytes(import.EntityName);
                            writer.Write((ushort)entityNameBytes.Length);
                            writer.Write(entityNameBytes);
                        }
                        throw new NotSupportedException($"Import kind {import.Kind} not yet supported in writer");

                    default:
                        throw new NotSupportedException($"Unsupported import kind: {import.Kind}");
                }
            }
        }

        public override uint GetSize()
        {
            // Implementation of GetSize method for ImportsSection
            throw new NotImplementedException();
        }
    }

    public enum EspbImportKind : byte { Function = 0, Table = 1, Memory = 2, Global = 3 }

    public class EspbImport
    {
        // module_num replaces module_name string in ESPB v1.7 fast/numeric imports.
        // Default/global is 0.
        public byte ModuleNum { get; set; } = 0;

        // For named imports this is the entity name. For indexed imports it can be empty.
        public string EntityName { get; set; } = string.Empty;

        public EspbImportKind Kind { get; set; }

        // For Function imports
        public int? TypeIndex { get; set; }
        public byte ImportFlags { get; set; } = 0;
        public ushort SymbolIndex { get; set; } = 0; // only if (ImportFlags & 0x10) != 0

        public bool IsVariadic { get; set; }

        // For Global imports
        public ValueType GlobalType { get; set; }
        public bool IsMutable { get; set; }
        public bool IsShared { get; set; }
    }

     // Секция Functions (ID=3)
     public class FunctionSection : EspbSection
     {
         public override byte Id => 3;
         public override byte SectionSpecificType => 3;
         public List<ushort> TypeIndices { get; set; } = new List<ushort>(); // Индекс сигнатуры для каждой функции

         public override void WriteData(BinaryWriter writer) // Реализация WriteData
         {
             writer.Write((uint)TypeIndices.Count);
             foreach (var idx in TypeIndices)
             {
                 writer.Write(idx);
             }
         }

         public override uint GetSize()
         {
             // Implementation of GetSize method for FunctionSection
             throw new NotImplementedException();
         }
     }

    // Секция Globals (ID=4)
    public class GlobalsSection : EspbSection
    {
        public override byte Id => 4;
        public override byte SectionSpecificType => 6;
        public List<EspbGlobal> Globals { get; set; } = new List<EspbGlobal>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            writer.Write((uint)Globals.Count);
            foreach (var global in Globals)
            {
                writer.Write((byte)global.Type);
                writer.Write(global.IsMutable ? (byte)1 : (byte)0);
                writer.Write(global.IsShared ? (byte)1 : (byte)0);
                writer.Write((byte)global.InitKind);
                switch (global.InitKind)
                {
                    case EspbGlobal.InitializerKind.Zero:
                        break; // Ничего не пишем для нулевой инициализации
                    case EspbGlobal.InitializerKind.Constant:
                        if (global.InitialValue != null)
                        {
                            WriteConstantValueHelper(writer, global.Type, global.InitialValue); // Use helper
                        }
                        else
                        {
                            _logger.WriteLine($"Warning: Global {global.GetHashCode()} has Constant InitKind but null InitialValue.");
                            // WriteZeroValue(writer, global.Type); // Можно раскомментировать для записи нуля
                        }
                        break;
                    case EspbGlobal.InitializerKind.DataSegmentOffset:
                        if (global.InitialValue is int offset && offset >= 0)
                        {
                            writer.Write((uint)offset); // Записываем смещение как U32
                        }
                        else
                        {
                            _logger.WriteLine($"Warning: Global {global.GetHashCode()} has DataSegmentOffset InitKind but invalid InitialValue ({global.InitialValue}). Writing 0 offset.");
                            writer.Write((uint)0); // Записываем 0 в случае ошибки
                        }
                        break;
                }
            }
        }

        // Keep helper private or move to a shared utility class
        private void WriteConstantValueHelper(BinaryWriter writer, ValueType type, object value)
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
                    case ValueType.PTR:
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
                throw;
            }
        }

        public override uint GetSize()
        {
            // Implementation of GetSize method for GlobalsSection
            throw new NotImplementedException();
        }
    }

    public class EspbGlobal
    {
        public enum InitializerKind : byte { Zero = 0, Constant = 1, DataSegmentOffset = 2 }
        public ValueType Type { get; set; }
        public bool IsMutable { get; set; }
        public bool IsShared { get; set; }
        public InitializerKind InitKind { get; set; }
        public object? InitialValue { get; set; } // <--- ИСПРАВЛЕНО: Добавлен '?' для поддержки null
    }

    // Секция Exports (ID=5)
    public class ExportsSection : EspbSection
    {
        public override byte Id => 5;
        public override byte SectionSpecificType => 7;
        public List<EspbExport> Exports { get; set; } = new List<EspbExport>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            writer.Write((uint)Exports.Count);
            foreach (var export in Exports)
            {
                byte[] nameBytes = Encoding.UTF8.GetBytes(export.Name);
                writer.Write((ushort)nameBytes.Length);
                writer.Write(nameBytes);
                writer.Write((byte)export.Kind);
                writer.Write(export.Index); // Индекс экспортируемой сущности (u32)
            }
        }

        public override uint GetSize()
        {
            // Implementation of GetSize method for ExportsSection
            throw new NotImplementedException();
        }
    }

    public enum EspbExportKind : byte { Function = 0, Table = 1, Memory = 2, Global = 3 }

    public class EspbExport
    {
        public string Name { get; set; } = string.Empty; // Инициализация для CS8618
        public EspbExportKind Kind { get; set; }
        public uint Index { get; set; } // u32 index
    }

     // Секция Code (ID=6)
     public class CodeSection : EspbSection
    {
        public override byte Id => 6;
        public override byte SectionSpecificType => 10;
        public List<FunctionBody> Bodies { get; set; } = new List<FunctionBody>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            writer.Write((uint)Bodies.Count); // Записываем количество тел функций
            foreach (var body in Bodies)
             {
                // Записываем размер тела (8 байт header + длина инструкций)
                // Важно: поле body_size НЕ включает 4 байта самого поля body_size
                uint bodySize = 8 + (uint)body.Instructions.Length;
                writer.Write(bodySize);

                _logger.WriteLine($"DEBUG CodeSection: body_size={bodySize}, flags=0x{body.Flags:X2}, max_reg={body.MaxRegUsed}, frame_size={body.FrameSize}, num_vregs={body.NumVirtualRegs}, instructions.Length={body.Instructions.Length}");

                // ✅ Записываем JIT-заголовок (8 байт)
                writer.Write(body.Flags);
                writer.Write(body.MaxRegUsed);
                writer.Write(body.FrameSize);
                writer.Write(body.NumVirtualRegs);
                writer.Write(body.Reserved);

                // Записываем инструкции
                writer.Write(body.Instructions);
            }
        }
        
        // GetSize() не используется для записи, но реализуем для полноты
        public override uint GetSize()
        {
           uint totalSize = 4; // Для поля num_bodies
           foreach (var body in Bodies)
           {
               // 4 байта для поля body_size + 8 байт header + длина инструкций
               totalSize += 4 + 8 + (uint)body.Instructions.Length; 
           }
           return totalSize;
        }
    }

     public class FunctionBody
    {
        // JIT metadata (8 bytes header)
        public byte Flags { get; set; }
        public byte MaxRegUsed { get; set; }
        public ushort FrameSize { get; set; }
        public ushort NumVirtualRegs { get; set; }
        public ushort Reserved { get; set; }
        
        // Bytecode
        public byte[] Instructions { get; set; } = Array.Empty<byte>(); // Инициализация для CS8618
    }

    // Секция Data (ID=8)
    public class DataSection : EspbSection
    {
        public override byte Id => 8;
        public override byte SectionSpecificType => 2;
        public List<DataSegment> Segments { get; set; } = new List<DataSegment>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            writer.Write((uint)Segments.Count);
            foreach (var segment in Segments)
            {
                writer.Write((byte)segment.SegmentType);
                if (segment.SegmentType == DataSegmentType.Active)
                {
                    writer.Write(segment.MemoryIndex); // u32 memory index
                    WriteOffsetExpression(writer, segment.OffsetExpressionValue);
                }
                writer.Write((uint)segment.Data.Length); // u32 size of data
                writer.Write(segment.Data); // byte sequence
            }
        }

        private void WriteOffsetExpression(BinaryWriter writer, int offset)
        {
            // Записываем выражение смещения: LDC.I32.IMM offset; END
            writer.Write((byte)0x18); // LDC.I32.IMM
            writer.Write((byte)0x00); // R0 register for result
            writer.Write(offset);     // immediate offset (imm32 LE)
            writer.Write((byte)0x0F); // END opcode
        }

        public override uint GetSize()
        {
            // Implementation of GetSize method for DataSection
            throw new NotImplementedException();
        }
    }

    public enum DataSegmentType : byte { Active = 0, Passive = 1 }

    public class DataSegment
    {
        public DataSegmentType SegmentType { get; set; }
        public uint MemoryIndex { get; set; } // u32 index (only for Active)
        public int OffsetExpressionValue { get; set; } // Value for simple const offset (only for Active)
        public byte[] Data { get; set; } = Array.Empty<byte>(); // Инициализация для CS8618
    }

    // --- Вспомогательные классы из спецификации (перемещены сюда) ---
    public class TypeSignature
    {
        public List<ValueType> Parameters { get; set; } = new List<ValueType>();
        public List<ValueType> Results { get; set; } = new List<ValueType>();
    }

    // Определение для описания памяти (взято из удаленного EspbMemorySection.cs)
    public class MemoryDefinition
    {
        // limits_flags (u8): Бит 0: есть max_size, Бит 1: shared
        public byte LimitsFlags { get; set; }
        public uint InitialPages { get; set; } // initial_size (u32) - В страницах
        public uint? MaxPages { get; set; }    // max_size (u32) - В страницах, если Бит 0 в LimitsFlags установлен
        // Бит shared (Бит 1) должен устанавливаться/читаться из LimitsFlags напрямую
    }

    public class EspbTableDesc
    {
        public EspbRefType element_type { get; set; }
        public EspbMemoryLimits limits { get; set; } = new EspbMemoryLimits();
    }
    
    public class EspbMemoryLimits
    {
        public byte flags { get; set; }
        public uint initial_size { get; set; }
        public uint max_size { get; set; }
    }


    // These might be more runtime-related, but are defined by the format
    public class Function
    {
        public int TypeIdx { get; set; } // Index into Types section
    }

    public class Import // This describes an import entry, part of the format
    {
        public int TypeIdx { get; set; } // Index into Types section (for Function imports)
        public string Name { get; set; } = string.Empty; // Инициализация для CS8618
        // Other properties depending on ImportKind (Table, Memory, Global)
    }

    public class Tag
    {
        public int TypeIdx { get; set; } // Index into Types section
    }

    public class MemorySection : EspbSection
    {
        public override byte Id => 14;
        public override byte SectionSpecificType => 5;
        // ИСПРАВЛЕНО: Использовать MemoryDefinition вместо MemoryType
        public List<MemoryDefinition> Memories { get; set; } = new List<MemoryDefinition>();

        public override void WriteData(BinaryWriter writer) // Реализация WriteData
        {
            writer.Write((uint)Memories.Count);
            foreach (var memDef in Memories) // ИСПРАВЛЕНО: переменная memDef
            {
                // Определяем флаги на основе полей memDef согласно спецификации v1.7
                byte limitsFlags = memDef.LimitsFlags; // Используем существующий флаг
                if (memDef.MaxPages.HasValue)
                {
                    limitsFlags |= 0x01; // Устанавливаем бит has_max, если MaxPages задано
                }
                // Предполагаем, что бит shared (0x02) уже установлен в memDef.LimitsFlags, если нужно

                writer.Write(limitsFlags);        // Записываем флаги (u8)
                writer.Write(memDef.InitialPages);  // initial_size (u32)
                if ((limitsFlags & 0x01) != 0) // Проверяем флаг has_max
                {
                    // Записываем max_size только если флаг установлен.
                    if (memDef.MaxPages.HasValue)
                    {
                        writer.Write(memDef.MaxPages.Value); // max_size (u32)
                    }
                    else
                    {
                        _logger.WriteLine($"Warning: MemoryDefinition has has_max flag set but MaxPages is null. Writing 0 for max_size.");
                        writer.Write((uint)0);
                    }
                }
            }
        }

        // GetSize() не используется для записи, но реализуем для полноты
        public override uint GetSize()
        {
            uint totalSize = 4; // Для поля num_memories
            foreach (var memDef in Memories)
            {
                totalSize += 1; // limits_flags
                totalSize += 4; // initial_pages
                if ((memDef.LimitsFlags & 0x01) != 0 || memDef.MaxPages.HasValue)
                {
                    totalSize += 4; // maximum_pages
                }
            }
            return totalSize;
        }
    }

    // Секция Callback Metadata (ID=10)
    public class CallbackMetadataSection : EspbSection
    {
        public override byte Id => 10;
        public override byte SectionSpecificType => 0; // В спецификации не указан, ставим 0

        public List<ImportCallbackInfo> ImportsWithCallbacks { get; set; } = new List<ImportCallbackInfo>();

        public override void WriteData(BinaryWriter writer)
        {
            _logger.WriteLine($"[CBMETA] Writing CallbackMetadata section:");
            _logger.WriteLine($"[CBMETA] - num_signatures: 0");
            _logger.WriteLine($"[CBMETA] - num_imports_with_cb: {ImportsWithCallbacks.Count}");
            
            writer.Write((byte)0); // num_signatures - всегда 0, как в спецификации
            writer.Write((ushort)ImportsWithCallbacks.Count);

            foreach (var importInfo in ImportsWithCallbacks)
            {
                _logger.WriteLine($"[CBMETA] - Import {importInfo.ImportIndex} ('{importInfo.ImportName}'): {importInfo.Callbacks.Count} callbacks");
                writer.Write(importInfo.ImportIndex);
                writer.Write((byte)importInfo.Callbacks.Count);

                foreach (var cb in importInfo.Callbacks)
                {
                    byte cbNibble = (byte)(cb.CallbackParamIndex & 0x0F);
                    byte udNibble = (byte)((cb.UserDataParamIndex.HasValue ? cb.UserDataParamIndex.Value : 0x0F) & 0x0F);
                    byte cbHeader = (byte)((udNibble << 4) | cbNibble);
                    
                    _logger.WriteLine($"[CBMETA]   - Callback param {cb.CallbackParamIndex}, userdata {cb.UserDataParamIndex}, EspbFunc {cb.EspbFuncIndex} for '{importInfo.ImportName}'");
                    
                    writer.Write(cbHeader);
                    // Кодируем 15-битный EspbFuncIndex в 2 байта, как ожидает интерпретатор
                    uint funcIdx = (uint)cb.EspbFuncIndex;
                    byte byte1 = (byte)(funcIdx & 0xFF); // Младшие 8 бит
                    // Старшие 7 бит в младшие 7 бит второго байта. Старший бит зарезервирован.
                    byte byte2 = (byte)((funcIdx >> 8) & 0x7F);
                    
                    writer.Write(byte1);
                    writer.Write(byte2);
                }
            }
            
            uint sectionSize = GetSize();
            _logger.WriteLine($"[CBMETA] Total section size: {sectionSize} bytes");
        }

        public override uint GetSize()
        {
            uint size = 1 + 2; // num_signatures + num_imports_with_cb
            foreach (var importInfo in ImportsWithCallbacks)
            {
                size += 2; // import_index
                size += 1; // num_callbacks
                size += (uint)importInfo.Callbacks.Count * 3; // 3 байта на каждый entry
            }
            return size;
        }
    }

    public class ImportCallbackInfo
    {
        public ushort ImportIndex { get; set; }
        public string ImportName { get; set; } = string.Empty; // ДОБАВЛЕНО: имя импорта для отладки
        public List<CallbackParameterInfo> Callbacks { get; set; } = new List<CallbackParameterInfo>();
    }

    // Секция Table (ID=11)
    public class TableSection : EspbSection
    {
        public override byte Id => 11;
        public override byte SectionSpecificType => 0;
        public List<EspbTableDesc> Tables { get; set; } = new List<EspbTableDesc>();

        public override void WriteData(BinaryWriter writer)
        {
            writer.Write((uint)Tables.Count);
            foreach (var tableDef in Tables)
            {
                writer.Write((byte)tableDef.element_type);
                writer.Write(tableDef.limits.flags);
                writer.Write(tableDef.limits.initial_size);
                if ((tableDef.limits.flags & 0x01) != 0)
                    writer.Write(tableDef.limits.max_size);
            }
        }

        public override uint GetSize()
        {
            uint size = 4;
            foreach (var tableDef in Tables)
            {
                size += 1; // elem type
                size += 1; // limits_flags
                size += 4; // initial pages
                if ((tableDef.limits.flags & 0x01) != 0)
                    size += 4; // max pages
            }
            return size;
        }
    }

    // Секция Element (ID=12)
    public class ElementSegment
    {
        public uint Flags { get; set; }
        public uint TableIndex { get; set; }
        public EspbRefType ElementType { get; set; }
        public List<uint> FunctionIndices { get; set; } = new List<uint>();
        public byte[] OffsetExpression { get; set; } = Array.Empty<byte>();
    }

    public class EspbElementSegment
    {
        public uint flags { get; set; }
        public uint table_index { get; set; }
        public EspbRefType element_type { get; set; }
        public List<uint> function_indices { get; set; } = new List<uint>();
        public byte[] offset_expression { get; set; } = Array.Empty<byte>();
    }

    public class ElementSection : EspbSection
    {
        public class ElementSegment
        {
            public uint Flags { get; set; }
            public uint TableIndex { get; set; }
            public EspbRefType ElementType { get; set; }
            public List<uint> FunctionIndices { get; set; } = new List<uint>();
            public byte[] OffsetExpression { get; set; } = Array.Empty<byte>();
        }

        public override byte Id => 12;
        public override byte SectionSpecificType => 0;
        public List<ElementSegment> Segments { get; set; } = new List<ElementSegment>();

        public override void WriteData(BinaryWriter writer)
        {
            writer.Write((uint)Segments.Count);
            foreach (var seg in Segments)
            {
                writer.Write(seg.Flags);
                if (seg.Flags == 0 || seg.Flags == 2)
                {
                    writer.Write(seg.TableIndex);
                    writer.Write(seg.OffsetExpression);
                }
                writer.Write((byte)seg.ElementType);
                writer.Write((uint)seg.FunctionIndices.Count);
                foreach (var idx in seg.FunctionIndices)
                    writer.Write(idx);
            }
        }

        public override uint GetSize()
        {
            uint size = 4;
            foreach (var seg in Segments)
            {
                size += 4;
                if (seg.Flags == 0 || seg.Flags == 2)
                    size += 4 + (uint)seg.OffsetExpression.Length;
                size += 1;
                size += 4 + (uint)(4 * seg.FunctionIndices.Count);
            }
            return size;
        }
    }

    // Секция Start (ID=15)
    public class StartSection : EspbSection
    {
        public override byte Id => 15;
        public override byte SectionSpecificType => 0;
        public uint FunctionIndex { get; set; }

        public override void WriteData(BinaryWriter writer)
        {
            writer.Write(FunctionIndex);
        }

        public override uint GetSize() => 4;
    }
    
    // Новая секция Function Pointer Map (ID=18)
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FunctionPointerMapEntry
    {
        public uint DataOffset; // Смещение в сегменте данных, где хранится "адрес"
        public ushort FunctionIndex; // Глобальный индекс функции в ESPB
    }

    public class FunctionPointerMapSection : EspbSection
    {
        public override byte Id => 18;
        public override byte SectionSpecificType => 0; // Не используется, ставим 0

        public List<FunctionPointerMapEntry> Entries { get; } = new List<FunctionPointerMapEntry>();

        public void AddEntry(uint dataOffset, ushort functionIndex)
        {
            Entries.Add(new FunctionPointerMapEntry { DataOffset = dataOffset, FunctionIndex = functionIndex });
        }

        public override void WriteData(BinaryWriter writer)
        {
            _logger.WriteLine($"[FuncPtrMapSection] Writing section. Entry count: {Entries.Count}");
            // Записываем количество записей
            writer.Write((uint)Entries.Count);

            // Записываем каждую запись
            foreach (var entry in Entries)
            {
                _logger.WriteLine($"[FuncPtrMapSection]   - Writing entry: DataOffset={entry.DataOffset}, FuncIndex={entry.FunctionIndex}");
                writer.Write(entry.DataOffset);
                writer.Write(entry.FunctionIndex); // This is now a ushort (16-bit)
            }
        }

        public override uint GetSize()
        {
            // 4 байта для количества записей + (4 для смещения + 2 для индекса) на каждую запись
            return 4 + (uint)(Entries.Count * 6);
        }
    }

    // Секция JIT Region Metadata (ID=19)
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct JitRegionDescriptor
    {
        public ushort FunctionIndex;  // func_idx (u16) — функция, содержащая регион
        public ushort StartOffset;    // start_off (u16) — offset первого опкода тела региона (относительно начала байткода функции)
        public ushort Length;         // len (u16) — размер тела региона в байтах
        public ushort Flags;          // flags (u16) — зарезервировано (0x0000)
    }

    public class JitRegionMetadataSection : EspbSection
    {
        public override byte Id => 19;
        public override byte SectionSpecificType => 0; // Не используется, ставим 0

        public List<JitRegionDescriptor> Regions { get; } = new List<JitRegionDescriptor>();

        public void AddRegion(ushort functionIndex, ushort startOffset, ushort length, ushort flags = 0)
        {
            Regions.Add(new JitRegionDescriptor 
            { 
                FunctionIndex = functionIndex, 
                StartOffset = startOffset, 
                Length = length, 
                Flags = flags 
            });
        }

        public override void WriteData(BinaryWriter writer)
        {
            _logger.WriteLine($"[JitRegionMetadata] Writing section. Region count: {Regions.Count}");
            // Записываем количество регионов (u16)
            writer.Write((ushort)Regions.Count);

            // Записываем каждый дескриптор региона (8 байт)
            foreach (var region in Regions)
            {
                _logger.WriteLine($"[JitRegionMetadata]   - Region: FuncIdx={region.FunctionIndex}, StartOff={region.StartOffset}, Len={region.Length}, Flags=0x{region.Flags:X4}");
                writer.Write(region.FunctionIndex);
                writer.Write(region.StartOffset);
                writer.Write(region.Length);
                writer.Write(region.Flags);
            }
        }

        public override uint GetSize()
        {
            // 2 байта для num_jit_regions + (8 байт на каждый дескриптор)
            return 2 + (uint)(Regions.Count * 8);
        }
    }


    // Секция Custom (ID=0) для произвольных пользовательских данных
    public class CustomSection : EspbSection
    {
        public override byte Id => 0;
        public override byte SectionSpecificType => 0;
        
        // Имя пользовательской секции (например, "espb.callback_signatures")
        public string Name { get; set; } = string.Empty;
        
        // Произвольные данные секции
        public byte[] Data { get; set; } = Array.Empty<byte>();
        
        public override void WriteData(BinaryWriter writer)
        {
            // Записываем длину имени
            writer.Write((uint)Name.Length);
            
            // Записываем само имя в UTF-8
            byte[] nameBytes = Encoding.UTF8.GetBytes(Name);
            writer.Write(nameBytes);
            
            // Записываем данные секции
            writer.Write(Data);
        }
        
        public override uint GetSize()
        {
            // 4 байта для длины имени + длина имени + размер данных
            return 4 + (uint)Encoding.UTF8.GetByteCount(Name) + (uint)Data.Length;
        }
    }
}
