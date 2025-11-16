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

#ifndef ESPB_CALLBACK_SYSTEM_H
#define ESPB_CALLBACK_SYSTEM_H

#include "espb_interpreter_common_types.h"
#include "ffi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Макрос для критичных callback функций, размещаемых в IRAM
#define ESPB_CALLBACK_CRITICAL __attribute__((noinline, optimize("O0")))

// Максимальное количество параметров в callback функции
#define ESPB_CALLBACK_MAX_PARAMS 16

// Структура для хранения информации о callback функции
typedef struct EspbCallbackInfo {
    uint32_t espb_func_idx;           // Индекс ESPB функции для callback
    EspbFuncSignature *espb_signature; // Сигнатура ESPB функции
    ffi_cif native_cif;               // FFI CIF для нативной функции
    ffi_type *native_arg_types[ESPB_CALLBACK_MAX_PARAMS]; // Типы аргументов для FFI
    int32_t user_data_param_index;    // Индекс параметра user_data (-1 если нет)
    void *original_user_data;         // Оригинальные пользовательские данные
    struct EspbCallbackInfo *next;    // Для связанного списка
} EspbCallbackInfo;

// Структура для контекста callback замыкания
typedef struct EspbCallbackClosure {
    ffi_closure *closure_ptr;         // Указатель на FFI замыкание
    void *executable_code;            // Исполняемый код замыкания
    EspbCallbackInfo *callback_info;  // Информация о callback
    EspbInstance *instance;           // Экземпляр ESPB модуля
    struct EspbCallbackClosure *next; // Для связанного списка
} EspbCallbackClosure;

// Глобальная структура для управления callback системой
typedef struct EspbCallbackSystem {
    SemaphoreHandle_t mutex;          // Мьютекс для потокобезопасности
    EspbCallbackClosure *active_closures; // Список активных замыканий
    bool initialized;                 // Флаг инициализации системы
} EspbCallbackSystem;

// Функции для работы с callback системой

/**
 * Инициализация глобальной callback системы
 * @return ESPB_OK при успехе
 */
EspbResult espb_callback_system_init(void);

/**
 * Деинициализация глобальной callback системы
 */
void espb_callback_system_deinit(void);

/**
 * Получение статуса активных замыканий
 * @param instance ESPB инстанс
 * @param out_active_closures Выходной указатель на активные замыкания (может быть NULL)
 * @return ESPB_OK при успехе
 */
EspbResult espb_get_active_closures(EspbClosureCtx **out_active_closures);

/**
 * Получение статистики использования IRAM pool для замыканий
 * @param out_total_size Общий размер IRAM pool (может быть NULL)
 * @param out_used_size Используемый размер (может быть NULL)
 * @param out_free_size Свободный размер (может быть NULL)
 * @return true если IRAM pool включен и статистика доступна
 */
bool espb_callback_get_iram_pool_stats(size_t *out_total_size, size_t *out_used_size, size_t *out_free_size);

/**
 * Создание callback замыкания для указанной функции
 * @param instance Экземпляр ESPB модуля
 * @param exec_ctx Контекст выполнения
 * @param import_idx Индекс импортируемой функции
 * @param callback_param_idx Индекс параметра callback в импортируемой функции
 * @param espb_func_idx Индекс ESPB функции для callback
 * @param user_data_param_idx Индекс параметра user_data в callback (-1 если нет)
 * @param original_user_data Оригинальные пользовательские данные
 * @param out_closure_ptr Выходной указатель на исполняемый код замыкания
 * @return ESPB_OK при успехе
 */
EspbResult espb_create_callback_closure(
    EspbInstance *instance,
    uint16_t import_idx,
    uint8_t callback_param_idx,
    uint32_t espb_func_idx,
    int32_t user_data_param_idx,
    void *original_user_data,
    void **out_closure_ptr
);

/**
 * Освобождение callback замыкания
 * @param closure_ptr Указатель на исполняемый код замыкания
 * @return ESPB_OK при успехе
 */
EspbResult espb_free_callback_closure(void *closure_ptr);

/**
 * Универсальный обработчик callback замыканий
 * КРИТИЧНАЯ ФУНКЦИЯ: размещается в IRAM для максимальной производительности
 * @param cif FFI CIF
 * @param ret_value Указатель на возвращаемое значение
 * @param ffi_args Массив аргументов FFI
 * @param user_data Пользовательские данные (EspbCallbackClosure*)
 */
ESPB_CALLBACK_CRITICAL
void espb_universal_callback_handler(ffi_cif *cif, void *ret_value, void **ffi_args, void *user_data);

/**
 * Автоматическое создание callback для импорта на основе cbmeta
 * @param instance Экземпляр ESPB модуля
 * @param exec_ctx Контекст выполнения
 * @param import_idx Индекс импортируемой функции
 * @param ffi_args Массив аргументов для FFI вызова
 * @param num_args Количество аргументов
 * @return ESPB_OK при успехе
 */
EspbResult espb_auto_create_callbacks_for_import(
    EspbInstance *instance,
    uint16_t import_idx,
    void **ffi_args,
    uint32_t num_args
);

/**
 * Поиск callback информации в cbmeta секции
 * @param module ESPB модуль
 * @param import_idx Индекс импортируемой функции
 * @param out_cbmeta_entry Выходная структура с информацией о callback
 * @return true если найдено
 */
bool espb_find_callback_metadata(
    const EspbModule *module,
    uint16_t import_idx,
    EspbCbmetaImportEntry **out_cbmeta_entry
);

/**
 * Декодирование callback записи из cbmeta
 * КРИТИЧНАЯ ФУНКЦИЯ: размещается в IRAM для быстрого доступа
 * @param entry_data Данные записи (3 байта)
 * @param out_param_idx Выходной индекс параметра callback
 * @param out_espb_func_idx Выходной индекс ESPB функции
 * @param out_user_data_idx Выходной индекс параметра user_data
 */
ESPB_CALLBACK_CRITICAL
void espb_decode_callback_entry(
    const uint8_t *entry_data,
    uint8_t *out_param_idx,
    uint32_t *out_espb_func_idx,
    int8_t *out_user_data_idx
);

#ifdef __cplusplus
}
#endif

#endif // ESPB_CALLBACK_SYSTEM_H
