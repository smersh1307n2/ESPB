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
/**
 * @file espb_jit_cache.c
 * @brief JIT Cache - хранилище скомпилированных функций
 */

#include "espb_jit.h"
#include "esp_log.h"
#include "esp_heap_caps.h"  // heap_caps_free for MALLOC_CAP_EXEC allocations
#include <stdlib.h>
#include <string.h>

#ifndef ESPB_JIT_DEBUG
#define ESPB_JIT_DEBUG 0
#endif

#if ESPB_JIT_DEBUG
#define JIT_LOGI ESP_LOGI
#define JIT_LOGD ESP_LOGD
#define JIT_LOGW ESP_LOGW
#else
#define JIT_LOGI(tag, fmt, ...) ((void)0)
#define JIT_LOGD(tag, fmt, ...) ((void)0)
#define JIT_LOGW(tag, fmt, ...) ((void)0)
#endif

static const char *TAG = "espb_jit_cache";

/**
 * @brief Инициализирует JIT cache
 */
EspbResult espb_jit_cache_init(EspbJitCache* cache, size_t capacity) {
    if (!cache || capacity == 0) {
        return ESPB_ERR_INVALID_OPERAND; // Используем существующую константу
    }

    cache->entries = (EspbJitCacheEntry*)calloc(capacity, sizeof(EspbJitCacheEntry));
    if (!cache->entries) {
        ESP_LOGE(TAG, "Failed to allocate JIT cache entries");
        return ESPB_ERR_OUT_OF_MEMORY;
    }

    cache->capacity = capacity;
    cache->count = 0;

    JIT_LOGI(TAG, "JIT cache initialized with capacity=%zu", capacity);
    return ESPB_OK;
}

/**
 * @brief Освобождает JIT cache
 */
void espb_jit_cache_free(EspbJitCache* cache) {
    if (!cache || !cache->entries) {
        return;
    }

    // Освобождаем все скомпилированные JIT-коды (выделены из heap_caps с MALLOC_CAP_EXEC)
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].is_valid && cache->entries[i].jit_code) {
            // JIT code is allocated from executable-capable heap (MALLOC_CAP_EXEC), so free via heap_caps_free.
            heap_caps_free(cache->entries[i].jit_code);
        }
    }

    free(cache->entries);
    cache->entries = NULL;
    cache->capacity = 0;
    cache->count = 0;

    JIT_LOGI(TAG, "JIT cache freed");
}

/**
 * @brief Ищет скомпилированную функцию в cache
 * @return Указатель на JIT-код или NULL, если не найдено
 */
void* espb_jit_cache_lookup(EspbJitCache* cache, uint32_t func_idx) {
    if (!cache || !cache->entries) {
        return NULL;
    }

    // Линейный поиск (для небольших cache это приемлемо)
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].is_valid && cache->entries[i].func_idx == func_idx) {
            JIT_LOGD(TAG, "Cache HIT: func_idx=%u", func_idx);
            return cache->entries[i].jit_code;
        }
    }

    JIT_LOGD(TAG, "Cache MISS: func_idx=%u", func_idx);
    return NULL;
}

/**
 * @brief Добавляет скомпилированную функцию в cache
 */
EspbResult espb_jit_cache_insert(EspbJitCache* cache, uint32_t func_idx, void* jit_code, size_t code_size) {
    if (!cache || !cache->entries || !jit_code) {
        return ESPB_ERR_INVALID_OPERAND; // Используем существующую константу
    }

    // Проверяем, не переполнен ли cache
    if (cache->count >= cache->capacity) {
        JIT_LOGW(TAG, "JIT cache is full (capacity=%zu), cannot insert func_idx=%u", 
                 cache->capacity, func_idx);
        return ESPB_ERR_OUT_OF_MEMORY;
    }

    // Проверяем, нет ли уже этой функции в cache
    if (espb_jit_cache_lookup(cache, func_idx) != NULL) {
        JIT_LOGD(TAG, "Function func_idx=%u already in cache, skipping", func_idx);
        return ESPB_OK;
    }

    // Добавляем новую запись
    EspbJitCacheEntry* entry = &cache->entries[cache->count];
    entry->func_idx = func_idx;
    entry->jit_code = jit_code;
    entry->code_size = code_size;
    entry->is_valid = true;

    cache->count++;

    JIT_LOGI(TAG, "Inserted func_idx=%u into cache (code_size=%zu, total=%zu/%zu)", 
             func_idx, code_size, cache->count, cache->capacity);
    return ESPB_OK;
}

/**
 * @brief Удаляет запись из cache
 */
void espb_jit_cache_remove(EspbJitCache* cache, uint32_t func_idx) {
    if (!cache || !cache->entries) {
        return;
    }

    for (size_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].is_valid && cache->entries[i].func_idx == func_idx) {
            // Освобождаем скомпилированный код
            if (cache->entries[i].jit_code) {
                // See espb_jit_cache_free(): JIT code must be freed with heap_caps_free.
                heap_caps_free(cache->entries[i].jit_code);
            }

            // Сдвигаем остальные записи
            if (i < cache->count - 1) {
                memmove(&cache->entries[i], &cache->entries[i + 1], 
                        (cache->count - i - 1) * sizeof(EspbJitCacheEntry));
            }

            cache->count--;
            JIT_LOGI(TAG, "Removed func_idx=%u from cache", func_idx);
            return;
        }
    }

    JIT_LOGD(TAG, "func_idx=%u not found in cache", func_idx);
}
