/*
 * espb component
 * Copyright (C) 2025 Smersh
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "espb_interpreter.h"
#include "espb_interpreter_parser.h"
#include "espb_interpreter_reader.h" // Для функций чтения read_u32 и т.д.

#include <stdlib.h> // для malloc, free, calloc
#include "safe_memory.h"
#include <string.h> // для memcpy, strdup
#include <stdio.h>  // для printf (отладка)
#include <inttypes.h> // для PRIx32 и т.д. (может понадобиться для отладки)

// Если используются логи ESP, раскомментируйте и убедитесь, что TAG определен
// #include "esp_log.h"
// static const char *TAG = "espb_parser"; 

// Вспомогательная функция для очистки полей EspbModule, связанных с секциями
static void espb_clear_module_sections(EspbModule *module) {
    if (!module) return;

    if (module->section_table) {
        free(module->section_table);
        module->section_table = NULL;
    }
    if (module->signatures) {
        for (uint32_t i = 0; i < module->num_signatures; ++i) {
            if (module->signatures[i].param_types) free(module->signatures[i].param_types);
            if (module->signatures[i].return_types) free(module->signatures[i].return_types);
        }
        free(module->signatures);
        module->signatures = NULL;
    }
    if (module->function_signature_indices) {
        free(module->function_signature_indices);
        module->function_signature_indices = NULL;
    }
    if (module->function_bodies) {
        // const uint8_t *code указывает на исходный буфер, не освобождаем
        free(module->function_bodies);
        module->function_bodies = NULL;
    }
    if (module->memories) {
        free(module->memories);
        module->memories = NULL;
    }
    if (module->globals) {
        // Данные инициализатора (если init_kind=CONST) являются частью исходного буфера
        free(module->globals);
        module->globals = NULL;
    }
    if (module->data_segments) {
        // const uint8_t *offset_expr и const uint8_t *data указывают на исходный буфер
        free(module->data_segments);
        module->data_segments = NULL;
    }
    if (module->imports) {
        for (uint32_t i = 0; i < module->num_imports; ++i) {
            if (module->imports[i].module_name) free(module->imports[i].module_name);
            if (module->imports[i].entity_name) free(module->imports[i].entity_name);
        }
        free(module->imports);
        module->imports = NULL;
    }
    if (module->exports) {
        for (uint32_t i = 0; i < module->num_exports; ++i) {
            if (module->exports[i].name) free(module->exports[i].name);
        }
        free(module->exports);
        module->exports = NULL;
    }
    if (module->relocations) {
        free(module->relocations);
        module->relocations = NULL;
    }
    if (module->tables) {
        free(module->tables);
        module->tables = NULL;
    }
    if (module->element_segments) {
        for (uint32_t i = 0; i < module->num_element_segments; ++i) {
            // const uint8_t* offset_expr указывает на исходный буфер
            if (module->element_segments[i].function_indices) free(module->element_segments[i].function_indices);
        }
        free(module->element_segments);
        module->element_segments = NULL;
    }
    // Очистка cbmeta
    if (module->cbmeta.signatures) {
        free(module->cbmeta.signatures);
        module->cbmeta.signatures = NULL;
    }
    if (module->cbmeta.imports) {
        free(module->cbmeta.imports);
        module->cbmeta.imports = NULL;
    }
    
    // Очистка immeta данных
    if (module->immeta.imports) {
        for (uint16_t i = 0; i < module->immeta.num_imports_with_meta; ++i) {
            if (module->immeta.imports[i].args) {
                free(module->immeta.imports[i].args);
            }
        }
        free(module->immeta.imports);
        module->immeta.imports = NULL;
    }
    module->immeta.num_imports_with_meta = 0;
    module->cbmeta.num_signatures = 0;
    module->cbmeta.num_imports_with_cb = 0;
    // Обнуляем счетчики
    module->num_signatures = 0;
    module->num_functions = 0;
    module->num_memories = 0;
    module->num_globals = 0;
    module->num_data_segments = 0;
    module->num_imports = 0;
    module->num_exports = 0;
    module->num_relocations = 0;
    module->num_tables = 0;
    module->num_element_segments = 0;
    module->has_start_function = false;
    module->start_function_index = 0;
}

bool espb_find_section(const EspbModule *module, uint8_t section_id, const uint8_t **out_data, uint32_t *out_size) {
    if (!module || !module->section_table || !out_data || !out_size) {
        return false;
    }
    for (uint16_t i = 0; i < module->header.num_sections; ++i) {
        if (module->section_table[i].section_id == section_id && module->section_table[i].section_size > 0) {
            // Проверка, что смещение и размер секции не выходят за пределы буфера
            if (module->section_table[i].section_offset > module->buffer_size ||
                module->section_table[i].section_offset + module->section_table[i].section_size > module->buffer_size) {
                // ESP_LOGE(TAG, "Section %u offset/size out of bounds", section_id);
                fprintf(stderr, "Section %u offset/size out of bounds\n", (unsigned int)section_id);
                return false;
            }
            *out_data = module->buffer + module->section_table[i].section_offset;
            *out_size = module->section_table[i].section_size;
            return true;
        }
    }
    return false;
}

EspbResult espb_parse_header_and_sections(EspbModule *module, const uint8_t *buffer, size_t buffer_size) {
    if (!module || !buffer) {
        // ESP_LOGE(TAG, "espb_parse_header_and_sections: Invalid arguments (module or buffer is NULL)");
        fprintf(stderr, "espb_parse_header_and_sections: Invalid arguments (module or buffer is NULL)\n");
        return ESPB_ERR_INVALID_HEADER; // Или другой подходящий код ошибки
    }

    fprintf(stderr, "DEBUG: Starting to parse header and sections. Buffer size: %zu bytes\n", buffer_size);

    module->buffer = buffer;
    module->buffer_size = buffer_size;
    module->section_table = NULL; // Инициализируем перед использованием

    // 1. Проверка минимального размера буфера для заголовка
    // Magic (4) + Version (4) + Flags (4) + Features (4) + NumSections (2) = 18 байт
    if (buffer_size < sizeof(EspbHeader)) { 
        // ESP_LOGE(TAG, "Buffer too small for header (%zu < %zu)", buffer_size, sizeof(EspbHeader));
        fprintf(stderr, "Buffer too small for header (%zu < %zu)\n", buffer_size, sizeof(EspbHeader));
        return ESPB_ERR_BUFFER_TOO_SMALL;
    }

    const uint8_t *ptr = buffer;
    const uint8_t *end_ptr = buffer + buffer_size;

    // Читаем заголовок во временные переменные
    uint32_t magic, version, flags, features;
    uint16_t num_sections;
    
    if (!read_u32(&ptr, end_ptr, &magic) ||
        !read_u32(&ptr, end_ptr, &version) ||
        !read_u32(&ptr, end_ptr, &flags) ||
        !read_u32(&ptr, end_ptr, &features) ||
        !read_u16(&ptr, end_ptr, &num_sections)) {
        fprintf(stderr, "Failed to read header fields.\n");
        return ESPB_ERR_INVALID_HEADER;
    }
    
    // Копируем данные в структуру
    module->header.magic = magic;
    module->header.version = version;
    module->header.flags = flags;
    module->header.features = features;
    module->header.num_sections = num_sections;

    fprintf(stderr, "DEBUG: Read header - Magic: 0x%08lX, Version: 0x%08lX, NumSections: %u\n", 
            (unsigned long)magic, (unsigned long)version, num_sections);

    if (module->header.magic != ESPB_MAGIC_NUMBER) {
        // ESP_LOGE(TAG, "Invalid magic number (0x%08" PRIX32 " != 0x%08X)", module->header.magic, ESPB_MAGIC_NUMBER);
        fprintf(stderr, "Invalid magic number (0x%08" PRIX32 " != 0x%08X)\n", module->header.magic, ESPB_MAGIC_NUMBER);
        return ESPB_ERR_INVALID_MAGIC;
    }

    if (module->header.version != ESPB_VERSION_1_7 && module->header.version != ESPB_VERSION_1_6) { // Проверяем поддерживаемые версии
        // ESP_LOGW(TAG, "Unsupported version (0x%08" PRIX32 ")", module->header.version);
        fprintf(stderr, "Unsupported version (0x%08" PRIX32 ")\n", module->header.version);
        // Можно вернуть ошибку или продолжить, если есть обратная совместимость
        return ESPB_ERR_UNSUPPORTED_VERSION; 
    }
    
    // printf("ESPb Header: Magic=0x%08x, Version=0x%08x, Flags=0x%08x, Features=0x%08x, NumSections=%hu\n",
    //        module->header.magic, module->header.version, module->header.flags, module->header.features, module->header.num_sections);

    if (module->header.num_sections == 0 || module->header.num_sections > 256) { // Разумный предел для количества секций
        // ESP_LOGE(TAG, "Invalid number of sections: %hu", module->header.num_sections);
        fprintf(stderr, "Invalid number of sections: %" PRIu16 "\n", module->header.num_sections);
        return ESPB_ERR_INVALID_SECTION_TABLE;
    }

    // Определяем текущую позицию после заголовка
    fprintf(stderr, "DEBUG: Current position after header: %td bytes\n", (ptrdiff_t)(ptr - buffer));

    // 3. Проверка размера буфера для таблицы секций
    // Каждая запись заголовка секции занимает 12 байт: ID(1) + резервный байт(1) + резерв(2) + смещение(4) + размер(4)
    size_t section_entry_size_in_file = 12;
    size_t section_table_size = (size_t)module->header.num_sections * section_entry_size_in_file;

    fprintf(stderr, "DEBUG: Section table starts at offset %td and contains %u entries of %zu bytes each (total %zu bytes)\n", 
            (ptrdiff_t)(ptr - buffer), module->header.num_sections, section_entry_size_in_file, section_table_size);

    if (ptr + section_table_size > end_ptr) {
        fprintf(stderr, "Buffer too small for section table (expected %zu entries of %zu bytes, available %td bytes)\n",
                (size_t)module->header.num_sections, section_entry_size_in_file, (ptrdiff_t)(end_ptr - ptr));
        return ESPB_ERR_BUFFER_TOO_SMALL;
    }

    // 4. Выделение памяти для таблицы секций
    module->section_table = (SectionHeaderEntry *)SAFE_CALLOC(module->header.num_sections, sizeof(SectionHeaderEntry));
    if (!module->section_table) {
        // ESP_LOGE(TAG, "Failed to allocate memory for section table");
        fprintf(stderr, "Failed to allocate memory for section table\n");
        return ESPB_ERR_MEMORY_ALLOC;
    }

    // 5. Чтение каждой записи таблицы секций
    for (uint16_t i = 0; i < module->header.num_sections; ++i) {
        // Чтение записи заголовка секции: ID (u8) + резервный байт (u8) + резерв (u16) + offset (u32) + size (u32)
        uint8_t sec_id;
        uint8_t reserve_byte;
        uint16_t reserve_ushort;
        if (!read_u8(&ptr, end_ptr, &sec_id) ||
            !read_u8(&ptr, end_ptr, &reserve_byte) ||
            !read_u16(&ptr, end_ptr, &reserve_ushort)) {
            fprintf(stderr, "Failed to read section header part for entry %hu\n", i);
            free(module->section_table);
            module->section_table = NULL;
            return ESPB_ERR_INVALID_SECTION_TABLE;
        }
        uint32_t sec_offset;
        uint32_t sec_size;
        
        // Печатаем информацию о чтении для отладки
        fprintf(stderr, "DEBUG: Section %u (ID=%u): About to read offset and size. Current buffer offset: %td\n",
                (unsigned int)i, (unsigned int)sec_id, (ptrdiff_t)(ptr - buffer));
        
        // Добавим отладочный вывод сырых байт ПЕРЕД чтением sec_offset
        if (ptr + 8 <= end_ptr) { // Убедимся, что есть хотя бы 8 байт для offset + size
             fprintf(stderr, "DEBUG: Section %u (ID=%u): Raw bytes for offset (4): %02x %02x %02x %02x\n",
                     (unsigned int)i, (unsigned int)sec_id, ptr[0], ptr[1], ptr[2], ptr[3]);
        } else {
            fprintf(stderr, "DEBUG: Section %u (ID=%u): Not enough bytes for offset.\n", (unsigned int)i, (unsigned int)sec_id);
        }

        if (!read_u32(&ptr, end_ptr, &sec_offset)) {
            fprintf(stderr, "Failed to read section offset for entry %hu\n", i);
            free(module->section_table);
            module->section_table = NULL;
            return ESPB_ERR_INVALID_SECTION_TABLE;
        }

        // Добавим отладочный вывод сырых байт ПЕРЕД чтением sec_size
        if (ptr + 4 <= end_ptr) { // Убедимся, что есть хотя бы 4 байта для size
             fprintf(stderr, "DEBUG: Section %u (ID=%u): Raw bytes for size (4) at buffer offset %td: %02x %02x %02x %02x\n",
                     (unsigned int)i, (unsigned int)sec_id, (ptrdiff_t)(ptr-buffer), ptr[0], ptr[1], ptr[2], ptr[3]);
        } else {
            fprintf(stderr, "DEBUG: Section %u (ID=%u): Not enough bytes for size.\n", (unsigned int)i, (unsigned int)sec_id);
        }
        
        if (!read_u32(&ptr, end_ptr, &sec_size)) {
            fprintf(stderr, "Failed to read section size for entry %hu\n", i);
            free(module->section_table);
            module->section_table = NULL;
            return ESPB_ERR_INVALID_SECTION_TABLE;
        }

        module->section_table[i].section_id = sec_id;
        module->section_table[i].section_offset = sec_offset;
        module->section_table[i].section_size = sec_size;
        
        // Проверка на разумность смещения и размера для всех секций
        if (sec_offset > buffer_size) {
            fprintf(stderr, "WARNING: Section %u (ID %u) has invalid offset: %u > %zu, skipping\n", 
                    (unsigned int)i, (unsigned int)sec_id, 
                    (unsigned int)sec_offset, buffer_size);
            // Маркируем секцию как недопустимую, устанавливая нулевой размер
            module->section_table[i].section_offset = 0;
            module->section_table[i].section_size = 0;
        }
        else if (sec_offset + sec_size > buffer_size) {
            unsigned int new_size = (unsigned int)(buffer_size - sec_offset);
            fprintf(stderr, "WARNING: Fixing section %u (ID %u) size: %u -> %u\n", 
                    (unsigned int)i, (unsigned int)sec_id, 
                    (unsigned int)sec_size, new_size);
            // Ограничиваем размер секции оставшимся размером файла
            module->section_table[i].section_size = new_size;
        }
        
        fprintf(stderr, "DEBUG: Section %u: ID=%u, Reserved=(0x%02X, 0x%04X), Offset=0x%X (%u), Size=0x%X (%u)\n", 
                (unsigned int)i, (unsigned int)sec_id, 
                (unsigned int)reserve_byte, (unsigned int)reserve_ushort,
                (unsigned int)sec_offset, (unsigned int)sec_offset, 
                (unsigned int)module->section_table[i].section_size, 
                (unsigned int)module->section_table[i].section_size);
        
        // Дополнительная проверка валидности смещений и размеров секций 
        if (module->section_table[i].section_size > 0 && // Проверяем только секции с ненулевым размером
            (module->section_table[i].section_offset > buffer_size || 
             module->section_table[i].section_offset + module->section_table[i].section_size > buffer_size)) {
            fprintf(stderr, "Section %u (ID %u) entry %u has invalid offset/size. Offset: %u, Size: %u, Buffer size: %zu\n",
                    (unsigned int)i,
                    (unsigned int)module->section_table[i].section_id,
                    (unsigned int)i,
                    (unsigned int)module->section_table[i].section_offset,
                    (unsigned int)module->section_table[i].section_size,
                    buffer_size);
            free(module->section_table);
            module->section_table = NULL;
            return ESPB_ERR_INVALID_SECTION_TABLE;
        }
    }
    
    fprintf(stderr, "DEBUG: Successfully read all %u section entries. Current position: %td bytes\n", 
            module->header.num_sections, (ptrdiff_t)(ptr - buffer));
    
    // Посчитаем, сколько секций имеют ненулевой размер (валидные)
    uint16_t valid_sections = 0;
    for (uint16_t i = 0; i < module->header.num_sections; ++i) {
        if (module->section_table[i].section_size > 0) {
            valid_sections++;
        }
    }
    fprintf(stderr, "DEBUG: Total valid sections: %u out of %u\n", 
            (unsigned int)valid_sections, (unsigned int)module->header.num_sections);
    
    return ESPB_OK;
}

EspbResult espb_parse_types_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 1 /* ID секции Types */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Types section (ID=1) not found. Assuming 0 types.");
        // printf("Info: Types section (ID=1) not found. Assuming 0 types.\n");
        module->num_signatures = 0;
        module->signatures = NULL;
        return ESPB_OK; // Секция типов не обязательна, если нет функций
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_signatures)) {
        // ESP_LOGE(TAG, "Failed to read number of signatures");
        fprintf(stderr, "Failed to read number of signatures\n");
        return ESPB_ERR_INVALID_TYPES_SECTION;
    }

    if (module->num_signatures == 0) {
        module->signatures = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Types section after 0 signatures count");
            fprintf(stderr, "Warning: Extra data in Types section after 0 signatures count\n");
        }
        return ESPB_OK;
    }
    
    if (module->num_signatures > 1024 * 1024) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many signatures declared: %u", module->num_signatures);
        fprintf(stderr, "Too many signatures declared: %" PRIu32 "\n", module->num_signatures);
        return ESPB_ERR_MEMORY_ALLOC; // Или другой код
    }

    module->signatures = (EspbFuncSignature *)calloc(module->num_signatures, sizeof(EspbFuncSignature));
    if (!module->signatures) {
        // ESP_LOGE(TAG, "Failed to allocate memory for signatures");
        fprintf(stderr, "Failed to allocate memory for signatures\n");
        module->num_signatures = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_signatures; ++i) {
        EspbFuncSignature *sig = &module->signatures[i];
        uint8_t num_params;
        // Читаем количество параметров отдельно
        if (!read_u8(&ptr, end_ptr, &num_params)) {
            fprintf(stderr, "Failed to read parameter count for signature %" PRIu32 "\n", i);
            goto error_cleanup_sig;
        }
        sig->num_params = num_params;

        if (sig->num_params > 0) {
            sig->param_types = (EspbValueType *)malloc(sig->num_params * sizeof(EspbValueType));
            if (!sig->param_types) {
                // ESP_LOGE(TAG, "Failed to allocate param_types for signature %u", i);
                fprintf(stderr, "Failed to allocate param_types for signature %" PRIu32 "\n", i);
                goto error_cleanup_sig;
            }
            for (uint8_t j = 0; j < sig->num_params; ++j) {
                uint8_t type_val;
                if (!read_u8(&ptr, end_ptr, &type_val)) {
                    // ESP_LOGE(TAG, "Failed to read param type %u for signature %u", j, i);
                    fprintf(stderr, "Failed to read param type %" PRIu8 " for signature %" PRIu32 "\n", j, i);
                    goto error_cleanup_sig;
                }
                // Проверка валидности типа (пример)
                if (type_val == ESPB_TYPE_VOID || type_val > ESPB_TYPE_V128) {
                    // ESP_LOGE(TAG, "Invalid param type %u for signature %u", type_val, i);
                    fprintf(stderr, "Invalid param type %" PRIu8 " for signature %" PRIu32 "\n", type_val, i);
                    goto error_cleanup_sig;
                }
                sig->param_types[j] = (EspbValueType)type_val;
            }
        }

        // Читаем количество возвращаемых значений отдельно
        {
            uint8_t num_returns;
            if (!read_u8(&ptr, end_ptr, &num_returns)) {
                fprintf(stderr, "Failed to read return count for signature %" PRIu32 "\n", i);
                goto error_cleanup_sig;
            }
            sig->num_returns = num_returns;
        }

        if (sig->num_returns > 0) {
            sig->return_types = (EspbValueType *)malloc(sig->num_returns * sizeof(EspbValueType));
            if (!sig->return_types) {
                // ESP_LOGE(TAG, "Failed to allocate return_types for signature %u", i);
                fprintf(stderr, "Failed to allocate return_types for signature %" PRIu32 "\n", i);
                goto error_cleanup_sig;
            }
            for (uint8_t j = 0; j < sig->num_returns; ++j) {
                uint8_t type_val;
                if (!read_u8(&ptr, end_ptr, &type_val)) {
                    // ESP_LOGE(TAG, "Failed to read return type %u for signature %u", j, i);
                    fprintf(stderr, "Failed to read return type %" PRIu8 " for signature %" PRIu32 "\n", j, i);
                    goto error_cleanup_sig;
                }
                if (type_val == ESPB_TYPE_VOID || type_val > ESPB_TYPE_V128) { // Возвращаемый тип не может быть VOID
                    // ESP_LOGE(TAG, "Invalid return type %u for signature %u", type_val, i);
                    fprintf(stderr, "Invalid return type %" PRIu8 " for signature %" PRIu32 "\n", type_val, i);
                    goto error_cleanup_sig;
                }
                sig->return_types[j] = (EspbValueType)type_val;
            }
        }
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Types section");
        fprintf(stderr, "Warning: Extra data at the end of Types section\n");
    }
    return ESPB_OK;

error_cleanup_sig:
    // Освобождаем все, что было выделено для сигнатур до ошибки
    // espb_clear_module_sections позаботится об этом при вызове espb_free_module
    // Но если ошибка внутри цикла, то уже выделенные части module->signatures[i] могут остаться.
    // Лучше делать это в espb_free_module или передавать module->num_signatures как количество успешно распарсенных.
    // Здесь для простоты предположим, что espb_free_module очистит все.
    // Чтобы избежать утечек при ошибке внутри цикла, нужно аккуратно освобождать память.
    // Простой вариант: обнулить num_signatures и позволить espb_free_module сделать остальное.
    // Более корректно: пройтись по уже аллоцированным и освободить.
    // Пока оставим так, espb_free_module должен быть достаточно умным.
    // В espb_clear_module_sections добавлена логика очистки signatures[i].param_types/return_types
    return ESPB_ERR_INVALID_TYPES_SECTION;
}

EspbResult espb_parse_functions_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 3 /* ID секции Functions */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Functions section (ID=3) not found. Assuming 0 functions.");
        // printf("Info: Functions section (ID=3) not found. Assuming 0 functions.\n");
        module->num_functions = 0;
        module->function_signature_indices = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_functions)) {
        // ESP_LOGE(TAG, "Failed to read number of functions");
        fprintf(stderr, "Failed to read number of functions\n");
        return ESPB_ERR_INVALID_FUNCTION_SECTION;
    }

    if (module->num_functions == 0) {
        module->function_signature_indices = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Functions section after 0 functions count");
            fprintf(stderr, "Warning: Extra data in Functions section after 0 functions count\n");
        }
        return ESPB_OK;
    }

    if (module->num_functions > 1024 * 1024) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many functions declared: %u", module->num_functions);
        fprintf(stderr, "Too many functions declared: %" PRIu32 "\n", module->num_functions);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->function_signature_indices = (uint16_t *)malloc(module->num_functions * sizeof(uint16_t));
    if (!module->function_signature_indices) {
        // ESP_LOGE(TAG, "Failed to allocate memory for function_signature_indices");
        fprintf(stderr, "Failed to allocate memory for function_signature_indices\n");
        module->num_functions = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_functions; ++i) {
        uint16_t sig_idx;
        if (!read_u16(&ptr, end_ptr, &sig_idx)) {
            // ESP_LOGE(TAG, "Failed to read signature index for function %u", i);
            fprintf(stderr, "Failed to read signature index for function %" PRIu32 "\n", i);
            free(module->function_signature_indices);
            module->function_signature_indices = NULL;
            module->num_functions = 0;
            return ESPB_ERR_INVALID_FUNCTION_SECTION;
        }
        if (sig_idx >= module->num_signatures) {
            // ESP_LOGE(TAG, "Invalid signature index %u for function %u (num_signatures: %u)", sig_idx, i, module->num_signatures);
            fprintf(stderr, "Invalid signature index %u for function %u (num_signatures: %u)\n",
                    (unsigned int)sig_idx,
                    (unsigned int)i,
                    (unsigned int)module->num_signatures);
            free(module->function_signature_indices);
            module->function_signature_indices = NULL;
            module->num_functions = 0;
            return ESPB_ERR_SIGNATURE_OUT_OF_RANGE;
        }
        module->function_signature_indices[i] = sig_idx;
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Functions section");
        fprintf(stderr, "Warning: Extra data at the end of Functions section\n");
    }
    return ESPB_OK;
}

EspbResult espb_parse_code_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Code обязательна, если есть функции (num_functions > 0)
    // Однако, если num_functions == 0 (например, модуль только с импортами/экспортами), то секция Code может отсутствовать.
    bool found = espb_find_section(module, 6 /* ID секции Code */, &section_data, &section_size);

    if (module->num_functions > 0 && !found) {
        // ESP_LOGE(TAG, "Code section (ID=6) not found, but %u functions defined", module->num_functions);
        fprintf(stderr, "Code section (ID=6) not found, but %" PRIu32 " functions defined\n", module->num_functions);
        return ESPB_ERR_INVALID_CODE_SECTION;
    }
    if (module->num_functions == 0 && !found) {
        // ESP_LOGI(TAG, "Code section not found, and no functions defined. OK.");
        // printf("Info: Code section not found, and no functions defined. OK.\n");
        module->function_bodies = NULL;
        return ESPB_OK;
    }
    if (module->num_functions == 0 && found) {
        // ESP_LOGW(TAG, "Code section found, but no functions defined. Section will be ignored.");
        // printf("Warning: Code section found, but no functions defined. Section will be ignored.\n");
        module->function_bodies = NULL;
        return ESPB_OK; // Игнорируем секцию, если нет функций
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;
    uint32_t num_bodies;

    if (!read_u32(&ptr, end_ptr, &num_bodies)) {
        // ESP_LOGE(TAG, "Failed to read number of function bodies");
        fprintf(stderr, "Failed to read number of function bodies\n");
        return ESPB_ERR_INVALID_CODE_SECTION;
    }

    if (num_bodies != module->num_functions) {
        // ESP_LOGE(TAG, "Number of function bodies (%u) does not match number of functions (%u)", num_bodies, module->num_functions);
        fprintf(stderr, "Number of function bodies (%" PRIu32 ") does not match number of functions (%" PRIu32 ")\n", num_bodies, module->num_functions);
        return ESPB_ERR_INVALID_CODE_SECTION;
    }

    if (module->num_functions == 0) { // Уже проверено выше, но для полноты
        module->function_bodies = NULL;
        if (ptr != end_ptr) {
             // ESP_LOGW(TAG, "Extra data in Code section after 0 function bodies count");
            fprintf(stderr, "Warning: Extra data in Code section after 0 function bodies count\n");
        }
        return ESPB_OK;
    }

    module->function_bodies = (EspbFunctionBody *)calloc(module->num_functions, sizeof(EspbFunctionBody));
    if (!module->function_bodies) {
        // ESP_LOGE(TAG, "Failed to allocate memory for function_bodies");
        fprintf(stderr, "Failed to allocate memory for function_bodies\n");
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_functions; ++i) {
        EspbFunctionBody *body = &module->function_bodies[i];
        uint32_t body_size; // Общий размер тела функции, включая locals_count и code_size
        uint16_t num_virtual_regs; // Ранее было locals_count
        uint32_t code_size_field; // Размер только байткода

        // Читаем общий размер тела функции (включая num_virtual_regs и сам код)
        if (!read_u32(&ptr, end_ptr, &body_size)) {
            // ESP_LOGE(TAG, "Failed to read body_size for function body %u", i);
            fprintf(stderr, "Failed to read body_size for function body %" PRIu32 "\n", i);
            goto error_cleanup_code;
        }

        const uint8_t *body_start_ptr = ptr; // Запоминаем начало данных тела функции

        if (body_size < sizeof(uint16_t)) { // Минимальный размер для num_virtual_regs
            // ESP_LOGE(TAG, "Body size %u too small for function body %u", body_size, i);
            fprintf(stderr, "Body size %" PRIu32 " too small for function body %" PRIu32 "\n", body_size, i);
            goto error_cleanup_code;
        }

        if (!read_u16(&ptr, end_ptr, &num_virtual_regs)) {
            // ESP_LOGE(TAG, "Failed to read num_virtual_regs for function body %u", i);
            fprintf(stderr, "Failed to read num_virtual_regs for function body %" PRIu32 "\n", i);
            goto error_cleanup_code;
        }
        body->num_virtual_regs = num_virtual_regs;
        
        // Размер кода это общий размер тела минус размер поля num_virtual_regs
        if (body_size < sizeof(uint16_t)) { // Еще раз на всякий случай, если body_size был очень мал
            fprintf(stderr, "Body size %" PRIu32 " invalid after reading num_virtual_regs for func %" PRIu32 "\n", body_size, i);
            goto error_cleanup_code;
        }
        code_size_field = body_size - sizeof(uint16_t);
        body->code_size = code_size_field;

        if (ptr + body->code_size > end_ptr || ptr + body->code_size < ptr) { // Проверка на переполнение и выход за границы
            // ESP_LOGE(TAG, "Not enough data for code of function body %u (expected %u, available %zu)", i, body->code_size, (size_t)(end_ptr - ptr));
            fprintf(stderr, "Not enough data for code of function body %" PRIu32 " (expected %" PRIu32 ", available %zu)\n", i, body->code_size, (size_t)(end_ptr - ptr));
            goto error_cleanup_code;
        }
        body->code = ptr;
        ptr += body->code_size;
        
        // Проверка, что мы не вышли за пределы body_size, прочитав все компоненты тела
        if ((size_t)(ptr - body_start_ptr) != body_size) {
            // ESP_LOGE(TAG, "Mismatch in parsed body size for func %u: expected %u, got %zu", i, body_size, (size_t)(ptr - body_start_ptr));
            fprintf(stderr, "Mismatch in parsed body size for func %" PRIu32 ": expected %" PRIu32 ", got %zu\n", i, body_size, (size_t)(ptr - body_start_ptr));
            goto error_cleanup_code;
        }
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Code section (%zu bytes)", (size_t)(end_ptr-ptr));
        fprintf(stderr, "Warning: Extra data at the end of Code section (%zu bytes)\n", (size_t)(end_ptr-ptr));
    }
    return ESPB_OK;

error_cleanup_code:
    if (module->function_bodies) {
        free(module->function_bodies);
        module->function_bodies = NULL;
    }
    // num_functions остается, т.к. проблема в Code, а не в Functions
    return ESPB_ERR_INVALID_CODE_SECTION;
}

EspbResult espb_parse_memory_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 14 /* ID секции Memory */, &section_data, &section_size)) {
        module->num_memories = 0;
        module->memories = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_memories)) {
        fprintf(stderr, "Failed to read number of memories\n");
        return ESPB_ERR_INVALID_MEMORY_SECTION;
    }

    if (module->num_memories > 0) {
        module->memories = (EspbMemoryDesc *)calloc(module->num_memories, sizeof(EspbMemoryDesc));
        if (!module->memories) {
            fprintf(stderr, "Failed to allocate memory for memories\n");
            module->num_memories = 0;
            return ESPB_ERR_MEMORY_ALLOC;
        }

        for (uint32_t i = 0; i < module->num_memories; ++i) {
            EspbMemoryLimits *limits = &module->memories[i].limits;
            uint8_t flags;
            if (!read_u8(&ptr, end_ptr, &flags) || !read_u32(&ptr, end_ptr, &limits->initial_size)) {
                goto error_cleanup_mem;
            }
            limits->flags = flags;
            if (limits->flags & 0x01) { // Has max_size
                if (!read_u32(&ptr, end_ptr, &limits->max_size)) goto error_cleanup_mem;
                if (limits->max_size < limits->initial_size) goto error_cleanup_mem;
            } else {
                limits->max_size = 0;
            }
        }
    }

    
    if (ptr != end_ptr) {
        fprintf(stderr, "Warning: Extra data at the end of Memory section\n");
    }

    return ESPB_OK;

error_cleanup_mem:
    if (module->memories) free(module->memories);
    module->num_memories = 0;
    if (module->cbmeta.signatures) free(module->cbmeta.signatures);
    if (module->cbmeta.imports) free(module->cbmeta.imports);
    memset(&module->cbmeta, 0, sizeof(module->cbmeta));
    return ESPB_ERR_INVALID_MEMORY_SECTION;
}
EspbResult espb_parse_cbmeta_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Callback Metadata (ID=10)
    if (!espb_find_section(module, 10, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Callback Metadata section (ID=10) not found. Assuming no callback metadata.");
        fprintf(stderr, "Info: Callback Metadata section (ID=10) not found. Assuming no callback metadata.\n");
        module->cbmeta.num_signatures = 0;
        module->cbmeta.num_imports_with_cb = 0;
        module->cbmeta.signatures = NULL;
        module->cbmeta.imports = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    // Читаем num_signatures (всегда 0 в текущей версии)
    uint8_t num_sigs = 0;
    if (!read_u8(&ptr, end_ptr, &num_sigs)) {
        fprintf(stderr, "Failed to read num_signatures from cbmeta section\n");
        return ESPB_ERR_INVALID_CBMETA_SECTION;
    }
    module->cbmeta.num_signatures = num_sigs;

    if (num_sigs > 0) {
        // Поддержка для будущих версий, где могут быть сигнатуры
        module->cbmeta.signatures = (EspbCbmetaSignature*)calloc(num_sigs, sizeof(EspbCbmetaSignature));
        if (!module->cbmeta.signatures) {
            fprintf(stderr, "Failed to allocate memory for cbmeta signatures\n");
            return ESPB_ERR_MEMORY_ALLOC;
        }
        // Парсинг сигнатур (пока не реализовано, так как num_sigs=0)
        for (uint8_t si = 0; si < num_sigs; ++si) {
            // TODO: Реализовать парсинг сигнатур при необходимости
        }
    } else {
        module->cbmeta.signatures = NULL;
    }

    // Читаем num_imports_with_cb
    uint16_t num_imports_with_cb = 0;
    if (!read_u16(&ptr, end_ptr, &num_imports_with_cb)) {
        fprintf(stderr, "Failed to read num_imports_with_cb from cbmeta section\n");
        goto error_cleanup_cbmeta;
    }
    module->cbmeta.num_imports_with_cb = num_imports_with_cb;

    if (num_imports_with_cb > 0) {
        module->cbmeta.imports = (EspbCbmetaImportEntry*)calloc(num_imports_with_cb, sizeof(EspbCbmetaImportEntry));
        if (!module->cbmeta.imports) {
            fprintf(stderr, "Failed to allocate memory for cbmeta imports\n");
            goto error_cleanup_cbmeta;
        }

        for (uint16_t mi = 0; mi < num_imports_with_cb; ++mi) {
            EspbCbmetaImportEntry *entry = &module->cbmeta.imports[mi];

            // Читаем import_index
            if (!read_u16(&ptr, end_ptr, &entry->import_index)) {
                fprintf(stderr, "Failed to read import_index for cbmeta entry %" PRIu16 "\n", mi);
                goto error_cleanup_cbmeta;
            }

            // Читаем num_callbacks
            if (!read_u8(&ptr, end_ptr, &entry->num_callbacks)) {
                fprintf(stderr, "Failed to read num_callbacks for cbmeta entry %" PRIu16 "\n", mi);
                goto error_cleanup_cbmeta;
            }

            // Выделяем память для entries (num_callbacks * 3 байта)
            size_t entries_size = (size_t)entry->num_callbacks * 3;
            if (ptr + entries_size > end_ptr) {
                fprintf(stderr, "Not enough data for callback entries in cbmeta entry %" PRIu16 "\n", mi);
                goto error_cleanup_cbmeta;
            }
            entry->entries = ptr;
            ptr += entries_size;
        }
    } else {
        module->cbmeta.imports = NULL;
    }

    if (ptr != end_ptr) {
        fprintf(stderr, "Warning: Extra data at the end of Callback Metadata section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }

    return ESPB_OK;

error_cleanup_cbmeta:
    if (module->cbmeta.signatures) free(module->cbmeta.signatures);
    if (module->cbmeta.imports) free(module->cbmeta.imports);
    memset(&module->cbmeta, 0, sizeof(module->cbmeta));
    return ESPB_ERR_INVALID_CBMETA_SECTION;
}

// Вспомогательная функция для пропуска выражения смещения (offset expression)
// Возвращает длину прочитанного выражения или 0 при ошибке/конце данных.
// Используется в Data и Element секциях.
// Формат: opcode (1 байт) + [данные для opcode]
// Простое выражение: op_const_i32 (0x01) + value (i32_le) = 1 + 4 = 5 байт
//                op_get_global (0x02) + global_idx (u32_le) = 1 + 4 = 5 байт
// Пока поддерживаем только их, как наиболее частые.
static size_t skip_offset_expression(const uint8_t **ptr_to_expr_ptr, const uint8_t *end_ptr) {
    const uint8_t *p = *ptr_to_expr_ptr;
    size_t expr_len = 0;

    if (p + 1 > end_ptr) return 0; // Нет места даже для опкода
    uint8_t opcode = *p++;
    expr_len++;

    switch (opcode) {
        case 0x18: { // LDC.I32.IMM
            // skip rd(u8) + imm32 + END
            if (p + 1 + 4 + 1 > end_ptr) return 0;
            p++; expr_len++; // skip reg
            p += 4; expr_len += 4; // skip imm32
            if (*p != 0x0F) return 0; // expect END opcode
            p++; expr_len++; // skip END
            break;
        }
        case 0x0F: { // END opcode of expression
            // nothing to skip beyond END
            break;
        }
        case 0x01: { // op_const_i32
            if (p + 4 > end_ptr) return 0;
            p += 4; expr_len += 4;
            // Опционально пропускаем END (0x0F)
            if (p < end_ptr && *p == 0x0F) { p++; expr_len++; }
            break;
        }
        case 0x02: { // op_get_global
            if (p + 4 > end_ptr) return 0;
            p += 4; expr_len += 4;
            // Опционально пропускаем END (0x0F)
            if (p < end_ptr && *p == 0x0F) { p++; expr_len++; }
            break;
        }
        default: {
            fprintf(stderr, "Unsupported opcode 0x%02x in offset expression\n", opcode);
            return 0;
        }
    }

    *ptr_to_expr_ptr = p; // Обновляем указатель во внешней переменной
    return expr_len;
}

EspbResult espb_parse_globals_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 4 /* ID секции Globals */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Globals section (ID=4) not found. Assuming 0 globals.");
        // printf("Info: Globals section (ID=4) not found. Assuming 0 globals.\n");
        module->num_globals = 0;
        module->globals = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_globals)) {
        // ESP_LOGE(TAG, "Failed to read number of globals");
        fprintf(stderr, "Failed to read number of globals\n");
        return ESPB_ERR_INVALID_GLOBAL_SECTION;
    }

    if (module->num_globals == 0) {
        module->globals = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Globals section after 0 globals count");
            fprintf(stderr, "Warning: Extra data in Globals section after 0 globals count\n");
        }
        return ESPB_OK;
    }
    
    if (module->num_globals > 65536) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many globals declared: %u", module->num_globals);
        fprintf(stderr, "Too many globals declared: %" PRIu32 "\n", module->num_globals);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->globals = (EspbGlobalDesc *)calloc(module->num_globals, sizeof(EspbGlobalDesc));
    if (!module->globals) {
        // ESP_LOGE(TAG, "Failed to allocate memory for globals");
        fprintf(stderr, "Failed to allocate memory for globals\n");
        module->num_globals = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_globals; ++i) {
        EspbGlobalDesc *gd = &module->globals[i];
        uint8_t type_val, mut_val, shared_val, init_kind_val;

        if (!read_u8(&ptr, end_ptr, &type_val) ||
            !read_u8(&ptr, end_ptr, &mut_val) ||
            !read_u8(&ptr, end_ptr, &shared_val) ||
            !read_u8(&ptr, end_ptr, &init_kind_val)) {
            // ESP_LOGE(TAG, "Failed to read global descriptor for global %u", i);
            fprintf(stderr, "Failed to read global descriptor for global %" PRIu32 "\n", i);
            goto error_cleanup_globals;
        }

        gd->type = (EspbValueType)type_val;
        gd->mutability = mut_val;
        gd->shared_flag = shared_val;
        gd->init_kind = (EspbInitKind)init_kind_val;

        // Валидация
        if (gd->type == ESPB_TYPE_VOID || gd->type > ESPB_TYPE_V128) goto error_cleanup_globals_msg;
        if (gd->mutability > 1) goto error_cleanup_globals_msg;
        if (gd->shared_flag > 1 || (gd->shared_flag == 1 && gd->mutability == 0)) goto error_cleanup_globals_msg;
        if (gd->init_kind > ESPB_INIT_KIND_DATA_OFFSET) goto error_cleanup_globals_msg;

        if (gd->init_kind == ESPB_INIT_KIND_CONST) {
            // Если это константа, читаем и сохраняем её значение
            size_t const_size = 0;
            switch (gd->type) {
                case ESPB_TYPE_I32: case ESPB_TYPE_U32: case ESPB_TYPE_F32: const_size = 4; break;
                case ESPB_TYPE_I64: case ESPB_TYPE_U64: case ESPB_TYPE_F64: const_size = 8; break;
                default:
                    fprintf(stderr, "Global %" PRIu32 ": const initializer for type %d not handled\n", i, gd->type);
                    goto error_cleanup_globals;
            }
            if (ptr + const_size > end_ptr) {
                fprintf(stderr, "Global %" PRIu32 ": not enough data for const initializer (type %d, size %zu)\n", i, gd->type, const_size);
                goto error_cleanup_globals;
            }
            // Сохраняем значение константы
            if (const_size == 4) {
                uint32_t temp32;
                if (!read_u32(&ptr, end_ptr, &temp32)) goto error_cleanup_globals;
                ptr -= 4; // Возвращаем указатель к началу
                gd->initializer.const_val_placeholder = (int64_t)(int32_t)temp32;
            } else if (const_size == 8) {
                uint64_t temp64;
                if (!read_u64(&ptr, end_ptr, &temp64)) goto error_cleanup_globals;
                ptr -= 8;
                gd->initializer.const_val_placeholder = (int64_t)temp64;
            }
            ptr += const_size;
        } else if (gd->init_kind == ESPB_INIT_KIND_DATA_OFFSET) {
            // Читаем смещение данных для глобала (u32)
            uint32_t data_offset_val; // Переименовал, чтобы не конфликтовать с полем структуры
            if (!read_u32(&ptr, end_ptr, &data_offset_val)) {
                fprintf(stderr, "Global %" PRIu32 ": not enough data for data offset\n", i);
                goto error_cleanup_globals;
            }
            gd->initializer.data_section_offset = data_offset_val; // <--- СОХРАНЯЕМ СМЕЩЕНИЕ
        }
        // Для ESPB_INIT_KIND_ZERO ничего дополнительно не читается
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Globals section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Globals section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    return ESPB_OK;

error_cleanup_globals_msg:
    // ESP_LOGE(TAG, "Invalid field value in global descriptor %u", (unsigned int)(i)); // i может быть не инициализировано, если ошибка на первом элементе
    fprintf(stderr, "Invalid field value in global descriptor\n");
error_cleanup_globals:
    if (module->globals) {
        free(module->globals);
        module->globals = NULL;
    }
    module->num_globals = 0;
    return ESPB_ERR_INVALID_GLOBAL_SECTION;
}

EspbResult espb_parse_data_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 8 /* ID секции Data ИЗМЕНЕНО С 7 НА 8 */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Data section (ID=8) not found. Assuming 0 data segments.");
        // printf("Info: Data section (ID=8) not found. Assuming 0 data segments.\n");
        module->num_data_segments = 0;
        module->data_segments = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_data_segments)) {
        // ESP_LOGE(TAG, "Failed to read number of data segments");
        fprintf(stderr, "Failed to read number of data segments\n");
        return ESPB_ERR_INVALID_DATA_SECTION;
    }

    if (module->num_data_segments == 0) {
        module->data_segments = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Data section after 0 segments count");
            fprintf(stderr, "Warning: Extra data in Data section after 0 segments count\n");
        }
        return ESPB_OK;
    }
    
    if (module->num_data_segments > 1024*1024) { // Разумный лимит
         // ESP_LOGE(TAG, "Too many data segments declared: %u", module->num_data_segments);
        fprintf(stderr, "Too many data segments declared: %" PRIu32 "\n", module->num_data_segments);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->data_segments = (EspbDataSegment *)calloc(module->num_data_segments, sizeof(EspbDataSegment));
    if (!module->data_segments) {
        // ESP_LOGE(TAG, "Failed to allocate memory for data_segments");
        fprintf(stderr, "Failed to allocate memory for data_segments\n");
        module->num_data_segments = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_data_segments; ++i) {
        EspbDataSegment *seg = &module->data_segments[i];
        uint8_t seg_type;

        if (!read_u8(&ptr, end_ptr, &seg_type)) {
            // ESP_LOGE(TAG, "Failed to read segment_type for data segment %u", i);
            fprintf(stderr, "Failed to read segment_type for data segment %" PRIu32 "\n", i);
            goto error_cleanup_data;
        }
        seg->segment_type = seg_type;

        if (seg->segment_type == 0) { // Active segment
            if (!read_u32(&ptr, end_ptr, &seg->memory_index)) {
                // ESP_LOGE(TAG, "Failed to read memory_index for active data segment %u", i);
                fprintf(stderr, "Failed to read memory_index for active data segment %" PRIu32 "\n", i);
                goto error_cleanup_data;
            }
            // Проверка memory_index. Должна быть хотя бы одна память, если есть активные сегменты.
            // Это проверяется позже при инстанцировании, но можно и здесь.
            if (module->num_memories == 0 && seg->memory_index == 0) {
                 // ESP_LOGE(TAG, "Data segment %u targets memory 0, but no memories defined", i);
                fprintf(stderr, "Data segment %" PRIu32 " targets memory 0, but no memories defined\n", i);
                // goto error_cleanup_data; // Или это ошибка инстанцирования?
            } else if (seg->memory_index >= module->num_memories) {
                // ESP_LOGE(TAG, "Data segment %u uses invalid memory_index %u (num_memories: %u)", i, seg->memory_index, module->num_memories);
                fprintf(stderr, "Data segment %" PRIu32 " uses invalid memory_index %" PRIu32 " (num_memories: %" PRIu32 ")\n", i, seg->memory_index, module->num_memories);
                goto error_cleanup_data;
            }

            seg->offset_expr = ptr; // Запоминаем указатель на начало выражения
            size_t expr_len = skip_offset_expression(&ptr, end_ptr);
            if (expr_len == 0) {
                // ESP_LOGE(TAG, "Failed to parse/skip offset_expression for data segment %u", i);
                fprintf(stderr, "Failed to parse/skip offset_expression for data segment %" PRIu32 "\n", i);
                goto error_cleanup_data;
            }
            seg->offset_expr_len = expr_len;
            // ptr уже обновлен функцией skip_offset_expression

        } else if (seg->segment_type == 1) { // Passive segment
            seg->memory_index = 0; // Не используется
            seg->offset_expr = NULL;
            seg->offset_expr_len = 0;
        } else {
            // ESP_LOGE(TAG, "Invalid segment_type %u for data segment %u", seg->segment_type, i);
            fprintf(stderr, "Invalid segment_type %" PRIu8 " for data segment %" PRIu32 "\n", seg->segment_type, i);
            goto error_cleanup_data;
        }

        if (!read_u32(&ptr, end_ptr, &seg->data_size)) {
            // ESP_LOGE(TAG, "Failed to read data_size for data segment %u", i);
            fprintf(stderr, "Failed to read data_size for data segment %" PRIu32 "\n", i);
            goto error_cleanup_data;
        }

        if (ptr + seg->data_size > end_ptr || ptr + seg->data_size < ptr) { // Проверка на переполнение и выход за границы
            // ESP_LOGE(TAG, "Not enough data for data segment %u (expected %u, available %zu)", i, seg->data_size, (size_t)(end_ptr - ptr));
            fprintf(stderr, "Not enough data for data segment %" PRIu32 " (expected %" PRIu32 ", available %zu)\n", i, seg->data_size, (size_t)(end_ptr - ptr));
            goto error_cleanup_data;
        }
        seg->data = ptr;
        ptr += seg->data_size;
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Data section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Data section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    return ESPB_OK;

error_cleanup_data:
    if (module->data_segments) {
        free(module->data_segments);
        module->data_segments = NULL;
    }
    module->num_data_segments = 0;
    return ESPB_ERR_INVALID_DATA_SECTION;
}

// Вспомогательная функция для чтения строки (длина u16 + данные)
// Выделяет память для строки, которую нужно будет освободить.
static char* read_string(const uint8_t **ptr, const uint8_t *end) {
    uint16_t len;
    if (!read_u16(ptr, end, &len)) {
        return NULL;
    }
    if (*ptr + len > end) {
        return NULL;
    }
    // Wasm строки не обязательно null-terminated в файле, но strdup создаст null-terminated копию.
    // Если строка может содержать нулевые байты внутри, strndup или memcpy + ручное добавление \0 безопаснее.
    // Для имен модулей/сущностей обычно это не проблема.
    char *str = (char *)malloc(len + 1); 
    if (!str) {
        return NULL;
    }
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    return str;
}

EspbResult espb_parse_imports_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 2 /* ID секции Imports */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Imports section (ID=2) not found. Assuming 0 imports.");
        // printf("Info: Imports section (ID=2) not found. Assuming 0 imports.\n");
        module->num_imports = 0;
        module->imports = NULL;
        module->num_imported_funcs = 0;  // ОПТИМИЗАЦИЯ: Кэшированное значение
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_imports)) {
        // ESP_LOGE(TAG, "Failed to read number of imports");
        fprintf(stderr, "Failed to read number of imports\n");
        return ESPB_ERR_INVALID_IMPORT_SECTION;
    }

    if (module->num_imports == 0) {
        module->imports = NULL;
        module->num_imported_funcs = 0;  // ОПТИМИЗАЦИЯ: Кэшированное значение
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Imports section after 0 imports count");
            fprintf(stderr, "Warning: Extra data in Imports section after 0 imports count\n");
        }
        return ESPB_OK;
    }

    if (module->num_imports > 65536) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many imports declared: %u", module->num_imports);
        fprintf(stderr, "Too many imports declared: %" PRIu32 "\n", module->num_imports);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->imports = (EspbImportDesc *)calloc(module->num_imports, sizeof(EspbImportDesc));
    if (!module->imports) {
        // ESP_LOGE(TAG, "Failed to allocate memory for imports");
        fprintf(stderr, "Failed to allocate memory for imports\n");
        module->num_imports = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_imports; ++i) {
        EspbImportDesc *imp = &module->imports[i];
        imp->module_name = read_string(&ptr, end_ptr);
        if (!imp->module_name) {
            // ESP_LOGE(TAG, "Failed to read module_name for import %u", i);
            fprintf(stderr, "Failed to read module_name for import %" PRIu32 "\n", i);
            goto error_cleanup_imports;
        }

        imp->entity_name = read_string(&ptr, end_ptr);
        if (!imp->entity_name) {
            // ESP_LOGE(TAG, "Failed to read entity_name for import %u ('%s')", i, imp->module_name);
            fprintf(stderr, "Failed to read entity_name for import %" PRIu32 " ('%s')\n", i, imp->module_name);
            goto error_cleanup_imports;
        }

        uint8_t kind_val;
        if (!read_u8(&ptr, end_ptr, &kind_val)) {
            // ESP_LOGE(TAG, "Failed to read kind for import %u (%s::%s)", i, imp->module_name, imp->entity_name);
            fprintf(stderr, "Failed to read kind for import %" PRIu32 " (%s::%s)\n", i, imp->module_name, imp->entity_name);
            goto error_cleanup_imports;
        }
        imp->kind = (EspbImportKind)kind_val;

        switch (imp->kind) {
            case ESPB_IMPORT_KIND_FUNC:
                if (!read_u16(&ptr, end_ptr, &imp->desc.func.type_idx) ||
                    !read_u8(&ptr, end_ptr, &imp->desc.func.import_flags)) {
                    // ESP_LOGE(TAG, "Failed to read func import data for %s::%s", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Failed to read func import data for %s::%s\n", imp->module_name, imp->entity_name);
                    goto error_cleanup_imports;
                }
                if (imp->desc.func.type_idx >= module->num_signatures) {
                    // ESP_LOGE(TAG, "Invalid type_idx %u for func import %s::%s (num_signatures %u)", 
                    //          imp->desc.func.type_idx, imp->module_name, imp->entity_name, module->num_signatures);
                    fprintf(stderr, "Invalid type_idx %u for func import %s::%s (num_signatures %u)\n",
                            (unsigned int)imp->desc.func.type_idx,
                            imp->module_name,
                            imp->entity_name,
                            (unsigned int)module->num_signatures);
                    goto error_cleanup_imports;
                }
                break;
            case ESPB_IMPORT_KIND_TABLE:
                if (!read_u8(&ptr, end_ptr, &imp->desc.table.element_type) ||
                    !read_u8(&ptr, end_ptr, &imp->desc.table.limits.flags) ||
                    !read_u32(&ptr, end_ptr, &imp->desc.table.limits.initial_size)) {
                    // ESP_LOGE(TAG, "Failed to read table import data for %s::%s (part 1)", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Failed to read table import data for %s::%s (part 1)\n", imp->module_name, imp->entity_name);
                    goto error_cleanup_imports;
                }
                if (imp->desc.table.limits.flags & 0x01) { // has_max
                    if (!read_u32(&ptr, end_ptr, &imp->desc.table.limits.max_size)) {
                        // ESP_LOGE(TAG, "Failed to read table.limits.max_size for %s::%s", imp->module_name, imp->entity_name);
                        fprintf(stderr, "Failed to read table.limits.max_size for %s::%s\n", imp->module_name, imp->entity_name);
                        goto error_cleanup_imports;
                    }
                } else {
                    imp->desc.table.limits.max_size = 0; // Или другое значение по умолчанию
                }
                if (imp->desc.table.element_type != ESPB_REF_TYPE_FUNCREF) { // В v1.7 только FUNCREF
                     // ESP_LOGE(TAG, "Invalid table element_type %u for %s::%s", imp->desc.table.element_type, imp->module_name, imp->entity_name);
                    fprintf(stderr, "Invalid table element_type %" PRIu8 " for %s::%s\n", imp->desc.table.element_type, imp->module_name, imp->entity_name);
                    goto error_cleanup_imports;
                }
                break;
            case ESPB_IMPORT_KIND_MEMORY:
                if (!read_u8(&ptr, end_ptr, &imp->desc.memory.flags) ||
                    !read_u32(&ptr, end_ptr, &imp->desc.memory.initial_size)) {
                    // ESP_LOGE(TAG, "Failed to read memory import data for %s::%s (part 1)", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Failed to read memory import data for %s::%s (part 1)\n", imp->module_name, imp->entity_name);
                    goto error_cleanup_imports;
                }
                if (imp->desc.memory.flags & 0x01) { // has_max
                    if (!read_u32(&ptr, end_ptr, &imp->desc.memory.max_size)) {
                         // ESP_LOGE(TAG, "Failed to read memory.max_size for %s::%s", imp->module_name, imp->entity_name);
                        fprintf(stderr, "Failed to read memory.max_size for %s::%s\n", imp->module_name, imp->entity_name);
                        goto error_cleanup_imports;
                    }
                } else {
                    imp->desc.memory.max_size = 0;
                }
                break;
            case ESPB_IMPORT_KIND_GLOBAL:
                uint8_t type_val, mut_val, shared_val;
                if (!read_u8(&ptr, end_ptr, &type_val) ||
                    !read_u8(&ptr, end_ptr, &mut_val) ||
                    !read_u8(&ptr, end_ptr, &shared_val)) {
                    // ESP_LOGE(TAG, "Failed to read global import data for %s::%s", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Failed to read global import data for %s::%s\n", imp->module_name, imp->entity_name);
                    goto error_cleanup_imports;
                }
                imp->desc.global.type = (EspbValueType)type_val;
                imp->desc.global.mutability = mut_val;
                imp->desc.global.shared_flag = shared_val;
                // Валидация
                if (imp->desc.global.type == ESPB_TYPE_VOID || imp->desc.global.type > ESPB_TYPE_V128) goto error_cleanup_imports_msg;
                if (imp->desc.global.mutability > 1) goto error_cleanup_imports_msg;
                if (imp->desc.global.shared_flag > 1 || (imp->desc.global.shared_flag == 1 && imp->desc.global.mutability == 0)) goto error_cleanup_imports_msg;
                break;
            default:
                // ESP_LOGE(TAG, "Invalid import kind %u for %s::%s", imp->kind, imp->module_name, imp->entity_name);
                fprintf(stderr, "Invalid import kind %" PRIu8 " for %s::%s\n", imp->kind, imp->module_name, imp->entity_name);
                goto error_cleanup_imports;
        }
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Imports section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Imports section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    
    // ОПТИМИЗАЦИЯ: Кэшируем количество импортированных функций для производительности
    module->num_imported_funcs = 0;
    for (uint32_t i = 0; i < module->num_imports; ++i) {
        if (module->imports[i].kind == ESPB_IMPORT_KIND_FUNC) {
            module->num_imported_funcs++;
        }
    }
    
    return ESPB_OK;

error_cleanup_imports_msg:
    // ESP_LOGE(TAG, "Invalid field value in import descriptor for %s::%s", module->imports[i].module_name, module->imports[i].entity_name);
    fprintf(stderr, "Invalid field value in import descriptor\n");
error_cleanup_imports:
    // espb_free_module позаботится об очистке строк и массива
    return ESPB_ERR_INVALID_IMPORT_SECTION;
}

EspbResult espb_parse_exports_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 5 /* ID секции Exports */, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Exports section (ID=5) not found. Assuming 0 exports.");
        // printf("Info: Exports section (ID=5) not found. Assuming 0 exports.\n");
        module->num_exports = 0;
        module->exports = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_exports)) {
        // ESP_LOGE(TAG, "Failed to read number of exports");
        fprintf(stderr, "Failed to read number of exports\n");
        return ESPB_ERR_INVALID_EXPORT_SECTION;
    }

    if (module->num_exports == 0) {
        module->exports = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Exports section after 0 exports count");
            fprintf(stderr, "Warning: Extra data in Exports section after 0 exports count\n");
        }
        return ESPB_OK;
    }

    if (module->num_exports > 65536) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many exports declared: %u", module->num_exports);
        fprintf(stderr, "Too many exports declared: %" PRIu32 "\n", module->num_exports);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->exports = (EspbExportDesc *)calloc(module->num_exports, sizeof(EspbExportDesc));
    if (!module->exports) {
        // ESP_LOGE(TAG, "Failed to allocate memory for exports");
        fprintf(stderr, "Failed to allocate memory for exports\n");
        module->num_exports = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_exports; ++i) {
        EspbExportDesc *exp = &module->exports[i];
        exp->name = read_string(&ptr, end_ptr);
        if (!exp->name) {
            // ESP_LOGE(TAG, "Failed to read name for export %u", i);
            fprintf(stderr, "Failed to read name for export %" PRIu32 "\n", i);
            goto error_cleanup_exports;
        }

        uint8_t kind_val;
        if (!read_u8(&ptr, end_ptr, &kind_val) ||
            !read_u32(&ptr, end_ptr, &exp->index)) {
            // ESP_LOGE(TAG, "Failed to read kind/index for export %u ('%s')", i, exp->name);
            fprintf(stderr, "Failed to read kind/index for export %" PRIu32 " ('%s')\n", i, exp->name);
            goto error_cleanup_exports;
        }
        exp->kind = (EspbExportKind)kind_val;

        // Валидация kind и index
        // TODO: Проверить, что exp->index находится в допустимых пределах для своего kind
        // Например, для ESPB_EXPORT_KIND_FUNC, exp->index < (num_imports_of_kind_func + num_functions)
        // Эту проверку лучше делать после парсинга всех релевантных секций или при инстанцировании.
        if (exp->kind > ESPB_IMPORT_KIND_GLOBAL) { // Используем EspbImportKind как верхнюю границу
            // ESP_LOGE(TAG, "Invalid export kind %u for '%s'", exp->kind, exp->name);
            fprintf(stderr, "Invalid export kind %" PRIu8 " for '%s'\n", exp->kind, exp->name);
            goto error_cleanup_exports;
        }
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Exports section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Exports section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    return ESPB_OK;

error_cleanup_exports:
    // espb_free_module позаботится об очистке
    return ESPB_ERR_INVALID_EXPORT_SECTION;
}

EspbResult espb_parse_relocations_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Relocations (ID ИЗМЕНЕН С 8 НА 9)
    if (!espb_find_section(module, 9, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Relocations section (ID=9) not found. Assuming 0 relocations.");
        // printf("Info: Relocations section (ID=9) not found. Assuming 0 relocations.\n");
        module->num_relocations = 0;
        module->relocations = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    uint8_t current_target_section_id; // ID секции, к которой относятся все релокации в этом блоке
    if (!read_u8(&ptr, end_ptr, &current_target_section_id)) {
        fprintf(stderr, "RELOC_PARSER_DEBUG: Failed to read overall target_section_id. Available: %td\n", (ptrdiff_t)(end_ptr - ptr));
        return ESPB_ERR_INVALID_RELOCATION_SECTION;
    }

    if (!read_u32(&ptr, end_ptr, &module->num_relocations)) {
        fprintf(stderr, "RELOC_PARSER_DEBUG: Failed to read module->num_relocations. Available: %td\n", (ptrdiff_t)(end_ptr - ptr));
        return ESPB_ERR_INVALID_RELOCATION_SECTION;
    }
 
    fprintf(stderr, "RELOC_PARSER_DEBUG: Overall TargetSectionID: %u, NumRelocations: %" PRIu32 ", SectionSize: %" PRIu32 "\n", 
            current_target_section_id, module->num_relocations, section_size);

    if (module->num_relocations == 0) {
        module->relocations = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Relocations section after 0 relocations count");
            fprintf(stderr, "Warning: Extra data in Relocations section after 0 relocations count\n");
        }
        return ESPB_OK;
    }
    
    if (module->num_relocations > 1024 * 1024) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many relocations declared: %u", module->num_relocations);
        fprintf(stderr, "Too many relocations declared: %" PRIu32 "\n", module->num_relocations);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->relocations = (EspbRelocationEntry *)calloc(module->num_relocations, sizeof(EspbRelocationEntry));
    if (!module->relocations) {
        // ESP_LOGE(TAG, "Failed to allocate memory for relocations");
        fprintf(stderr, "Failed to allocate memory for relocations\n");
        module->num_relocations = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_relocations; ++i) {
        EspbRelocationEntry *reloc = &module->relocations[i];
        uint8_t type_val;

        reloc->target_section_id = current_target_section_id; // Используем ID, прочитанный для всей секции

        fprintf(stderr, "RELOC_PARSER_DEBUG: Reloc #%" PRIu32 ", Reading type. Available: %td\n", i, (ptrdiff_t)(end_ptr - ptr));
        if (!read_u8(&ptr, end_ptr, &type_val) ||
            (fprintf(stderr, "RELOC_PARSER_DEBUG: Reloc #%" PRIu32 ", Reading offset. Available: %td\n", i, (ptrdiff_t)(end_ptr - ptr)), 
             !read_u32(&ptr, end_ptr, &reloc->offset)) ||
            (fprintf(stderr, "RELOC_PARSER_DEBUG: Reloc #%" PRIu32 ", Reading symbol_index. Available: %td\n", i, (ptrdiff_t)(end_ptr - ptr)),
             !read_u32(&ptr, end_ptr, &reloc->symbol_index))) {
            fprintf(stderr, "RELOC_PARSER_DEBUG: Failed to read relocation entry base (type, offset, or symbol_index) for reloc %" PRIu32 "\n", i);
            goto error_cleanup_relocs;
        }
        reloc->type = (EspbRelocType)type_val;
        
        fprintf(stderr, "RELOC_PARSER_DEBUG: Reloc #%" PRIu32 ", Type: %u, Offset: %" PRIu32 ", SymbolIndex: %" PRIu32 ". Reading addend. Available: %td\n", 
                i, reloc->type, reloc->offset, reloc->symbol_index, (ptrdiff_t)(end_ptr - ptr));
        if (!read_i32(&ptr, end_ptr, &reloc->addend)) {
            fprintf(stderr, "RELOC_PARSER_DEBUG: Failed to read addend for reloc %" PRIu32 ". Available: %td\n", i, (ptrdiff_t)(end_ptr - ptr));
            goto error_cleanup_relocs;
        }
        reloc->has_addend = true; 
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Relocations section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Relocations section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    return ESPB_OK;

error_cleanup_relocs:
    if (module->relocations) {
        free(module->relocations);
        module->relocations = NULL;
    }
    module->num_relocations = 0;
    return ESPB_ERR_INVALID_RELOCATION_SECTION;
}

EspbResult espb_parse_tables_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Table (ID=11)
    if (!espb_find_section(module, 11, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Tables section (ID=11) not found. Assuming 0 tables.");
        // printf("Info: Tables section (ID=11) not found. Assuming 0 tables.\n");
        module->num_tables = 0;
        module->tables = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_tables)) {
        // ESP_LOGE(TAG, "Failed to read number of tables");
        fprintf(stderr, "Failed to read number of tables\n");
        return ESPB_ERR_INVALID_TABLE_SECTION;
    }

    if (module->num_tables == 0) {
        module->tables = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Tables section after 0 tables count");
            fprintf(stderr, "Warning: Extra data in Tables section after 0 tables count\n");
        }
        return ESPB_OK;
    }

    // В ESPB v1.7 (как и в Wasm MVP) обычно поддерживается не более одной таблицы.
    if (module->num_tables > 1) {
        // ESP_LOGW(TAG, "Declared %u tables, but typically only 1 is used/supported.", module->num_tables);
        fprintf(stderr, "Declared %" PRIu32 " tables, but typically only 1 is used/supported.\n", module->num_tables);
        // Можно вернуть ошибку или просто парсить все, а использовать первую.
    }

    module->tables = (EspbTableDesc *)calloc(module->num_tables, sizeof(EspbTableDesc));
    if (!module->tables) {
        // ESP_LOGE(TAG, "Failed to allocate memory for tables");
        fprintf(stderr, "Failed to allocate memory for tables\n");
        module->num_tables = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_tables; ++i) {
        EspbTableDesc *tbl = &module->tables[i];
        uint8_t elem_type_val;
        if (!read_u8(&ptr, end_ptr, &elem_type_val)) {
            // ESP_LOGE(TAG, "Failed to read element_type for table %u", i);
            fprintf(stderr, "Failed to read element_type for table %" PRIu32 "\n", i);
            goto error_cleanup_tables;
        }
        tbl->element_type = (EspbRefType)elem_type_val;
        if (tbl->element_type != ESPB_REF_TYPE_FUNCREF) { // В ESPB v1.7 только funcref
            // ESP_LOGE(TAG, "Invalid element_type %u for table %u (only FUNCREF supported)", tbl->element_type, i);
            fprintf(stderr, "Invalid element_type %" PRIu8 " for table %" PRIu32 " (only FUNCREF supported)\n", tbl->element_type, i);
            goto error_cleanup_tables;
        }

        if (!read_u8(&ptr, end_ptr, &tbl->limits.flags) ||
            !read_u32(&ptr, end_ptr, &tbl->limits.initial_size)) {
            // ESP_LOGE(TAG, "Failed to read limits for table %u", i);
            fprintf(stderr, "Failed to read limits for table %" PRIu32 "\n", i);
            goto error_cleanup_tables;
        }

        if (tbl->limits.flags & 0x01) { // has_max
            if (!read_u32(&ptr, end_ptr, &tbl->limits.max_size)) {
                // ESP_LOGE(TAG, "Failed to read max_size for table %u", i);
                fprintf(stderr, "Failed to read max_size for table %" PRIu32 "\n", i);
                goto error_cleanup_tables;
            }
            if (tbl->limits.max_size < tbl->limits.initial_size) {
                // ESP_LOGE(TAG, "Table %u: max_size %u < initial_size %u", i, tbl->limits.max_size, tbl->limits.initial_size);
                fprintf(stderr, "Table %" PRIu32 ": max_size %" PRIu32 " < initial_size %" PRIu32 "\n", i, tbl->limits.max_size, tbl->limits.initial_size);
                goto error_cleanup_tables;
            }
        } else {
            tbl->limits.max_size = 0; // Или другое значение по умолчанию
        }
        // Флаг shared (0x02) для таблиц не используется в Wasm MVP / ESPB v1.7
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Tables section (%zu bytes)", (size_t)(end_ptr-ptr));
        fprintf(stderr, "Warning: Extra data at the end of Tables section (%zu bytes)\n", (size_t)(end_ptr-ptr));
    }
    return ESPB_OK;

error_cleanup_tables:
    if (module->tables) {
        free(module->tables);
        module->tables = NULL;
    }
    module->num_tables = 0;
    return ESPB_ERR_INVALID_TABLE_SECTION;
}

EspbResult espb_parse_element_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Element (ID ИЗМЕНЕН С 9 НА 12, чтобы не конфликтовать с Relocations ID=9)
    if (!espb_find_section(module, 12, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Element section (ID=12) not found. Assuming 0 element segments.");
        // printf("Info: Element section (ID=12) not found. Assuming 0 element segments.\n");
        module->num_element_segments = 0;
        module->element_segments = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_element_segments)) {
        // ESP_LOGE(TAG, "Failed to read number of element segments");
        fprintf(stderr, "Failed to read number of element segments\n");
        return ESPB_ERR_INVALID_ELEMENT_SECTION;
    }

    if (module->num_element_segments == 0) {
        module->element_segments = NULL;
        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data in Element section after 0 segments count");
            fprintf(stderr, "Warning: Extra data in Element section after 0 segments count\n");
        }
        return ESPB_OK;
    }
    
    if (module->num_element_segments > 1024*1024) { // Разумный лимит
        // ESP_LOGE(TAG, "Too many element segments declared: %u", module->num_element_segments);
        fprintf(stderr, "Too many element segments declared: %" PRIu32 "\n", module->num_element_segments);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    module->element_segments = (EspbElementSegment *)calloc(module->num_element_segments, sizeof(EspbElementSegment));
    if (!module->element_segments) {
        // ESP_LOGE(TAG, "Failed to allocate memory for element_segments");
        fprintf(stderr, "Failed to allocate memory for element_segments\n");
        module->num_element_segments = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    for (uint32_t i = 0; i < module->num_element_segments; ++i) {
        EspbElementSegment *seg = &module->element_segments[i];
        seg->function_indices = NULL; // Инициализация
        uint8_t elem_type_val;

        if (!read_u32(&ptr, end_ptr, &seg->flags)) { // flags в ESPB - это u32, не u8 как в Wasm
            // ESP_LOGE(TAG, "Failed to read flags for element segment %u", i);
            fprintf(stderr, "Failed to read flags for element segment %" PRIu32 "\n", i);
            goto error_cleanup_elem;
        }

        // Анализ флагов для определения типа сегмента
        // Флаги ESPB: 0=active, tableidx=0; 1=passive; 2=active, tableidx!=0; (3=explicit?)
        // Это немного отличается от WebAssembly, где flags это один байт.
        if (seg->flags == 0) { // Active, tableidx=0
            seg->table_index = 0;
            seg->offset_expr = ptr;
            size_t expr_len = skip_offset_expression(&ptr, end_ptr);
            if (expr_len == 0) {
                // ESP_LOGE(TAG, "Failed to parse offset_expression for active element segment %u (flags=0)", i);
                fprintf(stderr, "Failed to parse offset_expression for active element segment %" PRIu32 " (flags=0)\n", i);
                goto error_cleanup_elem;
            }
            seg->offset_expr_len = expr_len;
        } else if (seg->flags == 1) { // Passive
            seg->table_index = 0; // Не используется
            seg->offset_expr = NULL;
            seg->offset_expr_len = 0;
        } else if (seg->flags == 2) { // Active, tableidx!=0
            if (!read_u32(&ptr, end_ptr, &seg->table_index)) {
                // ESP_LOGE(TAG, "Failed to read table_index for active element segment %u (flags=2)", i);
                fprintf(stderr, "Failed to read table_index for active element segment %" PRIu32 " (flags=2)\n", i);
                goto error_cleanup_elem;
            }
            seg->offset_expr = ptr;
            size_t expr_len = skip_offset_expression(&ptr, end_ptr);
            if (expr_len == 0) {
                // ESP_LOGE(TAG, "Failed to parse offset_expression for active element segment %u (flags=2)", i);
                fprintf(stderr, "Failed to parse offset_expression for active element segment %" PRIu32 " (flags=2)\n", i);
                goto error_cleanup_elem;
            }
            seg->offset_expr_len = expr_len;
        } else {
            // ESP_LOGE(TAG, "Invalid flags %u for element segment %u", seg->flags, i);
            fprintf(stderr, "Invalid flags %" PRIu32 " for element segment %" PRIu32 "\n", seg->flags, i);
            goto error_cleanup_elem;
        }
        
        // Проверка table_index (если активный сегмент)
        if ((seg->flags == 0 || seg->flags == 2)) {
            if (module->num_tables == 0 && seg->table_index == 0) {
                // ESP_LOGE(TAG, "Element segment %u targets table 0, but no tables defined", i);
                fprintf(stderr, "Element segment %" PRIu32 " targets table 0, but no tables defined\n", i);
                // goto error_cleanup_elem; // Или ошибка инстанцирования?
            } else if (seg->table_index >= module->num_tables) {
                // ESP_LOGE(TAG, "Element segment %u uses invalid table_index %u (num_tables: %u)", i, seg->table_index, module->num_tables);
                fprintf(stderr, "Element segment %" PRIu32 " uses invalid table_index %" PRIu32 " (num_tables: %" PRIu32 ")\n", i, seg->table_index, module->num_tables);
                goto error_cleanup_elem;
            }
        }

        if (!read_u8(&ptr, end_ptr, &elem_type_val)) {
            // ESP_LOGE(TAG, "Failed to read element_type for element segment %u", i);
            fprintf(stderr, "Failed to read element_type for element segment %" PRIu32 "\n", i);
            goto error_cleanup_elem;
        }
        seg->element_type = (EspbRefType)elem_type_val;
        if (seg->element_type != ESPB_REF_TYPE_FUNCREF) { // Только funcref в ESPB v1.7
            // ESP_LOGE(TAG, "Invalid element_type %u for element segment %u", seg->element_type, i);
            fprintf(stderr, "Invalid element_type %" PRIu8 " for element segment %" PRIu32 "\n", seg->element_type, i);
            goto error_cleanup_elem;
        }

        if (!read_u32(&ptr, end_ptr, &seg->num_elements)) {
            // ESP_LOGE(TAG, "Failed to read num_elements for element segment %u", i);
            fprintf(stderr, "Failed to read num_elements for element segment %" PRIu32 "\n", i);
            goto error_cleanup_elem;
        }

        if (seg->num_elements > 0) {
            if (seg->num_elements > 1024*1024) { // Разумный лимит
                // ESP_LOGE(TAG, "Too many elements in segment %u: %u", i, seg->num_elements);
                fprintf(stderr, "Too many elements in segment %" PRIu32 ": %" PRIu32 "\n", i, seg->num_elements);
                goto error_cleanup_elem;
            }
            seg->function_indices = (uint32_t *)malloc(seg->num_elements * sizeof(uint32_t));
            if (!seg->function_indices) {
                // ESP_LOGE(TAG, "Failed to allocate function_indices for element segment %u", i);
                fprintf(stderr, "Failed to allocate function_indices for element segment %" PRIu32 "\n", i);
                goto error_cleanup_elem;
            }
            for (uint32_t j = 0; j < seg->num_elements; ++j) {
                if (!read_u32(&ptr, end_ptr, &seg->function_indices[j])) {
                    // ESP_LOGE(TAG, "Failed to read function_index %u for element segment %u", j, i);
                    fprintf(stderr, "Failed to read function_index %" PRIu32 " for element segment %" PRIu32 "\n", j, i);
                    // free(seg->function_indices); // Освободить частично выделенное
                    // seg->function_indices = NULL;
                    goto error_cleanup_elem;
                }
                // TODO: Валидация function_indices[j] (должен быть < общего числа функций, включая импорты)
                // Эту проверку лучше делать при инстанцировании.
            }
        }
    }

    if (ptr != end_ptr) {
        // ESP_LOGW(TAG, "Extra data at the end of Element section (%zu bytes)", (size_t)(end_ptr - ptr));
        fprintf(stderr, "Warning: Extra data at the end of Element section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }
    return ESPB_OK;

error_cleanup_elem:
    // espb_free_module позаботится об очистке element_segments и их содержимого
    return ESPB_ERR_INVALID_ELEMENT_SECTION;
}

EspbResult espb_parse_start_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Start (ID=15)
    if (espb_find_section(module, 15, &section_data, &section_size)) {
        if (section_size < sizeof(uint32_t)) {
            // ESP_LOGE(TAG, "Start section too small (%u bytes)", section_size);
            fprintf(stderr, "Start section too small (%" PRIu32 " bytes)\n", section_size);
            return ESPB_ERR_INVALID_START_SECTION;
        }
        const uint8_t *ptr = section_data;
        const uint8_t *end_ptr = section_data + section_size;
        if (!read_u32(&ptr, end_ptr, &module->start_function_index)) {
            // ESP_LOGE(TAG, "Failed to read start_function_index");
            fprintf(stderr, "Failed to read start_function_index\n");
            return ESPB_ERR_INVALID_START_SECTION;
        }
        module->has_start_function = true;

        // TODO: Валидация start_function_index (должен быть < общего числа функций и иметь сигнатуру () -> ())
        // Эту проверку лучше делать при инстанцировании или после парсинга Types и Functions.

        if (ptr != end_ptr) {
            // ESP_LOGW(TAG, "Extra data at the end of Start section");
            fprintf(stderr, "Warning: Extra data at the end of Start section\n");
        }
    } else {
        module->has_start_function = false;
        module->start_function_index = 0; // Неопределено
    }
    return ESPB_OK;
}

EspbResult espb_parse_immeta_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    // Секция Import Marshalling Metadata (ID=17)
    if (!espb_find_section(module, 17, &section_data, &section_size)) {
        // ESP_LOGI(TAG, "Import Marshalling Metadata section (ID=17) not found. Assuming no marshalling metadata.");
        fprintf(stderr, "Info: Import Marshalling Metadata section (ID=17) not found. Assuming no marshalling metadata.\n");
        module->immeta.num_imports_with_meta = 0;
        module->immeta.imports = NULL;
        return ESPB_OK;
    }

    printf("Found immeta section (ID=17) with size %u bytes\n", section_size);

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    // Читаем num_imports_with_meta
    uint16_t num_imports_with_meta = 0;
    if (!read_u16(&ptr, end_ptr, &num_imports_with_meta)) {
        fprintf(stderr, "Failed to read num_imports_with_meta from immeta section\n");
        return ESPB_ERR_INVALID_IMMETA_SECTION;
    }
    module->immeta.num_imports_with_meta = num_imports_with_meta;

    printf("Immeta section: %u imports with marshalling metadata\n", num_imports_with_meta);

    if (num_imports_with_meta > 0) {
        module->immeta.imports = (EspbImmetaImportEntry*)SAFE_CALLOC(num_imports_with_meta, sizeof(EspbImmetaImportEntry));
        if (!module->immeta.imports) {
            fprintf(stderr, "Failed to allocate memory for immeta imports\n");
            return ESPB_ERR_MEMORY_ALLOC;
        }

        for (uint16_t mi = 0; mi < num_imports_with_meta; ++mi) {
            EspbImmetaImportEntry *entry = &module->immeta.imports[mi];

            // Читаем import_index
            if (!read_u16(&ptr, end_ptr, &entry->import_index)) {
                fprintf(stderr, "Failed to read import_index for immeta entry %" PRIu16 "\n", mi);
                goto error_cleanup_immeta;
            }

            // Читаем num_marshalled_args
            if (!read_u8(&ptr, end_ptr, &entry->num_marshalled_args)) {
                fprintf(stderr, "Failed to read num_marshalled_args for immeta entry %" PRIu16 "\n", mi);
                goto error_cleanup_immeta;
            }

            printf("  Import #%u: %u marshalled arguments\n", entry->import_index, entry->num_marshalled_args);

            // Выделяем память для args (num_marshalled_args * 5 байт)
            if (entry->num_marshalled_args > 0) {
                entry->args = (EspbImmetaArgEntry*)SAFE_CALLOC(entry->num_marshalled_args, sizeof(EspbImmetaArgEntry));
                if (!entry->args) {
                    fprintf(stderr, "Failed to allocate memory for immeta args in entry %" PRIu16 "\n", mi);
                    goto error_cleanup_immeta;
                }

                // Читаем каждый аргумент (5 байт: arg_index, direction_flags, size_kind, size_value, handler_index)
                for (uint8_t ai = 0; ai < entry->num_marshalled_args; ++ai) {
                    EspbImmetaArgEntry *arg = &entry->args[ai];

                    if (!read_u8(&ptr, end_ptr, &arg->arg_index)) {
                        fprintf(stderr, "Failed to read arg_index for immeta entry %" PRIu16 " arg %" PRIu8 "\n", mi, ai);
                        goto error_cleanup_immeta;
                    }

                    if (!read_u8(&ptr, end_ptr, &arg->direction_flags)) {
                        fprintf(stderr, "Failed to read direction_flags for immeta entry %" PRIu16 " arg %" PRIu8 "\n", mi, ai);
                        goto error_cleanup_immeta;
                    }

                    if (!read_u8(&ptr, end_ptr, &arg->size_kind)) {
                        fprintf(stderr, "Failed to read size_kind for immeta entry %" PRIu16 " arg %" PRIu8 "\n", mi, ai);
                        goto error_cleanup_immeta;
                    }

                    if (!read_u8(&ptr, end_ptr, &arg->size_value)) {
                        fprintf(stderr, "Failed to read size_value for immeta entry %" PRIu16 " arg %" PRIu8 "\n", mi, ai);
                        goto error_cleanup_immeta;
                    }

                    if (!read_u8(&ptr, end_ptr, &arg->handler_index)) {
                        fprintf(stderr, "Failed to read handler_index for immeta entry %" PRIu16 " arg %" PRIu8 "\n", mi, ai);
                        goto error_cleanup_immeta;
                    }

                    const char *direction_str = 
                        (arg->direction_flags == ESPB_IMMETA_DIRECTION_IN) ? "IN" :
                        (arg->direction_flags == ESPB_IMMETA_DIRECTION_OUT) ? "OUT" :
                        (arg->direction_flags == ESPB_IMMETA_DIRECTION_INOUT) ? "INOUT" : "UNKNOWN";

                    const char *size_kind_str = 
                        (arg->size_kind == ESPB_IMMETA_SIZE_KIND_CONST) ? "CONST" :
                        (arg->size_kind == ESPB_IMMETA_SIZE_KIND_FROM_ARG) ? "FROM_ARG" : "UNKNOWN";

                    printf("    Arg %u: direction=%s, size=%s(%u), handler=%u\n", 
                           arg->arg_index, direction_str, size_kind_str, arg->size_value, arg->handler_index);
                }
            } else {
                entry->args = NULL;
            }
        }
    } else {
        module->immeta.imports = NULL;
    }

    if (ptr != end_ptr) {
        fprintf(stderr, "Warning: Extra data at the end of Import Marshalling Metadata section (%zu bytes)\n", (size_t)(end_ptr - ptr));
    }

    printf("Successfully parsed immeta section\n");
    return ESPB_OK;

error_cleanup_immeta:
    if (module->immeta.imports) {
        for (uint16_t i = 0; i < num_imports_with_meta; ++i) {
            if (module->immeta.imports[i].args) {
                free(module->immeta.imports[i].args);
            }
        }
        free(module->immeta.imports);
    }
    memset(&module->immeta, 0, sizeof(module->immeta));
    return ESPB_ERR_INVALID_IMMETA_SECTION;
}

// Comparator for qsort on EspbFuncPtrMapEntry
static int compare_func_ptr_map_entries(const void *a, const void *b) {
    const EspbFuncPtrMapEntry *entry_a = (const EspbFuncPtrMapEntry *)a;
    const EspbFuncPtrMapEntry *entry_b = (const EspbFuncPtrMapEntry *)b;
    if (entry_a->data_offset < entry_b->data_offset) return -1;
    if (entry_a->data_offset > entry_b->data_offset) return 1;
    return 0;
}

EspbResult espb_parse_func_ptr_map_section(EspbModule *module) {
    const uint8_t *section_data = NULL;
    uint32_t section_size = 0;

    if (!espb_find_section(module, 18 /* ID секции Function Pointer Map */, &section_data, &section_size)) {
        fprintf(stderr, "Info: Function Pointer Map section (ID=18) not found.\n");
        module->num_func_ptr_map_entries = 0;
        module->func_ptr_map = NULL;
        return ESPB_OK;
    }

    const uint8_t *ptr = section_data;
    const uint8_t *end_ptr = section_data + section_size;

    if (!read_u32(&ptr, end_ptr, &module->num_func_ptr_map_entries)) {
        fprintf(stderr, "Failed to read number of function pointer map entries\n");
        return ESPB_ERR_INVALID_DATA_SECTION; // Можно использовать более специфичную ошибку
    }
    
    uint32_t num_entries = module->num_func_ptr_map_entries;
    fprintf(stderr, "DEBUG: Found Function Pointer Map section with %u entries.\n", num_entries);

    if (num_entries == 0) {
        module->func_ptr_map = NULL;
        return ESPB_OK;
    }

    size_t expected_size = num_entries * sizeof(EspbFuncPtrMapEntry);
    if ((size_t)(end_ptr - ptr) < expected_size) {
        fprintf(stderr, "Function Pointer Map section size mismatch. Expected %zu bytes for entries, but only %td available.\n",
                expected_size, (ptrdiff_t)(end_ptr - ptr));
        return ESPB_ERR_INVALID_DATA_SECTION;
    }

    module->func_ptr_map = (EspbFuncPtrMapEntry *)SAFE_MALLOC(expected_size);
    if (!module->func_ptr_map) {
        fprintf(stderr, "Failed to allocate memory for function pointer map\n");
        module->num_func_ptr_map_entries = 0;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    // ИСПРАВЛЕНИЕ: Читаем каждую запись индивидуально для корректной обработки Little-Endian
    for (uint32_t i = 0; i < num_entries; ++i) {
        if (!read_u32(&ptr, end_ptr, &module->func_ptr_map[i].data_offset) ||
            !read_u16(&ptr, end_ptr, &module->func_ptr_map[i].function_index)) {
            fprintf(stderr, "Failed to read entry %u from Function Pointer Map section.\n", i);
            free(module->func_ptr_map);
            module->func_ptr_map = NULL;
            module->num_func_ptr_map_entries = 0;
            return ESPB_ERR_INVALID_DATA_SECTION;
        }
    }
    
    // Сортируем массив для бинарного поиска в рантайме
    if (num_entries > 0) {
        qsort(module->func_ptr_map, num_entries, sizeof(EspbFuncPtrMapEntry), compare_func_ptr_map_entries);
        fprintf(stderr, "DEBUG: Function Pointer Map sorted by data_offset.\n");
    }

    for(uint32_t i=0; i < num_entries; ++i) {
        fprintf(stderr, "DEBUG: Map Entry %u: offset=0x%x -> func_idx=%u\n", i, module->func_ptr_map[i].data_offset, module->func_ptr_map[i].function_index);
    }

    if (ptr != end_ptr) {
        fprintf(stderr, "Warning: Extra data at the end of Function Pointer Map section.\n");
    }

    return ESPB_OK;
}
void espb_free_module(EspbModule *module) {
    if (!module) {
        return;
    }
    // Вызываем вспомогательную функцию для очистки всех полей, связанных с секциями
    espb_clear_module_sections(module);

    // Освобождаем саму структуру модуля
    free(module);
}

EspbResult espb_parse_module(EspbModule **out_module, const uint8_t *buffer, size_t buffer_size) {
    if (!out_module || !buffer) {
        // ESP_LOGE(TAG, "espb_parse_module: Invalid arguments (out_module or buffer is NULL)");
        fprintf(stderr, "espb_parse_module: Invalid arguments (out_module or buffer is NULL)\n");
        return ESPB_ERR_PARSE_ERROR; // Общая ошибка парсинга
    }

    *out_module = NULL;
    EspbModule *module = (EspbModule *)SAFE_CALLOC(1, sizeof(EspbModule));
    if (!module) {
        // ESP_LOGE(TAG, "Failed to allocate memory for EspbModule");
        fprintf(stderr, "Failed to allocate memory for EspbModule\n");
        return ESPB_ERR_MEMORY_ALLOC;
    }

    EspbResult result;

    // 1. Парсинг заголовка и таблицы секций
    result = espb_parse_header_and_sections(module, buffer, buffer_size);
    if (result != ESPB_OK) {
        // ESP_LOGE(TAG, "Failed to parse header and sections: %d", result);
        fprintf(stderr, "Failed to parse header and sections: %d\n", result);
        espb_free_module(module); // Очистка в случае ошибки
        return result;
    }

    // 2. Последовательный парсинг всех известных секций
    // Порядок важен, так как некоторые секции зависят от данных, распарсенных в предыдущих
    // (например, Functions зависит от Types, Code зависит от Functions и т.д.)

    if ((result = espb_parse_types_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_imports_section(module)) != ESPB_OK) goto cleanup_error; // Imports могут ссылаться на Types
    if ((result = espb_parse_functions_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_tables_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_memory_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_cbmeta_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_immeta_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_globals_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_exports_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_start_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_element_section(module)) != ESPB_OK) goto cleanup_error; // Element зависит от Tables и Functions/Imports
    if ((result = espb_parse_code_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_data_section(module)) != ESPB_OK) goto cleanup_error;     // Data зависит от Memory
    if ((result = espb_parse_func_ptr_map_section(module)) != ESPB_OK) goto cleanup_error;
    if ((result = espb_parse_relocations_section(module)) != ESPB_OK) goto cleanup_error;

    // TODO: Возможно, здесь нужны дополнительные проверки целостности модуля после парсинга всех секций.

    *out_module = module;
    return ESPB_OK;

cleanup_error:
    // ESP_LOGE(TAG, "Failed to parse module section: %d", result);
    fprintf(stderr, "Failed to parse module section: %d\n", result);
    espb_free_module(module);
    *out_module = NULL;
    return result;
}

// Конец файла main/espb_interpreter_parser.c

