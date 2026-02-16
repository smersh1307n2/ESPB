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
#include "espb_heap_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "espb_heap";

EspbResult espb_heap_init(EspbInstance *instance, uint32_t heap_start_offset) {
    ESP_LOGD(TAG, "=== HEAP_INIT DEBUG === offset=%u", heap_start_offset);
    ESP_LOGD(TAG, "instance=%p, memory_size=%u", instance, instance ? instance->memory_size_bytes : 0);
    
    if (!instance || instance->heap_ctx.initialized) {
        ESP_LOGE(TAG, "HEAP_INIT FAILED: invalid state");
        return ESPB_ERR_INVALID_STATE;
    }

    uint32_t aligned_offset = (heap_start_offset + 7) & ~7;
    
    if (aligned_offset >= instance->memory_size_bytes) {
         ESP_LOGW(TAG, "No space available for heap after static data.");
         instance->heap_ctx.heap_handle = NULL;
         instance->heap_ctx.initialized = true; // Mark as initialized (but empty)
         return ESPB_OK;
    }

    uint8_t *heap_base = instance->memory_data + aligned_offset;
    size_t heap_size = instance->memory_size_bytes - aligned_offset;

    instance->heap_ctx.heap_handle = multi_heap_register(heap_base, heap_size);
    if (!instance->heap_ctx.heap_handle) {
        ESP_LOGE(TAG, "multi_heap_register failed for region at %p, size %zu", (void*)heap_base, heap_size);
        return ESPB_ERR_RUNTIME_ERROR;
    }

    instance->heap_ctx.initialized = true;
    ESP_LOGD(TAG, "Heap initialized. Base: %p, Size: %zu bytes", (void*)heap_base, heap_size);
    return ESPB_OK;
}

void* espb_heap_malloc(EspbInstance *instance, size_t size) {
    ESP_LOGD(TAG, "=== HEAP_MALLOC DEBUG === size=%zu", size);
    ESP_LOGD(TAG, "initialized=%d, handle=%p", instance ? instance->heap_ctx.initialized : -1, instance ? instance->heap_ctx.heap_handle : NULL);
    
    ESP_LOGD(TAG, "Checking conditions: initialized=%d, size=%zu, handle=%p", 
             instance->heap_ctx.initialized, size, instance->heap_ctx.heap_handle);
    
    if (!instance->heap_ctx.initialized) {
        ESP_LOGE(TAG, "HEAP_MALLOC FAILED: heap not initialized");
        return NULL;
    }
    if (size == 0) {
        ESP_LOGE(TAG, "HEAP_MALLOC FAILED: size is 0");
        return NULL;
    }
    if (!instance->heap_ctx.heap_handle) {
        ESP_LOGE(TAG, "HEAP_MALLOC FAILED: handle is NULL");
        return NULL;
    }

    ESP_LOGD(TAG, "About to call multi_heap_malloc with handle=%p, size=%zu", instance->heap_ctx.heap_handle, size);
    
    void *ptr = multi_heap_malloc(instance->heap_ctx.heap_handle, size);
    
    ESP_LOGD(TAG, "multi_heap_malloc returned: %p", ptr);
    
    if (ptr == NULL) {
        ESP_LOGW(TAG, "Malloc failed: size=%zu. Heap may be full.", size);
        // NOTE: Dynamic expansion is not implemented in this version for simplicity.
        // It would require a more complex memory management with multiple regions.
        return NULL;
    }
    
    // ИСПРАВЛЕНИЕ: Проверяем что указатель находится внутри линейной памяти
    uintptr_t abs_ptr = (uintptr_t)ptr;
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    
    if (abs_ptr < memory_base || abs_ptr >= memory_base + instance->memory_size_bytes) {
        ESP_LOGE(TAG, "Heap malloc returned pointer outside linear memory! ptr=%p, memory_base=%p, memory_size=%u", 
                 ptr, (void*)memory_base, instance->memory_size_bytes);
        multi_heap_free(instance->heap_ctx.heap_handle, ptr);
        return NULL;
    }
    
    ESP_LOGD(TAG, "SUCCESS: malloc size=%zu -> ptr=%p (offset=%u)", size, ptr, (uint32_t)(abs_ptr - memory_base));
    return ptr;
}

void* espb_heap_malloc_aligned(EspbInstance *instance, size_t size, size_t alignment) {
    ESP_LOGD(TAG, "=== HEAP_MALLOC_ALIGNED DEBUG === size=%zu, alignment=%zu", size, alignment);
    
    if (!instance->heap_ctx.initialized) {
        ESP_LOGE(TAG, "HEAP_MALLOC_ALIGNED FAILED: heap not initialized");
        return NULL;
    }
    if (size == 0) {
        ESP_LOGE(TAG, "HEAP_MALLOC_ALIGNED FAILED: size is 0");
        return NULL;
    }
    if (!instance->heap_ctx.heap_handle) {
        ESP_LOGE(TAG, "HEAP_MALLOC_ALIGNED FAILED: handle is NULL");
        return NULL;
    }
    
    // Проверяем что alignment является степенью двойки
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        ESP_LOGE(TAG, "HEAP_MALLOC_ALIGNED FAILED: invalid alignment %zu", alignment);
        return NULL;
    }
    
    // Если alignment <= 4, используем обычный malloc (multi_heap дает выравнивание по 4 байта)
    if (alignment <= 4) {
        return espb_heap_malloc(instance, size);
    }
    
    // Для большего выравнивания выделяем память с запасом
    size_t total_size = size + alignment - 1 + sizeof(void*);
    void *raw_ptr = multi_heap_malloc(instance->heap_ctx.heap_handle, total_size);
    
    if (raw_ptr == NULL) {
        ESP_LOGW(TAG, "Aligned malloc failed: size=%zu, alignment=%zu", size, alignment);
        return NULL;
    }
    
    // Вычисляем выровненный адрес
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    
    // Сохраняем оригинальный указатель перед выровненным для освобождения
    void **orig_storage = (void**)(aligned_addr - sizeof(void*));
    *orig_storage = raw_ptr;
    
    void *aligned_ptr = (void*)aligned_addr;
    
    // Проверяем что указатель находится в пределах линейной памяти
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    if (aligned_addr < memory_base || aligned_addr >= memory_base + instance->memory_size_bytes) {
        ESP_LOGE(TAG, "Aligned malloc returned pointer outside linear memory!");
        multi_heap_free(instance->heap_ctx.heap_handle, raw_ptr);
        return NULL;
    }
    
    ESP_LOGD(TAG, "SUCCESS: aligned malloc size=%zu align=%zu -> raw=%p aligned=%p", 
             size, alignment, raw_ptr, aligned_ptr);
    return aligned_ptr;
}

void espb_heap_free(EspbInstance *instance, void* ptr) {
    ESP_LOGD(TAG, "[HEAP_FREE] instance=%p ptr=%p", (void*)instance, ptr);
    if (!instance->heap_ctx.initialized || ptr == NULL || !instance->heap_ctx.heap_handle) {
        ESP_LOGD(TAG, "[HEAP_FREE] early return: init=%d ptr=%p handle=%p", 
                 instance->heap_ctx.initialized, ptr, instance->heap_ctx.heap_handle);
        return;
    }
    
    // Проверяем, является ли это выровненным указателем
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    uintptr_t heap_start = memory_base + instance->static_data_end_offset;
    
    // Если указатель выровнен по границе > 4 байт, возможно это aligned allocation
    if ((ptr_addr & 7) == 0 && ptr_addr >= heap_start) {
        // Пытаемся получить оригинальный указатель
        void **orig_storage = (void**)(ptr_addr - sizeof(void*));
        uintptr_t orig_addr = (uintptr_t)(*orig_storage);
        
        // Проверяем что оригинальный указатель выглядит разумно (в пределах heap области)
        if (orig_addr >= heap_start && orig_addr < memory_base + instance->memory_size_bytes && 
            orig_addr < ptr_addr) {
            // Это похоже на aligned allocation - освобождаем оригинальный указатель
            ESP_LOGD(TAG, "Free aligned: ptr=%p -> original=%p", ptr, *orig_storage);
            multi_heap_free(instance->heap_ctx.heap_handle, *orig_storage);
            return;
        }
    }
    
    // Обычное освобождение
    ESP_LOGD(TAG, "Free: ptr=%p", ptr);
    multi_heap_free(instance->heap_ctx.heap_handle, ptr);
}

void* espb_heap_realloc(EspbInstance *instance, void* ptr, size_t new_size) {
    if (!instance->heap_ctx.initialized || !instance->heap_ctx.heap_handle) {
        return NULL;
    }
    if (ptr == NULL) {
        return espb_heap_malloc(instance, new_size);
    }
    if (new_size == 0) {
        espb_heap_free(instance, ptr);
        return NULL;
    }

    void *new_ptr = multi_heap_realloc(instance->heap_ctx.heap_handle, ptr, new_size);
    if (new_ptr == NULL) {
        ESP_LOGW(TAG, "Realloc failed: ptr=%p, new_size=%zu. Heap may be full.", ptr, new_size);
    }

    ESP_LOGD(TAG, "Realloc: ptr=%p, new_size=%zu -> new_ptr=%p", ptr, new_size, new_ptr);
    return new_ptr;
}

void espb_heap_deinit(EspbInstance *instance) {
    if (instance && instance->heap_ctx.initialized) {
        instance->heap_ctx.initialized = false;
        // multi_heap_unregister is not public. The memory will be freed with the instance->memory_data block.
        ESP_LOGD(TAG, "Heap deinitialized.");
    }
}