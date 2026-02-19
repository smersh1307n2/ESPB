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
#include "espb_exec_memory.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include <string.h>

static const char *TAG = "espb_exec_mem";

static inline void log_ptr_region(const void *p)
{
    ESP_LOGE(TAG,
             "ptr=%p exec=%d iram=%d rom=%d dram=%d",
             p,
             esp_ptr_executable(p),
             esp_ptr_in_iram(p),
             esp_ptr_in_rom(p),
             esp_ptr_in_dram(p));
}

static void *alloc_checked(size_t size, uint32_t caps, const char *caps_name)
{
    void *p = heap_caps_malloc(size, caps);
    if (!p)
    {
        ESP_LOGW(TAG,
                 "heap_caps_malloc(size=%u, caps=%s) failed; free(exec)=%u largest(exec)=%u free(internal)=%u largest(internal)=%u",
                 (unsigned)size,
                 caps_name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return NULL;
    }

    // Note: esp_ptr_executable() may return false on some targets (e.g. ESP32-C6 with memprot
    // disabled) even when the memory IS executable. We trust heap_caps_malloc with EXEC caps
    // to return executable memory and skip the double-check to avoid false negatives.
#if 0
    if (!esp_ptr_executable(p))
    {
        ESP_LOGE(TAG, "Allocated memory is not executable (size=%u, caps=%s)", (unsigned)size, caps_name);
        log_ptr_region(p);
        heap_caps_free(p);
        return NULL;
    }
#endif

    ESP_LOGD(TAG, "Allocated exec buffer %p size=%u caps=%s free(exec)=%u largest(exec)=%u",
             p,
             (unsigned)size,
             caps_name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));

    return p;
}

void *espb_exec_alloc(size_t size)
{
    // Preferred: internal, 32-bit addressable executable memory (IRAM).
    void *p = alloc_checked(size, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT,
                            "EXEC|INTERNAL|32BIT");
    if (p) return p;

    p = alloc_checked(size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT, "EXEC|32BIT");
    if (p) return p;

    p = alloc_checked(size, MALLOC_CAP_EXEC, "EXEC");
    if (p) return p;

#ifdef MALLOC_CAP_IRAM_8BIT
    // ESP32-C6: IRAM can be allocated with IRAM_8BIT capability (32-bit aligned access).
    p = alloc_checked(size, MALLOC_CAP_IRAM_8BIT | MALLOC_CAP_32BIT, "IRAM_8BIT|32BIT");
    if (p) return p;
#endif

#ifdef MALLOC_CAP_TCM
    p = alloc_checked(size, MALLOC_CAP_TCM, "TCM");
    if (p) return p;
#endif

    return NULL;
}

void *espb_exec_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    if (!p) {
        p = espb_exec_alloc(size);
        if (!p) return NULL;
        if (ptr) {
            memcpy(p, ptr, size);
            heap_caps_free(ptr);
        }
    }
    return p;
}

void espb_exec_free(void *ptr)
{
    if (ptr)
        heap_caps_free(ptr);
}
