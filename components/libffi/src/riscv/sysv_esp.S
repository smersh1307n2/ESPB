# -----------------------------------------------------------------------
#   sysv_esp.S - Copyright (c) 2024
#   
#   Simplified RISC-V Foreign Function Interface for ESP-IDF
#   Based on libffi-3.4.8 version
#
#   Permission is hereby granted, free of charge, to any person obtaining
#   a copy of this software and associated documentation files (the
#   ``Software''), to deal in the Software without restriction, including
#   without limitation the rights to use, copy, modify, merge, publish,
#   distribute, sublicense, and/or sell copies of the Software, and to
#   permit persons to whom the Software is furnished to do so, subject to
#   the following conditions:
#
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
#   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
#   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
#   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#   DEALINGS IN THE SOFTWARE.
#   ----------------------------------------------------------------------- */

#define LIBFFI_ASM
#include <ffi.h>

/* Определяем алиасы для обработки различных ABI */
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

    /* Глобальная переменная ffi_closure_ptr удалена. */

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
  void ffi_call_asm (size_t *stackargs, struct call_context *regargs,
                     void (*fn) (void), void *closure);
*/

#define FRAME_LEN (8 * FLTS + 8 * PTRS + 16)

ffi_call_asm:
    # a0=stack_args_src, a1=regargs, a2=fn, a3=closure, a4=stack_bytes
    
    # Сохраняем callee-saved регистры
    addi    sp, sp, -64
    sw      ra,   60(sp)
    sw      s0,   56(sp)
    sw      s1,   52(sp)
    sw      s2,   48(sp)
    sw      s3,   44(sp)
    sw      s4,   40(sp)
    sw      s5,   36(sp)
    sw      s6,   32(sp)
    sw      s7,   28(sp)
    sw      s8,   24(sp)
    sw      s9,   20(sp)
    sw      s10,  16(sp)
    sw      s11,  12(sp)

    # Сохраняем аргументы, чтобы они не были затерты
    mv      s0, a0  # s0 = stack_args_src
    mv      s1, a1  # s1 = regargs
    mv      s2, a2  # s2 = fn
    mv      s3, a4  # s3 = stack_bytes

    # Выделяем место на стеке для аргументов, выравниваем до 16 байт
    mv      t0, s3
    addi    t0, t0, 15
    andi    t0, t0, -16
    sub     sp, sp, t0
    
    # Копируем стековые аргументы (если есть)
    bnez    s3, 1f
    j       2f
1:
    mv      a0, sp  # dst
    mv      a1, s0  # src
    mv      a2, s3  # n
    # Простой цикл memcpy
    li      t0, 0   # i = 0
.L_copy_loop:
    beq     t0, a2, .L_copy_done
    lbu     t1, 0(a1)
    sb      t1, 0(a0)
    addi    a0, a0, 1
    addi    a1, a1, 1
    addi    t0, t0, 1
    j       .L_copy_loop
.L_copy_done:

2:
    # Загружаем регистры из regargs (s1)
#if FLTS
    FLARG   fa0, 0*FLTS(s1)
    FLARG   fa1, 1*FLTS(s1)
    FLARG   fa2, 2*FLTS(s1)
    FLARG   fa3, 3*FLTS(s1)
    FLARG   fa4, 4*FLTS(s1)
    FLARG   fa5, 5*FLTS(s1)
    FLARG   fa6, 6*FLTS(s1)
    FLARG   fa7, 7*FLTS(s1)
#endif
    LARG    a0, 8*FLTS+0*PTRS(s1)
    LARG    a1, 8*FLTS+1*PTRS(s1)
    LARG    a2, 8*FLTS+2*PTRS(s1)
    LARG    a3, 8*FLTS+3*PTRS(s1)
    LARG    a4, 8*FLTS+4*PTRS(s1)
    LARG    a5, 8*FLTS+5*PTRS(s1)
    LARG    a6, 8*FLTS+6*PTRS(s1)
    LARG    a7, 8*FLTS+7*PTRS(s1)

    # Вызываем целевую функцию
    jalr    s2

    # Сохраняем возвращаемые значения в regargs (s1)
#if FLTS
    FSARG   fa0, 0*FLTS(s1)
    FSARG   fa1, 1*FLTS(s1)
#endif
    SARG    a0, 8*FLTS+0*PTRS(s1)
    SARG    a1, 8*FLTS+1*PTRS(s1)
    # Если необходимы дополнительные регистры для возвращаемых 64-битных значений:
    SARG    a2, 8*FLTS+2*PTRS(s1)
    SARG    a3, 8*FLTS+3*PTRS(s1)

    # Освобождаем место на стеке, выделенное для аргументов
    mv      t0, s3
    addi    t0, t0, 15
    andi    t0, t0, -16
    add     sp, sp, t0

    # Восстанавливаем callee-saved регистры
    lw      s11,  12(sp)
    lw      s10,  16(sp)
    lw      s9,   20(sp)
    lw      s8,   24(sp)
    lw      s7,   28(sp)
    lw      s6,   32(sp)
    lw      s5,   36(sp)
    lw      s4,   40(sp)
    lw      s3,   44(sp)
    lw      s2,   48(sp)
    lw      s1,   52(sp)
    lw      s0,   56(sp)
    lw      ra,   60(sp)
    addi    sp, sp, 64
    ret
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
    # Сохраняем полный контекст, как того требует FreeRTOS
    addi    sp, sp, -64
    sw      ra,   60(sp)
    sw      s0,   56(sp) # fp
    sw      s1,   52(sp)
    sw      s2,   48(sp)
    sw      s3,   44(sp)
    # НЕ сохраняем s4 здесь - мы его перезапишем
    sw      s5,   36(sp)
    sw      s6,   32(sp)
    sw      s7,   28(sp)
    sw      s8,   24(sp)
    sw      s9,   20(sp)
    sw      s10,  16(sp)
    sw      s11,  12(sp)

    # Сохраняем closure (из t1) на стеке в слот, который был
    # зарезервирован для s4, но не использовался.
    # Это делает решение потокобезопасным.
    sw      t1,   40(sp)

    # Выделяем место для call_context на стеке
    addi    sp, sp, -FRAME_LEN

    # Сохраняем аргументы функции в call_context
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

    # Загружаем параметры для вызова ffi_closure_inner
    # Загружаем closure со стека.
    # Смещение FRAME_LEN + 40 необходимо, так как указатель стека (sp)
    # был сдвинут для выделения места под call_context.
    lw      t0, FRAME_LEN+40(sp)
    # Смещения: cif=24, fun=28, user_data=32
    # Жестко задаем смещение 24, так как размер трамплина для ESP32-C3 равен 24 байтам.
    # Это решает проблему, если FFI_TRAMPOLINE_SIZE определен неверно.
    LARG    a0, 24(t0)  # cif
    LARG    a1, 28(t0)  # fun
    LARG    a2, 32(t0)  # user_data
    addi    a3, sp, FRAME_LEN                   # stackargs
    mv      a4, sp                              # regargs

    # Вызываем C функцию для обработки closure
    call    ffi_closure_inner

    # Восстанавливаем возвращаемые значения
#if FLTS
    FLARG   fa0, 0*FLTS(sp)
    FLARG   fa1, 1*FLTS(sp)
#endif
    LARG    a0, 8*FLTS+0*PTRS(sp)
    LARG    a1, 8*FLTS+1*PTRS(sp)
    # Загружаем дополнительные регистры для 64-битных значений
    LARG    a2, 8*FLTS+2*PTRS(sp)
    LARG    a3, 8*FLTS+3*PTRS(sp)

    # Восстанавливаем стек
    addi    sp, sp, FRAME_LEN

    # Восстанавливаем сохраненные регистры и возвращаемся
    lw      s11,  12(sp)
    lw      s10,  16(sp)
    lw      s9,   20(sp)
    lw      s8,   24(sp)
    lw      s7,   28(sp)
    lw      s6,   32(sp)
    lw      s5,   36(sp)
    # НЕ восстанавливаем s4 - мы его не сохраняли
    lw      s3,   44(sp)
    lw      s2,   48(sp)
    lw      s1,   52(sp)
    lw      s0,   56(sp)
    lw      ra,   60(sp)
    addi    sp, sp, 64
    ret
    .size ffi_closure_asm, .-ffi_closure_asm 

    .globl  ffi_clear_cache
    .type   ffi_clear_cache, @function
    .hidden ffi_clear_cache
ffi_clear_cache:
    fence.i
    ret
    .size   ffi_clear_cache, .-ffi_clear_cache