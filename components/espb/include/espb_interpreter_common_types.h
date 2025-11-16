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

#ifndef ESPB_INTERPRETER_COMMON_TYPES_H
#define ESPB_INTERPRETER_COMMON_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h> // Для FILE* в некоторых структурах (если используется, проверить)

// Зависимости для потокобезопасности
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Включаем ffi.h здесь, чтобы типы libffi были полностью определены
#include "ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Определяем макрос MIN, если он еще не определен
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

// Возвращаемые значения функций интерпретатора
typedef enum {
    ESPB_ERR_INVALID_MAGIC = -1,
    ESPB_ERR_INVALID_HEADER = -2,
    ESPB_ERR_UNSUPPORTED_VERSION = -3,
    ESPB_ERR_BUFFER_TOO_SMALL = -4,
    ESPB_ERR_INVALID_SECTION_TABLE = -5,
    ESPB_ERR_INVALID_SECTION = -6,
    ESPB_ERR_SECTION_NOT_FOUND = -7,
    ESPB_ERR_MEMORY_ALLOC = -8,
    ESPB_ERR_PARSE_ERROR = -9,
    ESPB_ERR_SIGNATURE_OUT_OF_RANGE = -10,
    ESPB_ERR_INVALID_VALUE_TYPE = -11,
    ESPB_ERR_INVALID_TYPES_SECTION = -12,
    ESPB_ERR_INVALID_TYPE_SECTION = -13,
    ESPB_ERR_INVALID_FUNCTION_SECTION = -14,
    ESPB_ERR_INVALID_CODE_SECTION = -15,
    ESPB_ERR_INVALID_MEMORY_SECTION = -16,
    ESPB_ERR_TOO_MANY_SHARED_MEMORIES = -17,
    ESPB_ERR_INVALID_GLOBAL_SECTION = -18,
    ESPB_ERR_INVALID_DATA_SECTION = -19,
    ESPB_ERR_INVALID_RELOCATION = -20,
    ESPB_ERR_INVALID_RELOCATION_SECTION = -21,
    ESPB_ERR_IMPORT_RESOLUTION_FAILED = -22,
    ESPB_ERR_INSTANTIATION_FAILED = -23,
    ESPB_ERR_VALIDATION_FAILED = -24,
    ESPB_ERR_INVALID_FUNC_INDEX = -25,
    ESPB_ERR_TYPE_MISMATCH = -26,
    ESPB_ERR_RUNTIME_ERROR = -27,
    ESPB_ERR_UNDEFINED_BEHAVIOR = -28,
    ESPB_ERR_OUT_OF_MEMORY = -29,
    ESPB_ERR_STACK_OVERFLOW = -30,
    ESPB_ERR_INVALID_OPCODE = -31,
    ESPB_ERR_UNKNOWN_OPCODE = -32,
    ESPB_ERR_INVALID_OPERAND = -33,
    ESPB_ERR_DIVISION_BY_ZERO = -34,
    ESPB_ERR_ARITHMETIC_OVERFLOW = -35,
    ESPB_ERR_UNALIGNED_MEMORY_ACCESS = -36,
    ESPB_ERR_UNINITIALIZED_ELEMENT = -37,
    ESPB_ERR_INVALID_EXPORT_SECTION = -38,
    ESPB_ERR_INVALID_IMPORT_SECTION = -39,
    ESPB_ERR_INVALID_TABLE_SECTION = -40,
    ESPB_ERR_INVALID_ELEMENT_SECTION = -41,
    ESPB_ERR_INVALID_START_SECTION = -42,
    ESPB_ERR_MEMORY_LIMIT_EXCEEDED = -43,
    ESPB_ERR_MEMORY_ACCESS_OUT_OF_BOUNDS = -44,
    ESPB_ERR_STACK_UNDERFLOW = -45,
    ESPB_ERR_INVALID_INIT_EXPR = -46,
    ESPB_ERR_INVALID_GLOBAL_INDEX = -47,
    ESPB_ERR_FEATURE_NOT_SUPPORTED = -48,
    ESPB_ERR_INVALID_REGISTER_INDEX = -49,
    ESPB_ERR_INVALID_MEMORY_INDEX = -50,
    ESPB_ERR_INVALID_CBMETA_SECTION = -51,
    ESPB_ERR_INVALID_IMMETA_SECTION = -52,
    ESPB_ERR_INVALID_STATE = -53,
    ESPB_ERR_INVALID_FUNC_PTR_MAP_SECTION = -54,
    ESPB_ERR_UNSUPPORTED_SIGNATURE = -56,
    ESPB_OK = 0
} EspbResult;

// Коды Типов (u8) из спецификации
typedef enum EspbValueType {
    ESPB_TYPE_UNKNOWN = 0x00,
    ESPB_TYPE_I8      = 0x01,
    ESPB_TYPE_U8      = 0x02,
    ESPB_TYPE_I16     = 0x03,
    ESPB_TYPE_U16     = 0x04,
    ESPB_TYPE_I32     = 0x05,
    ESPB_TYPE_U32     = 0x06,
    ESPB_TYPE_I64     = 0x07,
    ESPB_TYPE_U64     = 0x08,
    ESPB_TYPE_F32     = 0x09,
    ESPB_TYPE_F64     = 0x0A,
    ESPB_TYPE_PTR     = 0x0B,
    ESPB_TYPE_BOOL    = 0x0C, // Может отображаться в i32
    ESPB_TYPE_V128    = 0x0D, // Пока не используется
    ESPB_TYPE_INTERNAL_FUNC_IDX = 0x0E, // Индекс ESPB-функции внутри модуля
    ESPB_TYPE_VOID    = 0x0F  // Используется для обозначения отсутствия возвращаемого значения в сигнатурах
} EspbValueType;

// Forward declarations and basic types needed early
#include "multi_heap.h"

typedef struct EspbHeapContext {
    multi_heap_handle_t heap_handle;
    bool initialized;
} EspbHeapContext;

// Флаги возможностей модуля из заголовка ESPB файла
#define FEATURE_MULTI_RETURN        0x00000001
#define FEATURE_ATOMICS             0x00000002
#define FEATURE_EH                  0x00000004
#define FEATURE_SIMD_PLATFORM       0x00000008
#define FEATURE_BULK_OPERATIONS     0x00000010
#define FEATURE_SIMD_V128           0x00000020
#define FEATURE_SHARED_MEMORY       0x00000040
#define FEATURE_DATA_SYMBOLS        0x00000080
#define FEATURE_CALLBACK_AUTO       0x00000100 // Флаг для автоматической обработки колбэков через libffi
#define FEATURE_MARSHALLING_META    0x00000200 // Флаг для поддержки маршалинга указателей

// Флаг для пометки целочисленных аргументов, представляющих индексы ESPB-функций для колбэков
#define CALLBACK_FLAG_BIT ((uint32_t)0x80000000)  // Флаг для колбэков (внешние функции хоста)
#define FUNCPTR_FLAG_BIT  ((uint32_t)0x40000000)  // Флаг для указателей на ESPB функции

// Структура для хранения сигнатуры функции
typedef struct {
    uint8_t num_params;
    EspbValueType *param_types;
    uint8_t num_returns;
    EspbValueType *return_types;
} EspbFuncSignature;

// Структура заголовка файла ESPB
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t features;
    uint16_t num_sections;
} __attribute__((packed)) EspbHeader;

// Структура записи в таблице секций
typedef struct {
    uint8_t section_id;
    uint8_t reserved_byte;
    uint16_t reserved_ushort;
    uint32_t section_offset;
    uint32_t section_size;
} __attribute__((packed)) SectionHeaderEntry;

// Структура для хранения информации о теле функции из секции Code
typedef struct {
    uint16_t num_virtual_regs;
    uint32_t code_size;
    const uint8_t *code;

    // --- НОВЫЕ ПОЛЯ ДЛЯ DIRECT-THREADED CODE ---
    uint8_t* threaded_code_buffer;     // Указатель на "сырой" буфер с прошитым кодом
    size_t threaded_code_size_bytes; // Размер буфера в байтах
    bool is_threaded;                  // Флаг, показывающий, что трансляция выполнена
    // ---------------------------------------------
} EspbFunctionBody;

// Лимиты для памяти или таблиц
typedef struct {
    uint8_t flags;
    uint32_t initial_size;
    uint32_t max_size;
} EspbMemoryLimits;

// Описание линейной памяти из секции Memory
typedef struct {
    EspbMemoryLimits limits;
} EspbMemoryDesc;

// Тип инициализатора для глобальной переменной
typedef enum {
    ESPB_INIT_KIND_ZERO = 0,
    ESPB_INIT_KIND_CONST = 1,
    ESPB_INIT_KIND_DATA_OFFSET = 2,
} EspbInitKind;

// Описание глобальной переменной из секции Globals
typedef union {
    int64_t const_val_placeholder;
    uint32_t data_section_offset;
} EspbGlobalInitializer;

typedef struct {
    EspbValueType type;
    uint8_t mutability;
    uint8_t shared_flag;
    EspbInitKind init_kind;
    EspbGlobalInitializer initializer;
} EspbGlobalDesc;

// Описание сегмента данных из секции Data
typedef struct {
    uint8_t segment_type;
    uint32_t memory_index;
    const uint8_t *offset_expr;
    size_t offset_expr_len;
    uint32_t data_size;
    const uint8_t *data;
} EspbDataSegment;

// Тип импортируемой сущности
typedef enum {
    ESPB_IMPORT_KIND_FUNC   = 0,
    ESPB_IMPORT_KIND_TABLE  = 1,
    ESPB_IMPORT_KIND_MEMORY = 2,
    ESPB_IMPORT_KIND_GLOBAL = 3,
} EspbImportKind;

// Описание импортируемой сущности из секции Imports
typedef struct {
    char *module_name;
    char *entity_name;
    EspbImportKind kind;
    union {
        struct {
            uint16_t type_idx;
            uint8_t import_flags;
        } func;
        struct {
             uint8_t element_type;
             EspbMemoryLimits limits;
        } table;
        EspbMemoryLimits memory;
        struct {
            EspbValueType type;
            uint8_t mutability;
            uint8_t shared_flag;
        } global;
    } desc;
} EspbImportDesc;

// Тип экспортируемой сущности (совпадает с ImportKind)
typedef EspbImportKind EspbExportKind;

// Типы релокаций (v1.7)
typedef enum {
    ESPB_RELOC_ABS32_FUNC   = 0x01,
    ESPB_RELOC_ABS32_DATA   = 0x02,
    ESPB_RELOC_ABS32_GLOBAL = 0x03,
    ESPB_RELOC_REL32_CALL   = 0x04,
    ESPB_RELOC_REL32_BRANCH = 0x05,
    ESPB_RELOC_FUNC_INDEX   = 0x06,
    ESPB_RELOC_GLOBAL_INDEX = 0x07,
    ESPB_RELOC_TAG_INDEX    = 0x08,
    ESPB_RELOC_TABLE_INDEX  = 0x09,
    ESPB_RELOC_TYPE_INDEX   = 0x0A,
    ESPB_RELOC_MEM_ADDR_I32 = 0x0B,
} EspbRelocType;

// Описание записи релокации из секции Relocations
typedef struct {
    uint8_t target_section_id;
    EspbRelocType type;
    uint32_t offset;
    uint32_t symbol_index;
    int32_t addend;
    bool has_addend;
} EspbRelocationEntry;

// Описание экспортируемой сущности из секции Exports
typedef struct {
    char *name;
    EspbExportKind kind;
    uint32_t index;
} EspbExportDesc;

// Типы ссылок (для таблиц)
typedef enum {
    ESPB_REF_TYPE_FUNCREF = 0x01,
} EspbRefType;

// Описание таблицы из секции Table
typedef struct {
    EspbRefType element_type;
    EspbMemoryLimits limits;
} EspbTableDesc;

// Описание сегмента элементов из секции Element
typedef struct {
    uint32_t flags;
    uint32_t table_index;
    const uint8_t *offset_expr;
    size_t offset_expr_len;
    EspbRefType element_type;
    uint32_t num_elements;
    uint32_t *function_indices;
} EspbElementSegment;

// --- Compact Callback Metadata (cbmeta) ---
#define ESPB_CBTYPE_VOID   0x0
#define ESPB_CBTYPE_I8     0x1
#define ESPB_CBTYPE_U8     0x2
#define ESPB_CBTYPE_I16    0x3
#define ESPB_CBTYPE_U16    0x4
#define ESPB_CBTYPE_I32    0x5
#define ESPB_CBTYPE_U32    0x6
#define ESPB_CBTYPE_I64    0x7
#define ESPB_CBTYPE_U64    0x8
#define ESPB_CBTYPE_F32    0x9
#define ESPB_CBTYPE_F64    0xA
#define ESPB_CBTYPE_PTR    0xB
#define ESPB_CBTYPE_BOOL   0xC

typedef struct {
	uint8_t header;
	uint8_t packed0;
	const uint8_t *extra;
	uint8_t extra_len;
} EspbCbmetaSignature;

typedef struct {
	uint16_t import_index;
	uint8_t num_callbacks;
	const uint8_t *entries;
} EspbCbmetaImportEntry;

typedef struct {
	uint8_t num_signatures;
	EspbCbmetaSignature *signatures;
	uint16_t num_imports_with_cb;
	EspbCbmetaImportEntry *imports;
} EspbCbmeta;

// --- Import Marshalling Metadata (immeta) ---
#define ESPB_IMMETA_DIRECTION_IN     0x01
#define ESPB_IMMETA_DIRECTION_OUT    0x02
#define ESPB_IMMETA_DIRECTION_INOUT  0x03
#define ESPB_IMMETA_SIZE_KIND_CONST     0x00
#define ESPB_IMMETA_SIZE_KIND_FROM_ARG  0x01

typedef struct {
    uint8_t arg_index;       // Индекс аргумента
    uint8_t direction_flags; // Флаги направления (IN/OUT/INOUT)
    uint8_t size_kind;       // Тип размера (константа или из аргумента)
    uint8_t size_value;      // Значение размера
    uint8_t handler_index;   // Тип обработчика (0=стандартный, 1=асинхронный)
} EspbImmetaArgEntry;

typedef struct {
    uint16_t import_index;          // Индекс импорта
    uint8_t num_marshalled_args;    // Количество аргументов с маршалингом
    EspbImmetaArgEntry *args;       // Список аргументов
} EspbImmetaImportEntry;

typedef struct {
    int64_t num_imports_with_meta; // Количество импортов с метаданными маршалинга
    EspbImmetaImportEntry *imports; // Список записей для каждого импорта
} EspbImmeta;

// --- Function Pointer Map (ID=18) ---
typedef struct {
    uint32_t data_offset;
    uint16_t function_index;
} __attribute__((packed)) EspbFuncPtrMapEntry;

// Состояние парсера/загрузчика
typedef struct {
    const uint8_t *buffer;
    size_t buffer_size;
    EspbHeader header;
    SectionHeaderEntry *section_table;
    uint32_t num_signatures;
    EspbFuncSignature *signatures;
    uint32_t num_functions;
    uint16_t *function_signature_indices;
    EspbFunctionBody *function_bodies;
    uint32_t num_memories;
    EspbMemoryDesc *memories;
    uint32_t num_globals;
    EspbGlobalDesc *globals;
    uint32_t num_data_segments;
    EspbDataSegment *data_segments;
    uint32_t num_imports;
    EspbImportDesc *imports;
    uint32_t num_relocations;
    EspbRelocationEntry *relocations;
    uint32_t num_exports;
    EspbExportDesc *exports;
    uint32_t num_tables;
    EspbTableDesc *tables;
    uint32_t num_element_segments;
    EspbElementSegment *element_segments;
    EspbCbmeta cbmeta;
    EspbImmeta immeta;
    bool has_start_function;
    uint32_t start_function_index;
    
    // --- Function Pointer Map ---
    uint32_t num_func_ptr_map_entries;
    EspbFuncPtrMapEntry *func_ptr_map;
    
    // --- Cached values for performance ---
    uint32_t num_imported_funcs;  // Кэшированное количество импортированных функций
} EspbModule;

// === Async Wrapper System для OUT параметров (moved here before EspbInstance) ===

// Структура для хранения информации об OUT параметре
typedef struct {
    uint8_t arg_index;           // Индекс аргумента
    void *espb_memory_ptr;       // Указатель в памяти ESPB
    uint32_t buffer_size;        // Размер для копирования
} AsyncOutParam;

// Контекст для async wrapper
typedef struct AsyncWrapperContext {
    void *original_func_ptr;          // Оригинальная функция
    ffi_cif original_cif;            // CIF для вызова оригинальной функции
    uint8_t num_out_params;          // Количество OUT параметров
    AsyncOutParam *out_params;       // Динамический массив OUT параметров
} AsyncWrapperContext;

// Async wrapper structure
typedef struct AsyncWrapper {
    ffi_closure *closure_ptr;        // FFI closure
    void *executable_code;           // Исполняемый код
    AsyncWrapperContext context;     // Контекст wrapper'а
    bool is_initialized;             // Флаг инициализации
} AsyncWrapper;

// Представляет инстанцированный ESPb модуль
typedef struct EspbInstance {
    const EspbModule *module;
    uint8_t *memory_data;
    uint32_t memory_size_bytes;
    uint32_t memory_max_size_bytes;
    uint8_t *globals_data;
    uint32_t globals_data_size;
    uint32_t *global_offsets;
    void **table_data;
    uint32_t table_size;
    uint32_t table_max_size;
    void **resolved_import_funcs;
    void **resolved_import_globals;
    SemaphoreHandle_t instance_mutex;
    uint32_t passive_data_at_offset_zero_size;
    uint32_t runtime_stack_capacity;
    uint8_t *runtime_sp;
    
    // === Async Wrapper System ===
    AsyncWrapper **async_wrappers;   // Динамический массив async wrappers
    uint32_t num_async_wrappers;     // Количество async wrappers
    // --- ДОБАВИТЬ ЭТИ ПОЛЯ ---
    EspbHeapContext heap_ctx;
    uint32_t static_data_end_offset;
    // --- КОНЕЦ ДОБАВЛЕНИЯ ---
} EspbInstance;

// Представление значения на стеке операндов
typedef struct {
    EspbValueType type;
    union __attribute__((aligned(8))) {
        int8_t   i8;
        uint8_t  u8;
        int16_t  i16;
        uint16_t u16;
        int32_t  i32;
        uint32_t u32;
        int64_t  i64;
        uint64_t u64;
        float    f32;
        double   f64;
        void*    ptr;
    } value;
} Value;

// Представление кадра вызова функции в стеке вызовов
// РЕФАКТОРИНГ: Упрощенная структура для единого виртуального стека
typedef struct RuntimeFrame {
    int ReturnPC;
    size_t SavedFP; // Указатель на кадр вызывающей функции
    uint32_t caller_local_func_idx; // Индекс вызывающей функции для восстановления контекста

    // --- ИСПРАВЛЕНИЕ для CALL_INDIRECT: Сохранение полного кадра ---
    Value* saved_frame;               // Сохраненная копия регистров вызывающей стороны
    size_t saved_num_virtual_regs;    // Количество регистров в сохраненном кадре
    // -----------------------------------------------------------

    // Система отслеживания ALLOCA выделений
    void *alloca_ptrs[16];     // Массив ALLOCA указателей
    uint8_t alloca_count;      // Количество выделений
    bool has_custom_aligned;   // Флаг custom alignment
} RuntimeFrame;

// Контекст выполнения для одного потока
// РЕФАКТОРИНГ: Переход на модель единого виртуального стека
typedef struct ExecutionContext {
    RuntimeFrame* call_stack;
    int call_stack_top;

    // shadow_stack теперь используется как единый виртуальный стек
    uint8_t* shadow_stack_buffer;
    size_t shadow_stack_capacity;  // В байтах
    
    size_t sp; // Указатель стека (смещение в байтах) - было shadow_stack_ptr
    size_t fp; // Указатель кадра (смещение в байтах) - НОВОЕ

    // УДАЛЕНО: Value* registers; - больше не нужен динамический буфер
    // УДАЛЕНО: uint16_t num_virtual_regs; - размер кадра рассчитывается на лету

    // Остальные поля остаются
    uint32_t linear_memory_sp;
    uint32_t next_alloc_offset;
    bool feature_callback_auto_active;
    bool callback_system_initialized;
} ExecutionContext;

// Контекст для универсального диспетчера обратных вызовов
typedef struct CallbackCtx {
    EspbInstance *instance;
    uint32_t func_idx;
    void *user_arg;
} CallbackCtx;

// Контекст для libffi замыкания, передаваемый в C-функцию-обработчик
typedef struct EspbClosureCtx {
    EspbInstance *instance;
    uint32_t espb_func_idx;
    EspbFuncSignature *espb_func_sig;
    void *original_user_data;
    int32_t espb_user_data_param_index;
    ffi_closure *closure_ptr;
    void* executable_code;
    struct EspbClosureCtx *next;
} EspbClosureCtx;

// AsyncWrapper structures moved above EspbInstance definition

#ifdef __cplusplus
}
#endif

#endif // ESPB_INTERPRETER_COMMON_TYPES_H
