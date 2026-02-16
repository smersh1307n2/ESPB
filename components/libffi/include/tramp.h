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
/* -----------------------------------------------------------------------
   ffi_tramp.h - Copyright (C) 2021  Microsoft, Inc.

   Static trampoline definitions.

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the ``Software''), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   ----------------------------------------------------------------------- */

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
