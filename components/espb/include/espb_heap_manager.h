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
#ifndef ESPB_HEAP_MANAGER_H
#define ESPB_HEAP_MANAGER_H

#include "espb_interpreter_common_types.h"
#ifdef __cplusplus
extern "C" {
#endif

EspbResult espb_heap_init(EspbInstance *instance, uint32_t heap_start_offset);
void* espb_heap_malloc(EspbInstance *instance, size_t size);
void* espb_heap_malloc_aligned(EspbInstance *instance, size_t size, size_t alignment);
void espb_heap_free(EspbInstance *instance, void* ptr);
void* espb_heap_realloc(EspbInstance *instance, void* ptr, size_t new_size);
void espb_heap_deinit(EspbInstance *instance);

#ifdef __cplusplus
}
#endif

#endif // ESPB_HEAP_MANAGER_H
