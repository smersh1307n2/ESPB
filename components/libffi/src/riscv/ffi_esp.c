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

#include <ffi.h>
#include <ffi_common.h>
#include "ffitarget_esp.h"

#include <stdlib.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "espb_runtime_oc";

#if __riscv_float_abi_double
#define ABI_FLEN 64
#define ABI_FLOAT double
#elif __riscv_float_abi_single
#define ABI_FLEN 32
#define ABI_FLOAT float
#endif

#define NARGREG 8
#define STKALIGN 16
#define MAXCOPYARG (2 * sizeof(double))

typedef struct call_context
{
#if ABI_FLEN
    ABI_FLOAT fa[8];
#endif
    size_t a[8];
    /* used by the assembly code to in-place construct its own stack frame */
    char frame[16];
} call_context;

typedef struct call_builder
{
    call_context *aregs;
    int used_integer;
    int used_float;
    size_t *used_stack;
    void *struct_stack;
    void *rvalue;
    unsigned flags;
} call_builder;

static void marshal_atom(call_builder *cb, void *data, int type, int var);
static void unmarshal_atom(call_builder *cb, int type, void *data, int var);

/* integer (not pointer) less than ABI XLEN */
/* FFI_TYPE_INT does not appear to be used */
#if __SIZEOF_POINTER__ == 8
#define IS_INT(type) ((type) >= FFI_TYPE_UINT8 && (type) <= FFI_TYPE_SINT64)
#else
#define IS_INT(type) ((type) >= FFI_TYPE_UINT8 && (type) <= FFI_TYPE_SINT32)
#endif

#if ABI_FLEN
typedef struct {
    char as_elements, type1, offset2, type2;
} float_struct_info;

#if ABI_FLEN >= 64
#define IS_FLOAT(type) ((type) >= FFI_TYPE_FLOAT && (type) <= FFI_TYPE_DOUBLE)
#else
#define IS_FLOAT(type) ((type) == FFI_TYPE_FLOAT)
#endif

static ffi_type **flatten_struct(ffi_type *in, ffi_type **out, ffi_type **out_end) {
    int i;
    if (out == out_end) return out;
    if (in->type != FFI_TYPE_STRUCT) {
        *(out++) = in;
    } else {
        for (i = 0; in->elements[i]; i++)
            out = flatten_struct(in->elements[i], out, out_end);
    }
    return out;
}

/* Structs with at most two fields after flattening, one of which is of
   floating point type, are passed in multiple registers if sufficient
   registers are available. */
static float_struct_info struct_passed_as_elements(call_builder *cb, ffi_type *top) {
    float_struct_info ret = {0, 0, 0, 0};
    ffi_type *fields[3];
    int num_floats, num_ints;
    int num_fields = flatten_struct(top, fields, fields + 3) - fields;

    if (num_fields == 1) {
        if (IS_FLOAT(fields[0]->type)) {
            ret.as_elements = 1;
            ret.type1 = fields[0]->type;
        }
    } else if (num_fields == 2) {
        num_floats = IS_FLOAT(fields[0]->type) + IS_FLOAT(fields[1]->type);
        num_ints = IS_INT(fields[0]->type) + IS_INT(fields[1]->type);
        if (num_floats == 0 || num_floats + num_ints != 2)
            return ret;
        if (cb->used_float + num_floats > NARGREG || cb->used_integer + (2 - num_floats) > NARGREG)
            return ret;
        if (!IS_FLOAT(fields[0]->type) && !IS_FLOAT(fields[1]->type))
            return ret;

        ret.type1 = fields[0]->type;
        ret.type2 = fields[1]->type;
        ret.offset2 = FFI_ALIGN(fields[0]->size, fields[1]->alignment);
        ret.as_elements = 1;
    }

    return ret;
}
#endif

/* allocates a single register, float register, or XLEN-sized stack slot to a datum */
static void marshal_atom(call_builder *cb, void *data, int type, int var)
{
    ESP_LOGD(TAG,"marshal_atom(type=%d, var=%d, used_int=%d, used_float=%d, used_stack=%p, data=%p)\n",
           type, var, cb->used_integer, cb->used_float, (void*)cb->used_stack, data);

#if __SIZEOF_POINTER__ == 4
    if (type == FFI_TYPE_UINT64 || type == FFI_TYPE_SINT64 || type == FFI_TYPE_DOUBLE) {
        if (var && (cb->used_integer & 1)) {
            ESP_LOGD(TAG,"Aligning for 64-bit marshal, skipping reg %d\n", cb->used_integer);
            cb->used_integer++;
        }
        uint32_t parts[2];
        memcpy(parts, data, sizeof(uint64_t));

        if (cb->used_integer > NARGREG - 2) {
            if ((size_t)cb->used_stack & 7) {
                cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, 8);
            }
            ESP_LOGD(TAG,"marshal 64-bit to stack @%p: low=0x%08x, high=0x%08x\n", (void*)cb->used_stack, parts[0], parts[1]);
            *cb->used_stack++ = parts[0];
            *cb->used_stack++ = parts[1];
        } else {
            ESP_LOGD(TAG,"marshal 64-bit to regs idx[%d,%d]: low=0x%08x, high=0x%08x\n", cb->used_integer, cb->used_integer+1, parts[0], parts[1]);
            cb->aregs->a[cb->used_integer++] = parts[0];
            cb->aregs->a[cb->used_integer++] = parts[1];
        }
        return;
    }
#endif

    size_t value = 0;
    switch (type) {
        case FFI_TYPE_UINT8:   value = *(uint8_t *)data; break;
        case FFI_TYPE_SINT8:   value = *(int8_t *)data; break;
        case FFI_TYPE_UINT16:  value = *(uint16_t *)data; break;
        case FFI_TYPE_SINT16:  value = *(int16_t *)data; break;
        case FFI_TYPE_UINT32:  value = *(uint32_t *)data; break;
        case FFI_TYPE_SINT32:  value = *(int32_t *)data; break;
        case FFI_TYPE_POINTER: value = *(size_t *)data; break;
        case FFI_TYPE_FLOAT:
            memcpy(&value, data, sizeof(float));
            break;
        default: FFI_ASSERT(0); break;
    }

    if (cb->used_integer >= NARGREG) {
        *cb->used_stack++ = value;
    } else {
        cb->aregs->a[cb->used_integer++] = value;
    }
}

static void unmarshal_atom(call_builder *cb, int type, void *data, int var)
{
    ESP_LOGD(TAG,"unmarshal_atom(type=%d, var=%d, used_int=%d, used_float=%d, used_stack=%p)\n",
           type, var, cb->used_integer, cb->used_float, (void*)cb->used_stack);

#if __SIZEOF_POINTER__ == 4
    if (type == FFI_TYPE_UINT64 || type == FFI_TYPE_SINT64 || type == FFI_TYPE_DOUBLE) {
        if (var && (cb->used_integer & 1)) {
            ESP_LOGD(TAG,"Aligning for 64-bit arg, skipping reg %d\n", cb->used_integer);
            cb->used_integer++;
        }
        uint64_t temp_val;
        if (cb->used_integer > NARGREG - 2) {
            if ((size_t)cb->used_stack & 7) {
                cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, 8);
            }
            uint32_t lo = *cb->used_stack++;
            uint32_t hi = *cb->used_stack++;
            temp_val = ((uint64_t)hi << 32) | lo;
        } else {
            uint32_t lo = cb->aregs->a[cb->used_integer++];
            uint32_t hi = cb->aregs->a[cb->used_integer++];
            temp_val = ((uint64_t)hi << 32) | lo;
        }
        memcpy(data, &temp_val, sizeof(uint64_t));
        return;
    }
#endif

    size_t value;
    if (cb->used_integer >= NARGREG) {
        value = *cb->used_stack++;
    } else {
        value = cb->aregs->a[cb->used_integer++];
    }

    switch (type) {
        case FFI_TYPE_UINT8:   *(uint8_t *)data = value; break;
        case FFI_TYPE_SINT8:   *(int8_t *)data = value; break;
        case FFI_TYPE_UINT16:  *(uint16_t *)data = value; break;
        case FFI_TYPE_SINT16:  *(int16_t *)data = value; break;
        case FFI_TYPE_UINT32:  *(uint32_t *)data = value; break;
        case FFI_TYPE_SINT32:  *(int32_t *)data = value; break;
        case FFI_TYPE_FLOAT:
            memcpy(data, &value, sizeof(float));
            break;
        case FFI_TYPE_POINTER: *(size_t *)data = value; break;
        default: FFI_ASSERT(0); break;
    }
}

/* adds an argument to a call, or a not by reference return value */
static void marshal(call_builder *cb, void *data, ffi_type *type, int var) {
    ESP_LOGD(TAG,"marshal(type=%d, var=%d, used_int=%d, used_float=%d, stack=%p, data=%p)\n",
           type->type, var, cb->used_integer, cb->used_float, (void*)cb->used_stack, data);

    if (var) {
        ESP_LOGD(TAG,"marshal variadic branch: type=%d, used_int=%d, used_float=%d, used_stack=%p\n",
               type->type, cb->used_integer, cb->used_float, (void*)cb->used_stack);
    }
    if (type->type == FFI_TYPE_STRUCT) {
        /* Для структур нужно обрабатывать каждый элемент отдельно */
        int i;
        ffi_type **ptr = type->elements;
        size_t offset = 0;
        
        /* Для вариадических аргументов-структур выравниваем стек */
        if (var) {
            size_t align = type->alignment;
            if ((size_t)cb->used_stack % align != 0) {
                cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, align);
            }
        }
        
        for (i = 0; ptr[i]; i++) {
            /* Вычисляем смещение для каждого элемента структуры */
            offset = FFI_ALIGN(offset, ptr[i]->alignment);
            void *addr = (char*)data + offset;
            marshal_atom(cb, addr, ptr[i]->type, var);
            offset += ptr[i]->size;
        }
    } else {
        /* Для variadic-аргументов выравниваем регистры и стек для типов с alignment > pointer_size */
        if (var && type->alignment > __SIZEOF_POINTER__) {
            int aligned = FFI_ALIGN(cb->used_integer, 2);
            if (cb->used_integer != aligned) {
                ESP_LOGD(TAG,"Aligning variadic arg regs from %d to %d\n", cb->used_integer, aligned);
                cb->used_integer = aligned;
            }
            cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, 2 * __SIZEOF_POINTER__);
        }
        marshal_atom(cb, data, type->type, var);
    }
}

/* does the cache of the result of an FFI call, or the result of
   call->arg marshalling.  Must be a type that can be stored in
   registers */
static void *unmarshal(call_builder *cb, void *data, ffi_type *type, int var) {
    if (var) {
        ESP_LOGD(TAG,"unmarshal variadic branch: type=%d, used_int=%d, used_float=%d, used_stack=%p\n",
               type->type, cb->used_integer, cb->used_float, (void*)cb->used_stack);
    }
    if (type->type == FFI_TYPE_STRUCT) {
        /* Для структур нужно обрабатывать каждый элемент отдельно */
        int i;
        ffi_type **ptr = type->elements;
        size_t offset = 0;
        
        /* Для вариадических аргументов-структур выравниваем стек */
        if (var) {
            size_t align = type->alignment;
            if ((size_t)cb->used_stack % align != 0) {
                cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, align);
            }
        }
        
        for (i = 0; ptr[i]; i++) {
            /* Вычисляем смещение для каждого элемента структуры */
            offset = FFI_ALIGN(offset, ptr[i]->alignment);
            void *addr = (char*)data + offset;
            unmarshal_atom(cb, ptr[i]->type, addr, var);
            offset += ptr[i]->size;
        }
    } else {
        /* Для variadic-аргументов выравниваем регистры и стек для типов с alignment > pointer_size */
        if (var && type->alignment > __SIZEOF_POINTER__) {
            int aligned = FFI_ALIGN(cb->used_integer, 2);
            if (cb->used_integer != aligned) {
                ESP_LOGD(TAG,"Aligning variadic arg regs from %d to %d\n", cb->used_integer, aligned);
                cb->used_integer = aligned;
            }
            cb->used_stack = (size_t *)FFI_ALIGN(cb->used_stack, 2 * __SIZEOF_POINTER__);
        }
        unmarshal_atom(cb, type->type, data, var);
    }
    /* Возвращаем данные */
    return data;
}

// Специализированная версия для работы с указателями
static void *unmarshal_ptr(call_builder *cb, ffi_type *type, int var) {
    size_t value = 0;

    // Получаем значение указателя
    if (cb->used_integer == NARGREG) {
        value = *cb->used_stack++;
    } else {
        value = cb->aregs->a[cb->used_integer++];
    }

    return (void *)value;
}

/* Returns nonzero if the register(s) or memory used to store a
   function argument of type "type" can be reused.  Structs passed by
   value, floats, and longs on 32-bit systems can't have holes
   punched in them. */
static int passed_by_ref(call_builder *cb, ffi_type *type, int var) {
    if (type->size > 2 * __SIZEOF_POINTER__)
        return 0;

#if ABI_FLEN
    if (!var && cb->used_float < NARGREG && IS_FLOAT(type->type)) {
        return 0;
    }
#endif

    return type->size % __SIZEOF_POINTER__ != 0 || type->alignment % __SIZEOF_POINTER__ != 0;
}

/* Perform machine dependent cif processing */
/*
 * Локальное определение структуры _ffi_cif удалено.
 * Теперь будет использоваться стандартное определение из ffi_common.h,
 * что гарантирует консистентность во всем проекте.
 */

ffi_status ffi_prep_cif_machdep(ffi_cif *cif) {
    /* Для обычных функций количество фиксированных аргументов равно общему количеству */
    cif->flags = cif->nargs;
    return FFI_OK;
}

/* Perform machine dependent cif processing when we have a variadic function */

ffi_status ffi_prep_cif_machdep_var(ffi_cif *cif, unsigned int nfixedargs, unsigned int ntotalargs) {
    /* Сохраняем количество фиксированных аргументов в cif->flags */
    cif->flags = nfixedargs;
    return FFI_OK;
}

/* Low level routine for calling functions */
extern void ffi_call_asm(void *stackargs, struct call_context *regargs,
                         void (*fn)(void), void *closure, size_t stack_bytes) FFI_HIDDEN;

ffi_status ffi_call_int(ffi_cif *cif, void (*fn)(void), void *rvalue,
                        void **avalue, void *closure) {
    /* Allocate space for the stack arguments */
    size_t stack_bytes = cif->bytes;
    char *stack = alloca(stack_bytes);
    call_builder cb;
    int i;
    int nfixed = cif->flags;
    int is_variadic = nfixed != cif->nargs;
    
    /* Initialize call_builder */
    cb.used_integer = 0;
    cb.used_float = 0;
    cb.used_stack = (size_t *)stack;
    cb.rvalue = rvalue;
    cb.flags = cif->flags;
    cb.aregs = alloca(sizeof(struct call_context));

    /* Initialize register arguments */
    for (i = 0; i < cif->nargs; i++) {
        int var_flag = is_variadic && (i >= nfixed);
        
        /* Add debug print for variadic arguments */
        if (var_flag) {
            ESP_LOGD(TAG,"Processing variadic arg %d, type=%d\n", 
                   i, cif->arg_types[i]->type);
        }
        
        marshal(&cb, avalue[i], cif->arg_types[i], var_flag);
    }

    /* Perform the call */
    ffi_call_asm(stack, cb.aregs, fn, closure, stack_bytes);
    
    /* Handle the return value */
    if (rvalue && cif->rtype->type != FFI_TYPE_VOID) {
        cb.used_integer = 0;
        cb.used_float = 0;
        cb.used_stack = (size_t *)stack;
        unmarshal(&cb, rvalue, cif->rtype, 0);
    }

    return FFI_OK;
}

void
ffi_call (ffi_cif *cif, void (*fn) (void), void *rvalue, void **avalue)
{
  ffi_call_int(cif, fn, rvalue, avalue, NULL);
}

void
ffi_call_go (ffi_cif *cif, void (*fn) (void), void *rvalue,
	     void **avalue, void *closure)
{
  ffi_call_int(cif, fn, rvalue, avalue, closure);
}

extern void ffi_closure_asm(void) FFI_HIDDEN;

__attribute__((noinline, optimize("O0")))
ffi_status
ffi_prep_closure_loc (ffi_closure *closure,
		      ffi_cif *cif,
		      void (*fun) (ffi_cif *, void *, void **, void *),
		      void *user_data,
		      void *codeloc)
{
    uint32_t *tramp = (uint32_t *)codeloc;
    
    /* Для RISC-V: codeloc должен указывать на closure->tramp */
    /* Проверяем, что codeloc действительно указывает на tramp внутри closure */
    if (codeloc != (void*)closure->tramp) {
        ESP_LOGD(TAG,"TRAMP DEBUG: codeloc=%p, closure->tramp=%p (they should be equal)\n", codeloc, (void*)closure->tramp);
        // Не возвращаем ошибку, просто логируем для диагностики
    }

    if (cif->abi != FFI_SYSV)
        return FFI_BAD_ABI;

    /* Сохраняем метаданные */
    closure->cif = cif;
    closure->fun = fun;
    closure->user_data = user_data;

    /*
     * Генерация tramp для RISC-V (RV32I):
     *
     *  auipc   t1, 0
     *  lw      t2, 16(t1)    # загружаем адрес ffi_closure_asm
     *  lw      t1, 20(t1)    # загружаем closure в t1 (как ожидает ffi_closure_asm)
     *  jr      t2
     *
     *  [ +16 ] = ffi_closure_asm
     *  [ +20 ] = closure
     */

    tramp[0] = 0x00000317; // auipc t1, 0
    tramp[1] = 0x01032383; // lw    t2,16(t1)
    tramp[2] = 0x01432303; // lw    t1,20(t1)  # загружаем closure в t1
    tramp[3] = 0x00038067; // jr    t2

    /* Пишем правильные значения в tramp */
    *(uintptr_t *)((uint8_t *)tramp + 16) = (uintptr_t)ffi_closure_asm;
    *(uintptr_t *)((uint8_t *)tramp + 20) = (uintptr_t)closure;

    /* Добавляем логирование адресов и первых байтов трамплина */
    ESP_LOGD(TAG,"TRAMP DEBUG: codeloc=%p, closure=%p\n", codeloc, (void*)closure);
    ESP_LOGD(TAG,"TRAMP DEBUG: ffi_closure_asm=%p\n", (void*)ffi_closure_asm);
    ESP_LOGD(TAG,"TRAMP DEBUG: tramp[0-3]: 0x%08x 0x%08x 0x%08x 0x%08x\n", 
           tramp[0], tramp[1], tramp[2], tramp[3]);
    ESP_LOGD(TAG,"TRAMP DEBUG: addr+16 (ffi_closure_asm): 0x%08x\n", 
           *(uint32_t*)((uint8_t *)tramp + 16));
    ESP_LOGD(TAG,"TRAMP DEBUG: addr+20 (closure): 0x%08x\n", 
           *(uint32_t*)((uint8_t *)tramp + 20));
    ESP_LOGD(TAG,"TRAMP DEBUG: closure->cif=%p, closure->fun=%p, closure->user_data=%p\n", 
           (void*)closure->cif, (void*)closure->fun, closure->user_data);

    __builtin___clear_cache((char *)tramp, (char *)tramp + FFI_TRAMPOLINE_SIZE);

    /* Добавляем логирование содержимого трамплина ПОСЛЕ записи */
    ESP_LOGD(TAG,"TRAMP DEBUG AFTER: Final trampoline content at %p (size=%d):\n", codeloc, FFI_TRAMPOLINE_SIZE);
   
   #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    uint8_t *bytes = (uint8_t*)codeloc;
    for (int i = 0; i < FFI_TRAMPOLINE_SIZE; i += 16) {
        char hex_line[64] = {0};
        char ascii_line[17] = {0};
        int line_len = (FFI_TRAMPOLINE_SIZE - i < 16) ? (FFI_TRAMPOLINE_SIZE - i) : 16;
        
        for (int j = 0; j < line_len; j++) {
            sprintf(hex_line + j*3, "%02x ", bytes[i + j]);
            ascii_line[j] = (bytes[i + j] >= 32 && bytes[i + j] <= 126) ? bytes[i + j] : '.';
        }
        
        ESP_LOGD(TAG,"TRAMP DEBUG AFTER: %p+%02x: %-48s |%s|\n", codeloc, i, hex_line, ascii_line);
    }
    #endif
    /* Также показываем как 32-битные слова */
    uint32_t *words = (uint32_t*)codeloc;
    ESP_LOGD(TAG,"TRAMP DEBUG AFTER: As 32-bit words:\n");
    for (int i = 0; i < FFI_TRAMPOLINE_SIZE/4; i++) {
        ESP_LOGD(TAG,"TRAMP DEBUG AFTER: word[%d] = 0x%08x\n", i, words[i]);
    }

    return FFI_OK;
}

/*
 * ЗАМЕНА: Используем стандартную, более надежную реализацию ffi_closure_inner.
 * "Упрощенная" версия содержала ошибки в управлении памятью, что приводило к падению.
 * Эта версия корректно выделяет память и распаковывает аргументы.
 */
void FFI_HIDDEN
ffi_closure_inner(ffi_cif *cif,
                  void (*fun)(ffi_cif *, void *, void **, void *),
                  void *user_data,
                  size_t *stack, call_context *aregs)
{
    // Массив указателей на значения аргументов
    void **avalue = alloca(cif->nargs * sizeof(void *));

    // Область памяти для хранения самих значений аргументов
    char *astorage = alloca(cif->bytes);

    void *rvalue;
    call_builder cb;
    int i;
    unsigned int nfixed = cif->flags;

    cb.aregs = aregs;
    cb.used_integer = 0;
    cb.used_float = 0;
    cb.used_stack = stack;
    cb.struct_stack = NULL; // Не используется в этой логике

    // Определяем, как будет возвращаться значение (напрямую или по указателю)
    if (cif->rtype->type == FFI_TYPE_STRUCT && !passed_by_ref(&cb, cif->rtype, 0)) {
         rvalue = alloca(cif->rtype->size);
    } else if (cif->rtype->type == FFI_TYPE_STRUCT) {
        // Получаем указатель на структуру, куда нужно записать результат
        rvalue = unmarshal_ptr(&cb, &ffi_type_pointer, 0);
    } else {
        rvalue = alloca(cif->rtype->size);
    }
    
    // Сбрасываем счетчики перед распаковкой аргументов
    cb.used_integer = 0;
    cb.used_float = 0;

    // Распаковываем аргументы из регистров/стека в локальную память
    char *storage_ptr = astorage;
    for (i = 0; i < cif->nargs; i++)
    {
        ffi_type *type = cif->arg_types[i];
        int var = i >= nfixed;
        
        // Выравниваем указатель в storage
        size_t aligned_addr = FFI_ALIGN((size_t)storage_ptr, type->alignment);
        storage_ptr = (char *)aligned_addr;
        
        // Распаковываем аргумент
        avalue[i] = unmarshal(&cb, storage_ptr, type, var);

        // Перемещаем указатель на следующий свободный участок
        storage_ptr += type->size;
    }

    // Вызываем пользовательскую функцию
    fun(cif, rvalue, avalue, user_data);

    // Если значение возвращается через регистры, упаковываем его
    if (cif->rtype->type != FFI_TYPE_VOID && !(cif->rtype->type == FFI_TYPE_STRUCT && passed_by_ref(&cb, cif->rtype, 0)))
    {
        cb.used_integer = 0;
        cb.used_float = 0;
        marshal(&cb, rvalue, cif->rtype, 0);
    }
}

void ffi_closure_helper_riscv(ffi_closure *closure, void *rvalue_arg,
                              struct call_context *context, void *stack_arg) {
    ffi_cif *cif = closure->cif;
    ffi_type *rtype = cif->rtype;
    void *avalue[cif->nargs];
    call_builder cb;
    int i;
    unsigned int nfixed = cif->flags;
    void *rvalue = rvalue_arg;

    /* Initialize call_builder */
    cb.aregs = context;
    cb.used_stack = (size_t *)stack_arg;
    cb.used_integer = 0;
    cb.used_float = 0;
    cb.flags = cif->flags;
    cb.rvalue = rvalue;
    cb.struct_stack = NULL;

    /* Handle structure return by reference */
    if (passed_by_ref(&cb, rtype, 0)) {
        rvalue = unmarshal_ptr(&cb, &ffi_type_pointer, 0);
    } else {
        rvalue = alloca(rtype->size);
    }

    /* Unmarshal arguments */
    for (i = 0; i < cif->nargs; i++) {
        int var = (i >= nfixed);
        void *arg_storage = alloca(cif->arg_types[i]->size);
        
        // Выравнивание памяти для аргумента
        arg_storage = (void *)FFI_ALIGN((size_t)arg_storage, cif->arg_types[i]->alignment);
        
        // Для 64-битных аргументов в вариадических функциях нужно особое выравнивание
        if (var && (cif->arg_types[i]->type == FFI_TYPE_UINT64 || 
                   cif->arg_types[i]->type == FFI_TYPE_SINT64)) {
            ESP_LOGD(TAG,"Closure unmarshal: 64-bit variadic arg %d\n", i);
        }
        
        avalue[i] = unmarshal(&cb, arg_storage, cif->arg_types[i], var);
    }

    /* Call user closure function */
    closure->fun(cif, rvalue, avalue, closure->user_data);

    /* Marshal return value if not struct-by-ref */
    if (!passed_by_ref(&cb, rtype, 0) && rtype->type != FFI_TYPE_VOID) {
        cb.used_integer = 0;
        cb.used_float = 0;
        cb.used_stack = (size_t *)stack_arg;
        marshal(&cb, rvalue, rtype, 0);
    }
} 