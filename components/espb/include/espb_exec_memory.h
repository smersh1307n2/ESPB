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
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate executable memory for JIT code.
 * Uses strict heap capabilities to ensure the returned pointer is executable.
 */
void *espb_exec_alloc(size_t size);

/**
 * Reallocate executable memory for JIT code.
 * The returned pointer (if non-NULL) is executable.
 */
void *espb_exec_realloc(void *ptr, size_t size);

/**
 * Free executable memory allocated by espb_exec_alloc/espb_exec_realloc.
 */
void espb_exec_free(void *ptr);

#ifdef __cplusplus
}
#endif
