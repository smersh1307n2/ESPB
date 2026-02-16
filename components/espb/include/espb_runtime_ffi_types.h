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
#ifndef ESPB_RUNTIME_FFI_TYPES_H
#define ESPB_RUNTIME_FFI_TYPES_H

#include "ffi.h"
#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map ESPB value type to libffi type.
 *
 * JIT/backends helper: kept out of interpreter hot TU.
 */
ffi_type* espb_runtime_type_to_ffi_type(EspbValueType t);

#ifdef __cplusplus
}
#endif

#endif // ESPB_RUNTIME_FFI_TYPES_H
