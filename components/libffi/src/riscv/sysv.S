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

#define LIBFFI_ASM
#include <fficonfig.h>
#include <ffi.h>

/* Define aliases so that we can handle all ABIs uniformly */

#if __SIZEOF_POINTER__ == 8
#define PTRS 8
#define LARG ld
#define SARG sd
#else
#define PTRS 4
#define LARG lw
#define SARG sw
#endif

#if __riscv_float_abi_double
#define FLTS 8
#define FLARG fld
#define FSARG fsd
#elif __riscv_float_abi_single
#define FLTS 4
#define FLARG flw
#define FSARG fsw
#else
#define FLTS 0
#endif

#define fp s0

    .text
    .globl  ffi_call_asm
    .type   ffi_call_asm, @function
    .hidden ffi_call_asm
/*
  struct call_context {
      floatreg fa[8];
      intreg a[8];
      intreg pad[rv32 ? 2 : 0];
      intreg save_fp, save_ra;
  }
  void ffi_call_asm (void *stackargs_src, size_t stack_size,
                     struct call_context *regargs,
                     void (*fn) (void), void *closure);
*/

#define FRAME_LEN (8 * FLTS + 8 * PTRS + 16)

ffi_call_asm:
    .cfi_startproc

    # Расширенный фрейм для сохранения ra + s0–s11 (13 регистров)
    addi    sp, sp, -104
    .cfi_def_cfa_offset 104
    
    SARG    ra, 96(sp)
    .cfi_offset 1, -8
    SARG    s0, 88(sp)
    .cfi_offset 8, -16
    SARG    s1, 80(sp)
    .cfi_offset 9, -24
    SARG    s2, 72(sp)
    .cfi_offset 18, -32
    SARG    s3, 64(sp)
    .cfi_offset 19, -40
    SARG    s4, 56(sp)
    .cfi_offset 20, -48
    SARG    s5, 48(sp)
    .cfi_offset 21, -56
    SARG    s6, 40(sp)
    .cfi_offset 22, -64
    SARG    s7, 32(sp)
    .cfi_offset 23, -72
    SARG    s8, 24(sp)
    .cfi_offset 24, -80
    SARG    s9, 16(sp)
    .cfi_offset 25, -88
    SARG    s10, 8(sp)
    .cfi_offset 26, -96
    SARG    s11, 0(sp)
    .cfi_offset 27, -104
    
    # Сохраняем аргументы в защищенных регистрах
    mv      s0, a0          # s0 = stackargs_src
    mv      s1, a1          # s1 = stack_size
    mv      s2, a2          # s2 = regargs
    mv      s3, a3          # s3 = fn
    mv      s4, a4          # s4 = closure
    
    # Если есть стековые аргументы, выделяем место и копируем их
    beqz    s1, 1f          # Если stack_size == 0, пропускаем копирование
    
    # Выравниваем размер стека до 16 байт
    addi    t0, s1, 15
    andi    t0, t0, -16
    
    # Выделяем место на стеке
    sub     sp, sp, t0
    
    # Копируем стековые аргументы
    # Используем пословное копирование для лучшей производительности и совместимости
    mv      t0, s0          # t0 = src
    mv      t1, sp          # t1 = dst
    li      t2, 0           # t2 = counter
    
2:  # Цикл копирования
    bge     t2, s1, 1f      # Если скопировали все байты, выходим
    lb      t3, 0(t0)       # Загружаем байт из источника
    sb      t3, 0(t1)       # Сохраняем байт в назначение
    addi    t0, t0, 1       # src++
    addi    t1, t1, 1       # dst++
    addi    t2, t2, 1       # counter++
    j       2b
    
1:  # Загружаем регистры аргументов
#if FLTS
    FLARG   fa0, 0*FLTS(s2)
    FLARG   fa1, 1*FLTS(s2)
    FLARG   fa2, 2*FLTS(s2)
    FLARG   fa3, 3*FLTS(s2)
    FLARG   fa4, 4*FLTS(s2)
    FLARG   fa5, 5*FLTS(s2)
    FLARG   fa6, 6*FLTS(s2)
    FLARG   fa7, 7*FLTS(s2)
#endif
    LARG    a0, 8*FLTS+0*PTRS(s2)
    LARG    a1, 8*FLTS+1*PTRS(s2)
    LARG    a2, 8*FLTS+2*PTRS(s2)
    LARG    a3, 8*FLTS+3*PTRS(s2)
    LARG    a4, 8*FLTS+4*PTRS(s2)
    LARG    a5, 8*FLTS+5*PTRS(s2)
    LARG    a6, 8*FLTS+6*PTRS(s2)
    LARG    a7, 8*FLTS+7*PTRS(s2)
    
    # Вызываем целевую функцию
    jalr    s3
    
    # Сохраняем результаты
#if FLTS
    FSARG   fa0, 0*FLTS(s2)
    FSARG   fa1, 1*FLTS(s2)
#endif
    SARG    a0, 8*FLTS+0*PTRS(s2)
    SARG    a1, 8*FLTS+1*PTRS(s2)
    
    # Восстанавливаем стек до сохраненных регистров
    addi    t0, s1, 15
    andi    t0, t0, -16
    
    # Если мы выделяли место для стековых аргументов, освобождаем его
    beqz    s1, 3f
    add     sp, sp, t0
    
3:  # Восстанавливаем сохраненные регистры
    LARG    s11, 0(sp)
    LARG    s10, 8(sp)
    LARG    s9, 16(sp)
    LARG    s8, 24(sp)
    LARG    s7, 32(sp)
    LARG    s6, 40(sp)
    LARG    s5, 48(sp)
    LARG    s4, 56(sp)
    LARG    s3, 64(sp)
    LARG    s2, 72(sp)
    LARG    s1, 80(sp)
    LARG    s0, 88(sp)
    LARG    ra, 96(sp)
    
    # Освобождаем фрейм и возвращаемся
    addi    sp, sp, 104
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
    .size   ffi_call_asm, .-ffi_call_asm


/*
  ffi_closure_asm. Expects address of the passed-in ffi_closure in t1.
  void ffi_closure_inner (ffi_cif *cif,
		          void (*fun) (ffi_cif *, void *, void **, void *),
		          void *user_data,
		          size_t *stackargs, struct call_context *regargs)
*/

    .globl ffi_closure_asm
    .hidden ffi_closure_asm
    .type ffi_closure_asm, @function
ffi_closure_asm:
    .cfi_startproc

    addi    sp,  sp, -FRAME_LEN
    .cfi_def_cfa_offset FRAME_LEN

    /* make a frame */
    SARG    fp, FRAME_LEN - 2*PTRS(sp)
    .cfi_offset 8, -2*PTRS
    SARG    ra, FRAME_LEN - 1*PTRS(sp)
    .cfi_offset 1, -1*PTRS
    addi    fp, sp, FRAME_LEN

    /* save arguments */
#if FLTS
    FSARG   fa0, 0*FLTS(sp)
    FSARG   fa1, 1*FLTS(sp)
    FSARG   fa2, 2*FLTS(sp)
    FSARG   fa3, 3*FLTS(sp)
    FSARG   fa4, 4*FLTS(sp)
    FSARG   fa5, 5*FLTS(sp)
    FSARG   fa6, 6*FLTS(sp)
    FSARG   fa7, 7*FLTS(sp)
#endif

    SARG    a0, 8*FLTS+0*PTRS(sp)
    SARG    a1, 8*FLTS+1*PTRS(sp)
    SARG    a2, 8*FLTS+2*PTRS(sp)
    SARG    a3, 8*FLTS+3*PTRS(sp)
    SARG    a4, 8*FLTS+4*PTRS(sp)
    SARG    a5, 8*FLTS+5*PTRS(sp)
    SARG    a6, 8*FLTS+6*PTRS(sp)
    SARG    a7, 8*FLTS+7*PTRS(sp)

    /* enter C */
    LARG    a0, FFI_TRAMPOLINE_SIZE+0*PTRS(t1)
    LARG    a1, FFI_TRAMPOLINE_SIZE+1*PTRS(t1)
    LARG    a2, FFI_TRAMPOLINE_SIZE+2*PTRS(t1)
    addi    a3, sp, FRAME_LEN
    mv      a4, sp

    call    ffi_closure_inner

    /* return values */
#if FLTS
    FLARG   fa0, 0*FLTS(sp)
    FLARG   fa1, 1*FLTS(sp)
#endif

    LARG    a0, 8*FLTS+0*PTRS(sp)
    LARG    a1, 8*FLTS+1*PTRS(sp)

    /* restore and return */
    LARG    ra, FRAME_LEN-1*PTRS(sp)
    .cfi_restore 1
    LARG    fp, FRAME_LEN-2*PTRS(sp)
    .cfi_restore 8
    addi    sp, sp, FRAME_LEN
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
    .size ffi_closure_asm, .-ffi_closure_asm

/*
  ffi_go_closure_asm.  Expects address of the passed-in ffi_go_closure in t2.
  void ffi_closure_inner (ffi_cif *cif,
		          void (*fun) (ffi_cif *, void *, void **, void *),
		          void *user_data,
		          size_t *stackargs, struct call_context *regargs)
*/

    .globl ffi_go_closure_asm
    .hidden ffi_go_closure_asm
    .type ffi_go_closure_asm, @function
ffi_go_closure_asm:
    .cfi_startproc

    addi    sp,  sp, -FRAME_LEN
    .cfi_def_cfa_offset FRAME_LEN

    /* make a frame */
    SARG    fp, FRAME_LEN - 2*PTRS(sp)
    .cfi_offset 8, -2*PTRS
    SARG    ra, FRAME_LEN - 1*PTRS(sp)
    .cfi_offset 1, -1*PTRS
    addi    fp, sp, FRAME_LEN

    /* save arguments */
#if FLTS
    FSARG   fa0, 0*FLTS(sp)
    FSARG   fa1, 1*FLTS(sp)
    FSARG   fa2, 2*FLTS(sp)
    FSARG   fa3, 3*FLTS(sp)
    FSARG   fa4, 4*FLTS(sp)
    FSARG   fa5, 5*FLTS(sp)
    FSARG   fa6, 6*FLTS(sp)
    FSARG   fa7, 7*FLTS(sp)
#endif

    SARG    a0, 8*FLTS+0*PTRS(sp)
    SARG    a1, 8*FLTS+1*PTRS(sp)
    SARG    a2, 8*FLTS+2*PTRS(sp)
    SARG    a3, 8*FLTS+3*PTRS(sp)
    SARG    a4, 8*FLTS+4*PTRS(sp)
    SARG    a5, 8*FLTS+5*PTRS(sp)
    SARG    a6, 8*FLTS+6*PTRS(sp)
    SARG    a7, 8*FLTS+7*PTRS(sp)

    /* enter C */
    LARG    a0, 1*PTRS(t2)
    LARG    a1, 2*PTRS(t2)
    mv      a2, t2
    addi    a3, sp, FRAME_LEN
    mv      a4, sp

    call    ffi_closure_inner

    /* return values */
#if FLTS
    FLARG   fa0, 0*FLTS(sp)
    FLARG   fa1, 1*FLTS(sp)
#endif

    LARG    a0, 8*FLTS+0*PTRS(sp)
    LARG    a1, 8*FLTS+1*PTRS(sp)

    /* restore and return */
    LARG    ra, FRAME_LEN-1*PTRS(sp)
    .cfi_restore 1
    LARG    fp, FRAME_LEN-2*PTRS(sp)
    .cfi_restore 8
    addi    sp, sp, FRAME_LEN
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
    .size ffi_go_closure_asm, .-ffi_go_closure_asm
