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
#include "espb_jit_helpers.h"

#include "espb_runtime_alloca.h"
#include "espb_heap_manager.h"
#include "esp_log.h"

#include <string.h>
#include <inttypes.h>

#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
static const char *TAG = "espb_jit_alloca";
#endif

// JIT helper for ALLOCA opcode (0x8F).
// Shared implementation lives in espb_runtime_alloca.c.
void espb_jit_alloca_ex(EspbInstance *instance, Value *v_regs, uint16_t num_regs_allocated,
                        uint8_t rd, uint8_t rs_size, uint8_t align)
{
    if (!instance || !v_regs) {
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
        ESP_LOGE(TAG, "null JIT ALLOCA argument");
#endif
        return;
    }
    if (rd >= num_regs_allocated || rs_size >= num_regs_allocated) {
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
        ESP_LOGE(TAG, "register bounds failure: rd=%u rs=%u count=%u",
                 (unsigned)rd, (unsigned)rs_size, (unsigned)num_regs_allocated);
#endif
        return;
    }

#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
    ESP_LOGI(TAG, "[JIT_ALLOCA_ENTER] instance=%p regs=%p rd=%u rs=%u count=%u size=%" PRId32 " align=%u",
             (void *)instance, (void *)v_regs, (unsigned)rd, (unsigned)rs_size,
             (unsigned)num_regs_allocated, V_I32(v_regs[rs_size]), (unsigned)align);
#endif
    EspbResult result = espb_runtime_alloca(instance, NULL, v_regs,
                                            num_regs_allocated, rd, rs_size, align);
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
    ESP_LOGI(TAG, "[JIT_ALLOCA_RESULT] rc=%d rd=%u ptr=%p",
             (int)result, (unsigned)rd,
             result == ESPB_OK ? V_PTR(v_regs[rd]) : NULL);
#endif
}


__attribute__((noinline))
void* espb_jit_heap_calloc(EspbInstance *instance, uint32_t count, uint32_t size)
{
    size_t total;
    if (__builtin_mul_overflow((size_t)count, (size_t)size, &total)) {
        return NULL;
    }

    void *ptr = espb_heap_malloc(instance, total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void espb_jit_alloca(EspbInstance *instance, Value *v_regs, uint8_t rd, uint8_t rs_size, uint8_t align) {
    // Backward-compatible wrapper
    espb_jit_alloca_ex(instance, v_regs, 256, rd, rs_size, align);
}
