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
#include "esp_log.h"

static const char *TAG = "espb_jit_alloca";

// JIT helper for ALLOCA opcode (0x8F).
// Shared implementation lives in espb_runtime_alloca.c.
void espb_jit_alloca_ex(EspbInstance *instance, Value *v_regs, uint16_t num_regs_allocated,
                        uint8_t rd, uint8_t rs_size, uint8_t align)
{
    if ((uintptr_t)v_regs < 0x10000u) {
        ESP_LOGE(TAG, "[JIT_ALLOCA_BAD] instance=%p regs=%p count=%u rd=%u rs=%u align=%u",
                 (void *)instance, (void *)v_regs, (unsigned)num_regs_allocated,
                 (unsigned)rd, (unsigned)rs_size, (unsigned)align);
        return;
    }
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
    ESP_LOGI(TAG, "ENTER instance=%p regs=%p count=%u rd=%u rs=%u align=%u",
             (void *)instance, (void *)v_regs, (unsigned)num_regs_allocated,
             (unsigned)rd, (unsigned)rs_size, (unsigned)align);
#endif
    if (!instance || !v_regs) {
        ESP_LOGE(TAG, "null JIT ALLOCA argument");
        return;
    }
    if (rd >= num_regs_allocated || rs_size >= num_regs_allocated) {
        ESP_LOGE(TAG, "register bounds failure: rd=%u rs=%u count=%u",
                 (unsigned)rd, (unsigned)rs_size, (unsigned)num_regs_allocated);
        return;
    }
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
    ESP_LOGI(TAG, "size=%u", (unsigned)V_I32(v_regs[rs_size]));
#endif
    ESP_LOGI(TAG, "[JIT_ALLOCA_CTX] instance=%p regs=%p heap=%p initialized=%u mem=%p mem_size=%u count=%u rd=%u rs=%u size=%u align=%u",
             (void *)instance, (void *)v_regs,
             instance ? (void *)instance->heap_ctx.heap_handle : NULL,
             instance ? (unsigned)instance->heap_ctx.initialized : 0u,
             instance ? (void *)instance->memory_data : NULL,
             instance ? (unsigned)instance->memory_size_bytes : 0u,
             (unsigned)num_regs_allocated, (unsigned)rd, (unsigned)rs_size,
             (unsigned)V_I32(v_regs[rs_size]), (unsigned)align);
    EspbResult result = espb_runtime_alloca(instance, NULL, v_regs,
                                            num_regs_allocated, rd, rs_size, align);
#if defined(ESPB_JIT_ALLOCA_TRACE) && ESPB_JIT_ALLOCA_TRACE
    ESP_LOGI(TAG, "EXIT result=%d allocated=%p", (int)result, V_PTR(v_regs[rd]));
#endif
}

void espb_jit_alloca(EspbInstance *instance, Value *v_regs, uint8_t rd, uint8_t rs_size, uint8_t align) {
    // Backward-compatible wrapper
    espb_jit_alloca_ex(instance, v_regs, 256, rd, rs_size, align);
}

