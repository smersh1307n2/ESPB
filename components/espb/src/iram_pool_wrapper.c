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
// Временная обертка для функций IRAM pool
// Пока не решена проблема с линковкой libffi

#include "iram_pool.h"
#include <stdio.h>

// Простая заглушка для инициализации
void iram_pool_init_wrapper(void) {
    printf("IRAM Pool: Wrapper init called - using libffi implementation\n");
    // Вызываем настоящую функцию, если она доступна
    extern void iram_pool_init(void) __attribute__((weak));
    if (iram_pool_init) {
        iram_pool_init();
    }
}

// Простая заглушка для отладки
void iram_pool_debug_wrapper(void) {
    // printf("IRAM Pool: Wrapper debug called - using libffi implementation\n");
    // // Вызываем настоящую функцию, если она доступна
    // extern void iram_pool_debug(void) __attribute__((weak));
    // if (iram_pool_debug) {
    //     iram_pool_debug();
    // }
}