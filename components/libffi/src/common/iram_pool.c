/*
 * This file is part of the modified libffi library.
 *
 * Original libffi - Copyright (c) 1996-2025 Anthony Green, Red Hat, Inc and others.
 * See LICENSE-libffi.txt for the original license terms.
 *
 * Modifications for espb - Copyright (C) 2025 Smersh.
 *
 * This modified program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "iram_pool.h"
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "IRAM_POOL_SHIM";

void iram_pool_init(void) {
   ESP_LOGD(TAG, "Shim: iram_pool_init() called. Using heap_caps for executable memory.");
}

void* iram_pool_alloc(size_t size) {
   ESP_LOGD(TAG, "Shim: iram_pool_alloc(%d) -> heap_caps_malloc(MALLOC_CAP_EXEC)", size);
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_EXEC);//| MALLOC_CAP_32BIT);
    if (!ptr) {
        ESP_LOGE(TAG, "heap_caps_malloc failed to allocate %d bytes of executable memory", size);
    } else {
       ESP_LOGD(TAG, "Allocated %d bytes at %p (executable)", size, ptr);
    }
    return ptr;
}

void iram_pool_free(void* ptr) {
   ESP_LOGD(TAG, "Shim: iram_pool_free(%p)", ptr);
    heap_caps_free(ptr);
}

void iram_pool_debug(void) {
   ESP_LOGD(TAG, "Shim: iram_pool_debug() called.");
   ESP_LOGD(TAG, "Free executable memory: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_EXEC));
    heap_caps_dump(MALLOC_CAP_EXEC);
}

size_t iram_pool_get_total_size(void) {
    return heap_caps_get_total_size(MALLOC_CAP_EXEC);
}

size_t iram_pool_get_used_size(void) {
    return heap_caps_get_total_size(MALLOC_CAP_EXEC) - heap_caps_get_free_size(MALLOC_CAP_EXEC);
}

size_t iram_pool_get_free_size(void) {
    return heap_caps_get_free_size(MALLOC_CAP_EXEC);
}