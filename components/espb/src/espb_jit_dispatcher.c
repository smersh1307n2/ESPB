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
#include "espb_jit_dispatcher.h"
#include "espb_interpreter_common_types.h"
#include "espb_jit_compiler.h"
#include "espb_jit_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

#if CONFIG_ESPB_JIT_ENABLED

// Forward declarations / full implementation will be restored from known-good base
// This is a temporary minimal stub to keep the tree building while we push the full file.
EspbResult execute_jit_code(EspbInstance *instance, void *jit_code, const Value *args, uint8_t num_args, Value *results, uint16_t num_virtual_regs);

#endif
