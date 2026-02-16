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
#include "espb_runtime_ffi_types.h"

ffi_type* espb_runtime_type_to_ffi_type(EspbValueType t) {
    switch (t) {
        case ESPB_TYPE_VOID: return &ffi_type_void;
        case ESPB_TYPE_I8:   return &ffi_type_sint8;
        case ESPB_TYPE_U8:   return &ffi_type_uint8;
        case ESPB_TYPE_I16:  return &ffi_type_sint16;
        case ESPB_TYPE_U16:  return &ffi_type_uint16;
        case ESPB_TYPE_I32:  return &ffi_type_sint32;
        case ESPB_TYPE_U32:  return &ffi_type_uint32;
        case ESPB_TYPE_I64:  return &ffi_type_sint64;
        case ESPB_TYPE_U64:  return &ffi_type_uint64;
        case ESPB_TYPE_F32:  return &ffi_type_float;
        case ESPB_TYPE_F64:  return &ffi_type_double;
        case ESPB_TYPE_PTR:  return &ffi_type_pointer;
        case ESPB_TYPE_BOOL: return &ffi_type_sint32;
        default: return NULL;
    }
}
