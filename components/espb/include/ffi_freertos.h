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
#ifndef FFI_FREERTOS_H
#define FFI_FREERTOS_H

#include <ffi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t ffi_freertos_mutex;

/* Структура для передачи параметров в vTaskDelay */
typedef struct {
    TickType_t xTicksToDelay;
} delay_args_t;

typedef struct {
    const char* func_name;
    void (*func_ptr)();
    void* args;
    SemaphoreHandle_t done_semaphore;
} coroutine_context_t;

// Инициализация поддержки FFI для FreeRTOS
void freertos_ffi_init(void);

#define FFI_RTOS_LOG(fmt, ...) \
    do { printf("FFI_RTOS: " fmt, ##__VA_ARGS__); } while (0)

// Макрос для безопасного вызова функции FreeRTOS через FFI
#define FFI_FREERTOS_CALL(func, args) ffi_freertos_call_impl(#func, (void (*)())func, args)

// Внутренняя реализация вызова
void ffi_freertos_call_impl(const char* func_name, void (*func_ptr)(), void* args);

// Обёртка для vTaskDelay
void vTaskDelay_wrapper(TickType_t ticks);

// Функция для тестирования libffi с колбэками
void run_ffi_freertos_test(void);

// Объявление основной тестовой функции для замыканий
void test_libffi_closures(void);

// Структура для хранения данных пользовательского замыкания
typedef struct {
    void (*callback)(ffi_cif*, void*, void**, void*);  // Указатель на функцию обратного вызова
    ffi_cif *cif;                                      // Информация о вызове
    void *user_data;                                   // Пользовательские данные
} esp_ffi_closure_t;

// Создание пользовательского замыкания
esp_ffi_closure_t* esp_ffi_closure_create(
    void (*callback)(ffi_cif*, void*, void**, void*),
    ffi_cif *cif,
    void *user_data
);

// Освобождение ресурсов замыкания
void esp_ffi_closure_free(esp_ffi_closure_t *closure);

// Создание задачи FreeRTOS с замыканием
BaseType_t esp_ffi_task_create(
    esp_ffi_closure_t *closure,
    const char *name,
    uint32_t stack_size,
    void *arg,
    UBaseType_t priority,
    TaskHandle_t *task_handle
);

#ifdef __cplusplus
}
#endif

#endif // FFI_FREERTOS_H