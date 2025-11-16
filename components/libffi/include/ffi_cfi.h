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

#ifndef FFI_CFI_H
#define FFI_CFI_H

#ifdef HAVE_AS_CFI_PSEUDO_OP

# define cfi_startproc			.cfi_startproc
# define cfi_endproc			.cfi_endproc
# define cfi_def_cfa(reg, off)		.cfi_def_cfa reg, off
# define cfi_def_cfa_register(reg)	.cfi_def_cfa_register reg
# define cfi_def_cfa_offset(off)	.cfi_def_cfa_offset off
# define cfi_adjust_cfa_offset(off)	.cfi_adjust_cfa_offset off
# define cfi_offset(reg, off)		.cfi_offset reg, off
# define cfi_rel_offset(reg, off)	.cfi_rel_offset reg, off
# define cfi_register(r1, r2)		.cfi_register r1, r2
# define cfi_return_column(reg)		.cfi_return_column reg
# define cfi_restore(reg)		.cfi_restore reg
# define cfi_same_value(reg)		.cfi_same_value reg
# define cfi_undefined(reg)		.cfi_undefined reg
# define cfi_remember_state		.cfi_remember_state
# define cfi_restore_state		.cfi_restore_state
# define cfi_window_save		.cfi_window_save
# define cfi_personality(enc, exp)	.cfi_personality enc, exp
# define cfi_lsda(enc, exp)		.cfi_lsda enc, exp
# define cfi_escape(...)		.cfi_escape __VA_ARGS__
# define cfi_window_save		.cfi_window_save

#else

# define cfi_startproc
# define cfi_endproc
# define cfi_def_cfa(reg, off)
# define cfi_def_cfa_register(reg)
# define cfi_def_cfa_offset(off)
# define cfi_adjust_cfa_offset(off)
# define cfi_offset(reg, off)
# define cfi_rel_offset(reg, off)
# define cfi_register(r1, r2)
# define cfi_return_column(reg)
# define cfi_restore(reg)
# define cfi_same_value(reg)
# define cfi_undefined(reg)
# define cfi_remember_state
# define cfi_restore_state
# define cfi_window_save
# define cfi_personality(enc, exp)
# define cfi_lsda(enc, exp)
# define cfi_escape(...)
# define cfi_window_save

#endif /* HAVE_AS_CFI_PSEUDO_OP */
#endif /* FFI_CFI_H */
