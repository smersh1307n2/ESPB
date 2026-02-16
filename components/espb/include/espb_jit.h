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
#ifndef ESPB_JIT_H
#define ESPB_JIT_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// JIT Cache для хранения скомпилированных функций
// ============================================================================

/**
 * @brief Запись в JIT cache
 */
typedef struct {
    uint32_t func_idx;          // Индекс функции
    void* jit_code;             // Указатель на скомпилированный нативный код
    size_t code_size;           // Размер кода в байтах
    bool is_valid;              // Флаг валидности записи
} EspbJitCacheEntry;

/**
 * @brief JIT Cache - хранилище скомпилированных функций
 * 
 * Используем имя структуры, чтобы соответствовать forward declaration в common_types.h
 */
struct EspbJitCache {
    EspbJitCacheEntry* entries; // Массив записей
    size_t capacity;            // Максимальное количество записей
    size_t count;               // Текущее количество записей
};

/**
 * @brief Инициализирует JIT cache
 */
EspbResult espb_jit_cache_init(EspbJitCache* cache, size_t capacity);

/**
 * @brief Освобождает JIT cache
 */
void espb_jit_cache_free(EspbJitCache* cache);

/**
 * @brief Ищет скомпилированную функцию в cache
 * @return Указатель на JIT-код или NULL, если не найдено
 */
void* espb_jit_cache_lookup(EspbJitCache* cache, uint32_t func_idx);

/**
 * @brief Добавляет скомпилированную функцию в cache
 */
EspbResult espb_jit_cache_insert(EspbJitCache* cache, uint32_t func_idx, void* jit_code, size_t code_size);

/**
 * @brief Удаляет запись из cache
 */
void espb_jit_cache_remove(EspbJitCache* cache, uint32_t func_idx);

/**
 * @brief Компилирует одну функцию ESPB в нативный код.
 *
 * @param instance Указатель на экземпляр ESPB.
 * @param func_idx Индекс функции для отладки.
 * @param body Указатель на тело функции ESPB, включая метаданные.
 * @param out_code Выходной указатель на буфер с скомпилированным нативным кодом.
 *                 Память для этого буфера выделяется внутри функции.
 * @param out_size Выходной указатель на размер скомпилированного кода.
 * @return EspbResult ESPB_OK в случае успеха, или код ошибки.
 */
EspbResult espb_jit_compile_function(EspbInstance* instance, uint32_t func_idx, const EspbFunctionBody *body, void **out_code, size_t *out_size);

/**
 * @brief Компилирует JIT-регион (часть функции) в нативный код.
 *
 * @param instance Указатель на экземпляр ESPB.
 * @param func_idx Индекс функции, содержащей регион (для контекста).
 * @param region_bytecode Указатель на начало байткода региона (после заголовка JIT_REGION_START).
 * @param region_len Длина тела региона в байтах.
 * @param region_id ID региона (для кеширования/отладки).
 * @param num_virtual_regs Количество виртуальных регистров в функции.
 * @param out_code Выходной указатель на буфер с скомпилированным кодом региона.
 * @param out_size Выходной размер скомпилированного кода.
 * @return EspbResult ESPB_OK в случае успеха, или код ошибки.
 */
EspbResult espb_jit_compile_region(
    EspbInstance* instance,
    uint32_t func_idx,
    const uint8_t* region_bytecode,
    uint16_t region_len,
    uint16_t region_id,
    uint16_t num_virtual_regs,
    void** out_code,
    size_t* out_size
);

#ifdef __cplusplus
}
#endif

#endif // ESPB_JIT_H