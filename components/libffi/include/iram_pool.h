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
#ifndef IRAM_POOL_H
#define IRAM_POOL_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Инициализирует статический пул IRAM.
 */
void iram_pool_init(void);

/**
 * @brief Выделяет память из статического пула IRAM.
 * 
 * @param size Размер запрашиваемого блока.
 * @return Указатель на выделенную память или NULL, если места нет.
 */
void* iram_pool_alloc(size_t size);

/**
 * @brief Освобождает ранее выделенный блок из пула.
 * 
 * Для простого линейного аллокатора фактически не освобождает память,
 * но проверяет принадлежность блока пулу.
 */
void iram_pool_free(void* ptr);

/**
 * @brief Выводит отладочную информацию о состоянии IRAM пула.
 */
void iram_pool_debug(void);

/**
 * @brief Получает общий размер IRAM пула.
 * @return Общий размер в байтах
 */
size_t iram_pool_get_total_size(void);

/**
 * @brief Получает используемый размер IRAM пула.
 * @return Используемый размер в байтах
 */
size_t iram_pool_get_used_size(void);

/**
 * @brief Получает свободный размер IRAM пула.
 * @return Свободный размер в байтах
 */
size_t iram_pool_get_free_size(void);

#endif // IRAM_POOL_H 