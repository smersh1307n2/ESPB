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

#ifndef FFI_TRAMP_H
#define FFI_TRAMP_H

#ifdef __cplusplus
extern "C" {
#endif

int ffi_tramp_is_supported(void);
void *ffi_tramp_alloc (int flags);
void ffi_tramp_set_parms (void *tramp, void *data, void *code);
void *ffi_tramp_get_addr (void *tramp);
void ffi_tramp_free (void *tramp);

#ifdef __cplusplus
}
#endif

#endif /* FFI_TRAMP_H */
