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

#if defined(CONFIG_IDF_TARGET_ESP32C3)
static uint32_t s_aligned_alloc_seq;
#endif

EspbResult espb_heap_init(EspbInstance *instance, uint32_t heap_start_offset) {
    uint32_t aligned_offset = (heap_start_offset + 7) & ~7;
    
    if (aligned_offset >= instance->memory_size_bytes) {
#ifdef CONFIG_ESPB_DEBUG_CHECKS
         ESP_LOGW(TAG, "No space available for heap after static data.");
#endif
         instance->heap_ctx.heap_handle = NULL;
         instance->heap_ctx.initialized = true;
         return ESPB_OK;
    }

    uint8_t *heap_base = instance->memory_data + aligned_offset;
    size_t heap_size = instance->memory_size_bytes - aligned_offset;

    instance->heap_ctx.heap_handle = multi_heap_register(heap_base, heap_size);
    if (!instance->heap_ctx.heap_handle) {
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        ESP_LOGE(TAG, "multi_heap_register failed for region at %p, size %zu", (void*)heap_base, heap_size);
#endif
        return ESPB_ERR_RUNTIME_ERROR;
    }

    instance->heap_ctx.initialized = true;
    return ESPB_OK;
}

void* espb_heap_malloc(EspbInstance *instance, size_t size) {
    if (__builtin_expect(size == 0, 0)) {
        return NULL;
    }
    
    void *ptr = multi_heap_malloc(instance->heap_ctx.heap_handle, size);
    
    if (__builtin_expect(ptr == NULL, 0)) {
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        ESP_LOGW(TAG, "Malloc failed: size=%zu. Heap may be full.", size);
#endif
        return NULL;
    }
    
    uintptr_t abs_ptr = (uintptr_t)ptr;
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    
    if (__builtin_expect(abs_ptr < memory_base || abs_ptr >= memory_base + instance->memory_size_bytes, 0)) {
#ifdef CONFIG_ESPB_DEBUG_CHECKS
        ESP_LOGE(TAG, "Heap malloc returned pointer outside linear memory! ptr=%p", ptr);
#endif
        multi_heap_free(instance->heap_ctx.heap_handle, ptr);
        return NULL;
    }
    
    return ptr;
}

void* espb_heap_malloc_aligned(EspbInstance *instance, size_t size, size_t alignment) {
    if (__builtin_expect(size == 0 || (alignment & (alignment - 1)) != 0, 0)) {
        return NULL;
    }
    
    if (alignment <= 4) {
        return espb_heap_malloc(instance, size);
    }
    
    size_t total_size = size + alignment - 1 + sizeof(void*);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    const uint32_t alloc_seq = ++s_aligned_alloc_seq;
    if (!multi_heap_check(instance->heap_ctx.heap_handle, false)) {
        ESP_LOGE(TAG, "C3 aligned alloc #%u: heap corrupt BEFORE malloc; heap=%p size=%u align=%u total=%u",
                 (unsigned)alloc_seq, instance->heap_ctx.heap_handle,
                 (unsigned)size, (unsigned)alignment, (unsigned)total_size);
        return NULL;
    }
#endif
    void *raw_ptr = multi_heap_malloc(instance->heap_ctx.heap_handle, total_size);
    
    if (__builtin_expect(raw_ptr == NULL, 0)) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
        ESP_LOGE(TAG, "C3 aligned alloc #%u failed: heap=%p size=%u align=%u total=%u",
                 (unsigned)alloc_seq, instance->heap_ctx.heap_handle,
                 (unsigned)size, (unsigned)alignment, (unsigned)total_size);
#endif
        return NULL;
    }
    
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    
    void **orig_storage = (void**)(aligned_addr - sizeof(void*));
    *orig_storage = raw_ptr;
    
    void *aligned_ptr = (void*)aligned_addr;
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    if (!multi_heap_check(instance->heap_ctx.heap_handle, false)) {
        ESP_LOGE(TAG, "C3 aligned alloc #%u: heap corrupt AFTER malloc/header; raw=%p aligned=%p size=%u align=%u total=%u",
                 (unsigned)alloc_seq, raw_ptr, aligned_ptr,
                 (unsigned)size, (unsigned)alignment, (unsigned)total_size);
        return NULL;
    }
#endif
    
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    if (__builtin_expect(aligned_addr < memory_base || aligned_addr >= memory_base + instance->memory_size_bytes, 0)) {
        multi_heap_free(instance->heap_ctx.heap_handle, raw_ptr);
        return NULL;
    }
    
    return aligned_ptr;
}

void espb_heap_free(EspbInstance *instance, void* ptr) {
    if (__builtin_expect(ptr == NULL, 0)) {
        return;
    }
    
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t memory_base = (uintptr_t)instance->memory_data;
    uintptr_t heap_start = memory_base + instance->static_data_end_offset;
    
    if ((ptr_addr & 7) == 0 && ptr_addr >= heap_start) {
        void **orig_storage = (void**)(ptr_addr - sizeof(void*));
        uintptr_t orig_addr = (uintptr_t)(*orig_storage);
        
        if (orig_addr >= heap_start && orig_addr < memory_base + instance->memory_size_bytes && 
            orig_addr < ptr_addr) {
            multi_heap_free(instance->heap_ctx.heap_handle, *orig_storage);
            return;
        }
    }
    
    multi_heap_free(instance->heap_ctx.heap_handle, ptr);
}

void* espb_heap_realloc(EspbInstance *instance, void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return espb_heap_malloc(instance, new_size);
    }
    if (__builtin_expect(new_size == 0, 0)) {
        espb_heap_free(instance, ptr);
        return NULL;
    }

    void *new_ptr = multi_heap_realloc(instance->heap_ctx.heap_handle, ptr, new_size);
    return new_ptr;
}

void espb_heap_deinit(EspbInstance *instance) {
    if (instance && instance->heap_ctx.initialized) {
        instance->heap_ctx.initialized = false;
    }
}