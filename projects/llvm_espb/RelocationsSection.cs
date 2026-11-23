using System;
using System.Collections.Generic;
using System.IO;

namespace ESPB_Runtime
{
    // Типы релокаций согласно спецификации ESPB v1.7
    public enum RelocationType : byte
    {
        R_ABS32_FUNC = 0x01,   // Абсолютный 32-битный адрес функции: S + A
        R_ABS32_DATA = 0x02,   // Абсолютный 32-битный адрес символа данных: S + A
        R_ABS32_GLOBAL = 0x03, // Абсолютный 32-битный адрес глобальной переменной: S + A
        R_REL32_CALL = 0x04,   // Относительный 32-битный адрес для вызова: S + A - P
        R_REL32_BRANCH = 0x05, // Относительный 32-битный адрес для перехода: S + A - P
        R_FUNC_INDEX = 0x06,   // Индекс функции (внутренней или импортированной): S
        R_GLOBAL_INDEX = 0x07, // Индекс глобальной переменной: S
        R_TAG_INDEX = 0x08,    // Индекс тега исключения: S
        R_TABLE_INDEX = 0x09,  // Индекс таблицы: S
        R_TYPE_INDEX = 0x0A,   // Индекс типа (сигнатуры) в Types: S
        R_MEM_ADDR_I32 = 0x0B  // 32-битное значение из Data по адресу символа данных: *(S+A)
    }

    // Запись о релокации
    public class RelocationEntry
    {
        public RelocationType Type { get; set; }
        public uint Offset { get; set; }       // Смещение в целевой секции (или индекс для Globals)
        public uint SymbolIndex { get; set; }  // Индекс целевого символа (функции, глобала, символа данных...)
        public int Addend { get; set; }        // Добавок (может отсутствовать для некоторых типов)
    }

    // Секция Relocations (ID=9) согласно спецификации ESPB v1.7
    public class RelocationsSection : EspbSection
    {
        public override byte Id => 9;
        public override byte SectionSpecificType => 9;
        public byte TargetSectionId { get; set; } // ID секции (Code=6, Data=7, Globals=4), к которой применяются релокации
        public List<RelocationEntry> Relocations { get; set; } = new List<RelocationEntry>();

        public override void WriteData(BinaryWriter writer)
        {
            writer.Write(TargetSectionId); // Записываем ID целевой секции
            writer.Write((uint)Relocations.Count);
            foreach (var reloc in Relocations)
            {
                writer.Write((byte)reloc.Type);
                writer.Write(reloc.Offset);
                writer.Write(reloc.SymbolIndex);
                // Записываем Addend для всех типов для упрощения
                writer.Write(reloc.Addend);
            }
        }

        public override uint GetSize()
        {
            uint size = 0;
            size += sizeof(byte); // TargetSectionId
            size += sizeof(uint); // Count of relocations
            foreach (var reloc in Relocations)
            {
                size += sizeof(byte);  // Type (RelocationType as byte)
                size += sizeof(uint);  // Offset
                size += sizeof(uint);  // SymbolIndex
                size += sizeof(int);   // Addend
            }
            return size;
        }
    }
} 