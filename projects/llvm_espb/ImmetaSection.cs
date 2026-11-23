using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;

namespace ESPB_Runtime
{
    /// <summary>
    /// Структура для информации об аргументе в маршалинге
    /// </summary>
    public struct ImmetaArgEntry
    {
        public byte ArgIndex { get; set; }      // Индекс аргумента
        public byte DirectionFlags { get; set; } // Флаги направления (IN/OUT/INOUT)
        public byte SizeKind { get; set; }      // Тип размера (константа или из аргумента)
        public byte SizeValue { get; set; }     // Значение размера
        public byte HandlerIndex { get; set; }  // Новое поле: тип обработчика (0=стандартный, 1=асинхронный)

        public const byte DIRECTION_IN = 0x01;
        public const byte DIRECTION_OUT = 0x02;
        public const byte DIRECTION_INOUT = 0x03;
        public const byte SIZE_KIND_CONST = 0x00;
        public const byte SIZE_KIND_FROM_ARG = 0x01;
    }

    /// <summary>
    /// Структура для информации об импорте с маршалингом
    /// </summary>
    public struct ImmetaImportEntry
    {
        public ushort ImportIndex { get; set; }       // Индекс импорта
        public byte NumMarshalledArgs { get; set; }   // Количество аргументов с маршалингом
        public List<ImmetaArgEntry> Args { get; set; } // Список аргументов

        public ImmetaImportEntry(ushort importIdx)
        {
            ImportIndex = importIdx;
            NumMarshalledArgs = 0;
            Args = new List<ImmetaArgEntry>();
        }
    }

    /// <summary>
    /// Секция Import Marshalling Metadata (immeta) - ID=17
    /// Содержит информацию о маршалинге аргументов для импортированных функций
    /// </summary>
    public class ImmetaSection : EspbSection
    {
        public override byte Id => 17;
        public override byte SectionSpecificType => 18; // Уникальный тип для секции immeta

        /// <summary>
        /// Количество импортов с метаданными маршалинга
        /// </summary>
        public ushort NumImportsWithMeta { get; set; }

        /// <summary>
        /// Список записей для каждого импорта
        /// </summary>
        public List<ImmetaImportEntry> Imports { get; set; }

        public ImmetaSection()
        {
            NumImportsWithMeta = 0;
            Imports = new List<ImmetaImportEntry>();
        }

        /// <summary>
        /// Добавляет или обновляет информацию о маршалинге для указанного импорта
        /// </summary>
        public void AddMarshallingInfo(ushort importIndex, List<MarshallingInfo> marshalledArgs)
        {
            if (marshalledArgs == null || marshalledArgs.Count == 0)
                return;

            // Ищем существующую запись для этого импорта
            var existingEntryIndex = Imports.FindIndex(i => i.ImportIndex == importIndex);
            ImmetaImportEntry existingEntry;
            
            if (existingEntryIndex >= 0)
            {
                existingEntry = Imports[existingEntryIndex];
            }
            else
            {
                // Создаем новую запись с правильным ImportIndex
                existingEntry = new ImmetaImportEntry(importIndex);
            }

            // Добавляем только аргументы, требующие маршалинга
            foreach (var marshalling in marshalledArgs)
            {
                if (marshalling.Direction != MarshallingDirection.None &&
                    marshalling.Direction != MarshallingDirection.In) // IN не требует специального маршалинга
                {
                    var argEntry = new ImmetaArgEntry
                    {
                        ArgIndex = (byte)marshalledArgs.IndexOf(marshalling),
                        DirectionFlags = GetDirectionFlags(marshalling.Direction),
                        SizeKind = (byte)marshalling.SizeMode,
                        SizeValue = (byte)marshalling.SizeValue,
                        HandlerIndex = marshalling.HandlerIndex // Новое поле
                    };

                    existingEntry.Args.Add(argEntry);
                    existingEntry.NumMarshalledArgs = (byte)existingEntry.Args.Count;
                }
            }

            // Обновляем запись в списке
            if (existingEntry.NumMarshalledArgs > 0)
            {
                if (existingEntryIndex >= 0)
                {
                    // Обновляем существующую запись
                    Imports[existingEntryIndex] = existingEntry;
                }
                else
                {
                    // Добавляем новую запись
                    Imports.Add(existingEntry);
                }
                NumImportsWithMeta = (ushort)Imports.Count;
            }
        }

        /// <summary>
        /// Конвертирует направление маршалинга в битовые флаги
        /// </summary>
        private static byte GetDirectionFlags(MarshallingDirection direction)
        {
            switch (direction)
            {
                case MarshallingDirection.In:
                    return ImmetaArgEntry.DIRECTION_IN;
                case MarshallingDirection.Out:
                    return ImmetaArgEntry.DIRECTION_OUT;
                case MarshallingDirection.InOut:
                    return ImmetaArgEntry.DIRECTION_INOUT;
                default:
                    return 0x00;
            }
        }

        public override void WriteData(BinaryWriter writer)
        {
            _logger.WriteLine($"Writing Import Marshalling Metadata section with {NumImportsWithMeta} imports requiring marshalling");

            writer.Write(NumImportsWithMeta);

            foreach (var importEntry in Imports)
            {
                writer.Write(importEntry.ImportIndex);
                writer.Write(importEntry.NumMarshalledArgs);

                foreach (var argEntry in importEntry.Args)
                {
                    writer.Write(argEntry.ArgIndex);
                    writer.Write(argEntry.DirectionFlags);
                    writer.Write(argEntry.SizeKind);
                    writer.Write(argEntry.SizeValue);
                    writer.Write(argEntry.HandlerIndex); // Новое поле
                }
            }
        }

        public override uint GetSize()
        {
            uint size = sizeof(ushort); // NumImportsWithMeta

            foreach (var importEntry in Imports)
            {
                size += sizeof(ushort); // ImportIndex
                size += sizeof(byte);   // NumMarshalledArgs

                foreach (var argEntry in importEntry.Args)
                {
                    size += sizeof(byte) * 5; // 5 байт на каждый аргумент (ArgIndex, DirectionFlags, SizeKind, SizeValue, HandlerIndex)
                }
            }

            return size;
        }
    }
}
