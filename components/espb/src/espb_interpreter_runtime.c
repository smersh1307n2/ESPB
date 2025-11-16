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

#include "espb_heap_manager.h" // Убедитесь, что инклюд есть
#include "espb_interpreter_runtime.h"
#include "safe_memory.h"
#include "esp_log.h"
#include "espb_interpreter_parser.h"
#include "espb_host_symbols.h"
#include "sdkconfig.h"
#include "espb_interpreter.h"
// espb_interpreter.h должен включать espb_interpreter_common_types.h

// ВКЛЮЧАЕМ ЗАГОЛОВОК ДЛЯ ПЕРЕНЕСЕННОЙ ФУНКЦИИ
#include "espb_interpreter_runtime_oc.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "espb_interpreter_reader.h"  // Для функций read_u* и макросов PRIu*

// Если CONFIG_ESPB_LINEAR_MEMORY_SIZE не определен, используем значение по умолчанию
#ifndef CONFIG_ESPB_LINEAR_MEMORY_SIZE
#define CONFIG_ESPB_LINEAR_MEMORY_SIZE 65536
#endif

// Локальный TAG для сообщений от этого модуля
static const char *TAG __attribute__((unused)) = "espb_runtime";

// Дополнительные константы ошибок для внутреннего использования в runtime
#define ESPB_ERR_RUNTIME_TRAP_DIV_BY_ZERO      (ESPB_ERR_RUNTIME_ERROR - 1)
#define ESPB_ERR_RUNTIME_TRAP_INTEGER_OVERFLOW (ESPB_ERR_RUNTIME_ERROR - 2)
#define ESPB_ERR_RUNTIME_TRAP_BAD_BRANCH_TARGET (ESPB_ERR_RUNTIME_ERROR - 3)
#define ESPB_ERR_INVALID_MEMORY_INDEX  (ESPB_ERR_RUNTIME_ERROR - 4) // <--- ДОБАВЛЕНО
#define ESPB_ERR_RUNTIME_TRAP          (ESPB_ERR_RUNTIME_ERROR - 5)

// Вспомогательная функция для поиска символов хоста.
// Эта функция должна быть предоставлена средой, в которой исполняется интерпретатор.
// Пример объявления:
// extern const void *espb_lookup_host_symbol(const char *module_name, const char *entity_name);
// Если она уже объявлена в одном из заголовочных файлов, это объявление можно удалить.
extern const void *espb_lookup_host_symbol(const char *module_name, const char *entity_name);


static EspbResult allocate_linear_memory(EspbInstance *instance) {
    // ESP_LOGI(TAG, "Allocating linear memory..."); 
    printf("Runtime: Allocating linear memory...\\n");
    const EspbModule *module = instance->module;
    
    uint32_t initial_pages = 0;
    uint32_t max_pages = 0; 
    bool has_max = false;

    instance->memory_data = NULL;
    instance->memory_size_bytes = 0;
    // Wasm spec implies it can grow up to 2^16 pages (4GiB for Wasm32) if no max.
    // Or, implementation can choose a smaller practical limit.
    instance->memory_max_size_bytes = 0; 

    const EspbImportDesc *imported_mem_desc = NULL;
    for (uint32_t i = 0; i < module->num_imports; i++) {
        if (module->imports[i].kind == ESPB_IMPORT_KIND_MEMORY) {
            // Prioritize "env.memory" if multiple memory imports exist (though typically only one is primary)
            if (strcmp(module->imports[i].module_name, "env") == 0 && 
                strcmp(module->imports[i].entity_name, "memory") == 0) {
                imported_mem_desc = &module->imports[i];
                break; 
            }
            // Fallback to the first found memory import if "env.memory" is not specifically named.
            if (!imported_mem_desc) {
                imported_mem_desc = &module->imports[i];
            }
        }
    }

    if (imported_mem_desc) {
        printf("Runtime: Found imported memory '%s::%s'. Prioritizing it.\\n", 
               imported_mem_desc->module_name, imported_mem_desc->entity_name);
        initial_pages = imported_mem_desc->desc.memory.initial_size;
        if (imported_mem_desc->desc.memory.flags & 0x01) { // has_max
            max_pages = imported_mem_desc->desc.memory.max_size;
            has_max = true;
        }
        
        // ESPB Page Size is 1024 bytes
        uint64_t initial_bytes = (uint64_t)initial_pages * 65536ULL; 
        if (initial_bytes > UINT32_MAX) { // Check against Wasm32 limit for memory size
             fprintf(stderr, "Error: Initial memory size (%" PRIu64 ") bytes for import '%s::%s' exceeds Wasm32 addressable space (UINT32_MAX).\n", 
                     initial_bytes, imported_mem_desc->module_name, imported_mem_desc->entity_name);
             return ESPB_ERR_MEMORY_ALLOC;
        }
        // Используем значение из конфигурации, если оно больше запрошенного размера
        uint32_t configured_size = CONFIG_ESPB_LINEAR_MEMORY_SIZE;
        // Всегда используем значение из конфигурации
        printf("Runtime: Using configured linear memory size (%" PRIu32 " bytes) instead of declared size (%" PRIu64 " bytes).\n", 
               configured_size, initial_bytes);
        instance->memory_size_bytes = configured_size;

        if (has_max) {
            uint64_t max_bytes = (uint64_t)max_pages * 65536ULL;
            instance->memory_max_size_bytes = (max_bytes > UINT32_MAX) ? UINT32_MAX : (uint32_t)max_bytes;
        } else {
            // Default Wasm32 max (e.g., 4GiB) if no explicit max, or a practical implementation limit.
            instance->memory_max_size_bytes = UINT32_MAX; 
        }

        if (instance->memory_size_bytes > 0) {
            void* host_mem_ptr = (void*)espb_lookup_host_symbol(imported_mem_desc->module_name, imported_mem_desc->entity_name);
            if (!host_mem_ptr) {
                fprintf(stderr, "Error: Imported memory '%s::%s' (%" PRIu32 " pages) resolved to NULL by host, but size > 0.\\n", 
                        imported_mem_desc->module_name, imported_mem_desc->entity_name, initial_pages);
                return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
            }
            instance->memory_data = (uint8_t*)host_mem_ptr;
            // Ensure host memory is at least the initial size.
            // This check might be better done by the host or via a specific host API for memory size.
            printf("Runtime: Using host-provided memory at %p for '%s::%s' (%" PRIu32 " bytes initial, max %" PRIu32 " bytes).\\n",
                   instance->memory_data, imported_mem_desc->module_name, imported_mem_desc->entity_name, 
                   instance->memory_size_bytes, instance->memory_max_size_bytes);
        } else {
            printf("Runtime: Imported memory '%s::%s' initial size is 0 pages.\\n", 
                   imported_mem_desc->module_name, imported_mem_desc->entity_name);
            instance->memory_data = NULL; // memory_size_bytes is already 0
        }

    } else if (module->num_memories > 0) {
        if (module->num_memories > 1) {
            // Current simple interpreter supports only one declared memory (at index 0) if no "env.memory" is imported.
            fprintf(stderr, "Error: Multiple declared memories found (%" PRIu32 "), but only one (index 0) is supported when 'env.memory' is not imported.\n", module->num_memories);
            return ESPB_ERR_INSTANTIATION_FAILED; // Or ESPB_ERR_FEATURE_NOT_SUPPORTED
        }
        const EspbMemoryDesc *mem_desc = &module->memories[0]; // Use declared memory at index 0
        printf("Runtime: No 'env.memory' import found. Using module's declared memory (index 0).\\n");
        
        initial_pages = mem_desc->limits.initial_size;
        if (mem_desc->limits.flags & 0x01) { // has_max
            max_pages = mem_desc->limits.max_size;
            has_max = true;
        }
        printf("Runtime: Declared memory details: initial_pages=%" PRIu32 ", max_pages=%" PRIu32 ", has_max=%d\n", 
               initial_pages, max_pages, has_max);

        uint64_t initial_bytes = (uint64_t)initial_pages * 65536ULL; // ESPB Page Size
        if (initial_bytes > UINT32_MAX) {
             fprintf(stderr, "Error: Initial declared memory size (%" PRIu64 ") bytes exceeds Wasm32 addressable space (UINT32_MAX).\n", initial_bytes);
             return ESPB_ERR_MEMORY_ALLOC;
        }
        
        // Используем значение из конфигурации, если оно больше запрошенного размера
        uint32_t configured_size = CONFIG_ESPB_LINEAR_MEMORY_SIZE;
        // Всегда используем значение из конфигурации
        printf("Runtime: Using configured linear memory size (%" PRIu32 " bytes) instead of declared size (%" PRIu64 " bytes).\n", 
               configured_size, initial_bytes);
        instance->memory_size_bytes = configured_size;

        if (has_max) {
            uint64_t max_bytes = (uint64_t)max_pages * 65536ULL;
            instance->memory_max_size_bytes = (max_bytes > UINT32_MAX) ? UINT32_MAX : (uint32_t)max_bytes;
        } else {
            instance->memory_max_size_bytes = UINT32_MAX; 
        }
        
        if (instance->memory_size_bytes > 0) {
             instance->memory_data = (uint8_t *)SAFE_CALLOC(instance->memory_size_bytes, 1);
             if (!instance->memory_data) {
                 fprintf(stderr, "Error: Failed to allocate %" PRIu32 " bytes for declared linear memory.\\n", instance->memory_size_bytes);
                 return ESPB_ERR_MEMORY_ALLOC;
             }
             printf("Runtime: Allocated %" PRIu32 " bytes for declared linear memory (max: %" PRIu32 " bytes).\\n",
                    instance->memory_size_bytes, instance->memory_max_size_bytes);
        } else {
             printf("Runtime: Declared linear memory initial size is 0 pages.\\n");
             instance->memory_data = NULL; // memory_size_bytes is already 0
        }
    } else {
        printf("Runtime: No memory defined (neither imported 'env.memory' nor declared in module).\n");
        // Если есть пассивные сегменты данных, автоматически выделяем память для них
        instance->memory_data = NULL; // временно NULL
        if (module->num_data_segments > 0) {
            // Ищем максимальный размер пассивного сегмента
            uint32_t max_passive_size = 0;
            for (uint32_t di = 0; di < module->num_data_segments; ++di) {
                const EspbDataSegment *dseg = &module->data_segments[di];
                if (dseg->segment_type == 1 && dseg->data_size > max_passive_size) {
                    max_passive_size = dseg->data_size;
                }
            }
            if (max_passive_size > 0) {
                // Выделяем минимум одну страницу (1024 байта), покрывающую все пассивные данные
                const uint32_t page_size = 65536;
                uint32_t pages = (max_passive_size + page_size - 1) / page_size;
                if (pages == 0) pages = 1;
                
                // Используем значение из конфигурации, если оно больше расчетного размера
                uint32_t alloc_size = pages * page_size;
                uint32_t configured_size = CONFIG_ESPB_LINEAR_MEMORY_SIZE;
                // Всегда используем значение из конфигурации
                printf("Runtime: Using configured linear memory size (%" PRIu32 " bytes) instead of calculated size (%" PRIu32 " bytes).\n", 
                       configured_size, alloc_size);
                alloc_size = configured_size;
                
                instance->memory_size_bytes = alloc_size;
                instance->memory_max_size_bytes = alloc_size;
                instance->memory_data = (uint8_t*)calloc(alloc_size, 1);
                if (!instance->memory_data) {
                    fprintf(stderr, "Error: Auto-allocation of %" PRIu32 " bytes for passive data failed.\n", alloc_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                printf("Runtime: Auto-allocated %" PRIu32 " bytes (%" PRIu32 " pages) for passive data segments.\n", alloc_size, pages);
            } else {
                // Если нет пассивных сегментов данных, но нужна память, выделяем минимальный размер из конфигурации
                uint32_t configured_size = CONFIG_ESPB_LINEAR_MEMORY_SIZE;
                if (configured_size > 0) {
                    instance->memory_size_bytes = configured_size;
                    instance->memory_max_size_bytes = configured_size;
                    instance->memory_data = (uint8_t*)calloc(configured_size, 1);
                    if (!instance->memory_data) {
                        fprintf(stderr, "Error: Auto-allocation of %" PRIu32 " bytes for linear memory failed.\n", configured_size);
                        return ESPB_ERR_MEMORY_ALLOC;
                    }
                    printf("Runtime: Auto-allocated %" PRIu32 " bytes for linear memory from configuration.\n", configured_size);
                }
            }
        } else {
            // Если нет сегментов данных, но нужна память, выделяем минимальный размер из конфигурации
            uint32_t configured_size = CONFIG_ESPB_LINEAR_MEMORY_SIZE;
            if (configured_size > 0) {
                instance->memory_size_bytes = configured_size;
                instance->memory_max_size_bytes = configured_size;
                instance->memory_data = (uint8_t*)calloc(configured_size, 1);
                if (!instance->memory_data) {
                    fprintf(stderr, "Error: Auto-allocation of %" PRIu32 " bytes for linear memory failed.\n", configured_size);
                    return ESPB_ERR_MEMORY_ALLOC;
                }
                printf("Runtime: Auto-allocated %" PRIu32 " bytes for linear memory from configuration.\n", configured_size);
            } else {
                instance->memory_data = NULL;
            }
        }
    }
    
    return ESPB_OK;
}

static EspbResult allocate_globals(EspbInstance *instance) {
    // ESP_LOGI(TAG, "Allocating globals...");
    printf("Runtime: Allocating globals...\n");
    instance->globals_data = NULL;
    instance->globals_data_size = 0;
    instance->global_offsets = NULL;
    const EspbModule *module = instance->module;

    if (module->num_globals == 0) {
        // ESP_LOGI(TAG, "  No globals defined.");
        printf("Runtime: No globals defined.\n");
        return ESPB_OK;
    }

    instance->global_offsets = (uint32_t *)malloc(module->num_globals * sizeof(uint32_t));
    if (!instance->global_offsets) {
        // ESP_LOGE(TAG, "Failed to allocate memory for global offsets.");
        fprintf(stderr, "Error: Failed to allocate memory for global offsets.\n");
        return ESPB_ERR_MEMORY_ALLOC;
    }
    size_t current_offset = 0;
    size_t max_align = 1;
    for (uint32_t i = 0; i < module->num_globals; ++i) {
        const EspbGlobalDesc *g = &module->globals[i];
        size_t type_size = 0;
        size_t type_align = 0;

        switch (g->type) {
            case ESPB_TYPE_I8: case ESPB_TYPE_U8: case ESPB_TYPE_BOOL: type_size = 1; type_align = 1; break;
            case ESPB_TYPE_I16: case ESPB_TYPE_U16: type_size = 2; type_align = 2; break;
            case ESPB_TYPE_I32: case ESPB_TYPE_U32: case ESPB_TYPE_F32: case ESPB_TYPE_PTR: type_size = 4; type_align = 4; break;
            case ESPB_TYPE_I64: case ESPB_TYPE_U64: case ESPB_TYPE_F64: type_size = 8; type_align = 8; break;
            case ESPB_TYPE_V128: type_size = 16; type_align = 16; break; // Assuming V128 is aligned to 16
            default: 
                // ESP_LOGE(TAG, "Unknown global type %d for global %u", g->type, i);
                fprintf(stderr, "Error: Unknown global type %d for global %lu\n", g->type, (unsigned long)i);
                free(instance->global_offsets);
                instance->global_offsets = NULL;
                return ESPB_ERR_INSTANTIATION_FAILED;
        }

        size_t padding = (type_align - (current_offset % type_align)) % type_align;
        current_offset += padding;
        instance->global_offsets[i] = (uint32_t)current_offset;
        current_offset += type_size;
        if (type_align > max_align) max_align = type_align;
    }

    instance->globals_data_size = current_offset;
    // ESP_LOGI(TAG, "  Total size needed for %u globals: %zu bytes (max align: %zu).", module->num_globals, instance->globals_data_size, max_align);
    printf("Runtime: Total size for %lu globals: %zu bytes (max align: %zu).\n", (unsigned long)module->num_globals, instance->globals_data_size, max_align);

    if (instance->globals_data_size > 0) {
        instance->globals_data = (uint8_t*)calloc(instance->globals_data_size, 1); // calloc initializes to zero
        if (!instance->globals_data) {
            // ESP_LOGE(TAG, "Failed to allocate %zu bytes for globals.", instance->globals_data_size);
            fprintf(stderr, "Error: Failed to allocate %zu bytes for globals.\n", instance->globals_data_size);
            instance->globals_data_size = 0;
            free(instance->global_offsets);
            instance->global_offsets = NULL;
            return ESPB_ERR_MEMORY_ALLOC;
        }
        // ESP_LOGI(TAG, "  Allocated %zu bytes for globals.", instance->globals_data_size);
        printf("Runtime: Allocated %zu bytes for globals.\n", instance->globals_data_size);
         
        // Values from ESPB_INIT_KIND_CONST or ESPB_INIT_KIND_DATA_OFFSET 
        // will be set by espb_apply_relocations or direct initialization later.
        // calloc already zeroed memory for ESPB_INIT_KIND_ZERO.
    }
    return ESPB_OK;
}

static EspbResult allocate_tables(EspbInstance *instance) {
    // ESP_LOGI(TAG, "Allocating tables...");
    printf("Runtime: Allocating tables...\n");
    const EspbModule *module = instance->module;

    instance->table_data = NULL;
    instance->table_size = 0;
    instance->table_max_size = 0;

    if (module->num_tables > 1) {
        // ESP_LOGE(TAG, "Multiple tables not yet supported (found %u).", module->num_tables);
        fprintf(stderr, "Error: Multiple tables not yet supported (found %lu).\n", (unsigned long)module->num_tables);
        return ESPB_ERR_INSTANTIATION_FAILED;
    }
    if (module->num_tables == 1) {
        const EspbTableDesc* table_desc = &module->tables[0];
         if (table_desc->element_type != ESPB_REF_TYPE_FUNCREF) { // Only funcref supported for now
              // ESP_LOGE(TAG, "Only funcref tables supported (found type %d).", table_desc->element_type);
              fprintf(stderr, "Error: Only funcref tables supported (found type %d).\n", table_desc->element_type);
              return ESPB_ERR_INSTANTIATION_FAILED;
         }
         instance->table_size = table_desc->limits.initial_size;
         if (table_desc->limits.flags & 0x01) { // has_max
            instance->table_max_size = table_desc->limits.max_size;
         } else {
            // ИСПРАВЛЕНИЕ: Если max не указан, устанавливаем большое значение для динамического роста
            // В WebAssembly отсутствие max означает, что таблица может расти до impl-defined предела
            // Для ESPB используем 65536 как разумный максимум (можно изменить при необходимости)
            instance->table_max_size = 65536; // Разумный максимум для динамического роста
            printf("Runtime: Table %d has no max limit, using default max_size=%u for growth\n", 
                   0, instance->table_max_size);
         }


         if (instance->table_size > 0) {
             instance->table_data = (void**)calloc(instance->table_size, sizeof(void*)); // Table of function pointers/indices
             if (!instance->table_data) {
                  // ESP_LOGE(TAG, "Failed to allocate %u entries for table.", instance->table_size);
                  fprintf(stderr, "Error: Failed to allocate %lu entries for table.\n", (unsigned long)instance->table_size);
                  return ESPB_ERR_MEMORY_ALLOC;
             }
            // ESP_LOGI(TAG, "  Allocated table for %u funcref entries (max: %u).", instance->table_size, instance->table_max_size);
            printf("Runtime: Allocated table for %lu funcref entries (max: %lu).\n", (unsigned long)instance->table_size, (unsigned long)instance->table_max_size);
         } else {
            // ESP_LOGI(TAG, "  Table initial size is 0.");
            printf("Runtime: Table initial size is 0.\n");
         }
    } else {
        // ESP_LOGI(TAG, "  No tables defined.");
        printf("Runtime: No tables defined.\n");
    }
    return ESPB_OK;
}

static EspbResult resolve_imports(EspbInstance *instance) {
    // ESP_LOGI(TAG, "Resolving imports...");
    printf("Runtime: Resolving imports...\n");
    const EspbModule *module = instance->module;
    uint32_t num_imports = module->num_imports;

    instance->resolved_import_funcs = NULL;
    instance->resolved_import_globals = NULL;

    if (num_imports == 0) {
        // ESP_LOGI(TAG, "  No imports to resolve.");
        printf("Runtime: No imports to resolve.\n");
        return ESPB_OK;
    }

    // Allocate arrays to store resolved addresses/pointers.
    // These arrays are indexed by the import index.
    instance->resolved_import_funcs = (void**)calloc(num_imports, sizeof(void*)); 
    instance->resolved_import_globals = (void**)calloc(num_imports, sizeof(void*));

    if (!instance->resolved_import_funcs || !instance->resolved_import_globals) {
        // ESP_LOGE(TAG, "Failed to allocate memory for resolved import pointers.");
        fprintf(stderr, "Error: Failed to allocate memory for resolved import pointers.\n");
        free(instance->resolved_import_funcs); // free(NULL) is safe
        free(instance->resolved_import_globals);
        instance->resolved_import_funcs = NULL;
        instance->resolved_import_globals = NULL;
        return ESPB_ERR_MEMORY_ALLOC;
    }

    // ESP_LOGI(TAG, "  Resolving %u imports...", num_imports);
    printf("Runtime: Resolving %lu imports...\n", (unsigned long)num_imports);
    for (uint32_t i = 0; i < num_imports; ++i) {
        const EspbImportDesc *imp = &module->imports[i];
        // ESP_LOGD(TAG, "    Import %u: %s.%s (kind: %d)", i, imp->module_name, imp->entity_name, imp->kind);
        printf("  Import %lu: %s.%s (kind: %d)\n", (unsigned long)i, imp->module_name, imp->entity_name, imp->kind);
        
        const void *symbol_addr = espb_lookup_host_symbol(imp->module_name, imp->entity_name);
        
        switch (imp->kind) {
            case ESPB_IMPORT_KIND_FUNC:
                if (symbol_addr) {
                    instance->resolved_import_funcs[i] = (void*)symbol_addr;
                    // ESP_LOGD(TAG, "      Successfully resolved function import to %p", symbol_addr);
                } else {
                    // ESP_LOGE(TAG, "      Failed to resolve function import: %s::%s", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Error: Failed to resolve function import: %s::%s\n", imp->module_name, imp->entity_name);
                    // Note: Consider freeing allocated resolved_import_funcs/globals before returning.
                    return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
                }
                break;
            case ESPB_IMPORT_KIND_GLOBAL:
                if (symbol_addr) {
                    // TODO: Validate the type and mutability of the resolved global against imp->desc.global.
                    // EspbValueType imported_type = imp->desc.global.type;
                    // bool imported_mut = imp->desc.global.mutability;
                    // Need a way to query the type/mutability of symbol_addr from host.
                    instance->resolved_import_globals[i] = (void*)symbol_addr;
                    // ESP_LOGD(TAG, "      Successfully resolved global import to %p", symbol_addr);
                } else {
                    // ESP_LOGE(TAG, "      Failed to resolve global import: %s::%s", imp->module_name, imp->entity_name);
                    fprintf(stderr, "Error: Failed to resolve global import: %s::%s\n", imp->module_name, imp->entity_name);
                    return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
                }
                break;
            case ESPB_IMPORT_KIND_MEMORY:
                // ESP_LOGD(TAG, "      Memory import '%s::%s' is handled by allocate_linear_memory.", imp->module_name, imp->entity_name);
                // If a host-provided memory is used, allocate_linear_memory should deal with it.
                // If espb_lookup_host_symbol were to return a MemoryInstance*, logic would go here.
                // For now, assume memory is created or its descriptor used by allocate_linear_memory.
                // If symbol_addr is not NULL here, it means the host provided something; decide how to use it.
                // Currently, allocate_linear_memory creates memory based on the *first* memory definition (declared or imported).
                break;
            case ESPB_IMPORT_KIND_TABLE:
                // ESP_LOGE(TAG, "      Table import '%s::%s' not yet supported.", imp->module_name, imp->entity_name);
                 fprintf(stderr, "Error: Table import '%s::%s' not yet supported.\n", imp->module_name, imp->entity_name);
                return ESPB_ERR_FEATURE_NOT_SUPPORTED;
            default:
                // ESP_LOGE(TAG, "      Unknown import kind: %d for %s::%s", imp->kind, imp->module_name, imp->entity_name);
                fprintf(stderr, "Error: Unknown import kind: %d for %s::%s\n", imp->kind, imp->module_name, imp->entity_name);
                return ESPB_ERR_FEATURE_NOT_SUPPORTED; 
        }
    }
    // ESP_LOGI(TAG, "Imports resolved successfully.");
    printf("Runtime: Imports resolved successfully.\n");
    return ESPB_OK;
}

EspbResult espb_instantiate(EspbInstance **out_instance, const EspbModule *module) {
    printf("Runtime: Instantiating module...\n");
    EspbResult res;

    if (!module) {
        fprintf(stderr, "Error: Input module pointer is NULL for espb_instantiate.\n");
        return ESPB_ERR_INSTANTIATION_FAILED; 
    }

    EspbInstance *instance = (EspbInstance*)SAFE_CALLOC(1, sizeof(EspbInstance));
    if (!instance) {
         fprintf(stderr, "Error: calloc failed for EspbInstance.\n");
         return ESPB_ERR_MEMORY_ALLOC;
    }
    instance->module = module;
    instance->passive_data_at_offset_zero_size = 0; // Initialize new field
    
    // Initialize async wrapper system
    instance->async_wrappers = NULL;
    instance->num_async_wrappers = 0;

    instance->instance_mutex = xSemaphoreCreateMutex();
    if (instance->instance_mutex == NULL) {
        fprintf(stderr, "Error: Failed to create instance mutex.\n");
        espb_free_instance(instance);
        return ESPB_ERR_MEMORY_ALLOC;
    }

    res = allocate_linear_memory(instance);
    if (res != ESPB_OK) goto instantiate_error;

    res = allocate_globals(instance);
    if (res != ESPB_OK) goto instantiate_error;

    res = allocate_tables(instance);
    if (res != ESPB_OK) goto instantiate_error;

    res = resolve_imports(instance);
    if (res != ESPB_OK) goto instantiate_error;

    res = espb_apply_relocations(instance);
    if (res != ESPB_OK) goto instantiate_error;

    res = espb_initialize_data_segments(instance);
    if (res != ESPB_OK) goto instantiate_error;

    // --- ДОБАВИТЬ ЭТОТ БЛОК ---
    res = espb_heap_init(instance, instance->static_data_end_offset);
    if (res != ESPB_OK) {
        fprintf(stderr, "Runtime: Failed to initialize heap manager.\n");
        goto instantiate_error;
    }
    // --- КОНЕЦ ДОБАВЛЕНИЯ ---

    // Initialize next_alloc_offset for the bump allocator (ALLOCA)
    // to start after any data placed at offset 0 by passive segments.
    // This is now initialized in init_execution_context

    res = espb_initialize_element_segments(instance);
    if (res != ESPB_OK) goto instantiate_error;

    ExecutionContext *exec_ctx = init_execution_context();
    if (!exec_ctx) {
        res = ESPB_ERR_MEMORY_ALLOC;
        goto instantiate_error;
    }
    exec_ctx->next_alloc_offset = instance->passive_data_at_offset_zero_size;
    res = espb_run_start_function(instance, exec_ctx);
    if (res != ESPB_OK) {
        free_execution_context(exec_ctx);
        goto instantiate_error;
    }

    printf("Runtime: Module instantiated successfully.\n");
    *out_instance = instance;
    free_execution_context(exec_ctx);
    return ESPB_OK;

instantiate_error:
    fprintf(stderr, "Runtime: Instantiation failed with code %d\n", res);
    espb_free_instance(instance);
    *out_instance = NULL;
    return res; 
}

void espb_free_instance(EspbInstance *instance) {
    if (instance) {
        espb_heap_deinit(instance); // <-- ДОБАВИТЬ ВЫЗОВ
        printf("Runtime: Freeing instance...\n");

        // === CLEANUP ASYNC WRAPPER SYSTEM ===
        if (instance->async_wrappers) {
            ESP_LOGI(TAG, "ESPB ASYNC WRAPPER CLEANUP: Cleaning up async wrapper system");
            
            for (uint32_t i = 0; i < instance->num_async_wrappers; ++i) {
                if (instance->async_wrappers[i]) {
                    AsyncWrapper *wrapper = instance->async_wrappers[i];
                    
                    if (wrapper->closure_ptr) {
                        ESP_LOGI(TAG, "ESPB ASYNC WRAPPER CLEANUP: Freeing async wrapper #%u closure", i);
                        ffi_closure_free(wrapper->closure_ptr);
                        wrapper->closure_ptr = NULL;
                    }
                    
                    if (wrapper->context.out_params) {
                        free(wrapper->context.out_params);
                        wrapper->context.out_params = NULL;
                    }
                    
                    free(wrapper);
                    instance->async_wrappers[i] = NULL;
                }
            }
            
            free(instance->async_wrappers);
            instance->async_wrappers = NULL;
            instance->num_async_wrappers = 0;
            
            ESP_LOGI(TAG, "ESPB ASYNC WRAPPER CLEANUP: Async wrapper cleanup completed");
        }

        // Замыкания теперь освобождаются централизованно через espb_callback_system_deinit()
        if (instance->instance_mutex) {
            vSemaphoreDelete(instance->instance_mutex);
            instance->instance_mutex = NULL;
        }
        if (instance->memory_data) {
            free(instance->memory_data);
            instance->memory_data = NULL;
        }
        if (instance->globals_data) {
            free(instance->globals_data);
            instance->globals_data = NULL;
        }
        if (instance->global_offsets) { 
            free(instance->global_offsets);
            instance->global_offsets = NULL;
        }
        if (instance->table_data) {
            free(instance->table_data);
            instance->table_data = NULL;
        }
        if (instance->resolved_import_funcs) {
            free(instance->resolved_import_funcs);
            instance->resolved_import_funcs = NULL;
        }
        if (instance->resolved_import_globals) {
            free(instance->resolved_import_globals);
            instance->resolved_import_globals = NULL;
        }
        free(instance);
    }
}

static EspbResult espb_evaluate_init_expr(const EspbInstance *instance, const uint8_t *expr, size_t expr_len, uint32_t *out_value) {
    if (!expr || expr_len == 0) {
        fprintf(stderr, "Error: Init expr is NULL or empty.\n");
        return ESPB_ERR_INVALID_INIT_EXPR;
    }

    const uint8_t *ptr = expr;
    const uint8_t *end_expr_ptr = expr + expr_len;
    Value eval_stack[4]; 
    Value *eval_sp = eval_stack;
    uint8_t opcode_byte; // Use a byte for opcode

    while (ptr < end_expr_ptr) {
        if (!read_u8(&ptr, end_expr_ptr, &opcode_byte)) { // Read opcode using reader function
             fprintf(stderr, "Error: Failed to read opcode in init_expr.\n");
             return ESPB_ERR_INVALID_INIT_EXPR;
        }

        switch (opcode_byte) {
            case 0x01: // ESPB_OP_CONST_I32 (assuming 0x01 based on skip_offset_expression, needs actual define)
                if (eval_sp >= eval_stack + 4) { 
                    fprintf(stderr, "Error: InitExpr: Evaluation stack overflow for I32_CONST.\n");
                    return ESPB_ERR_STACK_OVERFLOW; 
                }
                eval_sp->type = ESPB_TYPE_I32;
                if (!read_i32(&ptr, end_expr_ptr, &eval_sp->value.i32)) { 
                    fprintf(stderr, "Error: InitExpr: Failed to read i32.const value.\n");
                    return ESPB_ERR_INVALID_INIT_EXPR; 
                }
                eval_sp++;
                break;
            case 0x02: { // ESPB_OP_GET_GLOBAL (assuming 0x02 based on skip_offset_expression, needs actual define)
                uint32_t global_idx_u32;
                if (!read_u32(&ptr, end_expr_ptr, &global_idx_u32)) { 
                     fprintf(stderr, "Error: InitExpr: Failed to read global.get index.\n");
                     return ESPB_ERR_INVALID_INIT_EXPR; 
                }

                uint32_t num_imported_globals = 0;
                 for (uint32_t i = 0; i < instance->module->num_imports; ++i) {
                     if (instance->module->imports[i].kind == ESPB_IMPORT_KIND_GLOBAL) num_imported_globals++;
                 }
                uint32_t total_globals = num_imported_globals + instance->module->num_globals;
                if (global_idx_u32 >= total_globals) {
                    fprintf(stderr, "Error: InitExpr: global.get index %lu out of bounds (%lu total).\n", (unsigned long)global_idx_u32, (unsigned long)total_globals);
                    return ESPB_ERR_INVALID_GLOBAL_INDEX;
                }

                if (global_idx_u32 < num_imported_globals) {
                    // TODO: Handle imported globals in init_expr. Requires knowing their type and having resolved_import_globals setup for value access.
                    // For now, only i32 type is supported for the output of init_expr.
                    // Need to check if the imported global is an i32.
                    fprintf(stderr, "Error: InitExpr: global.get for imported global %lu not supported yet for non-i32 or requires host value access.\n", (unsigned long)global_idx_u32);
                    return ESPB_ERR_FEATURE_NOT_SUPPORTED; 
                } else {
                    uint32_t local_global_idx = global_idx_u32 - num_imported_globals;
                    const EspbGlobalDesc *g_desc = &instance->module->globals[local_global_idx];
                    
                    if (g_desc->type != ESPB_TYPE_I32 && g_desc->type != ESPB_TYPE_U32 && g_desc->type != ESPB_TYPE_PTR ) { // Assuming PTR can be treated as U32 for offset purposes
                        fprintf(stderr, "Error: InitExpr: global.get for non-i32/u32/ptr local global %lu (type %d) not supported for offset calculation.\n", (unsigned long)local_global_idx, g_desc->type);
                        return ESPB_ERR_FEATURE_NOT_SUPPORTED;
                    }
                    
                    if (!instance->global_offsets || !instance->globals_data) {
                        fprintf(stderr, "Error: InitExpr: Global offsets or data buffer not available for global %lu.\n", (unsigned long)local_global_idx);
                        return ESPB_ERR_INSTANTIATION_FAILED;
                    }
                    uint32_t offset = instance->global_offsets[local_global_idx];
                    if (offset + sizeof(uint32_t) > instance->globals_data_size) {
                        fprintf(stderr, "Error: InitExpr: global.get read out of bounds for global %lu (offset %lu).\n", (unsigned long)local_global_idx, (unsigned long)offset);
                        return ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS;
                    }
                    
                    if (eval_sp >= eval_stack + 4) { 
                        fprintf(stderr, "Error: InitExpr: Evaluation stack overflow for GET_GLOBAL.\n");
                        return ESPB_ERR_STACK_OVERFLOW; 
                    }
                    eval_sp->type = ESPB_TYPE_I32; // Result of offset expr is an i32/u32 address
                    memcpy(&eval_sp->value.u32, (uint32_t*)((uint8_t*)instance->globals_data + offset), sizeof(uint32_t));
                    eval_sp++;
                }
                break;
            }
            // Wasm MVP init_expr typically ends implicitly after one instruction for consts, or with an 'end' opcode.
            // ESPB format for offset_expr might be just one instruction without an explicit end.
            // If an 'end' opcode is used in ESPB init_expr, it should be handled here.
            // case 0x0F: /* ESPB_OP_END, if exists */ goto expr_end_label; 
            default:
                fprintf(stderr, "Error: InitExpr: Unsupported opcode 0x%02x.\n", opcode_byte);
                return ESPB_ERR_INVALID_INIT_EXPR;
        }
        // If ESPB init_expr is only one instruction, break here.
        // For now, assume it's a single opcode that produces a value or that an END opcode will be explicitly parsed.
        // Let's assume for now that an offset expression is one opcode + its immediate(s).
        goto expr_end_label; // Process one instruction and finish for offset expressions
    }

expr_end_label: // Renamed from expr_end to avoid conflict if an opcode 0x0F is ESPB_OP_END.
    if (eval_sp != eval_stack + 1) {
        fprintf(stderr, "Error: InitExpr: Stack has %td values at END, expected 1.\n", (ptrdiff_t)(eval_sp - eval_stack));
        return ESPB_ERR_INVALID_INIT_EXPR;
    }
    // The result of an offset expression is always an i32/u32.
    if (eval_sp[-1].type != ESPB_TYPE_I32 && eval_sp[-1].type != ESPB_TYPE_U32 && eval_sp[-1].type != ESPB_TYPE_PTR) {
        fprintf(stderr, "Error: InitExpr: Final stack value type is %d, expected I32/U32/PTR for offset.\n", eval_sp[-1].type);
        return ESPB_ERR_TYPE_MISMATCH;
    }

    *out_value = eval_sp[-1].value.u32; 
    return ESPB_OK;
}

EspbResult espb_apply_relocations(EspbInstance *instance) {
    printf("Runtime: Applying relocations...\n");
    const EspbModule *module = instance->module;
    uint32_t num_relocs = module->num_relocations;

    if (num_relocs == 0) {
        printf("Runtime: No relocations to apply.\n");
        return ESPB_OK;
    }
    printf("Runtime: Processing %lu relocations:\n", (unsigned long)num_relocs);

    uint32_t num_imported_funcs = 0;
    uint32_t num_imported_globals = 0;
    for (uint32_t i = 0; i < module->num_imports; ++i) {
        switch (module->imports[i].kind) {
            case ESPB_IMPORT_KIND_FUNC: num_imported_funcs++; break;
            case ESPB_IMPORT_KIND_GLOBAL: num_imported_globals++; break;
            default: break;
        }
    }

    for (uint32_t i = 0; i < num_relocs; ++i) {
        const EspbRelocationEntry *reloc = &module->relocations[i];
        void *target_section_data_ptr = NULL;
        size_t target_section_total_size = 0;

        switch (reloc->target_section_id) {
            case 6: // Code Section - ESPB_SECTION_ID_CODE
                // Relocations in code section (e.g., for function pointers, string literals) are complex.
                // This usually involves patching bytecode. For now, this is not supported.
                fprintf(stderr, "Warning: Reloc[%lu]: Relocations in Code section (ID=6) not supported yet.\n", (unsigned long)i);
                continue; 
            case 7: // Data Section - ESPB_SECTION_ID_DATA (applies to linear memory)
                target_section_data_ptr = instance->memory_data;
                target_section_total_size = instance->memory_size_bytes;
                break;
            case 4: // Globals Section - ESPB_SECTION_ID_GLOBALS (applies to instance->globals_data)
                target_section_data_ptr = instance->globals_data;
                target_section_total_size = instance->globals_data_size;
                break;
            default:
                fprintf(stderr, "Error: Reloc[%lu]: Invalid target section ID %lu\n", (unsigned long)i, (unsigned long)reloc->target_section_id);
                return ESPB_ERR_INVALID_RELOCATION_SECTION;
        }

        if (!target_section_data_ptr && target_section_total_size > 0) {
             fprintf(stderr, "Error: Reloc[%lu]: Target section %lu has size > 0 but pointer is NULL.\n", (unsigned long)i, (unsigned long)reloc->target_section_id);
             return ESPB_ERR_INSTANTIATION_FAILED;
        }
        if (reloc->offset >= target_section_total_size) {
             fprintf(stderr, "Error: Reloc[%" PRIu32 "]: Offset %" PRIu32 " is out of bounds for target section ID %" PRIu32 " (size %zu).\n",
                     i, reloc->offset, (uint32_t)reloc->target_section_id, target_section_total_size);
             return ESPB_ERR_INVALID_RELOCATION_SECTION;
        }
        uint8_t *target_location_ptr = (uint8_t*)target_section_data_ptr + reloc->offset;
        uint32_t symbol_val = 0; // The value derived from the symbol index

        switch (reloc->type) {
            case ESPB_RELOC_ABS32_DATA:
            case ESPB_RELOC_MEM_ADDR_I32: // symbol_index is data segment index (0 for default memory), addend is offset in that segment.
                                          // Or, if it refers to a symbol, it's base address + addend.
                                          // For simplicity: assume symbol_index 0 refers to memory base if no other data symbols are defined.
                if (reloc->symbol_index == 0) { // Assuming 0 means base of the memory/data section itself.
                    symbol_val = 0; // Base address of memory is 0 for offset calculations.
                } else {
                    fprintf(stderr, "Warning: Reloc[%lu]: Data symbol %lu resolution beyond index 0 not fully implemented for ABS32_DATA/MEM_ADDR_I32.\n", (unsigned long)i, (unsigned long)reloc->symbol_index);
                    // Potentially look up data segment symbols if your linker/format defines them.
                    continue;
                }
                break;
            case ESPB_RELOC_ABS32_GLOBAL:
            case ESPB_RELOC_GLOBAL_INDEX: // symbol_index is global index.
                if (reloc->symbol_index < num_imported_globals) {
                    if (instance->resolved_import_globals && instance->resolved_import_globals[reloc->symbol_index]) {
                        // For ABS32_GLOBAL, this should be the address of the imported global.
                        // For GLOBAL_INDEX, this interpretation might be different (e.g. an index into a host-side table).
                        // This part needs careful definition based on ABI with host for imported globals.
                        // For now, let's assume we are getting an address for ABS32.
                        if(reloc->type == ESPB_RELOC_ABS32_GLOBAL){
                            symbol_val = (uint32_t)(uintptr_t)instance->resolved_import_globals[reloc->symbol_index];
                        } else { // GLOBAL_INDEX for imported global
                             fprintf(stderr, "Warning: Reloc[%lu]: GLOBAL_INDEX for imported global %lu not fully defined for application.\n", i, reloc->symbol_index);
                             continue; // Skip for now
                        }
                    } else {
                        fprintf(stderr, "Error: Reloc[%lu]: Imported global %lu not resolved for relocation.\n", i, reloc->symbol_index);
                        return ESPB_ERR_IMPORT_RESOLUTION_FAILED;
                    }
                } else {
                    uint32_t local_global_idx = reloc->symbol_index - num_imported_globals;
                    if (local_global_idx >= module->num_globals || !instance->global_offsets) {
                        fprintf(stderr, "Error: Reloc[%lu]: Invalid local global index %lu or global_offsets not init.\n", i, local_global_idx);
                        return ESPB_ERR_INVALID_GLOBAL_INDEX;
                    }
                    if (reloc->type == ESPB_RELOC_ABS32_GLOBAL) { // Value is offset in globals_data
                         symbol_val = instance->global_offsets[local_global_idx];
                    } else { // ESPB_RELOC_GLOBAL_INDEX - the value *is* the (local) index itself.
                         symbol_val = local_global_idx;
                    }
                }
                break;
            // Other relocation types like FUNC related ones are often for dynamic linkers or JITs
            // and might not be directly applicable by simple patching in an interpreter without more context.
            case ESPB_RELOC_ABS32_FUNC:
            case ESPB_RELOC_REL32_CALL:
            case ESPB_RELOC_FUNC_INDEX:
            case ESPB_RELOC_REL32_BRANCH:
            case ESPB_RELOC_TAG_INDEX:
            case ESPB_RELOC_TABLE_INDEX:
            case ESPB_RELOC_TYPE_INDEX:
                fprintf(stderr, "Warning: Reloc[%lu]: Relocation type %d not supported for application in this phase.\n", i, reloc->type);
                continue; 
            default:
                fprintf(stderr, "Error: Reloc[%lu]: Unknown relocation type: %d\n", i, reloc->type);
                return ESPB_ERR_UNKNOWN_OPCODE; // Or a more specific reloc error
        }

        uint32_t value_to_write = symbol_val + (reloc->has_addend ? reloc->addend : 0);
        size_t write_size = sizeof(uint32_t); // Assuming all these relocations write a 32-bit value.

        if (reloc->offset + write_size > target_section_total_size) {
            fprintf(stderr, "Error: Reloc[%" PRIu32 "]: Write (offset %" PRIu32 ", size %zu) out of bounds for section ID %" PRIu32 " (size %zu).\n",
                     i, reloc->offset, write_size, (uint32_t)reloc->target_section_id, target_section_total_size);
            return ESPB_ERR_INVALID_RELOCATION_SECTION;
        }
        memcpy(target_location_ptr, &value_to_write, write_size);
        // ESP_LOGD(TAG, "Reloc[%u] applied: Type=%d, TargetLoc=%p, Value=0x%x", i, reloc->type, target_location_ptr, value_to_write);
    }
    printf("Runtime: Relocations applied.\n");
    return ESPB_OK;
}

EspbResult espb_initialize_data_segments(EspbInstance *instance) {
    printf("Runtime: Initializing data segments...\n");
    const EspbModule *module = instance->module;
    uint32_t num_segments = module->num_data_segments;
    EspbResult res;
    bool first_passive_segment_processed = false;
    uint32_t max_offset_end = 0; // Локальная переменная

    if (num_segments == 0) {
        printf("Runtime: No data segments to initialize.\n");
        instance->static_data_end_offset = 0;
        return ESPB_OK;
    }

    for (uint32_t i = 0; i < num_segments; ++i) {
        const EspbDataSegment *seg = &module->data_segments[i];
        if (seg->segment_type == 0) { // Active segment
            if (seg->memory_index != 0) {
                fprintf(stderr, "Error: Data segment %lu targets unsupported memory index %lu.\n", (unsigned long)i, (unsigned long)seg->memory_index);
                return ESPB_ERR_INSTANTIATION_FAILED;
            }
            if (!instance->memory_data && instance->memory_size_bytes == 0 && seg->data_size > 0) {
                fprintf(stderr, "Error: Active data segment %lu has data, but no linear memory allocated.\n", (unsigned long)i);
                return ESPB_ERR_INSTANTIATION_FAILED;
            } else if (!instance->memory_data && instance->memory_size_bytes > 0 ){
                fprintf(stderr, "Error: Internal error: memory size > 0 but memory_data is NULL for data segment %lu.\n", (unsigned long)i);
                return ESPB_ERR_INSTANTIATION_FAILED;
            }

            uint32_t offset;
            res = espb_evaluate_init_expr(instance, seg->offset_expr, seg->offset_expr_len, &offset);
            if (res != ESPB_OK) {
                fprintf(stderr, "Error: Failed to evaluate offset expression for data segment %lu.\n", (unsigned long)i);
                return res;
            }
            
            if (offset + seg->data_size > max_offset_end) {
                max_offset_end = offset + seg->data_size;
            }

            uint64_t end_offset = (uint64_t)offset + seg->data_size;
            if (end_offset > instance->memory_size_bytes) {
                fprintf(stderr, "Error: Data segment %lu (offset %lu, size %lu) exceeds memory bounds (%lu bytes).\n",
                         (unsigned long)i, (unsigned long)offset, (unsigned long)seg->data_size, (unsigned long)instance->memory_size_bytes);
                return ESPB_ERR_INSTANTIATION_FAILED;
            }

            if (seg->data_size > 0) {
                if (!instance->memory_data) {
                    fprintf(stderr, "Error: Trying to copy data segment %lu but memory_data is NULL (size %lu).\n",
                            (unsigned long)i, (unsigned long)instance->memory_size_bytes);
                    return ESPB_ERR_INSTANTIATION_FAILED;
                }
                memcpy(instance->memory_data + offset, seg->data, seg->data_size);
                printf("Runtime: Copied %lu bytes from ACTIVE data segment %lu to memory offset %lu\n",
                       (unsigned long)seg->data_size, (unsigned long)i, (unsigned long)offset);
            }
        } else if (seg->segment_type == 1 && !first_passive_segment_processed) {
            uint32_t target_passive_offset = 0;
            
            if (target_passive_offset + seg->data_size > max_offset_end) {
                max_offset_end = target_passive_offset + seg->data_size;
            }

            if (instance->memory_data && (target_passive_offset + seg->data_size <= instance->memory_size_bytes)) {
                memcpy(instance->memory_data + target_passive_offset, seg->data, seg->data_size);
                first_passive_segment_processed = true;
                instance->passive_data_at_offset_zero_size = seg->data_size;
                printf("Runtime: Copied %lu bytes from FIRST PASSIVE data segment %lu to memory offset %lu (for DATA_OFFSET global)\n",
                       (unsigned long)seg->data_size, (unsigned long)i, (unsigned long)target_passive_offset);

                if (instance->memory_data && seg->data_size > 0) {
                    printf("Runtime DBG: Content at instance->memory_data[0] after memcpy (first 20 chars): START>>%.20s<<END\n", (char*)instance->memory_data);
                    printf("Runtime DBG: Hex at instance->memory_data[0] after memcpy (first 12 bytes): ");
                    for (uint32_t k=0; k < seg->data_size && k < 12; ++k) {
                        printf("%02X ", ((unsigned char*)instance->memory_data)[k]);
                    }
                    printf("\n");
                    fflush(stdout);
                }
            } else {
                 fprintf(stderr, "Error: Cannot copy first passive data segment %lu to offset 0 (mem_size: %lu, data_size: %lu).\n",
                    (unsigned long)i, (unsigned long)instance->memory_size_bytes, (unsigned long)seg->data_size);
                if (!instance->memory_data && seg->data_size > 0) {
                    return ESPB_ERR_INSTANTIATION_FAILED;
                }
            }
        }
    }
    
    instance->static_data_end_offset = max_offset_end; // Сохраняем в инстанс
    printf("Runtime: Data segments initialized. Static data ends at offset: %u\n", max_offset_end);
    return ESPB_OK;
}

EspbResult espb_initialize_element_segments(EspbInstance *instance) {
    printf("Runtime: Initializing element segments...\n");
    const EspbModule *module = instance->module;
    uint32_t num_segments = module->num_element_segments;
    EspbResult res;

    if (num_segments == 0) {
        printf("Runtime: No element segments to initialize.\n");
        return ESPB_OK;
    }

    for (uint32_t i = 0; i < num_segments; ++i) {
        const EspbElementSegment *seg = &module->element_segments[i];
        bool is_active = (seg->flags == 0 || seg->flags == 2); // flags 0 (active, tableidx=0), 2 (active, tableidx!=0)

        if (is_active) {
            if (seg->table_index != 0) { // Supporting only table index 0 for now
                fprintf(stderr, "Error: Element segment %lu targets unsupported table index %lu.\n", 
                         (unsigned long)i, (unsigned long)seg->table_index);
                return ESPB_ERR_INSTANTIATION_FAILED;
            }
            if (seg->element_type != ESPB_REF_TYPE_FUNCREF) {
                 fprintf(stderr, "Error: Element segment %lu has unsupported element type %d (only FUNCREF supported).\n", 
                          (unsigned long)i, seg->element_type);
                 return ESPB_ERR_INSTANTIATION_FAILED;
            }
            if (!instance->table_data && instance->table_size == 0 && seg->num_elements > 0) {
                 fprintf(stderr, "Error: Active element segment %lu has elements, but no table 0 allocated or size is 0.\n", 
                          (unsigned long)i);
                 return ESPB_ERR_INSTANTIATION_FAILED;
            } else if (!instance->table_data && instance->table_size > 0) {
                 fprintf(stderr, "Error: Internal: table_size > 0 but table_data is NULL for elem segment %lu.\n", 
                          (unsigned long)i);
                 return ESPB_ERR_INSTANTIATION_FAILED;
            }

            uint32_t offset;
            res = espb_evaluate_init_expr(instance, seg->offset_expr, seg->offset_expr_len, &offset);
            if (res != ESPB_OK) {
                fprintf(stderr, "Error: Failed to evaluate offset expression for element segment %lu.\n", 
                         (unsigned long)i);
                return res;
            }

            if (seg->num_elements > 0 && !instance->table_data) {
                 fprintf(stderr, "Error: Trying to initialize table for segment %lu but table_data is NULL (size %lu, offset %lu, num_elements %lu).\n", 
                    (unsigned long)i, (unsigned long)instance->table_size, (unsigned long)offset, (unsigned long)seg->num_elements);
                 return ESPB_ERR_INSTANTIATION_FAILED;
            }

            for (uint32_t k = 0; k < seg->num_elements; ++k) {
                uint32_t table_idx_to_write = offset + k;
                if (table_idx_to_write >= instance->table_size) {
                    fprintf(stderr, "Error: Element segment %lu initialization out of table bounds (index %lu >= size %lu).\n",
                             (unsigned long)i, (unsigned long)table_idx_to_write, (unsigned long)instance->table_size);
                    return ESPB_ERR_INSTANTIATION_FAILED;
                }

                uint32_t func_idx_from_segment = seg->function_indices[k];
                // Validate func_idx_from_segment (must be within total functions: imported + local)
                uint32_t num_imported_funcs = 0;
                 for(uint32_t imp_idx = 0; imp_idx < module->num_imports; ++imp_idx) {
                     if (module->imports[imp_idx].kind == ESPB_IMPORT_KIND_FUNC) num_imported_funcs++;
                 }
                 uint32_t total_funcs = num_imported_funcs + module->num_functions;
                 if (func_idx_from_segment >= total_funcs) {
                     fprintf(stderr, "Error: Element segment %lu contains invalid function index %lu (total funcs %lu).\n",
                              (unsigned long)i, (unsigned long)func_idx_from_segment, (unsigned long)total_funcs);
                     return ESPB_ERR_INSTANTIATION_FAILED;
                 }
                // Store the function index itself. The actual call mechanism will resolve this to a callable target.
                instance->table_data[table_idx_to_write] = (void*)(uintptr_t)func_idx_from_segment;
                // ESP_LOGD(TAG, "  Table[%u] = FuncIndex %u from ElemSeg %u", table_idx_to_write, func_idx_from_segment, i);
            }
            if(seg->num_elements > 0) {
                printf("Runtime: Initialized %lu elements in table 0 from segment %lu at offset %lu.\n", 
                        (unsigned long)seg->num_elements, (unsigned long)i, (unsigned long)offset);
            }
        } 
    }
    printf("Runtime: Element segments initialized.\n");
    return ESPB_OK;
}

EspbResult espb_run_start_function(EspbInstance *instance, ExecutionContext *exec_ctx) {
    const EspbModule *module = instance->module;
    if (module->has_start_function) {
        uint32_t start_func_idx = module->start_function_index;
        printf("Runtime: Running start function (index %lu)...\n", (unsigned long)start_func_idx);

        uint32_t num_imported_funcs = 0;
        for(uint32_t imp_idx = 0; imp_idx < module->num_imports; ++imp_idx) {
            if (module->imports[imp_idx].kind == ESPB_IMPORT_KIND_FUNC) num_imported_funcs++;
        }
        uint32_t total_funcs = num_imported_funcs + module->num_functions;
        if (start_func_idx >= total_funcs) {
            fprintf(stderr, "Error: Start function index %lu is out of bounds (total funcs %lu).\n",
                     (unsigned long)start_func_idx, (unsigned long)total_funcs);
            return ESPB_ERR_INVALID_START_SECTION; 
        }

        // Call the start function. Start functions have signature () -> ().
        ESP_LOGW(TAG, "ESPB START FUNCTION: *** EXECUTING START FUNCTION - IF IT FAILS, ALL CLOSURES WILL BE DESTROYED! ***");
        if (exec_ctx) {
            ESP_LOGW(TAG, "ESPB START FUNCTION: start_func_idx=%lu", (unsigned long)start_func_idx);
        }

        EspbResult call_res = espb_call_function(instance, exec_ctx, start_func_idx, NULL, NULL);

        if (call_res != ESPB_OK) {
             ESP_LOGE(TAG, "ESPB START FUNCTION: *** START FUNCTION FAILED - THIS WILL DESTROY ALL CLOSURES! ***");
             ESP_LOGE(TAG, "Error: Start function (index %lu) execution failed with code %ld.",
                     (unsigned long)start_func_idx, (long)call_res);
             return call_res;
        }
        
        ESP_LOGI(TAG, "ESPB START FUNCTION: Start function completed successfully - closures preserved");
        printf("Runtime: Start function executed successfully.\n");
    } else {
        printf("Runtime: No start function defined.\n");
    }
    return ESPB_OK;
}
