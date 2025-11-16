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

#ifndef SAFE_MEMORY_H
#define SAFE_MEMORY_H

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *SAFE_MEM_TAG = "SAFE_MEM";

// Безопасный malloc с проверкой и логированием
static inline void* safe_malloc(size_t size) {
    if (size == 0) {
        ESP_LOGW(SAFE_MEM_TAG, "Attempting to malloc 0 bytes");
        return NULL;
    }
    
    void *ptr = malloc(size);
    if (!ptr) {
        ESP_LOGE(SAFE_MEM_TAG, "malloc failed for %d bytes. Free heap: %d", size, esp_get_free_heap_size());
        // Попробуем принудительную сборку мусора
        heap_caps_check_integrity_all(true);
        ptr = malloc(size);
        if (!ptr) {
            ESP_LOGE(SAFE_MEM_TAG, "malloc failed even after heap check");
        }
    } else {
        ESP_LOGD(SAFE_MEM_TAG, "malloc success: %d bytes at %p. Free heap: %d", size, ptr, esp_get_free_heap_size());
    }
    return ptr;
}

// Безопасный calloc с проверкой и логированием
static inline void* safe_calloc(size_t num, size_t size) {
    if (num == 0 || size == 0) {
        ESP_LOGW(SAFE_MEM_TAG, "Attempting to calloc 0 elements or 0 size");
        return NULL;
    }
    
    size_t total_size = num * size;
    void *ptr = calloc(num, size);
    if (!ptr) {
        ESP_LOGE(SAFE_MEM_TAG, "calloc failed for %d*%d=%d bytes. Free heap: %d", num, size, total_size, esp_get_free_heap_size());
        // Попробуем принудительную сборку мусора
        heap_caps_check_integrity_all(true);
        ptr = calloc(num, size);
        if (!ptr) {
            ESP_LOGE(SAFE_MEM_TAG, "calloc failed even after heap check");
        }
    } else {
        ESP_LOGD(SAFE_MEM_TAG, "calloc success: %d*%d=%d bytes at %p. Free heap: %d", num, size, total_size, ptr, esp_get_free_heap_size());
    }
    return ptr;
}

// Безопасный heap_caps_malloc с проверкой
static inline void* safe_heap_caps_malloc(size_t size, uint32_t caps) {
    if (size == 0) {
        ESP_LOGW(SAFE_MEM_TAG, "Attempting to heap_caps_malloc 0 bytes");
        return NULL;
    }
    
    void *ptr = heap_caps_malloc(size, caps);
    if (!ptr) {
        ESP_LOGE(SAFE_MEM_TAG, "heap_caps_malloc failed for %d bytes with caps 0x%X", size, caps);
        ESP_LOGE(SAFE_MEM_TAG, "Free heap (MALLOC_CAP_DEFAULT): %d", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
        ESP_LOGE(SAFE_MEM_TAG, "Free heap (MALLOC_CAP_EXEC): %d", heap_caps_get_free_size(MALLOC_CAP_EXEC));
        ESP_LOGE(SAFE_MEM_TAG, "Free heap (MALLOC_CAP_32BIT): %d", heap_caps_get_free_size(MALLOC_CAP_32BIT));
        
        // Попробуем принудительную сборку мусора
        heap_caps_check_integrity_all(true);
        ptr = heap_caps_malloc(size, caps);
        if (!ptr) {
            ESP_LOGE(SAFE_MEM_TAG, "heap_caps_malloc failed even after heap check");
        }
    } else {
        ESP_LOGD(SAFE_MEM_TAG, "heap_caps_malloc success: %d bytes at %p with caps 0x%X", size, ptr, caps);
    }
    return ptr;
}

// Макросы для замены стандартных функций
#define SAFE_MALLOC(size) safe_malloc(size)
#define SAFE_CALLOC(num, size) safe_calloc(num, size)
#define SAFE_HEAP_CAPS_MALLOC(size, caps) safe_heap_caps_malloc(size, caps)

#endif // SAFE_MEMORY_H