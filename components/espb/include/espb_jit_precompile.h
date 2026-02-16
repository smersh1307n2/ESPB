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
#ifndef ESPB_JIT_PRECOMPILE_H
#define ESPB_JIT_PRECOMPILE_H

#include "espb_interpreter_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback функция для отслеживания прогресса предварительной компиляции
 * 
 * @param current Текущий номер компилируемой функции (1-based)
 * @param total Общее количество HOT функций
 * @param func_name Имя функции (если доступно) или NULL
 */
typedef void (*espb_jit_precompile_progress_cb)(uint32_t current, uint32_t total, const char* func_name);

/**
 * @brief Предварительно компилирует все HOT функции в модуле
 * 
 * Проходит по всем функциям модуля и компилирует те, которые помечены флагом ESPB_FUNC_FLAG_HOT.
 * Функции, которые уже скомпилированы, пропускаются.
 * 
 * @param instance Экземпляр ESPB модуля
 * @param progress_cb Опциональный callback для отслеживания прогресса (может быть NULL)
 * @return ESPB_OK если все HOT функции скомпилированы успешно,
 *         или код последней ошибки компиляции (компиляция продолжится для остальных функций)
 */
EspbResult espb_jit_precompile_hot_functions(EspbInstance* instance, espb_jit_precompile_progress_cb progress_cb);

/**
 * @brief Предварительно компилирует конкретную функцию по индексу
 * 
 * @param instance Экземпляр ESPB модуля
 * @param func_idx Глобальный индекс функции (включая импорты)
 * @return ESPB_OK в случае успеха, код ошибки иначе
 */
EspbResult espb_jit_precompile_function(EspbInstance* instance, uint32_t func_idx);

/**
 * @brief Предварительно компилирует функцию по имени (если есть экспорт)
 * 
 * @param instance Экземпляр ESPB модуля
 * @param func_name Имя функции для компиляции
 * @return ESPB_OK в случае успеха, ESPB_ERR_NOT_FOUND если функция не найдена
 */
EspbResult espb_jit_precompile_by_name(EspbInstance* instance, const char* func_name);

/**
 * @brief Получить статистику JIT компиляции
 * 
 * @param instance Экземпляр ESPB модуля
 * @param out_total Общее количество функций (может быть NULL)
 * @param out_hot Количество HOT функций (может быть NULL)
 * @param out_compiled Количество скомпилированных функций (может быть NULL)
 * @param out_jit_size Общий размер JIT кода в байтах (может быть NULL)
 * @return ESPB_OK в случае успеха
 */
EspbResult espb_jit_get_stats(EspbInstance* instance, uint32_t* out_total, uint32_t* out_hot, 
                               uint32_t* out_compiled, size_t* out_jit_size);

#ifdef __cplusplus
}
#endif

#endif // ESPB_JIT_PRECOMPILE_H
