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
 * This the hope that it program is distributed in will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LIBFFI_TARGET_H
#define LIBFFI_TARGET_H

#ifndef LIBFFI_H
#error "Please do not include ffitarget.h directly into your source.  Use ffi.h instead."
#endif

#ifndef __riscv
#error "libffi was configured for a RISC-V target but this does not appear to be a RISC-V compiler."
#endif

#ifndef LIBFFI_ASM

typedef unsigned long ffi_arg;
typedef   signed long ffi_sarg;

typedef enum ffi_abi {
    FFI_FIRST_ABI = 0,
    FFI_SYSV,
    /* Оставляем только необходимый минимум ABI для ESP-IDF */
    FFI_LAST_ABI,

    FFI_DEFAULT_ABI = FFI_SYSV
} ffi_abi;

#endif /* LIBFFI_ASM */

/* ---- Definitions for closures ----------------------------------------- */

#define FFI_CLOSURES 1
#define FFI_GO_CLOSURES 1
#define FFI_TRAMPOLINE_SIZE 24
#define FFI_NATIVE_RAW_API 0

/*
 * Отказываемся от кастомных полей. Вместо этого для хранения
 * количества фиксированных аргументов будет использоваться
 * стандартное поле cif->flags.
 */
#define FFI_EXTRA_CIF_FIELDS
#define FFI_TARGET_SPECIFIC_VARIADIC 1

#endif 