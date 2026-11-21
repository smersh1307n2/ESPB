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
#include "espb_interpreter_reader.h"
#include <stdio.h>  // для printf в print_memory_dump
#include <string.h> // для memcpy (если потребуется для более сложных read функций в будущем)

// Читает u8, проверяет границы, обновляет указатель
bool read_u8(const uint8_t **ptr, const uint8_t *end, uint8_t *value) {
    if (*ptr + sizeof(uint8_t) > end) {
        // ESP_LOGE(TAG, "read_u8: Buffer too small"); // Логирование можно добавить при необходимости
        return false;
    }
    *value = **ptr;
    (*ptr)++;
    return true;
}

// Читает u16 (little-endian), проверяет границы, обновляет указатель
bool read_u16(const uint8_t **ptr, const uint8_t *end, uint16_t *value) {
    if (*ptr + sizeof(uint16_t) > end) {
        // ESP_LOGE(TAG, "read_u16: Buffer too small");
        return false;
    }
    *value = (uint16_t)((*ptr)[0] | ((uint16_t)(*ptr)[1] << 8));
    *ptr += sizeof(uint16_t);
    return true;
}

// Читает u32 (little-endian), проверяет границы, обновляет указатель
bool read_u32(const uint8_t **ptr, const uint8_t *end, uint32_t *value) {
    if (*ptr + sizeof(uint32_t) > end) {
        // ESP_LOGE(TAG, "read_u32: Buffer too small");
        return false;
    }
    
    *value = (uint32_t)((*ptr)[0] | 
                     ((uint32_t)(*ptr)[1] << 8) |
                     ((uint32_t)(*ptr)[2] << 16) |
                     ((uint32_t)(*ptr)[3] << 24));
    
    *ptr += sizeof(uint32_t);
    return true;
}

// Читает i32 (little-endian), проверяет границы, обновляет указатель
bool read_i32(const uint8_t **ptr, const uint8_t *end, int32_t *value) {
    if (*ptr + sizeof(int32_t) > end) {
        // ESP_LOGE(TAG, "read_i32: Buffer too small");
        return false;
    }
    uint32_t temp_val;
    temp_val = (uint32_t)((*ptr)[0] | 
                       ((uint32_t)(*ptr)[1] << 8) |
                       ((uint32_t)(*ptr)[2] << 16) |
                       ((uint32_t)(*ptr)[3] << 24));
    *value = (int32_t)temp_val;
    *ptr += sizeof(int32_t);
    return true;
}

// Читает i8 (little-endian), проверяет границы
bool read_i8(const uint8_t **ptr, const uint8_t *end, int8_t *value) {
    if (*ptr + sizeof(int8_t) > end) {
        return false;
    }
    *value = (int8_t)**ptr;
    (*ptr)++;
    return true;
}

// Читает i16 (little-endian), проверяет границы, обновляет указатель
bool read_i16(const uint8_t **ptr, const uint8_t *end, int16_t *value) {
    if (*ptr + sizeof(int16_t) > end) {
        return false;
    }
    uint16_t temp = (uint16_t)((*ptr)[0] | ((uint16_t)(*ptr)[1] << 8));
    *value = (int16_t)temp;
    *ptr += sizeof(int16_t);
    return true;
}

// Читает i64 (little-endian), проверяет границы, обновляет указатель
bool read_i64(const uint8_t **ptr, const uint8_t *end, int64_t *value) {
    if (*ptr + sizeof(int64_t) > end) {
        return false;
    }
    uint64_t temp = (uint64_t)(*ptr)[0] |
                        ((uint64_t)(*ptr)[1] << 8) |
                        ((uint64_t)(*ptr)[2] << 16) |
                        ((uint64_t)(*ptr)[3] << 24) |
                        ((uint64_t)(*ptr)[4] << 32) |
                        ((uint64_t)(*ptr)[5] << 40) |
                        ((uint64_t)(*ptr)[6] << 48) |
                        ((uint64_t)(*ptr)[7] << 56);
    *value = (int64_t)temp;
    *ptr += sizeof(int64_t);
    return true;
}

// Читает u64 (little-endian), проверяет границы, обновляет указатель
bool read_u64(const uint8_t **ptr, const uint8_t *end, uint64_t *value) {
    if (*ptr + sizeof(uint64_t) > end) {
        return false;
    }
    *value = (uint64_t)(*ptr)[0] |
              ((uint64_t)(*ptr)[1] << 8) |
              ((uint64_t)(*ptr)[2] << 16) |
              ((uint64_t)(*ptr)[3] << 24) |
              ((uint64_t)(*ptr)[4] << 32) |
              ((uint64_t)(*ptr)[5] << 40) |
              ((uint64_t)(*ptr)[6] << 48) |
              ((uint64_t)(*ptr)[7] << 56);
    *ptr += sizeof(uint64_t);
    return true;
}

// Читает f32 (little-endian), проверяет границы, обновляет указатель
bool read_f32(const uint8_t **ptr, const uint8_t *end, float *value) {
    if (*ptr + sizeof(float) > end) {
        return false;
    }
    memcpy(value, *ptr, sizeof(float));
    *ptr += sizeof(float);
    return true;
}

// Читает f64 (little-endian), проверяет границы, обновляет указатель
bool read_f64(const uint8_t **ptr, const uint8_t *end, double *value) {
    if (*ptr + sizeof(double) > end) {
        return false;
    }
    memcpy(value, *ptr, sizeof(double));
    *ptr += sizeof(double);
    return true;
}

// Функция для печати дампа памяти в шестнадцатеричном формате
void print_memory_dump(const uint8_t *data, size_t size, const char *prefix) {
    if (!data) {
        printf("%s(null)\n", prefix ? prefix : "");
        return;
    }
    for (size_t i = 0; i < size; i += 16) {
        printf("%s%04zx: ", prefix ? prefix : "", i);
        
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }
        
        printf(" | ");
        
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = data[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }
}
