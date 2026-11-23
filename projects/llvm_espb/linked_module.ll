; ModuleID = 'llvm_output\linked_module.bc'
source_filename = "llvm-link"
target datalayout = "e-m:e-p:32:32-i64:64-n32-S128"
target triple = "riscv32-esp-unknown-elf"

%struct.SensorData = type { i32, float, [16 x i8], i8 }

@llvm.global.annotations = appending global [31 x { ptr, ptr, ptr, i32, ptr }] [{ ptr, ptr, ptr, i32, ptr } { ptr @add_function_1, ptr @.str.403, ptr @.str.404, i32 1173, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @print_int_array, ptr @.str.403, ptr @.str.404, i32 866, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_memory_operations, ptr @.str.403, ptr @.str.404, i32 393, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_i64_imm8_ops, ptr @.str.403, ptr @.str.404, i32 781, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_table_operations, ptr @.str.403, ptr @.str.404, i32 1191, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @add_function, ptr @.str.403, ptr @.str.404, i32 1387, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @counter_task, ptr @.str.403, ptr @.str.404, i32 128, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @call_volatile_function, ptr @.str.403, ptr @.str.404, i32 216, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @multiply_function_1, ptr @.str.403, ptr @.str.404, i32 1179, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_call_indirect_non_optimizable, ptr @.str.403, ptr @.str.404, i32 223, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @multiply_function, ptr @.str.403, ptr @.str.404, i32 1393, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_while_and_do_while_loops, ptr @.str.403, ptr @.str.404, i32 1005, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_break_and_continue, ptr @.str.403, ptr @.str.404, i32 1086, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @subtract_function, ptr @.str.403, ptr @.str.404, i32 1399, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_call_indirect_ptr_unsafe, ptr @.str.403, ptr @.str.404, i32 1419, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_complex_data_types, ptr @.str.403, ptr @.str.404, i32 884, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @mul_func, ptr @.str.403, ptr @.str.404, i32 153, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @cb_with_int, ptr @.str.403, ptr @.str.404, i32 112, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @modify_sensor_by_ref, ptr @.str.403, ptr @.str.404, i32 877, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @call_function_by_address, ptr @.str.403, ptr @.str.404, i32 1407, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @add_func, ptr @.str.403, ptr @.str.404, i32 146, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @call_operation_by_index, ptr @.str.403, ptr @.str.404, i32 207, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_control_flow, ptr @.str.403, ptr @.str.404, i32 468, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_timer_cb, ptr @.str.403, ptr @.str.404, i32 119, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_second, ptr @.str.403, ptr @.str.404, i32 1553, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_call_indirect_with_table, ptr @.str.403, ptr @.str.404, i32 1262, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @cb_print, ptr @.str.403, ptr @.str.404, i32 107, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_call_indirect, ptr @.str.403, ptr @.str.404, i32 169, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @call_operation, ptr @.str.403, ptr @.str.404, i32 161, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @subtract_function_1, ptr @.str.403, ptr @.str.404, i32 1185, ptr null }, { ptr, ptr, ptr, i32, ptr } { ptr @test_first, ptr @.str.403, ptr @.str.404, i32 1468, ptr null }], section "llvm.metadata"
@g_my_global_int = dso_local local_unnamed_addr global i32 123, align 4, !dbg !0
@g_my_global_long = dso_local local_unnamed_addr global i64 9876543210, align 8, !dbg !66
@g_my_global_bool = dso_local local_unnamed_addr global i8 0, align 1, !dbg !68
@.str = private unnamed_addr constant [36 x i8] c"counter_task start, pvParameter=%p\0A\00", align 1, !dbg !71
@.str.1 = private unnamed_addr constant [13 x i8] c"num is = %d\0A\00", align 1, !dbg !76
@.str.2 = private unnamed_addr constant [31 x i8] c"add_func called: %d + %d = %d\0A\00", align 1, !dbg !81
@.str.3 = private unnamed_addr constant [31 x i8] c"mul_func called: %d * %d = %d\0A\00", align 1, !dbg !86
@.str.4 = private unnamed_addr constant [54 x i8] c"call_operation: calling function pointer with %d, %d\0A\00", align 1, !dbg !88
@.str.6 = private unnamed_addr constant [24 x i8] c"Direct call result: %d\0A\00", align 1, !dbg !95
@.str.7 = private unnamed_addr constant [32 x i8] c"Indirect call result (add): %d\0A\00", align 1, !dbg !100
@.str.8 = private unnamed_addr constant [32 x i8] c"Indirect call result (mul): %d\0A\00", align 1, !dbg !105
@.str.9 = private unnamed_addr constant [37 x i8] c"Changed function pointer result: %d\0A\00", align 1, !dbg !107
@.str.11 = private unnamed_addr constant [53 x i8] c"call_operation_by_index: op=%d, calling with %d, %d\0A\00", align 1, !dbg !117
@operations = internal unnamed_addr constant [2 x ptr] [ptr @add_func, ptr @mul_func], align 4, !dbg !1090
@.str.12 = private unnamed_addr constant [45 x i8] c"call_volatile_function: calling with %d, %d\0A\00", align 1, !dbg !122
@.str.14 = private unnamed_addr constant [34 x i8] c"Result from array call (add): %d\0A\00", align 1, !dbg !132
@.str.15 = private unnamed_addr constant [34 x i8] c"Result from array call (mul): %d\0A\00", align 1, !dbg !137
@.str.16 = private unnamed_addr constant [31 x i8] c"Result from volatile call: %d\0A\00", align 1, !dbg !139
@.str.19 = private unnamed_addr constant [29 x i8] c"Initial g_my_global_int: %d\0A\00", align 1, !dbg !151
@.str.20 = private unnamed_addr constant [32 x i8] c"Initial g_my_global_long: %lld\0A\00", align 1, !dbg !156
@.str.21 = private unnamed_addr constant [30 x i8] c"Initial g_my_global_bool: %s\0A\00", align 1, !dbg !158
@.str.22 = private unnamed_addr constant [5 x i8] c"true\00", align 1, !dbg !163
@.str.23 = private unnamed_addr constant [6 x i8] c"false\00", align 1, !dbg !168
@.str.24 = private unnamed_addr constant [31 x i8] c"\0AModified g_my_global_int: %d\0A\00", align 1, !dbg !173
@.str.25 = private unnamed_addr constant [33 x i8] c"Modified g_my_global_long: %lld\0A\00", align 1, !dbg !175
@.str.26 = private unnamed_addr constant [31 x i8] c"Modified g_my_global_bool: %s\0A\00", align 1, !dbg !180
@.str.29 = private unnamed_addr constant [40 x i8] c"i32_a=%d, i32_b=%d, u32_a=%u, u32_b=%u\0A\00", align 1, !dbg !189
@.str.30 = private unnamed_addr constant [9 x i8] c"ADD: %d\0A\00", align 1, !dbg !191
@.str.31 = private unnamed_addr constant [9 x i8] c"SUB: %d\0A\00", align 1, !dbg !196
@.str.32 = private unnamed_addr constant [9 x i8] c"MUL: %d\0A\00", align 1, !dbg !198
@.str.33 = private unnamed_addr constant [10 x i8] c"DIVS: %d\0A\00", align 1, !dbg !200
@.str.34 = private unnamed_addr constant [10 x i8] c"REMS: %d\0A\00", align 1, !dbg !205
@.str.35 = private unnamed_addr constant [10 x i8] c"DIVU: %u\0A\00", align 1, !dbg !207
@.str.36 = private unnamed_addr constant [10 x i8] c"REMU: %u\0A\00", align 1, !dbg !209
@.str.37 = private unnamed_addr constant [9 x i8] c"AND: %d\0A\00", align 1, !dbg !211
@.str.38 = private unnamed_addr constant [8 x i8] c"OR: %d\0A\00", align 1, !dbg !213
@.str.39 = private unnamed_addr constant [9 x i8] c"XOR: %d\0A\00", align 1, !dbg !218
@.str.40 = private unnamed_addr constant [9 x i8] c"SHL: %d\0A\00", align 1, !dbg !220
@.str.41 = private unnamed_addr constant [10 x i8] c"SHRS: %d\0A\00", align 1, !dbg !222
@.str.42 = private unnamed_addr constant [10 x i8] c"SHRU: %u\0A\00", align 1, !dbg !224
@.str.43 = private unnamed_addr constant [9 x i8] c"NOT: %d\0A\00", align 1, !dbg !226
@.str.46 = private unnamed_addr constant [48 x i8] c"i64_a=%lld, i64_b=%lld, u64_a=%llu, u64_b=%llu\0A\00", align 1, !dbg !232
@.str.47 = private unnamed_addr constant [11 x i8] c"ADD: %lld\0A\00", align 1, !dbg !237
@.str.48 = private unnamed_addr constant [11 x i8] c"SUB: %lld\0A\00", align 1, !dbg !242
@.str.49 = private unnamed_addr constant [11 x i8] c"MUL: %lld\0A\00", align 1, !dbg !244
@.str.50 = private unnamed_addr constant [12 x i8] c"DIVS: %lld\0A\00", align 1, !dbg !246
@.str.51 = private unnamed_addr constant [12 x i8] c"REMS: %lld\0A\00", align 1, !dbg !251
@.str.52 = private unnamed_addr constant [12 x i8] c"DIVU: %llu\0A\00", align 1, !dbg !253
@.str.53 = private unnamed_addr constant [12 x i8] c"REMU: %llu\0A\00", align 1, !dbg !255
@.str.54 = private unnamed_addr constant [11 x i8] c"AND: %lld\0A\00", align 1, !dbg !257
@.str.55 = private unnamed_addr constant [10 x i8] c"OR: %lld\0A\00", align 1, !dbg !259
@.str.56 = private unnamed_addr constant [11 x i8] c"XOR: %lld\0A\00", align 1, !dbg !261
@.str.57 = private unnamed_addr constant [11 x i8] c"SHL: %lld\0A\00", align 1, !dbg !263
@.str.58 = private unnamed_addr constant [12 x i8] c"SHRS: %lld\0A\00", align 1, !dbg !265
@.str.59 = private unnamed_addr constant [12 x i8] c"SHRU: %llu\0A\00", align 1, !dbg !267
@.str.60 = private unnamed_addr constant [11 x i8] c"NOT: %lld\0A\00", align 1, !dbg !269
@.str.62 = private unnamed_addr constant [20 x i8] c"f32_a=%f, f32_b=%f\0A\00", align 1, !dbg !273
@.str.63 = private unnamed_addr constant [9 x i8] c"ADD: %f\0A\00", align 1, !dbg !278
@.str.64 = private unnamed_addr constant [9 x i8] c"SUB: %f\0A\00", align 1, !dbg !280
@.str.65 = private unnamed_addr constant [9 x i8] c"MUL: %f\0A\00", align 1, !dbg !282
@.str.66 = private unnamed_addr constant [9 x i8] c"DIV: %f\0A\00", align 1, !dbg !284
@.str.67 = private unnamed_addr constant [9 x i8] c"ABS: %f\0A\00", align 1, !dbg !286
@.str.68 = private unnamed_addr constant [10 x i8] c"SQRT: %f\0A\00", align 1, !dbg !288
@.str.69 = private unnamed_addr constant [9 x i8] c"MIN: %f\0A\00", align 1, !dbg !290
@.str.70 = private unnamed_addr constant [9 x i8] c"MAX: %f\0A\00", align 1, !dbg !292
@.str.72 = private unnamed_addr constant [22 x i8] c"f64_a=%lf, f64_b=%lf\0A\00", align 1, !dbg !296
@.str.73 = private unnamed_addr constant [10 x i8] c"ADD: %lf\0A\00", align 1, !dbg !301
@.str.74 = private unnamed_addr constant [10 x i8] c"SUB: %lf\0A\00", align 1, !dbg !303
@.str.75 = private unnamed_addr constant [10 x i8] c"MUL: %lf\0A\00", align 1, !dbg !305
@.str.76 = private unnamed_addr constant [10 x i8] c"DIV: %lf\0A\00", align 1, !dbg !307
@.str.77 = private unnamed_addr constant [10 x i8] c"ABS: %lf\0A\00", align 1, !dbg !309
@.str.78 = private unnamed_addr constant [11 x i8] c"SQRT: %lf\0A\00", align 1, !dbg !311
@.str.79 = private unnamed_addr constant [10 x i8] c"MIN: %lf\0A\00", align 1, !dbg !313
@.str.80 = private unnamed_addr constant [10 x i8] c"MAX: %lf\0A\00", align 1, !dbg !315
@.str.82 = private unnamed_addr constant [24 x i8] c"CVT.F64.I32: %lf -> %d\0A\00", align 1, !dbg !322
@.str.83 = private unnamed_addr constant [25 x i8] c"CVT.I64.F32: %lld -> %f\0A\00", align 1, !dbg !324
@.str.84 = private unnamed_addr constant [29 x i8] c"TRUNC.I32.I16: 123456 -> %d\0A\00", align 1, !dbg !326
@.str.85 = private unnamed_addr constant [23 x i8] c"SEXT.I8.I32: -5 -> %d\0A\00", align 1, !dbg !328
@.str.89 = private unnamed_addr constant [38 x i8] c"STORE/LOAD I32: Stored %d, Loaded %d\0A\00", align 1, !dbg !345
@.str.90 = private unnamed_addr constant [42 x i8] c"STORE/LOAD I64: Stored %lld, Loaded %lld\0A\00", align 1, !dbg !350
@.str.91 = private unnamed_addr constant [38 x i8] c"STORE/LOAD F32: Stored %f, Loaded %f\0A\00", align 1, !dbg !352
@.str.92 = private unnamed_addr constant [40 x i8] c"STORE/LOAD F64: Stored %lf, Loaded %lf\0A\00", align 1, !dbg !354
@.str.93 = private unnamed_addr constant [37 x i8] c"STORE/LOAD I8: Stored %d, Loaded %d\0A\00", align 1, !dbg !356
@.str.94 = private unnamed_addr constant [37 x i8] c"STORE/LOAD U8: Stored %u, Loaded %u\0A\00", align 1, !dbg !358
@.str.95 = private unnamed_addr constant [38 x i8] c"STORE/LOAD I16: Stored %d, Loaded %d\0A\00", align 1, !dbg !360
@.str.96 = private unnamed_addr constant [38 x i8] c"STORE/LOAD U16: Stored %u, Loaded %u\0A\00", align 1, !dbg !362
@.str.97 = private unnamed_addr constant [38 x i8] c"STORE/LOAD PTR: Stored %p, Loaded %p\0A\00", align 1, !dbg !364
@.str.110 = private unnamed_addr constant [29 x i8] c"  Loop i=%d, current sum=%d\0A\00", align 1, !dbg !396
@.str.111 = private unnamed_addr constant [29 x i8] c"Final sum: %d (expected 15)\0A\00", align 1, !dbg !398
@.str.115 = private unnamed_addr constant [30 x i8] c"Testing switch for value %d: \00", align 1, !dbg !406
@.str.124 = private unnamed_addr constant [64 x i8] c"Before call, magic_container = %d (Address in ESPB memory: %p)\0A\00", align 1, !dbg !430
@.str.125 = private unnamed_addr constant [34 x i8] c"After call, magic_container = %d\0A\00", align 1, !dbg !435
@.str.130 = private unnamed_addr constant [23 x i8] c"SELECT.I32 result: %d\0A\00", align 1, !dbg !448
@.str.131 = private unnamed_addr constant [25 x i8] c"SELECT.I64 result: %lld\0A\00", align 1, !dbg !450
@.str.132 = private unnamed_addr constant [23 x i8] c"SELECT.F32 result: %f\0A\00", align 1, !dbg !452
@.str.133 = private unnamed_addr constant [24 x i8] c"SELECT.F64 result: %lf\0A\00", align 1, !dbg !454
@.str.134 = private unnamed_addr constant [23 x i8] c"SELECT.PTR result: %p\0A\00", align 1, !dbg !456
@.str.137 = private unnamed_addr constant [38 x i8] c"PTRTOINT: Address of my_var is 0x%lx\0A\00", align 1, !dbg !462
@.str.138 = private unnamed_addr constant [38 x i8] c"OK: INTTOPTR successful, value is %d\0A\00", align 1, !dbg !464
@.str.143 = private unnamed_addr constant [30 x i8] c"Initial atomic I32 value: %d\0A\00", align 1, !dbg !474
@.str.144 = private unnamed_addr constant [28 x i8] c"ATOMIC.ADD: old=%d, new=%d\0A\00", align 1, !dbg !476
@.str.145 = private unnamed_addr constant [29 x i8] c"ATOMIC.XCHG: old=%d, new=%d\0A\00", align 1, !dbg !481
@.str.146 = private unnamed_addr constant [47 x i8] c"ATOMIC.CMPXCHG (success): expected=42, new=%d\0A\00", align 1, !dbg !483
@.str.147 = private unnamed_addr constant [71 x i8] c"ATOMIC.CMPXCHG (failure): expected=99, old_val_in_expected=%d, new=%d\0A\00", align 1, !dbg !485
@.str.149 = private unnamed_addr constant [32 x i8] c"Initial atomic I64 value: %lld\0A\00", align 1, !dbg !492
@.str.150 = private unnamed_addr constant [36 x i8] c"ATOMIC.SUB.I64: old=%lld, new=%lld\0A\00", align 1, !dbg !494
@.str.151 = private unnamed_addr constant [49 x i8] c"ATOMIC.STORE/LOAD I64: stored=5555, loaded=%lld\0A\00", align 1, !dbg !496
@.str.156 = private unnamed_addr constant [63 x i8] c"OK: malloc(128) succeeded. Allocated buffer at offset/ptr %p.\0A\00", align 1, !dbg !512
@.str.157 = private unnamed_addr constant [12 x i8] c"Test Malloc\00", align 1, !dbg !517
@.str.161 = private unnamed_addr constant [57 x i8] c"Copying %zu bytes from static data to dynamic buffer...\0A\00", align 1, !dbg !531
@STATIC_DATA_FOR_INIT = internal constant [58 x i8] c"Hello from a static data segment! This tests MEMORY.INIT.\00", align 1, !dbg !1093
@.str.170 = private unnamed_addr constant [78 x i8] c"OK: Memory was successfully reused after free(). Allocated new buffer at %p.\0A\00", align 1, !dbg !555
@.str.174 = private unnamed_addr constant [44 x i8] c"Initial values: i64_a = %lld, u64_a = %llu\0A\00", align 1, !dbg !572
@.str.175 = private unnamed_addr constant [34 x i8] c"DIVS.I64.IMM8: %lld / -10 = %lld\0A\00", align 1, !dbg !577
@.str.176 = private unnamed_addr constant [33 x i8] c"DIVU.I64.IMM8: %llu / 20 = %llu\0A\00", align 1, !dbg !579
@.str.177 = private unnamed_addr constant [34 x i8] c"REMS.I64.IMM8: %lld %% 13 = %lld\0A\00", align 1, !dbg !581
@.str.178 = private unnamed_addr constant [34 x i8] c"REMU.I64.IMM8: %llu %% 17 = %llu\0A\00", align 1, !dbg !583
@.str.179 = private unnamed_addr constant [33 x i8] c"SHRU.I64.IMM8: %llu >> 3 = %llu\0A\00", align 1, !dbg !585
@.str.181 = private unnamed_addr constant [37 x i8] c"  [Helper] Printing array contents: \00", align 1, !dbg !589
@.str.182 = private unnamed_addr constant [4 x i8] c"%d \00", align 1, !dbg !591
@.str.184 = private unnamed_addr constant [16 x i8] c"Modified Sensor\00", align 1, !dbg !601
@.str.187 = private unnamed_addr constant [16 x i8] c"Initial array: \00", align 1, !dbg !610
@.str.188 = private unnamed_addr constant [41 x i8] c"Value at index 2 after modification: %d\0A\00", align 1, !dbg !612
@.str.189 = private unnamed_addr constant [37 x i8] c"Matrix[1][1] value: %d (expected 5)\0A\00", align 1, !dbg !614
@.str.191 = private unnamed_addr constant [13 x i8] c"TempSensor-A\00", align 1, !dbg !618
@.str.192 = private unnamed_addr constant [47 x i8] c"Sensor ID: %d, Name: %s, Temp: %f, Active: %s\0A\00", align 1, !dbg !620
@.str.193 = private unnamed_addr constant [4 x i8] c"yes\00", align 1, !dbg !622
@.str.194 = private unnamed_addr constant [3 x i8] c"no\00", align 1, !dbg !624
@.str.195 = private unnamed_addr constant [37 x i8] c"Temperature updated via pointer: %f\0A\00", align 1, !dbg !629
@.str.196 = private unnamed_addr constant [57 x i8] c"After modify_sensor_by_ref: Name=%s, Temp=%f, Active=%s\0A\00", align 1, !dbg !631
@.str.198 = private unnamed_addr constant [40 x i8] c"Size of DataConverter union: %zu bytes\0A\00", align 1, !dbg !635
@.str.199 = private unnamed_addr constant [20 x i8] c"Assigned float: %f\0A\00", align 1, !dbg !637
@.str.200 = private unnamed_addr constant [33 x i8] c"  Read as integer (hex): 0x%08X\0A\00", align 1, !dbg !639
@.str.201 = private unnamed_addr constant [48 x i8] c"  Read as raw bytes (hex): %02x %02x %02x %02x\0A\00", align 1, !dbg !641
@.str.202 = private unnamed_addr constant [23 x i8] c"\0AAssigned integer: %d\0A\00", align 1, !dbg !643
@.str.203 = private unnamed_addr constant [60 x i8] c"  Read as float: %f (value is now interpreted differently)\0A\00", align 1, !dbg !645
@.str.205 = private unnamed_addr constant [49 x i8] c"Initial state: %d (expected 1 for STATE_ACTIVE)\0A\00", align 1, !dbg !652
@.str.211 = private unnamed_addr constant [16 x i8] c"Engine-1\00\00\00\00\00\00\00\00", align 4, !dbg !670
@.str.212 = private unnamed_addr constant [16 x i8] c"Freezer-3\00\00\00\00\00\00\00", align 4, !dbg !672
@.str.214 = private unnamed_addr constant [30 x i8] c"  Sensor #%d: ID=%d, Name=%s\0A\00", align 1, !dbg !676
@.str.216 = private unnamed_addr constant [55 x i8] c"Size of DeviceStatus struct: %zu byte(s) (expected 1)\0A\00", align 1, !dbg !680
@.str.217 = private unnamed_addr constant [47 x i8] c"Status check: Power=%u, WiFi=%u, ErrorCode=%u\0A\00", align 1, !dbg !682
@.str.224 = private unnamed_addr constant [35 x i8] c"    Loop iter: counter=%d, sum=%d\0A\00", align 1, !dbg !696
@.str.225 = private unnamed_addr constant [31 x i8] c"  Final sum: %d (expected 10)\0A\00", align 1, !dbg !701
@.str.238 = private unnamed_addr constant [26 x i8] c"  Counter after loop: %d\0A\00", align 1, !dbg !733
@.str.245 = private unnamed_addr constant [31 x i8] c"  Final sum: %d (expected 25)\0A\00", align 1, !dbg !750
@.str.250 = private unnamed_addr constant [53 x i8] c"  Loop exited. Final counter value: %d (expected 5)\0A\00", align 1, !dbg !769
@.str.254 = private unnamed_addr constant [41 x i8] c"  Outer loop ran %d times (expected 3).\0A\00", align 1, !dbg !780
@.str.255 = private unnamed_addr constant [43 x i8] c"  Total inner loop runs: %d (expected 9).\0A\00", align 1, !dbg !782
@.str.259 = private unnamed_addr constant [31 x i8] c"  add_function(%d, %d) called\0A\00", align 1, !dbg !790
@.str.260 = private unnamed_addr constant [36 x i8] c"  multiply_function(%d, %d) called\0A\00", align 1, !dbg !792
@.str.261 = private unnamed_addr constant [36 x i8] c"  subtract_function(%d, %d) called\0A\00", align 1, !dbg !794
@.str.264 = private unnamed_addr constant [31 x i8] c"  Initial size of table 0: %d\0A\00", align 1, !dbg !800
@.str.265 = private unnamed_addr constant [43 x i8] c"  ERROR: Expected initial size 10, got %d\0A\00", align 1, !dbg !802
@.str.280 = private unnamed_addr constant [29 x i8] c"  Final size of table 0: %d\0A\00", align 1, !dbg !838
@.str.281 = private unnamed_addr constant [66 x i8] c"  ERROR: Expected size >= 9 (expanded to fit operations), got %d\0A\00", align 1, !dbg !840
@.str.282 = private unnamed_addr constant [45 x i8] c"  PASS: Table size expanded correctly to %d\0A\00", align 1, !dbg !842
@.str.285 = private unnamed_addr constant [28 x i8] c"  Size after expansion: %d\0A\00", align 1, !dbg !851
@.str.286 = private unnamed_addr constant [38 x i8] c"  ERROR: Expected size >= 18, got %d\0A\00", align 1, !dbg !853
@.str.287 = private unnamed_addr constant [35 x i8] c"  PASS: Table expanded to size %d\0A\00", align 1, !dbg !855
@.str.303 = private unnamed_addr constant [30 x i8] c"  table[0] = %d (expected 0)\0A\00", align 1, !dbg !887
@.str.304 = private unnamed_addr constant [30 x i8] c"  table[1] = %d (expected 1)\0A\00", align 1, !dbg !889
@.str.305 = private unnamed_addr constant [30 x i8] c"  table[2] = %d (expected 2)\0A\00", align 1, !dbg !891
@.str.315 = private unnamed_addr constant [28 x i8] c"  Result: %d (expected 15)\0A\00", align 1, !dbg !914
@.str.317 = private unnamed_addr constant [28 x i8] c"  Result: %d (expected 50)\0A\00", align 1, !dbg !918
@.str.319 = private unnamed_addr constant [27 x i8] c"  Result: %d (expected 5)\0A\00", align 1, !dbg !922
@.str.322 = private unnamed_addr constant [49 x i8] c"  Iteration %d: calling func_table[%d](7, 3)...\0A\00", align 1, !dbg !931
@.str.323 = private unnamed_addr constant [32 x i8] c"  -> func_table[%d](7, 3) = %d\0A\00", align 1, !dbg !933
@.str.324 = private unnamed_addr constant [50 x i8] c"  ERROR: func_table[%d] returned %d, expected %d\0A\00", align 1, !dbg !935
@.str.331 = private unnamed_addr constant [49 x i8] c"  \E2\9C\93 Function indices stored: 0=%d, 1=%d, 2=%d\0A\00", align 1, !dbg !949
@.str.334 = private unnamed_addr constant [66 x i8] c"  Static calls: add=%d (exp 15), mul=%d (exp 50), sub=%d (exp 5)\0A\00", align 1, !dbg !955
@.str.337 = private unnamed_addr constant [56 x i8] c"    call_function_by_address: calling %p with (%d, %d)\0A\00", align 1, !dbg !961
@.str.342 = private unnamed_addr constant [23 x i8] c"    add_function = %p\0A\00", align 1, !dbg !971
@.str.343 = private unnamed_addr constant [28 x i8] c"    multiply_function = %p\0A\00", align 1, !dbg !973
@.str.344 = private unnamed_addr constant [28 x i8] c"    subtract_function = %p\0A\00", align 1, !dbg !975
@.str.347 = private unnamed_addr constant [50 x i8] c"  call_by_address(add, 12, 8) = %d (expected 20)\0A\00", align 1, !dbg !981
@.str.349 = private unnamed_addr constant [49 x i8] c"  call_by_address(mul, 6, 7) = %d (expected 42)\0A\00", align 1, !dbg !985
@.str.351 = private unnamed_addr constant [49 x i8] c"  call_by_address(sub, 15, 6) = %d (expected 9)\0A\00", align 1, !dbg !989
@.str.355 = private unnamed_addr constant [21 x i8] c"    Got: %d, %d, %d\0A\00", align 1, !dbg !997
@.str.358 = private unnamed_addr constant [19 x i8] c"first num is = %d\0A\00", align 1, !dbg !1003
@.str.370 = private unnamed_addr constant [18 x i8] c"f1 = %f, f2 = %f\0A\00", align 1, !dbg !1030
@.str.371 = private unnamed_addr constant [14 x i8] c"f1 + f2 = %f\0A\00", align 1, !dbg !1032
@.str.372 = private unnamed_addr constant [14 x i8] c"f2 - f1 = %f\0A\00", align 1, !dbg !1034
@.str.373 = private unnamed_addr constant [14 x i8] c"f1 * f2 = %f\0A\00", align 1, !dbg !1036
@.str.374 = private unnamed_addr constant [15 x i8] c"f2 / f1 = %f\0A\0A\00", align 1, !dbg !1038
@.str.377 = private unnamed_addr constant [20 x i8] c"d1 = %lf, d2 = %lf\0A\00", align 1, !dbg !1044
@.str.378 = private unnamed_addr constant [15 x i8] c"d1 + d2 = %lf\0A\00", align 1, !dbg !1046
@.str.379 = private unnamed_addr constant [15 x i8] c"d2 - d1 = %lf\0A\00", align 1, !dbg !1048
@.str.380 = private unnamed_addr constant [15 x i8] c"d1 * d2 = %lf\0A\00", align 1, !dbg !1050
@.str.381 = private unnamed_addr constant [16 x i8] c"d2 / d1 = %lf\0A\0A\00", align 1, !dbg !1052
@.str.386 = private unnamed_addr constant [4 x i8] c"tcb\00", align 1, !dbg !1062
@.str.389 = private unnamed_addr constant [13 x i8] c"counter_task\00", align 1, !dbg !1068
@.str.390 = private unnamed_addr constant [26 x i8] c"Main loop iteration = %d\0A\00", align 1, !dbg !1070
@.str.400 = private unnamed_addr constant [24 x i8] c"cb_print: user_data=%p\0A\00", align 1, !dbg !1099
@.str.401 = private unnamed_addr constant [33 x i8] c"cb_with_int: x=%d, user_data=%p\0A\00", align 1, !dbg !1101
@test_timer_cb.tick = internal unnamed_addr global i32 0, align 4, !dbg !1103
@.str.402 = private unnamed_addr constant [37 x i8] c"test_timer_cb: tick=%d, timer_ud=%p\0A\00", align 1, !dbg !1115
@.str.403 = private unnamed_addr constant [13 x i8] c"espb_jit_hot\00", section "llvm.metadata"
@.str.404 = private unnamed_addr constant [90 x i8] c"c:\\andrey\\projects\\vs_code\\espb prodaction\\esp32_prj_to_llvm\\main\\ir_gen_temp\\all_tests.c\00", section "llvm.metadata"
@str = private unnamed_addr constant [30 x i8] c"=== Testing CALL_INDIRECT ===\00", align 1
@str.405 = private unnamed_addr constant [39 x i8] c"=== CALL_INDIRECT tests completed ===\0A\00", align 1
@str.406 = private unnamed_addr constant [46 x i8] c"=== Testing Non-Optimizable CALL_INDIRECT ===\00", align 1
@str.407 = private unnamed_addr constant [55 x i8] c"=== Non-Optimizable CALL_INDIRECT tests completed ===\0A\00", align 1
@str.408 = private unnamed_addr constant [25 x i8] c"\0A=== Testing Globals ===\00", align 1
@str.409 = private unnamed_addr constant [32 x i8] c"=== Globals test completed ===\0A\00", align 1
@str.410 = private unnamed_addr constant [24 x i8] c"\0A--- I32 Operations ---\00", align 1
@str.411 = private unnamed_addr constant [33 x i8] c"\0A=== Testing Math Operations ===\00", align 1
@str.412 = private unnamed_addr constant [24 x i8] c"\0A--- I64 Operations ---\00", align 1
@str.413 = private unnamed_addr constant [24 x i8] c"\0A--- F32 Operations ---\00", align 1
@str.414 = private unnamed_addr constant [24 x i8] c"\0A--- F64 Operations ---\00", align 1
@str.415 = private unnamed_addr constant [26 x i8] c"\0A--- Type Conversions ---\00", align 1
@str.416 = private unnamed_addr constant [40 x i8] c"=== Math Operations test completed ===\0A\00", align 1
@str.417 = private unnamed_addr constant [35 x i8] c"\0A=== Testing Memory Operations ===\00", align 1
@str.418 = private unnamed_addr constant [41 x i8] c"ALLOCA: Allocated a buffer on the stack.\00", align 1
@str.419 = private unnamed_addr constant [42 x i8] c"=== Memory Operations test completed ===\0A\00", align 1
@str.420 = private unnamed_addr constant [30 x i8] c"\0A=== Testing Control Flow ===\00", align 1
@str.421 = private unnamed_addr constant [28 x i8] c"\0A--- 1. Testing if-else ---\00", align 1
@str.422 = private unnamed_addr constant [28 x i8] c"OK: a < b is true (10 < 20)\00", align 1
@str.423 = private unnamed_addr constant [29 x i8] c"OK: a > b is false (10 > 20)\00", align 1
@str.424 = private unnamed_addr constant [20 x i8] c"OK: a == 10 is true\00", align 1
@str.425 = private unnamed_addr constant [35 x i8] c"OK: d1 < d2 is true (3.14 < 3.141)\00", align 1
@str.426 = private unnamed_addr constant [29 x i8] c"\0A--- 2. Testing for loop ---\00", align 1
@str.427 = private unnamed_addr constant [25 x i8] c"OK: Loop sum is correct.\00", align 1
@str.429 = private unnamed_addr constant [32 x i8] c"\0A--- 3. Testing switch-case ---\00", align 1
@str.430 = private unnamed_addr constant [25 x i8] c"\0A--- 4. Testing goto ---\00", align 1
@str.431 = private unnamed_addr constant [48 x i8] c"OK: Successfully jumped over a line using goto.\00", align 1
@str.432 = private unnamed_addr constant [37 x i8] c"=== Control Flow test completed ===\0A\00", align 1
@str.433 = private unnamed_addr constant [7 x i8] c"Case 2\00", align 1
@str.434 = private unnamed_addr constant [7 x i8] c"Case 1\00", align 1
@str.435 = private unnamed_addr constant [7 x i8] c"Case 0\00", align 1
@str.436 = private unnamed_addr constant [13 x i8] c"Default case\00", align 1
@str.437 = private unnamed_addr constant [31 x i8] c"--- Marshalling Test Start ---\00", align 1
@str.438 = private unnamed_addr constant [69 x i8] c"FAILURE: Marshalling for OUT parameter FAILED! Value did not change.\00", align 1
@str.439 = private unnamed_addr constant [46 x i8] c"SUCCESS: Marshalling for OUT parameter works!\00", align 1
@str.440 = private unnamed_addr constant [29 x i8] c"--- Marshalling Test End ---\00", align 1
@str.441 = private unnamed_addr constant [32 x i8] c"\0A=== Testing SELECT Opcodes ===\00", align 1
@str.442 = private unnamed_addr constant [36 x i8] c"=== Testing SELECT Opcodes END ===\0A\00", align 1
@str.443 = private unnamed_addr constant [33 x i8] c"\0A=== Testing Pointer Opcodes ===\00", align 1
@str.444 = private unnamed_addr constant [24 x i8] c"ERROR: INTTOPTR failed!\00", align 1
@str.445 = private unnamed_addr constant [10 x i8] c"\0A===  ===\00", align 1
@str.446 = private unnamed_addr constant [35 x i8] c"\0A=== Testing Atomic Operations ===\00", align 1
@str.447 = private unnamed_addr constant [21 x i8] c"\0A--- I32 Atomics ---\00", align 1
@str.448 = private unnamed_addr constant [21 x i8] c"\0A--- I64 Atomics ---\00", align 1
@str.449 = private unnamed_addr constant [32 x i8] c"=== Atomics test completed ===\0A\00", align 1
@str.450 = private unnamed_addr constant [42 x i8] c"\0A=== Testing Heap & Memory Operations ===\00", align 1
@str.451 = private unnamed_addr constant [45 x i8] c"\0A--- 1. Testing HEAP_MALLOC (via malloc) ---\00", align 1
@str.452 = private unnamed_addr constant [56 x i8] c"ERROR: Data corruption in dynamically allocated memory.\00", align 1
@str.453 = private unnamed_addr constant [60 x i8] c"OK: Data integrity in dynamically allocated memory is fine.\00", align 1
@str.454 = private unnamed_addr constant [84 x i8] c"ERROR: malloc(128) failed! The HEAP_MALLOC opcode implementation might have issues.\00", align 1
@str.455 = private unnamed_addr constant [45 x i8] c"\0A--- 2. Testing MEMORY.COPY (via memcpy) ---\00", align 1
@str.456 = private unnamed_addr constant [58 x i8] c"ERROR: MEMORY.COPY test FAILED. Data mismatch after copy.\00", align 1
@str.457 = private unnamed_addr constant [56 x i8] c"OK: MEMORY.COPY test passed. Data was copied correctly.\00", align 1
@str.458 = private unnamed_addr constant [45 x i8] c"\0A--- 3. Testing MEMORY.FILL (via memset) ---\00", align 1
@str.459 = private unnamed_addr constant [50 x i8] c"Filling 32 bytes of the buffer with 'X' (0x58)...\00", align 1
@str.460 = private unnamed_addr constant [58 x i8] c"ERROR: MEMORY.FILL test FAILED. Data mismatch after fill.\00", align 1
@str.461 = private unnamed_addr constant [58 x i8] c"OK: MEMORY.FILL test passed. Buffer was filled correctly.\00", align 1
@str.462 = private unnamed_addr constant [41 x i8] c"\0A--- 4. Testing HEAP_FREE (via free) ---\00", align 1
@str.463 = private unnamed_addr constant [40 x i8] c"OK: Freed dynamically allocated buffer.\00", align 1
@str.464 = private unnamed_addr constant [44 x i8] c"ERROR: Failed to reuse memory after free().\00", align 1
@str.465 = private unnamed_addr constant [49 x i8] c"=== Heap & Memory Operations test completed ===\0A\00", align 1
@str.466 = private unnamed_addr constant [54 x i8] c"\0A=== Testing I64 IMM8 Operations (using volatile) ===\00", align 1
@str.467 = private unnamed_addr constant [44 x i8] c"=== I64 IMM8 Operations test completed ===\0A\00", align 1
@str.468 = private unnamed_addr constant [36 x i8] c"\0A=== Testing Complex Data Types ===\00", align 1
@str.469 = private unnamed_addr constant [27 x i8] c"\0A--- 1. Testing Arrays ---\00", align 1
@str.470 = private unnamed_addr constant [40 x i8] c"\0A--- 2. Testing Structures (struct) ---\00", align 1
@str.471 = private unnamed_addr constant [35 x i8] c"\0A--- 3. Testing Unions (union) ---\00", align 1
@str.472 = private unnamed_addr constant [40 x i8] c"\0A--- 4. Testing Enumerations (enum) ---\00", align 1
@str.473 = private unnamed_addr constant [29 x i8] c"OK: Device is in ERROR state\00", align 1
@str.474 = private unnamed_addr constant [37 x i8] c"\0A--- 5. Testing Array of Structs ---\00", align 1
@str.475 = private unnamed_addr constant [36 x i8] c"Iterating through sensor inventory:\00", align 1
@str.476 = private unnamed_addr constant [41 x i8] c"\0A--- 6. Testing Bitfields in Structs ---\00", align 1
@str.477 = private unnamed_addr constant [32 x i8] c"OK: Bitfield access is correct.\00", align 1
@str.478 = private unnamed_addr constant [43 x i8] c"=== Complex Data Types test completed ===\0A\00", align 1
@str.479 = private unnamed_addr constant [42 x i8] c"\0A=== Testing While and Do-While Loops ===\00", align 1
@str.480 = private unnamed_addr constant [31 x i8] c"\0A--- 1. Testing while loop ---\00", align 1
@str.481 = private unnamed_addr constant [52 x i8] c"  Testing standard while loop (should run 5 times):\00", align 1
@str.483 = private unnamed_addr constant [38 x i8] c"  OK: Standard while loop is correct.\00", align 1
@str.484 = private unnamed_addr constant [50 x i8] c"\0A  Testing zero-trip while loop (should not run):\00", align 1
@str.485 = private unnamed_addr constant [55 x i8] c"  OK: Zero-trip while loop body was correctly skipped.\00", align 1
@str.486 = private unnamed_addr constant [34 x i8] c"\0A--- 2. Testing do-while loop ---\00", align 1
@str.487 = private unnamed_addr constant [55 x i8] c"  Testing standard do-while loop (should run 5 times):\00", align 1
@str.489 = private unnamed_addr constant [41 x i8] c"  OK: Standard do-while loop is correct.\00", align 1
@str.490 = private unnamed_addr constant [61 x i8] c"\0A  Testing one-trip do-while loop (should run exactly once):\00", align 1
@str.491 = private unnamed_addr constant [60 x i8] c"    OK: One-trip do-while loop body executed (as expected).\00", align 1
@str.492 = private unnamed_addr constant [52 x i8] c"  OK: One-trip do-while loop executed exactly once.\00", align 1
@str.493 = private unnamed_addr constant [49 x i8] c"=== While and Do-While Loops test completed ===\0A\00", align 1
@str.494 = private unnamed_addr constant [47 x i8] c"\0A=== Testing Break and Continue Statements ===\00", align 1
@str.495 = private unnamed_addr constant [45 x i8] c"\0A--- 1. Testing 'continue' in a for loop ---\00", align 1
@str.496 = private unnamed_addr constant [53 x i8] c"  This loop should sum only odd numbers from 0 to 9.\00", align 1
@str.498 = private unnamed_addr constant [65 x i8] c"  OK: 'continue' test passed. Iterations were skipped correctly.\00", align 1
@str.499 = private unnamed_addr constant [44 x i8] c"\0A--- 2. Testing 'break' in a while loop ---\00", align 1
@str.500 = private unnamed_addr constant [88 x i8] c"  This loop is designed to run forever but should terminate when the counter reaches 5.\00", align 1
@str.502 = private unnamed_addr constant [68 x i8] c"  OK: 'break' test passed. Loop was exited prematurely as expected.\00", align 1
@str.503 = private unnamed_addr constant [45 x i8] c"\0A--- 3. Testing 'break' in a nested loop ---\00", align 1
@str.505 = private unnamed_addr constant [48 x i8] c"  OK: 'break' in nested loop behaved correctly.\00", align 1
@str.506 = private unnamed_addr constant [54 x i8] c"=== Break and Continue Statements test completed ===\0A\00", align 1
@str.507 = private unnamed_addr constant [34 x i8] c"\0A=== Testing TABLE Operations ===\00", align 1
@str.508 = private unnamed_addr constant [51 x i8] c"\0A[Test 1] TABLE.SIZE - checking initial table size\00", align 1
@str.509 = private unnamed_addr constant [37 x i8] c"  PASS: Initial size is correct (10)\00", align 1
@str.510 = private unnamed_addr constant [63 x i8] c"\0A[Test 2] TABLE.INIT - initializing table from element segment\00", align 1
@str.511 = private unnamed_addr constant [94 x i8] c"  Initializing 2 elements at table offset 5 from element segment 0 (starting at seg offset 0)\00", align 1
@str.512 = private unnamed_addr constant [23 x i8] c"  TABLE.INIT completed\00", align 1
@str.513 = private unnamed_addr constant [86 x i8] c"  Note: Element segment 0 contains function indices [0, 0] (both point to function 0)\00", align 1
@str.514 = private unnamed_addr constant [54 x i8] c"\0A[Test 3] TABLE.FILL - filling table range with value\00", align 1
@str.515 = private unnamed_addr constant [44 x i8] c"  Filling table[0..2] with function index 1\00", align 1
@str.516 = private unnamed_addr constant [23 x i8] c"  TABLE.FILL completed\00", align 1
@str.517 = private unnamed_addr constant [53 x i8] c"  Expected: table[0] = 1, table[1] = 1, table[2] = 1\00", align 1
@str.518 = private unnamed_addr constant [53 x i8] c"\0A[Test 4] TABLE.COPY - copying elements within table\00", align 1
@str.519 = private unnamed_addr constant [59 x i8] c"  Copying 2 elements from table offset 0 to table offset 7\00", align 1
@str.520 = private unnamed_addr constant [23 x i8] c"  TABLE.COPY completed\00", align 1
@str.521 = private unnamed_addr constant [61 x i8] c"  Expected: table[7] = table[0] = 1, table[8] = table[1] = 1\00", align 1
@str.522 = private unnamed_addr constant [54 x i8] c"\0A[Test 5] TABLE.SIZE - checking size after operations\00", align 1
@str.523 = private unnamed_addr constant [54 x i8] c"\0A[Test 6] Testing table expansion beyond initial size\00", align 1
@str.524 = private unnamed_addr constant [79 x i8] c"  Filling table[15..17] with function index 2 (should expand table to size 18)\00", align 1
@str.526 = private unnamed_addr constant [61 x i8] c"FAIL: Some TABLE operations did not produce expected results\00", align 1
@str.527 = private unnamed_addr constant [50 x i8] c"PASS: All TABLE operations executed successfully!\00", align 1
@str.528 = private unnamed_addr constant [31 x i8] c"  - TABLE.SIZE works correctly\00", align 1
@str.529 = private unnamed_addr constant [49 x i8] c"  - TABLE.INIT initializes from element segments\00", align 1
@str.530 = private unnamed_addr constant [34 x i8] c"  - TABLE.FILL fills table ranges\00", align 1
@str.531 = private unnamed_addr constant [35 x i8] c"  - TABLE.COPY copies within table\00", align 1
@str.532 = private unnamed_addr constant [35 x i8] c"  - Table auto-expands when needed\00", align 1
@str.533 = private unnamed_addr constant [41 x i8] c"=== TABLE Operations test completed ===\0A\00", align 1
@str.534 = private unnamed_addr constant [42 x i8] c"\0A=== Testing CALL_INDIRECT with TABLE ===\00", align 1
@str.535 = private unnamed_addr constant [56 x i8] c"\0A[Test 1] TABLE.SET - placing function indices in table\00", align 1
@str.536 = private unnamed_addr constant [38 x i8] c"  Setting table[0] = 0 (add_function)\00", align 1
@str.537 = private unnamed_addr constant [43 x i8] c"  Setting table[1] = 1 (multiply_function)\00", align 1
@str.538 = private unnamed_addr constant [43 x i8] c"  Setting table[2] = 2 (subtract_function)\00", align 1
@str.539 = private unnamed_addr constant [58 x i8] c"\0A[Test 2] TABLE.GET - reading function indices from table\00", align 1
@str.540 = private unnamed_addr constant [35 x i8] c"  FAIL: Function indices mismatch!\00", align 1
@str.541 = private unnamed_addr constant [60 x i8] c"  PASS: All function indices correctly stored and retrieved\00", align 1
@str.542 = private unnamed_addr constant [76 x i8] c"\0A[Test 3] CALL_INDIRECT - indirect function calls through function pointers\00", align 1
@str.543 = private unnamed_addr constant [54 x i8] c"  Creating function pointer array with 3 functions...\00", align 1
@str.544 = private unnamed_addr constant [41 x i8] c"  func_table[0] = add_function (index 0)\00", align 1
@str.545 = private unnamed_addr constant [46 x i8] c"  func_table[1] = multiply_function (index 1)\00", align 1
@str.546 = private unnamed_addr constant [46 x i8] c"  func_table[2] = subtract_function (index 2)\00", align 1
@str.547 = private unnamed_addr constant [38 x i8] c"  Function pointer array initialized!\00", align 1
@str.548 = private unnamed_addr constant [49 x i8] c"  Calling func_table[0](10, 5) [add_function]...\00", align 1
@str.549 = private unnamed_addr constant [54 x i8] c"  Calling func_table[1](10, 5) [multiply_function]...\00", align 1
@str.550 = private unnamed_addr constant [54 x i8] c"  Calling func_table[2](10, 5) [subtract_function]...\00", align 1
@str.551 = private unnamed_addr constant [65 x i8] c"\0A  [CRITICAL TEST] Dynamic call: choosing function at runtime...\00", align 1
@str.552 = private unnamed_addr constant [83 x i8] c"  This test MUST use CALL_INDIRECT because index 'i' is not known at compile time!\00", align 1
@str.553 = private unnamed_addr constant [55 x i8] c"  FAIL: Some dynamic calls returned incorrect results!\00", align 1
@str.554 = private unnamed_addr constant [52 x i8] c"  PASS: All dynamic calls returned correct results!\00", align 1
@str.555 = private unnamed_addr constant [17 x i8] c"\0A=== Summary ===\00", align 1
@str.556 = private unnamed_addr constant [48 x i8] c"\E2\9D\8C FAIL: Some CALL_INDIRECT operations failed!\00", align 1
@str.557 = private unnamed_addr constant [50 x i8] c"  Dynamic calls in loop failed - see errors above\00", align 1
@str.558 = private unnamed_addr constant [50 x i8] c"\E2\9C\85 PASS: All CALL_INDIRECT operations succeeded!\00", align 1
@str.559 = private unnamed_addr constant [46 x i8] c"  \E2\9C\93 TABLE.SET/GET operations work correctly\00", align 1
@str.560 = private unnamed_addr constant [69 x i8] c"  \E2\9C\93 Static indirect calls (func_table[0], [1], [2]) work correctly\00", align 1
@str.561 = private unnamed_addr constant [68 x i8] c"  \E2\9C\93 Dynamic indirect calls (func_table[i] in loop) work correctly\00", align 1
@str.562 = private unnamed_addr constant [40 x i8] c"  \E2\9C\93 All results match expected values\00", align 1
@str.563 = private unnamed_addr constant [50 x i8] c"\0A=== CALL_INDIRECT with TABLE test completed ===\0A\00", align 1
@str.564 = private unnamed_addr constant [44 x i8] c"\0A=== Testing CALL_INDIRECT_PTR (UNSAFE) ===\00", align 1
@str.565 = private unnamed_addr constant [50 x i8] c"This test requires ESPB_UNSAFE_MODE to be enabled\00", align 1
@str.566 = private unnamed_addr constant [50 x i8] c"\0A[Test] Direct address calls (forced unsafe mode)\00", align 1
@str.567 = private unnamed_addr constant [22 x i8] c"  Function addresses:\00", align 1
@str.568 = private unnamed_addr constant [39 x i8] c"\0A  Attempting unsafe calls by address:\00", align 1
@str.569 = private unnamed_addr constant [37 x i8] c"  Calling add_function by address...\00", align 1
@str.570 = private unnamed_addr constant [42 x i8] c"  Calling multiply_function by address...\00", align 1
@str.571 = private unnamed_addr constant [42 x i8] c"  Calling subtract_function by address...\00", align 1
@str.572 = private unnamed_addr constant [17 x i8] c"\0A=== Results ===\00", align 1
@str.573 = private unnamed_addr constant [63 x i8] c"\E2\9D\8C FAIL: Some direct address calls returned incorrect results\00", align 1
@str.574 = private unnamed_addr constant [24 x i8] c"    Expected: 20, 42, 9\00", align 1
@str.575 = private unnamed_addr constant [60 x i8] c"\E2\9C\85 PASS: All direct address calls returned correct results\00", align 1
@str.576 = private unnamed_addr constant [43 x i8] c"\0A=== CALL_INDIRECT_PTR test completed ===\0A\00", align 1
@str.577 = private unnamed_addr constant [22 x i8] c"\0A--- Testing bool ---\00", align 1
@str.578 = private unnamed_addr constant [18 x i8] c"OK: flag1 is true\00", align 1
@str.579 = private unnamed_addr constant [19 x i8] c"OK: !flag2 is true\00", align 1
@str.580 = private unnamed_addr constant [28 x i8] c"OK: flag1 && flag2 is false\00", align 1
@str.581 = private unnamed_addr constant [37 x i8] c"OK: flag2 is now true after negation\00", align 1
@str.582 = private unnamed_addr constant [26 x i8] c"--- End of bool test ---\0A\00", align 1
@str.583 = private unnamed_addr constant [22 x i8] c"--- Testing float ---\00", align 1
@str.584 = private unnamed_addr constant [27 x i8] c"--- End of float test ---\0A\00", align 1
@str.585 = private unnamed_addr constant [23 x i8] c"--- Testing double ---\00", align 1
@str.586 = private unnamed_addr constant [28 x i8] c"--- End of double test ---\0A\00", align 1
@str.587 = private unnamed_addr constant [31 x i8] c"--- Testing host_invoke_cb ---\00", align 1
@str.588 = private unnamed_addr constant [32 x i8] c"--- Testing host_invoke_cb2 ---\00", align 1
@str.589 = private unnamed_addr constant [38 x i8] c"--- Testing FreeRTOS xTimerCreate ---\00", align 1
@str.590 = private unnamed_addr constant [27 x i8] c"ERROR: xTimerCreate failed\00", align 1
@str.591 = private unnamed_addr constant [37 x i8] c"--- Testing FreeRTOS xTaskCreate ---\00", align 1
@str.592 = private unnamed_addr constant [47 x i8] c"\0AAll tests completed. Main task will now exit.\00", align 1
@str.593 = private unnamed_addr constant [31 x i8] c"Stopping and deleting timer...\00", align 1
@str.594 = private unnamed_addr constant [38 x i8] c"\0AStopping and deleting timer complete\00", align 1
@str.595 = private unnamed_addr constant [31 x i8] c"\0Acounter_task_handle not valid\00", align 1
@str.596 = private unnamed_addr constant [25 x i8] c"Deleting counter_task...\00", align 1
@str.597 = private unnamed_addr constant [32 x i8] c"Deleting counter_task completed\00", align 1
@str.598 = private unnamed_addr constant [5 x i8] c"\0Adl1\00", align 1
@str.599 = private unnamed_addr constant [11 x i8] c"\0Amain exit\00", align 1
@str.600 = private unnamed_addr constant [14 x i8] c"Hello world!\0A\00", align 1
@.str.1.2 = private unnamed_addr constant [22 x i8] c"Received integer: %d\0A\00", align 1, !dbg !1117
@.str.2.3 = private unnamed_addr constant [22 x i8] c"Received double:  %f\0A\00", align 1, !dbg !1120
@.str.3.4 = private unnamed_addr constant [24 x i8] c"Received string:  \22%s\22\0A\00", align 1, !dbg !1122
@str.1 = private unnamed_addr constant [37 x i8] c"--- ESPB 'test' function called! ---\00", align 1
@str.6 = private unnamed_addr constant [25 x i8] c"Received string:  (null)\00", align 1
@str.7 = private unnamed_addr constant [35 x i8] c"----------------------------------\00", align 1

; Function Attrs: nofree noinline nounwind
define dso_local i32 @add_function_1(i32 noundef %0, i32 noundef %1) #0 !dbg !1140 {
    #dbg_value(i32 %0, !1142, !DIExpression(), !1144)
    #dbg_value(i32 %1, !1143, !DIExpression(), !1144)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.259, i32 noundef %0, i32 noundef %1), !dbg !1145
  %4 = add nsw i32 %1, %0, !dbg !1146
  ret i32 %4, !dbg !1147
}

; Function Attrs: nofree noinline nounwind
define dso_local void @print_int_array(ptr nocapture noundef readonly %0, i32 noundef %1) #0 !dbg !1148 {
    #dbg_value(ptr %0, !1156, !DIExpression(), !1160)
    #dbg_value(i32 %1, !1157, !DIExpression(), !1160)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.181), !dbg !1161
    #dbg_value(i32 0, !1158, !DIExpression(), !1162)
  %4 = icmp eq i32 %1, 0, !dbg !1163
  br i1 %4, label %5, label %7, !dbg !1165

5:                                                ; preds = %7, %2
  %6 = tail call i32 @putchar(i32 10), !dbg !1166
  ret void, !dbg !1167

7:                                                ; preds = %2, %7
  %8 = phi i32 [ %12, %7 ], [ 0, %2 ]
    #dbg_value(i32 %8, !1158, !DIExpression(), !1162)
  %9 = getelementptr inbounds i32, ptr %0, i32 %8, !dbg !1168
  %10 = load i32, ptr %9, align 4, !dbg !1168, !tbaa !1170
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.182, i32 noundef %10), !dbg !1174
  %12 = add nuw i32 %8, 1, !dbg !1175
    #dbg_value(i32 %12, !1158, !DIExpression(), !1162)
  %13 = icmp eq i32 %12, %1, !dbg !1163
  br i1 %13, label %5, label %7, !dbg !1165, !llvm.loop !1176
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_memory_operations() #0 !dbg !1180 {
  %1 = alloca [100 x i8], align 1, !DIAssignID !1209
    #dbg_assign(i1 undef, !1184, !DIExpression(), !1209, ptr %1, !DIExpression(), !1210)
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.417), !dbg !1211
  call void @llvm.lifetime.start.p0(i64 100, ptr nonnull %1) #19, !dbg !1212
    #dbg_value(i32 0, !1188, !DIExpression(), !1213)
  br label %24, !dbg !1214

3:                                                ; preds = %24
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.418), !dbg !1215
    #dbg_value(ptr %1, !1190, !DIExpression(), !1210)
  store i32 123456789, ptr %1, align 4, !dbg !1216, !tbaa !1170
    #dbg_value(i32 123456789, !1191, !DIExpression(), !1210)
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.89, i32 noundef 123456789, i32 noundef 123456789), !dbg !1217
  %6 = getelementptr inbounds i8, ptr %1, i32 8, !dbg !1218
    #dbg_value(ptr %6, !1192, !DIExpression(), !1210)
  store i64 -987654321098765, ptr %6, align 8, !dbg !1219, !tbaa !1220
    #dbg_value(i64 -987654321098765, !1193, !DIExpression(), !1210)
  %7 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.90, i64 noundef -987654321098765, i64 noundef -987654321098765), !dbg !1222
  %8 = getelementptr inbounds i8, ptr %1, i32 20, !dbg !1223
    #dbg_value(ptr %8, !1194, !DIExpression(), !1210)
  store float 0x400921FA00000000, ptr %8, align 4, !dbg !1224, !tbaa !1225
    #dbg_value(float 0x400921FA00000000, !1195, !DIExpression(), !1210)
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.91, double noundef 0x400921FA00000000, double noundef 0x400921FA00000000), !dbg !1227
  %10 = getelementptr inbounds i8, ptr %1, i32 30, !dbg !1228
    #dbg_value(ptr %10, !1196, !DIExpression(), !1210)
  store double 0x4005BF0A8B145703, ptr %10, align 8, !dbg !1229, !tbaa !1230
    #dbg_value(double 0x4005BF0A8B145703, !1197, !DIExpression(), !1210)
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.92, double noundef 0x4005BF0A8B145703, double noundef 0x4005BF0A8B145703), !dbg !1232
  %12 = getelementptr inbounds i8, ptr %1, i32 40, !dbg !1233
    #dbg_value(ptr %12, !1198, !DIExpression(), !1210)
  store i8 -120, ptr %12, align 1, !dbg !1234, !tbaa !1235
    #dbg_value(i8 -120, !1199, !DIExpression(), !1210)
  %13 = getelementptr inbounds i8, ptr %1, i32 41, !dbg !1236
    #dbg_value(ptr %13, !1200, !DIExpression(), !1210)
  store i8 -6, ptr %13, align 1, !dbg !1237, !tbaa !1235
    #dbg_value(i8 -6, !1201, !DIExpression(), !1210)
  %14 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.93, i32 noundef -120, i32 noundef -120), !dbg !1238
  %15 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.94, i32 noundef 250, i32 noundef 250), !dbg !1239
  %16 = getelementptr inbounds i8, ptr %1, i32 44, !dbg !1240
    #dbg_value(ptr %16, !1202, !DIExpression(), !1210)
  store i16 -32000, ptr %16, align 2, !dbg !1241, !tbaa !1242
    #dbg_value(i16 -32000, !1203, !DIExpression(), !1210)
  %17 = getelementptr inbounds i8, ptr %1, i32 46, !dbg !1244
    #dbg_value(ptr %17, !1204, !DIExpression(), !1210)
  store i16 -536, ptr %17, align 2, !dbg !1245, !tbaa !1242
    #dbg_value(i16 -536, !1205, !DIExpression(), !1210)
  %18 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.95, i32 noundef -32000, i32 noundef -32000), !dbg !1246
  %19 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.96, i32 noundef 65000, i32 noundef 65000), !dbg !1247
  %20 = getelementptr inbounds i8, ptr %1, i32 50, !dbg !1248
    #dbg_value(ptr %20, !1206, !DIExpression(), !1210)
    #dbg_value(ptr inttoptr (i32 -559038737 to ptr), !1207, !DIExpression(), !1210)
  store volatile ptr inttoptr (i32 -559038737 to ptr), ptr %20, align 4, !dbg !1249, !tbaa !1250
  %21 = load volatile ptr, ptr %20, align 4, !dbg !1252, !tbaa !1250
    #dbg_value(ptr %21, !1208, !DIExpression(), !1210)
  %22 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.97, ptr noundef nonnull inttoptr (i32 -559038737 to ptr), ptr noundef %21), !dbg !1253
  %23 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.419), !dbg !1254
  call void @llvm.lifetime.end.p0(i64 100, ptr nonnull %1) #19, !dbg !1255
  ret void, !dbg !1255

24:                                               ; preds = %0, %24
  %25 = phi i32 [ 0, %0 ], [ %28, %24 ]
    #dbg_value(i32 %25, !1188, !DIExpression(), !1213)
  %26 = trunc i32 %25 to i8, !dbg !1256
  %27 = getelementptr inbounds [100 x i8], ptr %1, i32 0, i32 %25, !dbg !1259
  store i8 %26, ptr %27, align 1, !dbg !1260, !tbaa !1235
  %28 = add nuw nsw i32 %25, 1, !dbg !1261
    #dbg_value(i32 %28, !1188, !DIExpression(), !1213)
  %29 = icmp eq i32 %28, 100, !dbg !1262
  br i1 %29, label %3, label %24, !dbg !1214, !llvm.loop !1263
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_i64_imm8_ops() #0 !dbg !1265 {
  %1 = alloca i64, align 8, !DIAssignID !1281
    #dbg_assign(i1 undef, !1267, !DIExpression(), !1281, ptr %1, !DIExpression(), !1282)
  %2 = alloca i64, align 8, !DIAssignID !1283
    #dbg_assign(i1 undef, !1269, !DIExpression(), !1283, ptr %2, !DIExpression(), !1282)
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.466), !dbg !1284
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %1), !dbg !1285
  store volatile i64 -1000, ptr %1, align 8, !dbg !1286, !tbaa !1220, !DIAssignID !1287
    #dbg_assign(i64 -1000, !1267, !DIExpression(), !1287, ptr %1, !DIExpression(), !1282)
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %2), !dbg !1288
  store volatile i64 2000, ptr %2, align 8, !dbg !1289, !tbaa !1220, !DIAssignID !1290
    #dbg_assign(i64 2000, !1269, !DIExpression(), !1290, ptr %2, !DIExpression(), !1282)
  %4 = load volatile i64, ptr %1, align 8, !dbg !1291, !tbaa !1220
    #dbg_value(i64 %4, !1274, !DIExpression(), !1282)
  %5 = load volatile i64, ptr %2, align 8, !dbg !1292, !tbaa !1220
    #dbg_value(i64 %5, !1275, !DIExpression(), !1282)
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.174, i64 noundef %4, i64 noundef %5), !dbg !1293
  %7 = sdiv i64 %4, -10, !dbg !1294
    #dbg_value(i64 %7, !1276, !DIExpression(), !1282)
  %8 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.175, i64 noundef %4, i64 noundef %7), !dbg !1295
  %9 = udiv i64 %5, 20, !dbg !1296
    #dbg_value(i64 %9, !1277, !DIExpression(), !1282)
  %10 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.176, i64 noundef %5, i64 noundef %9), !dbg !1297
  %11 = srem i64 %4, 13, !dbg !1298
    #dbg_value(i64 %11, !1278, !DIExpression(), !1282)
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.177, i64 noundef %4, i64 noundef %11), !dbg !1299
  %13 = urem i64 %5, 17, !dbg !1300
    #dbg_value(i64 %13, !1279, !DIExpression(), !1282)
  %14 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.178, i64 noundef %5, i64 noundef %13), !dbg !1301
  %15 = lshr i64 %5, 3, !dbg !1302
    #dbg_value(i64 %15, !1280, !DIExpression(), !1282)
  %16 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.179, i64 noundef %5, i64 noundef %15), !dbg !1303
  %17 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.467), !dbg !1304
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %2), !dbg !1305
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %1), !dbg !1305
  ret void, !dbg !1305
}

; Function Attrs: noinline nounwind
define dso_local void @test_table_operations() #1 !dbg !1306 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.507), !dbg !1311
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.508), !dbg !1312
  %3 = tail call i32 @OP_table_size(i32 noundef 0) #19, !dbg !1313
    #dbg_value(i32 %3, !1308, !DIExpression(), !1314)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.264, i32 noundef %3), !dbg !1315
  %5 = icmp eq i32 %3, 10, !dbg !1316
  br i1 %5, label %8, label %6, !dbg !1318

6:                                                ; preds = %0
  %7 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.265, i32 noundef %3), !dbg !1319
  br label %10, !dbg !1321

8:                                                ; preds = %0
  %9 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.509), !dbg !1322
  br label %10

10:                                               ; preds = %8, %6
  %11 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.510), !dbg !1324
  %12 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.511), !dbg !1325
  tail call void @OP_table_init(i32 noundef 0, i32 noundef 0, i32 noundef 5, i32 noundef 0, i32 noundef 2) #19, !dbg !1326
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.512), !dbg !1327
  %14 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.513), !dbg !1328
  %15 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.514), !dbg !1329
  %16 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.515), !dbg !1330
  tail call void @OP_table_fill(i32 noundef 0, i32 noundef 0, i32 noundef 1, i32 noundef 3) #19, !dbg !1331
  %17 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.516), !dbg !1332
  %18 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.517), !dbg !1333
  %19 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.518), !dbg !1334
  %20 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.519), !dbg !1335
  tail call void @OP_table_copy(i32 noundef 0, i32 noundef 0, i32 noundef 7, i32 noundef 0, i32 noundef 2) #19, !dbg !1336
  %21 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.520), !dbg !1337
  %22 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.521), !dbg !1338
  %23 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.522), !dbg !1339
  %24 = tail call i32 @OP_table_size(i32 noundef 0) #19, !dbg !1340
    #dbg_value(i32 %24, !1309, !DIExpression(), !1314)
  %25 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.280, i32 noundef %24), !dbg !1341
  %26 = icmp slt i32 %24, 9, !dbg !1342
  %27 = select i1 %26, ptr @.str.281, ptr @.str.282
  %28 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) %27, i32 noundef %24), !dbg !1344
  %29 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.523), !dbg !1345
  %30 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.524), !dbg !1346
  tail call void @OP_table_fill(i32 noundef 0, i32 noundef 15, i32 noundef 2, i32 noundef 3) #19, !dbg !1347
  %31 = tail call i32 @OP_table_size(i32 noundef 0) #19, !dbg !1348
    #dbg_value(i32 %31, !1310, !DIExpression(), !1314)
  %32 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.285, i32 noundef %31), !dbg !1349
  %33 = icmp slt i32 %31, 18, !dbg !1350
  %34 = select i1 %33, ptr @.str.286, ptr @.str.287, !dbg !1352
  %35 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) %34, i32 noundef %31), !dbg !1353
  %36 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.555), !dbg !1354
  %37 = icmp sgt i32 %24, 8
  %38 = icmp sgt i32 %31, 17
  %39 = and i1 %37, %38, !dbg !1355
  %40 = and i1 %5, %39, !dbg !1355
  br i1 %40, label %41, label %47, !dbg !1355

41:                                               ; preds = %10
  %42 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.527), !dbg !1357
  %43 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.528), !dbg !1359
  %44 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.529), !dbg !1360
  %45 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.530), !dbg !1361
  %46 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.531), !dbg !1362
  br label %47, !dbg !1363

47:                                               ; preds = %10, %41
  %48 = phi ptr [ @str.532, %41 ], [ @str.526, %10 ]
  %49 = tail call i32 @puts(ptr nonnull dereferenceable(1) %48), !dbg !1364
  %50 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.533), !dbg !1365
  ret void, !dbg !1366
}

; Function Attrs: nofree noinline nounwind
define dso_local i32 @add_function(i32 noundef %0, i32 noundef %1) #0 !dbg !1367 {
    #dbg_value(i32 %0, !1369, !DIExpression(), !1371)
    #dbg_value(i32 %1, !1370, !DIExpression(), !1371)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.259, i32 noundef %0, i32 noundef %1), !dbg !1372
  %4 = add nsw i32 %1, %0, !dbg !1373
  ret i32 %4, !dbg !1374
}

; Function Attrs: noinline noreturn nounwind
define dso_local void @counter_task(ptr noundef %0) #2 !dbg !1375 {
    #dbg_value(ptr %0, !1379, !DIExpression(), !1381)
    #dbg_value(i32 0, !1380, !DIExpression(), !1381)
  %2 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, ptr noundef %0), !dbg !1382
  br label %3, !dbg !1383

3:                                                ; preds = %3, %1
  %4 = phi i32 [ 0, %1 ], [ %5, %3 ], !dbg !1381
    #dbg_value(i32 %4, !1380, !DIExpression(), !1381)
  %5 = add nuw nsw i32 %4, 1, !dbg !1384
    #dbg_value(i32 %5, !1380, !DIExpression(), !1381)
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.1, i32 noundef %4), !dbg !1386
  tail call void @vTaskDelay(i32 noundef 100) #19, !dbg !1387
  br label %3, !dbg !1383, !llvm.loop !1388
}

; Function Attrs: noinline nounwind
define dso_local i32 @call_volatile_function(ptr nocapture noundef readonly %0, i32 noundef %1, i32 noundef %2) #1 !dbg !1390 {
    #dbg_value(ptr %0, !1394, !DIExpression(), !1397)
    #dbg_value(i32 %1, !1395, !DIExpression(), !1397)
    #dbg_value(i32 %2, !1396, !DIExpression(), !1397)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.12, i32 noundef %1, i32 noundef %2), !dbg !1398
  %5 = tail call i32 %0(i32 noundef %1, i32 noundef %2) #19, !dbg !1399
  ret i32 %5, !dbg !1400
}

; Function Attrs: nofree noinline nounwind
define dso_local i32 @multiply_function_1(i32 noundef %0, i32 noundef %1) #0 !dbg !1401 {
    #dbg_value(i32 %0, !1403, !DIExpression(), !1405)
    #dbg_value(i32 %1, !1404, !DIExpression(), !1405)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.260, i32 noundef %0, i32 noundef %1), !dbg !1406
  %4 = mul nsw i32 %1, %0, !dbg !1407
  ret i32 %4, !dbg !1408
}

; Function Attrs: noinline nounwind
define dso_local void @test_call_indirect_non_optimizable() #1 !dbg !1409 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.406), !dbg !1415
  %2 = tail call i32 @call_operation_by_index(i32 noundef 0, i32 noundef 5, i32 noundef 3), !dbg !1416
    #dbg_value(i32 %2, !1411, !DIExpression(), !1417)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.14, i32 noundef %2), !dbg !1418
  %4 = tail call i32 @call_operation_by_index(i32 noundef 1, i32 noundef 4, i32 noundef 7), !dbg !1419
    #dbg_value(i32 %4, !1412, !DIExpression(), !1417)
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.15, i32 noundef %4), !dbg !1420
    #dbg_value(ptr @add_func, !1413, !DIExpression(), !1417)
  %6 = tail call i32 @call_volatile_function(ptr noundef nonnull @add_func, i32 noundef 6, i32 noundef 9), !dbg !1421
    #dbg_value(i32 %6, !1414, !DIExpression(), !1417)
  %7 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.16, i32 noundef %6), !dbg !1422
  %8 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.407), !dbg !1423
  ret void, !dbg !1424
}

; Function Attrs: nofree noinline nounwind
define dso_local i32 @multiply_function(i32 noundef %0, i32 noundef %1) #0 !dbg !1425 {
    #dbg_value(i32 %0, !1427, !DIExpression(), !1429)
    #dbg_value(i32 %1, !1428, !DIExpression(), !1429)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.260, i32 noundef %0, i32 noundef %1), !dbg !1430
  %4 = mul nsw i32 %1, %0, !dbg !1431
  ret i32 %4, !dbg !1432
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_while_and_do_while_loops() #0 !dbg !1433 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.479), !dbg !1441
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.480), !dbg !1442
    #dbg_value(i32 0, !1435, !DIExpression(), !1443)
    #dbg_value(i32 0, !1436, !DIExpression(), !1443)
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.481), !dbg !1444
    #dbg_value(i32 0, !1436, !DIExpression(), !1443)
    #dbg_value(i32 0, !1435, !DIExpression(), !1443)
  br label %4, !dbg !1445

4:                                                ; preds = %0, %4
  %5 = phi i32 [ 0, %0 ], [ %7, %4 ]
  %6 = phi i32 [ 0, %0 ], [ %9, %4 ]
    #dbg_value(i32 %5, !1436, !DIExpression(), !1443)
    #dbg_value(i32 %6, !1435, !DIExpression(), !1443)
  %7 = add nuw nsw i32 %5, %6, !dbg !1446
    #dbg_value(i32 %7, !1436, !DIExpression(), !1443)
  %8 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.224, i32 noundef %6, i32 noundef %7), !dbg !1448
  %9 = add nuw nsw i32 %6, 1, !dbg !1449
    #dbg_value(i32 %9, !1435, !DIExpression(), !1443)
  %10 = icmp eq i32 %9, 5, !dbg !1450
  br i1 %10, label %11, label %4, !dbg !1445, !llvm.loop !1451

11:                                               ; preds = %4
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.225, i32 noundef 10), !dbg !1453
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.483), !dbg !1454
  %14 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.484), !dbg !1457
    #dbg_value(i32 10, !1437, !DIExpression(), !1443)
    #dbg_value(i32 10, !1437, !DIExpression(), !1443)
  %15 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.485), !dbg !1458
  %16 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.486), !dbg !1461
    #dbg_value(i32 0, !1438, !DIExpression(), !1443)
    #dbg_value(i32 0, !1439, !DIExpression(), !1443)
  %17 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.487), !dbg !1462
  br label %18, !dbg !1463

18:                                               ; preds = %18, %11
  %19 = phi i32 [ 0, %11 ], [ %23, %18 ], !dbg !1443
  %20 = phi i32 [ 0, %11 ], [ %21, %18 ], !dbg !1443
    #dbg_value(i32 %20, !1439, !DIExpression(), !1443)
    #dbg_value(i32 %19, !1438, !DIExpression(), !1443)
  %21 = add nuw nsw i32 %20, %19, !dbg !1464
    #dbg_value(i32 %21, !1439, !DIExpression(), !1443)
  %22 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.224, i32 noundef %19, i32 noundef %21), !dbg !1466
  %23 = add nuw nsw i32 %19, 1, !dbg !1467
    #dbg_value(i32 %23, !1438, !DIExpression(), !1443)
  %24 = icmp eq i32 %23, 5, !dbg !1468
  br i1 %24, label %25, label %18, !dbg !1469, !llvm.loop !1470

25:                                               ; preds = %18
  %26 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.225, i32 noundef 10), !dbg !1472
  %27 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.489), !dbg !1473
  %28 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.490), !dbg !1476
    #dbg_value(i32 10, !1440, !DIExpression(), !1443)
    #dbg_value(i32 10, !1440, !DIExpression(), !1443)
  %29 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.491), !dbg !1477
    #dbg_value(i32 11, !1440, !DIExpression(), !1443)
  %30 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.238, i32 noundef 11), !dbg !1479
  %31 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.492), !dbg !1480
  %32 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.493), !dbg !1483
  ret void, !dbg !1484
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_break_and_continue() #0 !dbg !1485 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.494), !dbg !1499
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.495), !dbg !1500
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.496), !dbg !1501
    #dbg_value(i32 0, !1487, !DIExpression(), !1502)
    #dbg_value(i32 0, !1488, !DIExpression(), !1503)
    #dbg_value(i32 undef, !1488, !DIExpression(), !1503)
    #dbg_value(i32 poison, !1487, !DIExpression(), !1502)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.245, i32 noundef 25), !dbg !1504
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.498), !dbg !1505
  %6 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.499), !dbg !1508
  %7 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.500), !dbg !1509
    #dbg_value(i32 0, !1490, !DIExpression(), !1502)
    #dbg_value(i32 poison, !1490, !DIExpression(), !1502)
  %8 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.250, i32 noundef 5), !dbg !1510
  %9 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.502), !dbg !1511
  %10 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.503), !dbg !1514
    #dbg_value(i32 0, !1491, !DIExpression(), !1502)
    #dbg_value(i32 0, !1492, !DIExpression(), !1502)
    #dbg_value(i32 0, !1493, !DIExpression(), !1515)
    #dbg_value(i32 poison, !1493, !DIExpression(), !1515)
    #dbg_value(i32 poison, !1492, !DIExpression(), !1502)
    #dbg_value(i32 poison, !1491, !DIExpression(), !1502)
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.254, i32 noundef 3), !dbg !1516
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.255, i32 noundef 9), !dbg !1517
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.505), !dbg !1518
  %14 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.506), !dbg !1521
  ret void, !dbg !1522
}

; Function Attrs: nofree noinline nounwind
define dso_local i32 @subtract_function(i32 noundef %0, i32 noundef %1) #0 !dbg !1523 {
    #dbg_value(i32 %0, !1525, !DIExpression(), !1527)
    #dbg_value(i32 %1, !1526, !DIExpression(), !1527)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.261, i32 noundef %0, i32 noundef %1), !dbg !1528
  %4 = sub nsw i32 %0, %1, !dbg !1529
  ret i32 %4, !dbg !1530
}

; Function Attrs: noinline nounwind
define dso_local void @test_call_indirect_ptr_unsafe() #1 !dbg !1531 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.564), !dbg !1539
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.565), !dbg !1540
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.566), !dbg !1541
    #dbg_value(ptr @add_function, !1533, !DIExpression(), !1542)
    #dbg_value(ptr @multiply_function, !1534, !DIExpression(), !1542)
    #dbg_value(ptr @subtract_function, !1535, !DIExpression(), !1542)
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.567), !dbg !1543
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.342, ptr noundef nonnull @add_function), !dbg !1544
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.343, ptr noundef nonnull @multiply_function), !dbg !1545
  %7 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.344, ptr noundef nonnull @subtract_function), !dbg !1546
  %8 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.568), !dbg !1547
  %9 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.569), !dbg !1548
  %10 = tail call i32 @call_function_by_address(ptr noundef nonnull @add_function, i32 noundef 12, i32 noundef 8), !dbg !1549
    #dbg_value(i32 %10, !1536, !DIExpression(), !1542)
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.347, i32 noundef %10), !dbg !1550
  %12 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.570), !dbg !1551
  %13 = tail call i32 @call_function_by_address(ptr noundef nonnull @multiply_function, i32 noundef 6, i32 noundef 7), !dbg !1552
    #dbg_value(i32 %13, !1537, !DIExpression(), !1542)
  %14 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.349, i32 noundef %13), !dbg !1553
  %15 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.571), !dbg !1554
  %16 = tail call i32 @call_function_by_address(ptr noundef nonnull @subtract_function, i32 noundef 15, i32 noundef 6), !dbg !1555
    #dbg_value(i32 %16, !1538, !DIExpression(), !1542)
  %17 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.351, i32 noundef %16), !dbg !1556
  %18 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.572), !dbg !1557
  %19 = icmp eq i32 %10, 20, !dbg !1558
  %20 = icmp eq i32 %13, 42
  %21 = and i1 %19, %20, !dbg !1560
  %22 = icmp eq i32 %16, 9
  %23 = and i1 %21, %22, !dbg !1560
  br i1 %23, label %27, label %24, !dbg !1560

24:                                               ; preds = %0
  %25 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.573), !dbg !1561
  %26 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.355, i32 noundef %10, i32 noundef %13, i32 noundef %16), !dbg !1563
  br label %27

27:                                               ; preds = %0, %24
  %28 = phi ptr [ @str.574, %24 ], [ @str.575, %0 ]
  %29 = tail call i32 @puts(ptr nonnull dereferenceable(1) %28), !dbg !1564
  %30 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.576), !dbg !1565
  ret void, !dbg !1566
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_complex_data_types() #0 !dbg !1567 {
  %1 = alloca [5 x i32], align 4, !DIAssignID !1608
    #dbg_assign(i1 undef, !1569, !DIExpression(), !1608, ptr %1, !DIExpression(), !1609)
  %2 = alloca %struct.SensorData, align 4, !DIAssignID !1610
    #dbg_assign(i1 undef, !1576, !DIExpression(), !1610, ptr %2, !DIExpression(), !1609)
  %3 = alloca [2 x %struct.SensorData], align 4, !DIAssignID !1611
    #dbg_assign(i1 undef, !1596, !DIExpression(), !1611, ptr %3, !DIExpression(), !1609)
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.468), !dbg !1612
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.469), !dbg !1613
  call void @llvm.lifetime.start.p0(i64 20, ptr nonnull %1) #19, !dbg !1614
  store i32 10, ptr %1, align 4, !dbg !1615, !DIAssignID !1616
    #dbg_assign(i32 10, !1569, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !1616, ptr %1, !DIExpression(), !1609)
  %6 = getelementptr inbounds i32, ptr %1, i32 1, !dbg !1615
  store i32 20, ptr %6, align 4, !dbg !1615, !DIAssignID !1617
    #dbg_assign(i32 20, !1569, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !1617, ptr %6, !DIExpression(), !1609)
  %7 = getelementptr inbounds i32, ptr %1, i32 2, !dbg !1615
  store i32 30, ptr %7, align 4, !dbg !1615, !DIAssignID !1618
    #dbg_assign(i32 30, !1569, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !1618, ptr %7, !DIExpression(), !1609)
  %8 = getelementptr inbounds i32, ptr %1, i32 3, !dbg !1615
  store i32 40, ptr %8, align 4, !dbg !1615, !DIAssignID !1619
    #dbg_assign(i32 40, !1569, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !1619, ptr %8, !DIExpression(), !1609)
  %9 = getelementptr inbounds i32, ptr %1, i32 4, !dbg !1615
  store i32 50, ptr %9, align 4, !dbg !1615, !DIAssignID !1620
    #dbg_assign(i32 50, !1569, !DIExpression(DW_OP_LLVM_fragment, 128, 32), !1620, ptr %9, !DIExpression(), !1609)
  %10 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.187), !dbg !1621
    #dbg_value(i32 0, !1571, !DIExpression(), !1622)
  br label %46, !dbg !1623

11:                                               ; preds = %46
  %12 = tail call i32 @putchar(i32 10), !dbg !1624
  store i32 35, ptr %7, align 4, !dbg !1625, !tbaa !1170, !DIAssignID !1626
    #dbg_assign(i32 35, !1569, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !1626, ptr %7, !DIExpression(), !1609)
  %13 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.188, i32 noundef 35), !dbg !1627
  call void @print_int_array(ptr noundef nonnull %1, i32 noundef 5), !dbg !1628
    #dbg_value(i32 1, !1573, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !1609)
    #dbg_value(i32 2, !1573, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !1609)
    #dbg_value(i32 3, !1573, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !1609)
    #dbg_value(i32 4, !1573, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !1609)
    #dbg_value(i32 5, !1573, !DIExpression(DW_OP_LLVM_fragment, 128, 32), !1609)
    #dbg_value(i32 6, !1573, !DIExpression(DW_OP_LLVM_fragment, 160, 32), !1609)
  %14 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.189, i32 noundef 5), !dbg !1629
  %15 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.470), !dbg !1630
  call void @llvm.lifetime.start.p0(i64 28, ptr nonnull %2) #19, !dbg !1631
  store i32 101, ptr %2, align 4, !dbg !1632, !tbaa !1633, !DIAssignID !1636
    #dbg_assign(i32 101, !1576, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !1636, ptr %2, !DIExpression(), !1609)
  %16 = getelementptr inbounds %struct.SensorData, ptr %2, i32 0, i32 1, !dbg !1637
  store float 0x4037666660000000, ptr %16, align 4, !dbg !1638, !tbaa !1639, !DIAssignID !1640
    #dbg_assign(float 0x4037666660000000, !1576, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !1640, ptr %16, !DIExpression(), !1609)
  %17 = getelementptr inbounds %struct.SensorData, ptr %2, i32 0, i32 2, !dbg !1641
  call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 4 dereferenceable(13) %17, ptr noundef nonnull align 1 dereferenceable(13) @.str.191, i32 13, i1 false), !dbg !1642
  %18 = getelementptr inbounds %struct.SensorData, ptr %2, i32 0, i32 3, !dbg !1643
  store i8 1, ptr %18, align 4, !dbg !1644, !tbaa !1645, !DIAssignID !1646
    #dbg_assign(i8 1, !1576, !DIExpression(DW_OP_LLVM_fragment, 192, 8), !1646, ptr %18, !DIExpression(), !1609)
  %19 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.192, i32 noundef 101, ptr noundef nonnull %17, double noundef 0x4037666660000000, ptr noundef nonnull @.str.193), !dbg !1647
    #dbg_value(ptr %2, !1584, !DIExpression(), !1609)
  store float 0x40381999A0000000, ptr %16, align 4, !dbg !1648, !tbaa !1639
  %20 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.195, double noundef 0x40381999A0000000), !dbg !1649
  call void @modify_sensor_by_ref(ptr noundef nonnull %2), !dbg !1650
  %21 = load float, ptr %16, align 4, !dbg !1651, !tbaa !1639
  %22 = fpext float %21 to double, !dbg !1652
  %23 = load i8, ptr %18, align 4, !dbg !1653, !tbaa !1645, !range !1654, !noundef !1655
  %24 = icmp eq i8 %23, 0, !dbg !1653
  %25 = select i1 %24, ptr @.str.193, ptr @.str.194, !dbg !1656
  %26 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.196, ptr noundef nonnull %17, double noundef %22, ptr noundef nonnull %25), !dbg !1657
  %27 = call i32 @puts(ptr nonnull dereferenceable(1) @str.471), !dbg !1658
  %28 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.198, i32 noundef 4), !dbg !1659
    #dbg_value(i32 1078530000, !1586, !DIExpression(), !1609)
  %29 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.199, double noundef 0x400921FA00000000), !dbg !1660
  %30 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.200, i32 noundef 1078530000), !dbg !1661
  %31 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.201, i32 noundef 208, i32 noundef 15, i32 noundef 73, i32 noundef 64), !dbg !1662
    #dbg_value(i32 100, !1586, !DIExpression(), !1609)
  %32 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.202, i32 noundef 100), !dbg !1663
  %33 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.203, double noundef 0x3709000000000000), !dbg !1664
  %34 = call i32 @puts(ptr nonnull dereferenceable(1) @str.472), !dbg !1665
    #dbg_value(i32 1, !1594, !DIExpression(), !1609)
  %35 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.205, i32 noundef 1), !dbg !1666
    #dbg_value(i32 2, !1594, !DIExpression(), !1609)
  %36 = call i32 @puts(ptr nonnull dereferenceable(1) @str.473), !dbg !1667
  %37 = call i32 @puts(ptr nonnull dereferenceable(1) @str.474), !dbg !1669
  call void @llvm.lifetime.start.p0(i64 56, ptr nonnull %3) #19, !dbg !1670
  store i32 201, ptr %3, align 4, !dbg !1671, !tbaa.struct !1672, !DIAssignID !1674
  %38 = getelementptr inbounds i8, ptr %3, i32 4, !dbg !1671
  store float 5.500000e+01, ptr %38, align 4, !dbg !1671, !tbaa.struct !1675, !DIAssignID !1676
  %39 = getelementptr inbounds i8, ptr %3, i32 8, !dbg !1671
  call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 4 dereferenceable(16) %39, ptr noundef nonnull align 4 dereferenceable(16) @.str.211, i32 16, i1 false), !dbg !1671, !tbaa.struct !1677, !DIAssignID !1678
  %40 = getelementptr inbounds i8, ptr %3, i32 24, !dbg !1671
  store i8 1, ptr %40, align 4, !dbg !1671, !tbaa.struct !1679, !DIAssignID !1680
    #dbg_assign(i32 201, !1596, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !1674, ptr %3, !DIExpression(), !1609)
    #dbg_assign(float 5.500000e+01, !1596, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !1676, ptr %38, !DIExpression(), !1609)
    #dbg_assign(i1 undef, !1596, !DIExpression(DW_OP_LLVM_fragment, 64, 128), !1678, ptr %39, !DIExpression(), !1609)
    #dbg_assign(i8 1, !1596, !DIExpression(DW_OP_LLVM_fragment, 192, 8), !1680, ptr %40, !DIExpression(), !1609)
    #dbg_assign(i1 undef, !1596, !DIExpression(DW_OP_LLVM_fragment, 200, 24), !1681, ptr %3, !DIExpression(DW_OP_plus_uconst, 25), !1609)
  %41 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 1, !dbg !1682
  store i32 202, ptr %41, align 4, !dbg !1683, !tbaa.struct !1672, !DIAssignID !1684
  %42 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 1, i32 1, !dbg !1683
  store float 1.250000e+01, ptr %42, align 4, !dbg !1683, !tbaa.struct !1675, !DIAssignID !1685
  %43 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 1, i32 2, !dbg !1683
  call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 4 dereferenceable(16) %43, ptr noundef nonnull align 4 dereferenceable(16) @.str.212, i32 16, i1 false), !dbg !1683, !tbaa.struct !1677, !DIAssignID !1686
  %44 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 1, i32 3, !dbg !1683
  store i8 0, ptr %44, align 4, !dbg !1683, !tbaa.struct !1679, !DIAssignID !1687
    #dbg_assign(i32 202, !1596, !DIExpression(DW_OP_LLVM_fragment, 224, 32), !1684, ptr %41, !DIExpression(), !1609)
    #dbg_assign(float 1.250000e+01, !1596, !DIExpression(DW_OP_LLVM_fragment, 256, 32), !1685, ptr %42, !DIExpression(), !1609)
    #dbg_assign(i1 undef, !1596, !DIExpression(DW_OP_LLVM_fragment, 288, 128), !1686, ptr %43, !DIExpression(), !1609)
    #dbg_assign(i8 0, !1596, !DIExpression(DW_OP_LLVM_fragment, 416, 8), !1687, ptr %44, !DIExpression(), !1609)
    #dbg_assign(i1 undef, !1596, !DIExpression(DW_OP_LLVM_fragment, 424, 24), !1688, ptr %3, !DIExpression(DW_OP_plus_uconst, 53), !1609)
  %45 = call i32 @puts(ptr nonnull dereferenceable(1) @str.475), !dbg !1689
    #dbg_value(i32 0, !1598, !DIExpression(), !1690)
  br label %59, !dbg !1691

46:                                               ; preds = %0, %46
  %47 = phi i32 [ 0, %0 ], [ %51, %46 ]
    #dbg_value(i32 %47, !1571, !DIExpression(), !1622)
  %48 = getelementptr inbounds [5 x i32], ptr %1, i32 0, i32 %47, !dbg !1692
  %49 = load i32, ptr %48, align 4, !dbg !1692, !tbaa !1170
  %50 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.182, i32 noundef %49), !dbg !1695
  %51 = add nuw nsw i32 %47, 1, !dbg !1696
    #dbg_value(i32 %51, !1571, !DIExpression(), !1622)
  %52 = icmp eq i32 %51, 5, !dbg !1697
  br i1 %52, label %11, label %46, !dbg !1623, !llvm.loop !1698

53:                                               ; preds = %59
  %54 = call i32 @puts(ptr nonnull dereferenceable(1) @str.476), !dbg !1700
  %55 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.216, i32 noundef 1), !dbg !1701
    #dbg_value(i8 1, !1600, !DIExpression(), !1609)
    #dbg_value(i8 1, !1600, !DIExpression(), !1609)
    #dbg_value(i8 21, !1600, !DIExpression(), !1609)
    #dbg_value(i8 21, !1600, !DIExpression(), !1609)
  %56 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.217, i32 noundef 1, i32 noundef 0, i32 noundef 5), !dbg !1702
  %57 = call i32 @puts(ptr nonnull dereferenceable(1) @str.477), !dbg !1703
  %58 = call i32 @puts(ptr nonnull dereferenceable(1) @str.478), !dbg !1706
  call void @llvm.lifetime.end.p0(i64 56, ptr nonnull %3) #19, !dbg !1707
  call void @llvm.lifetime.end.p0(i64 28, ptr nonnull %2) #19, !dbg !1707
  call void @llvm.lifetime.end.p0(i64 20, ptr nonnull %1) #19, !dbg !1707
  ret void, !dbg !1707

59:                                               ; preds = %11, %59
  %60 = phi i32 [ 0, %11 ], [ %65, %59 ]
    #dbg_value(i32 %60, !1598, !DIExpression(), !1690)
  %61 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 %60, !dbg !1708
  %62 = load i32, ptr %61, align 4, !dbg !1711, !tbaa !1633
  %63 = getelementptr inbounds [2 x %struct.SensorData], ptr %3, i32 0, i32 %60, i32 2, !dbg !1712
  %64 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.214, i32 noundef %60, i32 noundef %62, ptr noundef nonnull %63), !dbg !1713
  %65 = add nuw nsw i32 %60, 1, !dbg !1714
    #dbg_value(i32 %65, !1598, !DIExpression(), !1690)
  %66 = icmp eq i32 %60, 0, !dbg !1715
  br i1 %66, label %59, label %53, !dbg !1691, !llvm.loop !1716
}

; Function Attrs: nofree noinline nounwind
define dso_local noundef i32 @mul_func(i32 noundef %0, i32 noundef %1) #0 !dbg !1718 {
    #dbg_value(i32 %0, !1720, !DIExpression(), !1722)
    #dbg_value(i32 %1, !1721, !DIExpression(), !1722)
  %3 = mul nsw i32 %1, %0, !dbg !1723
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.3, i32 noundef %0, i32 noundef %1, i32 noundef %3), !dbg !1724
  ret i32 %3, !dbg !1725
}

; Function Attrs: nofree noinline nounwind
define internal void @cb_with_int(i32 noundef %0, ptr noundef %1) #0 !dbg !1726 {
    #dbg_value(i32 %0, !1730, !DIExpression(), !1732)
    #dbg_value(ptr %1, !1731, !DIExpression(), !1732)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.401, i32 noundef %0, ptr noundef %1), !dbg !1733
  ret void, !dbg !1734
}

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind willreturn memory(argmem: readwrite)
define dso_local void @modify_sensor_by_ref(ptr nocapture noundef %0) #3 !dbg !1735 {
    #dbg_value(ptr %0, !1739, !DIExpression(), !1740)
  %2 = getelementptr inbounds %struct.SensorData, ptr %0, i32 0, i32 1, !dbg !1741
  %3 = load float, ptr %2, align 4, !dbg !1742, !tbaa !1639
  %4 = fadd float %3, 5.000000e+00, !dbg !1742
  store float %4, ptr %2, align 4, !dbg !1742, !tbaa !1639
  %5 = getelementptr inbounds %struct.SensorData, ptr %0, i32 0, i32 2, !dbg !1743
  tail call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 1 dereferenceable(16) %5, ptr noundef nonnull align 1 dereferenceable(16) @.str.184, i32 16, i1 false), !dbg !1744
  %6 = getelementptr inbounds %struct.SensorData, ptr %0, i32 0, i32 3, !dbg !1745
  store i8 0, ptr %6, align 4, !dbg !1746, !tbaa !1645
  ret void, !dbg !1747
}

; Function Attrs: noinline nounwind optnone
define dso_local i32 @call_function_by_address(ptr noundef %0, i32 noundef %1, i32 noundef %2) #4 !dbg !1748 {
  %4 = alloca ptr, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca ptr, align 4
  store ptr %0, ptr %4, align 4, !tbaa !1250
    #dbg_declare(ptr %4, !1752, !DIExpression(), !1756)
  store i32 %1, ptr %5, align 4, !tbaa !1170
    #dbg_declare(ptr %5, !1753, !DIExpression(), !1757)
  store i32 %2, ptr %6, align 4, !tbaa !1170
    #dbg_declare(ptr %6, !1754, !DIExpression(), !1758)
  %8 = load ptr, ptr %4, align 4, !dbg !1759, !tbaa !1250
  %9 = load i32, ptr %5, align 4, !dbg !1760, !tbaa !1170
  %10 = load i32, ptr %6, align 4, !dbg !1761, !tbaa !1170
  %11 = call i32 (ptr, ...) @printf(ptr noundef @.str.337, ptr noundef %8, i32 noundef %9, i32 noundef %10), !dbg !1762
  call void @llvm.lifetime.start.p0(i64 4, ptr %7) #19, !dbg !1763
    #dbg_declare(ptr %7, !1755, !DIExpression(), !1764)
  %12 = load ptr, ptr %4, align 4, !dbg !1765, !tbaa !1250
  store ptr %12, ptr %7, align 4, !dbg !1764, !tbaa !1250
  %13 = load ptr, ptr %7, align 4, !dbg !1766, !tbaa !1250
  %14 = load i32, ptr %5, align 4, !dbg !1767, !tbaa !1170
  %15 = load i32, ptr %6, align 4, !dbg !1768, !tbaa !1170
  %16 = call i32 %13(i32 noundef %14, i32 noundef %15), !dbg !1766
  call void @llvm.lifetime.end.p0(i64 4, ptr %7) #19, !dbg !1769
  ret i32 %16, !dbg !1770
}

; Function Attrs: nofree noinline nounwind
define dso_local noundef i32 @add_func(i32 noundef %0, i32 noundef %1) #0 !dbg !1771 {
    #dbg_value(i32 %0, !1773, !DIExpression(), !1775)
    #dbg_value(i32 %1, !1774, !DIExpression(), !1775)
  %3 = add nsw i32 %1, %0, !dbg !1776
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.2, i32 noundef %0, i32 noundef %1, i32 noundef %3), !dbg !1777
  ret i32 %3, !dbg !1778
}

; Function Attrs: noinline nounwind
define dso_local i32 @call_operation_by_index(i32 noundef %0, i32 noundef %1, i32 noundef %2) #1 !dbg !1779 {
    #dbg_value(i32 %0, !1783, !DIExpression(), !1786)
    #dbg_value(i32 %1, !1784, !DIExpression(), !1786)
    #dbg_value(i32 %2, !1785, !DIExpression(), !1786)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.11, i32 noundef %0, i32 noundef %1, i32 noundef %2), !dbg !1787
  %5 = srem i32 %0, 2, !dbg !1788
  %6 = getelementptr inbounds [2 x ptr], ptr @operations, i32 0, i32 %5, !dbg !1789
  %7 = load ptr, ptr %6, align 4, !dbg !1789, !tbaa !1250
  %8 = tail call i32 %7(i32 noundef %1, i32 noundef %2) #19, !dbg !1789
  ret i32 %8, !dbg !1790
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_control_flow() #0 !dbg !1791 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.420), !dbg !1803
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.421), !dbg !1804
    #dbg_value(i32 10, !1793, !DIExpression(), !1805)
    #dbg_value(i32 20, !1794, !DIExpression(), !1805)
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.422), !dbg !1806
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.423), !dbg !1809
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.424), !dbg !1812
    #dbg_value(double 3.140000e+00, !1795, !DIExpression(), !1805)
    #dbg_value(double 3.141000e+00, !1796, !DIExpression(), !1805)
  %6 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.425), !dbg !1815
  %7 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.426), !dbg !1818
    #dbg_value(i32 0, !1797, !DIExpression(), !1805)
    #dbg_value(i32 1, !1798, !DIExpression(), !1819)
  br label %12, !dbg !1820

8:                                                ; preds = %12
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.111, i32 noundef 15), !dbg !1821
  %10 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.427), !dbg !1822
  %11 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.429), !dbg !1825
    #dbg_value(i32 0, !1800, !DIExpression(), !1826)
  br label %23, !dbg !1827

12:                                               ; preds = %0, %12
  %13 = phi i32 [ 1, %0 ], [ %17, %12 ]
  %14 = phi i32 [ 0, %0 ], [ %15, %12 ]
    #dbg_value(i32 %13, !1798, !DIExpression(), !1819)
    #dbg_value(i32 %14, !1797, !DIExpression(), !1805)
  %15 = add nuw nsw i32 %13, %14, !dbg !1828
    #dbg_value(i32 %15, !1797, !DIExpression(), !1805)
  %16 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.110, i32 noundef %13, i32 noundef %15), !dbg !1831
  %17 = add nuw nsw i32 %13, 1, !dbg !1832
    #dbg_value(i32 %17, !1798, !DIExpression(), !1819)
  %18 = icmp eq i32 %17, 6, !dbg !1833
  br i1 %18, label %8, label %12, !dbg !1820, !llvm.loop !1834

19:                                               ; preds = %29
  %20 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.430), !dbg !1836
    #dbg_label(!1802, !1837)
  %21 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.431), !dbg !1838
  %22 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.432), !dbg !1839
  ret void, !dbg !1840

23:                                               ; preds = %8, %29
  %24 = phi i32 [ 0, %8 ], [ %32, %29 ]
    #dbg_value(i32 %24, !1800, !DIExpression(), !1826)
  %25 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.115, i32 noundef %24), !dbg !1841
  switch i32 %24, label %28 [
    i32 0, label %29
    i32 1, label %26
    i32 2, label %27
  ], !dbg !1844

26:                                               ; preds = %23
  br label %29, !dbg !1845

27:                                               ; preds = %23
  br label %29, !dbg !1847

28:                                               ; preds = %23
  br label %29, !dbg !1848

29:                                               ; preds = %23, %26, %27, %28
  %30 = phi ptr [ @str.434, %26 ], [ @str.433, %27 ], [ @str.436, %28 ], [ @str.435, %23 ]
  %31 = tail call i32 @puts(ptr nonnull dereferenceable(1) %30), !dbg !1849
  %32 = add nuw nsw i32 %24, 1, !dbg !1850
    #dbg_value(i32 %32, !1800, !DIExpression(), !1826)
  %33 = icmp eq i32 %32, 4, !dbg !1851
  br i1 %33, label %19, label %23, !dbg !1827, !llvm.loop !1852
}

; Function Attrs: nounwind
define internal void @test_timer_cb(ptr noundef %0) #5 !dbg !1105 {
    #dbg_value(ptr %0, !1113, !DIExpression(), !1854)
  %2 = tail call ptr @pvTimerGetTimerID(ptr noundef %0) #19, !dbg !1855
    #dbg_value(ptr %2, !1114, !DIExpression(), !1854)
  %3 = load i32, ptr @test_timer_cb.tick, align 4, !dbg !1856, !tbaa !1170
  %4 = add nsw i32 %3, 1, !dbg !1856
  store i32 %4, ptr @test_timer_cb.tick, align 4, !dbg !1856, !tbaa !1170
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.402, i32 noundef %3, ptr noundef %2), !dbg !1857
  ret void, !dbg !1858
}

; Function Attrs: noinline nounwind
define dso_local void @test_second() #1 !dbg !1859 {
  %1 = alloca ptr, align 4, !DIAssignID !1869
    #dbg_assign(i1 undef, !1862, !DIExpression(), !1869, ptr %1, !DIExpression(), !1870)
    #dbg_value(ptr null, !1861, !DIExpression(), !1870)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1) #19, !dbg !1871
  store ptr null, ptr %1, align 4, !dbg !1872, !tbaa !1250, !DIAssignID !1873
    #dbg_assign(ptr null, !1862, !DIExpression(), !1873, ptr %1, !DIExpression(), !1870)
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.587), !dbg !1874
  tail call void @host_invoke_cb(ptr noundef nonnull @cb_print, ptr noundef nonnull inttoptr (i32 305419896 to ptr)) #19, !dbg !1875
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.588), !dbg !1876
  tail call void @host_invoke_cb2(ptr noundef nonnull @cb_with_int, i32 noundef 42, ptr noundef nonnull inttoptr (i32 -889275714 to ptr)) #19, !dbg !1877
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.589), !dbg !1878
  %5 = tail call ptr @xTimerCreate(ptr noundef nonnull @.str.386, i32 noundef 200, i32 noundef 1, ptr noundef nonnull inttoptr (i32 -559023410 to ptr), ptr noundef nonnull @test_timer_cb) #19, !dbg !1879
    #dbg_value(ptr %5, !1861, !DIExpression(), !1870)
  %6 = icmp eq ptr %5, null, !dbg !1880
  br i1 %6, label %10, label %7, !dbg !1882

7:                                                ; preds = %0
  %8 = tail call i32 @xTaskGetTickCount() #19, !dbg !1883
  %9 = tail call i32 @xTimerGenericCommand(ptr noundef nonnull %5, i32 noundef 1, i32 noundef %8, ptr noundef null, i32 noundef 0) #19, !dbg !1883
  br label %12, !dbg !1885

10:                                               ; preds = %0
  %11 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.590), !dbg !1886
  br label %12

12:                                               ; preds = %10, %7
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.591), !dbg !1888
    #dbg_value(ptr @counter_task, !1889, !DIExpression(), !1909)
    #dbg_value(ptr @.str.389, !1904, !DIExpression(), !1909)
    #dbg_value(i32 4048, !1905, !DIExpression(), !1909)
    #dbg_value(ptr inttoptr (i32 -1430532899 to ptr), !1906, !DIExpression(), !1909)
    #dbg_value(i32 5, !1907, !DIExpression(), !1909)
    #dbg_value(ptr %1, !1908, !DIExpression(), !1909)
  %14 = call i32 @xTaskCreatePinnedToCore(ptr noundef nonnull @counter_task, ptr noundef nonnull @.str.389, i32 noundef 4048, ptr noundef nonnull inttoptr (i32 -1430532899 to ptr), i32 noundef 5, ptr noundef nonnull %1, i32 noundef 2147483647) #19, !dbg !1911
    #dbg_value(i32 0, !1867, !DIExpression(), !1912)
  br label %17, !dbg !1913

15:                                               ; preds = %17
  %16 = call i32 @puts(ptr nonnull dereferenceable(1) @str.592), !dbg !1914
  call void @vTaskDelay(i32 noundef 200) #19, !dbg !1915
  br i1 %6, label %27, label %22, !dbg !1916

17:                                               ; preds = %12, %17
  %18 = phi i32 [ 0, %12 ], [ %20, %17 ]
    #dbg_value(i32 %18, !1867, !DIExpression(), !1912)
  %19 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.390, i32 noundef %18), !dbg !1917
  call void @vTaskDelay(i32 noundef 1000) #19, !dbg !1920
  %20 = add nuw nsw i32 %18, 1, !dbg !1921
    #dbg_value(i32 %20, !1867, !DIExpression(), !1912)
  %21 = icmp eq i32 %18, 0, !dbg !1922
  br i1 %21, label %17, label %15, !dbg !1913, !llvm.loop !1923

22:                                               ; preds = %15
  %23 = call i32 @puts(ptr nonnull dereferenceable(1) @str.593), !dbg !1925
  %24 = call i32 @xTimerGenericCommand(ptr noundef nonnull %5, i32 noundef 3, i32 noundef 0, ptr noundef null, i32 noundef 0) #19, !dbg !1928
  %25 = call i32 @xTimerGenericCommand(ptr noundef nonnull %5, i32 noundef 5, i32 noundef 0, ptr noundef null, i32 noundef 0) #19, !dbg !1929
  %26 = call i32 @puts(ptr nonnull dereferenceable(1) @str.594), !dbg !1930
  br label %27, !dbg !1931

27:                                               ; preds = %22, %15
  %28 = load ptr, ptr %1, align 4, !dbg !1932, !tbaa !1250
  %29 = icmp eq ptr %28, null, !dbg !1932
  br i1 %29, label %33, label %30, !dbg !1934

30:                                               ; preds = %27
  %31 = call i32 @puts(ptr nonnull dereferenceable(1) @str.596), !dbg !1935
  %32 = load ptr, ptr %1, align 4, !dbg !1937, !tbaa !1250
  call void @vTaskDelete(ptr noundef %32) #19, !dbg !1938
  br label %33, !dbg !1939

33:                                               ; preds = %27, %30
  %34 = phi ptr [ @str.597, %30 ], [ @str.595, %27 ]
  %35 = call i32 @puts(ptr nonnull dereferenceable(1) %34), !dbg !1940
  %36 = call i32 @puts(ptr nonnull dereferenceable(1) @str.598), !dbg !1941
  call void @vTaskDelay(i32 noundef 2000) #19, !dbg !1942
  %37 = call i32 @puts(ptr nonnull dereferenceable(1) @str.599), !dbg !1943
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1) #19, !dbg !1944
  ret void, !dbg !1944
}

; Function Attrs: nounwind
define dso_local void @test_call_indirect_with_table() #5 !dbg !1945 {
  %1 = alloca [3 x ptr], align 4, !DIAssignID !1968
    #dbg_assign(i1 undef, !1950, !DIExpression(), !1968, ptr %1, !DIExpression(), !1969)
  %2 = alloca [3 x i32], align 4, !DIAssignID !1970
    #dbg_assign(i1 undef, !1956, !DIExpression(), !1970, ptr %2, !DIExpression(), !1969)
  %3 = alloca [3 x i32], align 4, !DIAssignID !1971
    #dbg_assign(i1 undef, !1963, !DIExpression(), !1971, ptr %3, !DIExpression(), !1969)
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.534), !dbg !1972
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.535), !dbg !1973
  %6 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.536), !dbg !1974
  tail call void @OP_table_set(i32 noundef 0, i32 noundef 0, i32 noundef 0) #19, !dbg !1975
  %7 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.537), !dbg !1976
  tail call void @OP_table_set(i32 noundef 0, i32 noundef 1, i32 noundef 1) #19, !dbg !1977
  %8 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.538), !dbg !1978
  tail call void @OP_table_set(i32 noundef 0, i32 noundef 2, i32 noundef 2) #19, !dbg !1979
  %9 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.539), !dbg !1980
  %10 = tail call i32 @OP_table_get(i32 noundef 0, i32 noundef 0) #19, !dbg !1981
    #dbg_value(i32 %10, !1947, !DIExpression(), !1969)
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.303, i32 noundef %10), !dbg !1982
  %12 = tail call i32 @OP_table_get(i32 noundef 0, i32 noundef 1) #19, !dbg !1983
    #dbg_value(i32 %12, !1948, !DIExpression(), !1969)
  %13 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.304, i32 noundef %12), !dbg !1984
  %14 = tail call i32 @OP_table_get(i32 noundef 0, i32 noundef 2) #19, !dbg !1985
    #dbg_value(i32 %14, !1949, !DIExpression(), !1969)
  %15 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.305, i32 noundef %14), !dbg !1986
  %16 = icmp eq i32 %10, 0, !dbg !1987
  %17 = icmp eq i32 %12, 1
  %18 = and i1 %16, %17, !dbg !1989
  %19 = icmp eq i32 %14, 2
  %20 = and i1 %18, %19, !dbg !1989
  %21 = select i1 %20, ptr @str.541, ptr @str.540
  %22 = tail call i32 @puts(ptr nonnull dereferenceable(1) %21), !dbg !1990
  %23 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.542), !dbg !1991
  %24 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.543), !dbg !1992
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %1) #19, !dbg !1993
  %25 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.544), !dbg !1994
  store ptr @add_function_1, ptr %1, align 4, !dbg !1995, !tbaa !1250, !DIAssignID !1996
    #dbg_assign(ptr @add_function_1, !1950, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !1996, ptr %1, !DIExpression(), !1969)
  %26 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.545), !dbg !1997
  %27 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 1, !dbg !1998
  store ptr @multiply_function_1, ptr %27, align 4, !dbg !1999, !tbaa !1250, !DIAssignID !2000
    #dbg_assign(ptr @multiply_function_1, !1950, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !2000, ptr %27, !DIExpression(), !1969)
  %28 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.546), !dbg !2001
  %29 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 2, !dbg !2002
  store ptr @subtract_function_1, ptr %29, align 4, !dbg !2003, !tbaa !1250, !DIAssignID !2004
    #dbg_assign(ptr @subtract_function_1, !1950, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !2004, ptr %29, !DIExpression(), !1969)
  %30 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.547), !dbg !2005
  %31 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.548), !dbg !2006
  %32 = tail call i32 @add_function_1(i32 noundef 10, i32 noundef 5) #19, !dbg !2007
    #dbg_value(i32 %32, !1953, !DIExpression(), !1969)
  %33 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.315, i32 noundef %32), !dbg !2008
  %34 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.549), !dbg !2009
  %35 = tail call i32 @multiply_function_1(i32 noundef 10, i32 noundef 5) #19, !dbg !2010
    #dbg_value(i32 %35, !1954, !DIExpression(), !1969)
  %36 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.317, i32 noundef %35), !dbg !2011
  %37 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.550), !dbg !2012
  %38 = tail call i32 @subtract_function_1(i32 noundef 10, i32 noundef 5) #19, !dbg !2013
    #dbg_value(i32 %38, !1955, !DIExpression(), !1969)
  %39 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.319, i32 noundef %38), !dbg !2014
  %40 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.551), !dbg !2015
  %41 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.552), !dbg !2016
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %2) #19, !dbg !2017
    #dbg_value(i32 0, !1958, !DIExpression(), !2018)
  br label %45, !dbg !2019

42:                                               ; preds = %45
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %3) #19, !dbg !2020
  store i32 10, ptr %3, align 4, !dbg !2021, !DIAssignID !2022
    #dbg_assign(i32 10, !1963, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !2022, ptr %3, !DIExpression(), !1969)
  %43 = getelementptr inbounds i32, ptr %3, i32 1, !dbg !2021
  store i32 21, ptr %43, align 4, !dbg !2021, !DIAssignID !2023
    #dbg_assign(i32 21, !1963, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !2023, ptr %43, !DIExpression(), !1969)
  %44 = getelementptr inbounds i32, ptr %3, i32 2, !dbg !2021
  store i32 4, ptr %44, align 4, !dbg !2021, !DIAssignID !2024
    #dbg_assign(i32 4, !1963, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !2024, ptr %44, !DIExpression(), !1969)
    #dbg_value(i8 1, !1964, !DIExpression(), !1969)
    #dbg_value(i32 0, !1965, !DIExpression(), !2025)
    #dbg_value(i8 poison, !1964, !DIExpression(), !1969)
  br label %65, !dbg !2026

45:                                               ; preds = %0, %45
  %46 = phi i32 [ 0, %0 ], [ %53, %45 ]
    #dbg_value(i32 %46, !1958, !DIExpression(), !2018)
  %47 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.322, i32 noundef %46, i32 noundef %46), !dbg !2027
  %48 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 %46, !dbg !2028
  %49 = load ptr, ptr %48, align 4, !dbg !2028, !tbaa !1250
  %50 = tail call i32 %49(i32 noundef 7, i32 noundef 3) #19, !dbg !2028
    #dbg_value(i32 %50, !1960, !DIExpression(), !2029)
  %51 = getelementptr inbounds [3 x i32], ptr %2, i32 0, i32 %46, !dbg !2030
  store i32 %50, ptr %51, align 4, !dbg !2031, !tbaa !1170
  %52 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.323, i32 noundef %46, i32 noundef %50), !dbg !2032
  %53 = add nuw nsw i32 %46, 1, !dbg !2033
    #dbg_value(i32 %53, !1958, !DIExpression(), !2018)
  %54 = icmp eq i32 %53, 3, !dbg !2034
  br i1 %54, label %42, label %45, !dbg !2019, !llvm.loop !2035

55:                                               ; preds = %75
  %56 = select i1 %76, ptr @str.554, ptr @str.553
  %57 = tail call i32 @puts(ptr nonnull dereferenceable(1) %56), !dbg !2037
  %58 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.555), !dbg !2039
  %59 = icmp eq i32 %32, 15, !dbg !2040
  %60 = icmp eq i32 %35, 50
  %61 = and i1 %59, %60, !dbg !2041
  %62 = icmp eq i32 %38, 5
  %63 = and i1 %61, %62, !dbg !2041
  %64 = select i1 %63, i1 %76, i1 false, !dbg !2041
    #dbg_value(i1 %64, !1967, !DIExpression(DW_OP_LLVM_convert, 1, DW_ATE_unsigned, DW_OP_LLVM_convert, 8, DW_ATE_unsigned, DW_OP_stack_value), !1969)
  br i1 %64, label %79, label %85, !dbg !2042

65:                                               ; preds = %42, %75
  %66 = phi i32 [ 0, %42 ], [ %77, %75 ]
  %67 = phi i1 [ true, %42 ], [ %76, %75 ]
    #dbg_value(i32 %66, !1965, !DIExpression(), !2025)
  %68 = getelementptr inbounds [3 x i32], ptr %2, i32 0, i32 %66, !dbg !2043
  %69 = load i32, ptr %68, align 4, !dbg !2043, !tbaa !1170
  %70 = getelementptr inbounds [3 x i32], ptr %3, i32 0, i32 %66, !dbg !2047
  %71 = load i32, ptr %70, align 4, !dbg !2047, !tbaa !1170
  %72 = icmp eq i32 %69, %71, !dbg !2048
  br i1 %72, label %75, label %73, !dbg !2049

73:                                               ; preds = %65
  %74 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.324, i32 noundef %66, i32 noundef %69, i32 noundef %71), !dbg !2050
    #dbg_value(i8 0, !1964, !DIExpression(), !1969)
  br label %75, !dbg !2052

75:                                               ; preds = %65, %73
  %76 = phi i1 [ false, %73 ], [ %67, %65 ]
    #dbg_value(i8 poison, !1964, !DIExpression(), !1969)
  %77 = add nuw nsw i32 %66, 1, !dbg !2053
    #dbg_value(i32 %77, !1965, !DIExpression(), !2025)
  %78 = icmp eq i32 %77, 3, !dbg !2054
  br i1 %78, label %55, label %65, !dbg !2026, !llvm.loop !2055

79:                                               ; preds = %55
  %80 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.558), !dbg !2057
  %81 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.559), !dbg !2060
  %82 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.560), !dbg !2061
  %83 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.561), !dbg !2062
  %84 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.331, i32 noundef %10, i32 noundef %12, i32 noundef %14), !dbg !2063
  br label %95, !dbg !2064

85:                                               ; preds = %55
  %86 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.556), !dbg !2065
  %87 = icmp ne i32 %32, 15, !dbg !2067
  %88 = icmp ne i32 %35, 50
  %89 = or i1 %87, %88, !dbg !2069
  %90 = icmp ne i32 %38, 5
  %91 = or i1 %89, %90, !dbg !2069
  br i1 %91, label %92, label %94, !dbg !2069

92:                                               ; preds = %85
  %93 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.334, i32 noundef %32, i32 noundef %35, i32 noundef %38), !dbg !2070
  br label %94, !dbg !2072

94:                                               ; preds = %85, %92
  br i1 %76, label %98, label %95, !dbg !2073

95:                                               ; preds = %94, %79
  %96 = phi ptr [ @str.562, %79 ], [ @str.557, %94 ]
  %97 = tail call i32 @puts(ptr nonnull dereferenceable(1) %96), !dbg !2074
  br label %98, !dbg !2075

98:                                               ; preds = %95, %94
  %99 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.563), !dbg !2075
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %3) #19, !dbg !2076
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %2) #19, !dbg !2076
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %1) #19, !dbg !2076
  ret void, !dbg !2076
}

; Function Attrs: nofree noinline nounwind
define internal void @cb_print(ptr noundef %0) #0 !dbg !2077 {
    #dbg_value(ptr %0, !2079, !DIExpression(), !2080)
  %2 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.400, ptr noundef %0), !dbg !2081
  ret void, !dbg !2082
}

; Function Attrs: noinline nounwind
define dso_local void @test_call_indirect() #1 !dbg !2083 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str), !dbg !2090
    #dbg_value(ptr @add_func, !2085, !DIExpression(), !2091)
  %2 = tail call i32 @add_func(i32 noundef 5, i32 noundef 3), !dbg !2092
    #dbg_value(i32 %2, !2086, !DIExpression(), !2091)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.6, i32 noundef %2), !dbg !2093
  %4 = tail call i32 @call_operation(ptr noundef nonnull @add_func, i32 noundef 10, i32 noundef 20), !dbg !2094
    #dbg_value(i32 %4, !2087, !DIExpression(), !2091)
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.7, i32 noundef %4), !dbg !2095
  %6 = tail call i32 @call_operation(ptr noundef nonnull @mul_func, i32 noundef 4, i32 noundef 7), !dbg !2096
    #dbg_value(i32 %6, !2088, !DIExpression(), !2091)
  %7 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.8, i32 noundef %6), !dbg !2097
    #dbg_value(ptr @mul_func, !2085, !DIExpression(), !2091)
  %8 = tail call i32 @mul_func(i32 noundef 6, i32 noundef 9), !dbg !2098
    #dbg_value(i32 %8, !2089, !DIExpression(), !2091)
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.9, i32 noundef %8), !dbg !2099
  %10 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.405), !dbg !2100
  ret void, !dbg !2101
}

; Function Attrs: noinline nounwind
define dso_local i32 @call_operation(ptr nocapture noundef readonly %0, i32 noundef %1, i32 noundef %2) #1 !dbg !2102 {
    #dbg_value(ptr %0, !2106, !DIExpression(), !2109)
    #dbg_value(i32 %1, !2107, !DIExpression(), !2109)
    #dbg_value(i32 %2, !2108, !DIExpression(), !2109)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.4, i32 noundef %1, i32 noundef %2), !dbg !2110
  %5 = tail call i32 %0(i32 noundef %1, i32 noundef %2) #19, !dbg !2111
  ret i32 %5, !dbg !2112
}

; Function Attrs: nofree noinline nounwind
define dso_local i32 @subtract_function_1(i32 noundef %0, i32 noundef %1) #0 !dbg !2113 {
    #dbg_value(i32 %0, !2115, !DIExpression(), !2117)
    #dbg_value(i32 %1, !2116, !DIExpression(), !2117)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.261, i32 noundef %0, i32 noundef %1), !dbg !2118
  %4 = sub nsw i32 %0, %1, !dbg !2119
  ret i32 %4, !dbg !2120
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_first() #0 !dbg !2121 {
    #dbg_value(i64 10, !2123, !DIExpression(), !2133)
    #dbg_value(i64 5, !2124, !DIExpression(), !2133)
    #dbg_value(i64 3, !2125, !DIExpression(), !2133)
    #dbg_value(i64 20, !2123, !DIExpression(), !2133)
    #dbg_value(i64 15, !2123, !DIExpression(), !2133)
    #dbg_value(i64 10, !2123, !DIExpression(), !2133)
    #dbg_value(i64 0, !2123, !DIExpression(), !2133)
    #dbg_value(i64 3, !2123, !DIExpression(), !2133)
    #dbg_value(i64 6, !2123, !DIExpression(), !2133)
    #dbg_value(i64 12, !2123, !DIExpression(), !2133)
    #dbg_value(i64 3, !2123, !DIExpression(), !2133)
    #dbg_value(i64 1, !2123, !DIExpression(), !2133)
  %1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.358, i32 noundef 1), !dbg !2134
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.577), !dbg !2135
    #dbg_value(i8 1, !2126, !DIExpression(), !2133)
    #dbg_value(i8 0, !2127, !DIExpression(), !2133)
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.578), !dbg !2136
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.579), !dbg !2139
    #dbg_value(i8 0, !2128, !DIExpression(), !2133)
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.580), !dbg !2142
    #dbg_value(i8 1, !2127, !DIExpression(), !2133)
  %6 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.581), !dbg !2145
  %7 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.582), !dbg !2148
  %8 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.583), !dbg !2149
    #dbg_value(float 1.500000e+00, !2129, !DIExpression(), !2133)
    #dbg_value(float 2.250000e+00, !2130, !DIExpression(), !2133)
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.370, double noundef 1.500000e+00, double noundef 2.250000e+00), !dbg !2150
  %10 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.371, double noundef 3.750000e+00), !dbg !2151
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.372, double noundef 7.500000e-01), !dbg !2152
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.373, double noundef 3.375000e+00), !dbg !2153
  %13 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.374, double noundef 1.500000e+00), !dbg !2154
  %14 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.584), !dbg !2155
  %15 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.585), !dbg !2156
    #dbg_value(double 0x3FF3C0CA4283DE1B, !2131, !DIExpression(), !2133)
    #dbg_value(double 0x4023C0CA4588F633, !2132, !DIExpression(), !2133)
  %16 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.377, double noundef 0x3FF3C0CA4283DE1B, double noundef 0x4023C0CA4588F633), !dbg !2157
  %17 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.378, double noundef 0x402638E38DD971F6), !dbg !2158
  %18 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.379, double noundef 0x402148B0FD387A70), !dbg !2159
  %19 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.380, double noundef 0x402862F361ECCCD3), !dbg !2160
  %20 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.381, double noundef 0x40200000027234CC), !dbg !2161
  %21 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.586), !dbg !2162
  ret void, !dbg !2163
}

; Function Attrs: nofree nounwind
declare !dbg !2164 dso_local noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #6

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr nocapture noundef readonly) local_unnamed_addr #7

declare !dbg !2169 dso_local void @OP_table_set(i32 noundef, i32 noundef, i32 noundef) local_unnamed_addr #8

declare !dbg !2172 dso_local i32 @OP_table_get(i32 noundef, i32 noundef) local_unnamed_addr #8

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #9

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #9

declare !dbg !2173 dso_local void @host_invoke_cb(ptr noundef, ptr noundef) local_unnamed_addr #8

declare !dbg !2176 dso_local void @host_invoke_cb2(ptr noundef, i32 noundef, ptr noundef) local_unnamed_addr #8

declare !dbg !2180 dso_local ptr @xTimerCreate(ptr noundef, i32 noundef, i32 noundef, ptr noundef, ptr noundef) local_unnamed_addr #8

declare !dbg !2187 dso_local i32 @xTaskGetTickCount() local_unnamed_addr #8

declare !dbg !2190 dso_local i32 @xTimerGenericCommand(ptr noundef, i32 noundef, i32 noundef, ptr noundef, i32 noundef) local_unnamed_addr #8

declare !dbg !2195 dso_local i32 @xTaskCreatePinnedToCore(ptr noundef, ptr noundef, i32 noundef, ptr noundef, i32 noundef, ptr noundef, i32 noundef) local_unnamed_addr #8

declare !dbg !2198 dso_local void @vTaskDelay(i32 noundef) local_unnamed_addr #8

declare !dbg !2201 dso_local void @vTaskDelete(ptr noundef) local_unnamed_addr #8

declare !dbg !2204 dso_local ptr @pvTimerGetTimerID(ptr noundef) local_unnamed_addr #8

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i32, i1 immarg) #10

; Function Attrs: nofree nounwind
declare noundef i32 @putchar(i32 noundef) local_unnamed_addr #7

declare !dbg !2208 dso_local i32 @OP_table_size(i32 noundef) local_unnamed_addr #8

declare !dbg !2211 dso_local void @OP_table_init(i32 noundef, i32 noundef, i32 noundef, i32 noundef, i32 noundef) local_unnamed_addr #8

declare !dbg !2214 dso_local void @OP_table_fill(i32 noundef, i32 noundef, i32 noundef, i32 noundef) local_unnamed_addr #8

declare !dbg !2217 dso_local void @OP_table_copy(i32 noundef, i32 noundef, i32 noundef, i32 noundef, i32 noundef) local_unnamed_addr #8

; Function Attrs: nofree noinline nounwind
define dso_local void @test_globals() local_unnamed_addr #0 !dbg !2218 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.408), !dbg !2219
  %2 = load i32, ptr @g_my_global_int, align 4, !dbg !2220, !tbaa !1170
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.19, i32 noundef %2), !dbg !2221
  %4 = load i64, ptr @g_my_global_long, align 8, !dbg !2222, !tbaa !1220
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.20, i64 noundef %4), !dbg !2223
  %6 = load i8, ptr @g_my_global_bool, align 1, !dbg !2224, !tbaa !1673, !range !1654, !noundef !1655
  %7 = icmp eq i8 %6, 0, !dbg !2224
  %8 = select i1 %7, ptr @.str.23, ptr @.str.22, !dbg !2224
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.21, ptr noundef nonnull %8), !dbg !2225
  %10 = load i32, ptr @g_my_global_int, align 4, !dbg !2226, !tbaa !1170
  %11 = add nsw i32 %10, 100, !dbg !2226
  store i32 %11, ptr @g_my_global_int, align 4, !dbg !2226, !tbaa !1170
  %12 = load i64, ptr @g_my_global_long, align 8, !dbg !2227, !tbaa !1220
  %13 = add nsw i64 %12, -10, !dbg !2227
  store i64 %13, ptr @g_my_global_long, align 8, !dbg !2227, !tbaa !1220
  %14 = load i8, ptr @g_my_global_bool, align 1, !dbg !2228, !tbaa !1673, !range !1654, !noundef !1655
  %15 = xor i8 %14, 1, !dbg !2229
  store i8 %15, ptr @g_my_global_bool, align 1, !dbg !2229, !tbaa !1673
  %16 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.24, i32 noundef %11), !dbg !2230
  %17 = load i64, ptr @g_my_global_long, align 8, !dbg !2231, !tbaa !1220
  %18 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.25, i64 noundef %17), !dbg !2232
  %19 = load i8, ptr @g_my_global_bool, align 1, !dbg !2233, !tbaa !1673, !range !1654, !noundef !1655
  %20 = icmp eq i8 %19, 0, !dbg !2233
  %21 = select i1 %20, ptr @.str.23, ptr @.str.22, !dbg !2233
  %22 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.26, ptr noundef nonnull %21), !dbg !2234
  %23 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.409), !dbg !2235
  ret void, !dbg !2236
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_math_operations32() local_unnamed_addr #0 !dbg !2237 {
  %1 = alloca i32, align 4, !DIAssignID !2245
    #dbg_assign(i1 undef, !2239, !DIExpression(), !2245, ptr %1, !DIExpression(), !2246)
  %2 = alloca i32, align 4, !DIAssignID !2247
    #dbg_assign(i1 undef, !2241, !DIExpression(), !2247, ptr %2, !DIExpression(), !2246)
  %3 = alloca i32, align 4, !DIAssignID !2248
    #dbg_assign(i1 undef, !2242, !DIExpression(), !2248, ptr %3, !DIExpression(), !2246)
  %4 = alloca i32, align 4, !DIAssignID !2249
    #dbg_assign(i1 undef, !2244, !DIExpression(), !2249, ptr %4, !DIExpression(), !2246)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1), !dbg !2250
  store volatile i32 -100, ptr %1, align 4, !dbg !2251, !tbaa !1170, !DIAssignID !2252
    #dbg_assign(i32 -100, !2239, !DIExpression(), !2252, ptr %1, !DIExpression(), !2246)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %2), !dbg !2253
  store volatile i32 25, ptr %2, align 4, !dbg !2254, !tbaa !1170, !DIAssignID !2255
    #dbg_assign(i32 25, !2241, !DIExpression(), !2255, ptr %2, !DIExpression(), !2246)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %3), !dbg !2256
  store volatile i32 100, ptr %3, align 4, !dbg !2257, !tbaa !1170, !DIAssignID !2258
    #dbg_assign(i32 100, !2242, !DIExpression(), !2258, ptr %3, !DIExpression(), !2246)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %4), !dbg !2259
  store volatile i32 25, ptr %4, align 4, !dbg !2260, !tbaa !1170, !DIAssignID !2261
    #dbg_assign(i32 25, !2244, !DIExpression(), !2261, ptr %4, !DIExpression(), !2246)
  %5 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.410), !dbg !2262
  %6 = load volatile i32, ptr %1, align 4, !dbg !2263, !tbaa !1170
  %7 = load volatile i32, ptr %2, align 4, !dbg !2264, !tbaa !1170
  %8 = load volatile i32, ptr %3, align 4, !dbg !2265, !tbaa !1170
  %9 = load volatile i32, ptr %4, align 4, !dbg !2266, !tbaa !1170
  %10 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.29, i32 noundef %6, i32 noundef %7, i32 noundef %8, i32 noundef %9), !dbg !2267
  %11 = load volatile i32, ptr %1, align 4, !dbg !2268, !tbaa !1170
  %12 = load volatile i32, ptr %2, align 4, !dbg !2269, !tbaa !1170
  %13 = add nsw i32 %12, %11, !dbg !2270
  %14 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.30, i32 noundef %13), !dbg !2271
  %15 = load volatile i32, ptr %1, align 4, !dbg !2272, !tbaa !1170
  %16 = load volatile i32, ptr %2, align 4, !dbg !2273, !tbaa !1170
  %17 = sub nsw i32 %15, %16, !dbg !2274
  %18 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.31, i32 noundef %17), !dbg !2275
  %19 = load volatile i32, ptr %1, align 4, !dbg !2276, !tbaa !1170
  %20 = load volatile i32, ptr %2, align 4, !dbg !2277, !tbaa !1170
  %21 = mul nsw i32 %20, %19, !dbg !2278
  %22 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.32, i32 noundef %21), !dbg !2279
  %23 = load volatile i32, ptr %1, align 4, !dbg !2280, !tbaa !1170
  %24 = load volatile i32, ptr %2, align 4, !dbg !2281, !tbaa !1170
  %25 = sdiv i32 %23, %24, !dbg !2282
  %26 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.33, i32 noundef %25), !dbg !2283
  %27 = load volatile i32, ptr %1, align 4, !dbg !2284, !tbaa !1170
  %28 = load volatile i32, ptr %2, align 4, !dbg !2285, !tbaa !1170
  %29 = srem i32 %27, %28, !dbg !2286
  %30 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.34, i32 noundef %29), !dbg !2287
  %31 = load volatile i32, ptr %3, align 4, !dbg !2288, !tbaa !1170
  %32 = load volatile i32, ptr %4, align 4, !dbg !2289, !tbaa !1170
  %33 = udiv i32 %31, %32, !dbg !2290
  %34 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.35, i32 noundef %33), !dbg !2291
  %35 = load volatile i32, ptr %3, align 4, !dbg !2292, !tbaa !1170
  %36 = load volatile i32, ptr %4, align 4, !dbg !2293, !tbaa !1170
  %37 = urem i32 %35, %36, !dbg !2294
  %38 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.36, i32 noundef %37), !dbg !2295
  %39 = load volatile i32, ptr %1, align 4, !dbg !2296, !tbaa !1170
  %40 = and i32 %39, 255, !dbg !2297
  %41 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.37, i32 noundef %40), !dbg !2298
  %42 = load volatile i32, ptr %1, align 4, !dbg !2299, !tbaa !1170
  %43 = or i32 %42, 128, !dbg !2300
  %44 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.38, i32 noundef %43), !dbg !2301
  %45 = load volatile i32, ptr %1, align 4, !dbg !2302, !tbaa !1170
  %46 = xor i32 %45, 255, !dbg !2303
  %47 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.39, i32 noundef %46), !dbg !2304
  %48 = load volatile i32, ptr %2, align 4, !dbg !2305, !tbaa !1170
  %49 = shl i32 %48, 2, !dbg !2306
  %50 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.40, i32 noundef %49), !dbg !2307
  %51 = load volatile i32, ptr %1, align 4, !dbg !2308, !tbaa !1170
  %52 = ashr i32 %51, 2, !dbg !2309
  %53 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.41, i32 noundef %52), !dbg !2310
  %54 = load volatile i32, ptr %3, align 4, !dbg !2311, !tbaa !1170
  %55 = lshr i32 %54, 2, !dbg !2312
  %56 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.42, i32 noundef %55), !dbg !2313
  %57 = load volatile i32, ptr %2, align 4, !dbg !2314, !tbaa !1170
  %58 = xor i32 %57, -1, !dbg !2315
  %59 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.43, i32 noundef %58), !dbg !2316
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %4), !dbg !2317
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %3), !dbg !2317
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %2), !dbg !2317
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1), !dbg !2317
  ret void, !dbg !2317
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_math_operations() local_unnamed_addr #0 !dbg !2318 {
  %1 = alloca i32, align 4, !DIAssignID !2338
    #dbg_assign(i1 undef, !2320, !DIExpression(), !2338, ptr %1, !DIExpression(), !2339)
  %2 = alloca i32, align 4, !DIAssignID !2340
    #dbg_assign(i1 undef, !2321, !DIExpression(), !2340, ptr %2, !DIExpression(), !2339)
  %3 = alloca i32, align 4, !DIAssignID !2341
    #dbg_assign(i1 undef, !2322, !DIExpression(), !2341, ptr %3, !DIExpression(), !2339)
  %4 = alloca i32, align 4, !DIAssignID !2342
    #dbg_assign(i1 undef, !2323, !DIExpression(), !2342, ptr %4, !DIExpression(), !2339)
  %5 = alloca i64, align 8, !DIAssignID !2343
    #dbg_assign(i1 undef, !2324, !DIExpression(), !2343, ptr %5, !DIExpression(), !2339)
  %6 = alloca i64, align 8, !DIAssignID !2344
    #dbg_assign(i1 undef, !2325, !DIExpression(), !2344, ptr %6, !DIExpression(), !2339)
  %7 = alloca i64, align 8, !DIAssignID !2345
    #dbg_assign(i1 undef, !2326, !DIExpression(), !2345, ptr %7, !DIExpression(), !2339)
  %8 = alloca i64, align 8, !DIAssignID !2346
    #dbg_assign(i1 undef, !2327, !DIExpression(), !2346, ptr %8, !DIExpression(), !2339)
  %9 = alloca float, align 4, !DIAssignID !2347
    #dbg_assign(i1 undef, !2328, !DIExpression(), !2347, ptr %9, !DIExpression(), !2339)
  %10 = alloca float, align 4, !DIAssignID !2348
    #dbg_assign(i1 undef, !2330, !DIExpression(), !2348, ptr %10, !DIExpression(), !2339)
  %11 = alloca double, align 8, !DIAssignID !2349
    #dbg_assign(i1 undef, !2331, !DIExpression(), !2349, ptr %11, !DIExpression(), !2339)
  %12 = alloca double, align 8, !DIAssignID !2350
    #dbg_assign(i1 undef, !2333, !DIExpression(), !2350, ptr %12, !DIExpression(), !2339)
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.411), !dbg !2351
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1), !dbg !2352
  store volatile i32 -100, ptr %1, align 4, !dbg !2353, !tbaa !1170, !DIAssignID !2354
    #dbg_assign(i32 -100, !2320, !DIExpression(), !2354, ptr %1, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %2), !dbg !2355
  store volatile i32 25, ptr %2, align 4, !dbg !2356, !tbaa !1170, !DIAssignID !2357
    #dbg_assign(i32 25, !2321, !DIExpression(), !2357, ptr %2, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %3), !dbg !2358
  store volatile i32 100, ptr %3, align 4, !dbg !2359, !tbaa !1170, !DIAssignID !2360
    #dbg_assign(i32 100, !2322, !DIExpression(), !2360, ptr %3, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %4), !dbg !2361
  store volatile i32 25, ptr %4, align 4, !dbg !2362, !tbaa !1170, !DIAssignID !2363
    #dbg_assign(i32 25, !2323, !DIExpression(), !2363, ptr %4, !DIExpression(), !2339)
  %14 = load volatile i32, ptr %1, align 4, !dbg !2364, !tbaa !1170
  %15 = load volatile i32, ptr %2, align 4, !dbg !2365, !tbaa !1170
  %16 = load volatile i32, ptr %3, align 4, !dbg !2366, !tbaa !1170
  %17 = load volatile i32, ptr %4, align 4, !dbg !2367, !tbaa !1170
  %18 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.29, i32 noundef %14, i32 noundef %15, i32 noundef %16, i32 noundef %17), !dbg !2368
  %19 = load volatile i32, ptr %1, align 4, !dbg !2369, !tbaa !1170
  %20 = load volatile i32, ptr %2, align 4, !dbg !2370, !tbaa !1170
  %21 = add nsw i32 %20, %19, !dbg !2371
  %22 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.30, i32 noundef %21), !dbg !2372
  %23 = load volatile i32, ptr %1, align 4, !dbg !2373, !tbaa !1170
  %24 = load volatile i32, ptr %2, align 4, !dbg !2374, !tbaa !1170
  %25 = sub nsw i32 %23, %24, !dbg !2375
  %26 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.31, i32 noundef %25), !dbg !2376
  %27 = load volatile i32, ptr %1, align 4, !dbg !2377, !tbaa !1170
  %28 = load volatile i32, ptr %2, align 4, !dbg !2378, !tbaa !1170
  %29 = mul nsw i32 %28, %27, !dbg !2379
  %30 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.32, i32 noundef %29), !dbg !2380
  %31 = load volatile i32, ptr %1, align 4, !dbg !2381, !tbaa !1170
  %32 = load volatile i32, ptr %2, align 4, !dbg !2382, !tbaa !1170
  %33 = sdiv i32 %31, %32, !dbg !2383
  %34 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.33, i32 noundef %33), !dbg !2384
  %35 = load volatile i32, ptr %1, align 4, !dbg !2385, !tbaa !1170
  %36 = load volatile i32, ptr %2, align 4, !dbg !2386, !tbaa !1170
  %37 = srem i32 %35, %36, !dbg !2387
  %38 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.34, i32 noundef %37), !dbg !2388
  %39 = load volatile i32, ptr %3, align 4, !dbg !2389, !tbaa !1170
  %40 = load volatile i32, ptr %4, align 4, !dbg !2390, !tbaa !1170
  %41 = udiv i32 %39, %40, !dbg !2391
  %42 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.35, i32 noundef %41), !dbg !2392
  %43 = load volatile i32, ptr %3, align 4, !dbg !2393, !tbaa !1170
  %44 = load volatile i32, ptr %4, align 4, !dbg !2394, !tbaa !1170
  %45 = urem i32 %43, %44, !dbg !2395
  %46 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.36, i32 noundef %45), !dbg !2396
  %47 = load volatile i32, ptr %1, align 4, !dbg !2397, !tbaa !1170
  %48 = and i32 %47, 255, !dbg !2398
  %49 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.37, i32 noundef %48), !dbg !2399
  %50 = load volatile i32, ptr %1, align 4, !dbg !2400, !tbaa !1170
  %51 = or i32 %50, 128, !dbg !2401
  %52 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.38, i32 noundef %51), !dbg !2402
  %53 = load volatile i32, ptr %1, align 4, !dbg !2403, !tbaa !1170
  %54 = xor i32 %53, 255, !dbg !2404
  %55 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.39, i32 noundef %54), !dbg !2405
  %56 = load volatile i32, ptr %2, align 4, !dbg !2406, !tbaa !1170
  %57 = shl i32 %56, 2, !dbg !2407
  %58 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.40, i32 noundef %57), !dbg !2408
  %59 = load volatile i32, ptr %1, align 4, !dbg !2409, !tbaa !1170
  %60 = ashr i32 %59, 2, !dbg !2410
  %61 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.41, i32 noundef %60), !dbg !2411
  %62 = load volatile i32, ptr %3, align 4, !dbg !2412, !tbaa !1170
  %63 = lshr i32 %62, 2, !dbg !2413
  %64 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.42, i32 noundef %63), !dbg !2414
  %65 = load volatile i32, ptr %2, align 4, !dbg !2415, !tbaa !1170
  %66 = xor i32 %65, -1, !dbg !2416
  %67 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.43, i32 noundef %66), !dbg !2417
  %68 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.412), !dbg !2418
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %5), !dbg !2419
  store volatile i64 -10000000000, ptr %5, align 8, !dbg !2420, !tbaa !1220, !DIAssignID !2421
    #dbg_assign(i64 -10000000000, !2324, !DIExpression(), !2421, ptr %5, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %6), !dbg !2422
  store volatile i64 2500000000, ptr %6, align 8, !dbg !2423, !tbaa !1220, !DIAssignID !2424
    #dbg_assign(i64 2500000000, !2325, !DIExpression(), !2424, ptr %6, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %7), !dbg !2425
  store volatile i64 10000000000, ptr %7, align 8, !dbg !2426, !tbaa !1220, !DIAssignID !2427
    #dbg_assign(i64 10000000000, !2326, !DIExpression(), !2427, ptr %7, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %8), !dbg !2428
  store volatile i64 2500000000, ptr %8, align 8, !dbg !2429, !tbaa !1220, !DIAssignID !2430
    #dbg_assign(i64 2500000000, !2327, !DIExpression(), !2430, ptr %8, !DIExpression(), !2339)
  %69 = load volatile i64, ptr %5, align 8, !dbg !2431, !tbaa !1220
  %70 = load volatile i64, ptr %6, align 8, !dbg !2432, !tbaa !1220
  %71 = load volatile i64, ptr %7, align 8, !dbg !2433, !tbaa !1220
  %72 = load volatile i64, ptr %8, align 8, !dbg !2434, !tbaa !1220
  %73 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.46, i64 noundef %69, i64 noundef %70, i64 noundef %71, i64 noundef %72), !dbg !2435
  %74 = load volatile i64, ptr %5, align 8, !dbg !2436, !tbaa !1220
  %75 = load volatile i64, ptr %6, align 8, !dbg !2437, !tbaa !1220
  %76 = add nsw i64 %75, %74, !dbg !2438
  %77 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.47, i64 noundef %76), !dbg !2439
  %78 = load volatile i64, ptr %5, align 8, !dbg !2440, !tbaa !1220
  %79 = load volatile i64, ptr %6, align 8, !dbg !2441, !tbaa !1220
  %80 = sub nsw i64 %78, %79, !dbg !2442
  %81 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.48, i64 noundef %80), !dbg !2443
  %82 = load volatile i64, ptr %5, align 8, !dbg !2444, !tbaa !1220
  %83 = shl nsw i64 %82, 1, !dbg !2445
  %84 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.49, i64 noundef %83), !dbg !2446
  %85 = load volatile i64, ptr %5, align 8, !dbg !2447, !tbaa !1220
  %86 = load volatile i64, ptr %6, align 8, !dbg !2448, !tbaa !1220
  %87 = sdiv i64 %85, %86, !dbg !2449
  %88 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.50, i64 noundef %87), !dbg !2450
  %89 = load volatile i64, ptr %5, align 8, !dbg !2451, !tbaa !1220
  %90 = load volatile i64, ptr %6, align 8, !dbg !2452, !tbaa !1220
  %91 = srem i64 %89, %90, !dbg !2453
  %92 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.51, i64 noundef %91), !dbg !2454
  %93 = load volatile i64, ptr %7, align 8, !dbg !2455, !tbaa !1220
  %94 = load volatile i64, ptr %8, align 8, !dbg !2456, !tbaa !1220
  %95 = udiv i64 %93, %94, !dbg !2457
  %96 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.52, i64 noundef %95), !dbg !2458
  %97 = load volatile i64, ptr %7, align 8, !dbg !2459, !tbaa !1220
  %98 = load volatile i64, ptr %8, align 8, !dbg !2460, !tbaa !1220
  %99 = urem i64 %97, %98, !dbg !2461
  %100 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.53, i64 noundef %99), !dbg !2462
  %101 = load volatile i64, ptr %5, align 8, !dbg !2463, !tbaa !1220
  %102 = and i64 %101, 4294967295, !dbg !2464
  %103 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.54, i64 noundef %102), !dbg !2465
  %104 = load volatile i64, ptr %5, align 8, !dbg !2466, !tbaa !1220
  %105 = or i64 %104, 2147483648, !dbg !2467
  %106 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.55, i64 noundef %105), !dbg !2468
  %107 = load volatile i64, ptr %5, align 8, !dbg !2469, !tbaa !1220
  %108 = xor i64 %107, 4294967295, !dbg !2470
  %109 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.56, i64 noundef %108), !dbg !2471
  %110 = load volatile i64, ptr %6, align 8, !dbg !2472, !tbaa !1220
  %111 = shl i64 %110, 2, !dbg !2473
  %112 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.57, i64 noundef %111), !dbg !2474
  %113 = load volatile i64, ptr %5, align 8, !dbg !2475, !tbaa !1220
  %114 = ashr i64 %113, 2, !dbg !2476
  %115 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.58, i64 noundef %114), !dbg !2477
  %116 = load volatile i64, ptr %7, align 8, !dbg !2478, !tbaa !1220
  %117 = lshr i64 %116, 2, !dbg !2479
  %118 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.59, i64 noundef %117), !dbg !2480
  %119 = load volatile i64, ptr %6, align 8, !dbg !2481, !tbaa !1220
  %120 = xor i64 %119, -1, !dbg !2482
  %121 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.60, i64 noundef %120), !dbg !2483
  %122 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.413), !dbg !2484
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %9), !dbg !2485
  store volatile float 0xC05EDCCCC0000000, ptr %9, align 4, !dbg !2486, !tbaa !1225, !DIAssignID !2487
    #dbg_assign(float 0xC05EDCCCC0000000, !2328, !DIExpression(), !2487, ptr %9, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %10), !dbg !2488
  store volatile float 2.500000e+00, ptr %10, align 4, !dbg !2489, !tbaa !1225, !DIAssignID !2490
    #dbg_assign(float 2.500000e+00, !2330, !DIExpression(), !2490, ptr %10, !DIExpression(), !2339)
  %123 = load volatile float, ptr %9, align 4, !dbg !2491, !tbaa !1225
  %124 = fpext float %123 to double, !dbg !2491
  %125 = load volatile float, ptr %10, align 4, !dbg !2492, !tbaa !1225
  %126 = fpext float %125 to double, !dbg !2492
  %127 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.62, double noundef %124, double noundef %126), !dbg !2493
  %128 = load volatile float, ptr %9, align 4, !dbg !2494, !tbaa !1225
  %129 = load volatile float, ptr %10, align 4, !dbg !2495, !tbaa !1225
  %130 = fadd float %128, %129, !dbg !2496
  %131 = fpext float %130 to double, !dbg !2494
  %132 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.63, double noundef %131), !dbg !2497
  %133 = load volatile float, ptr %9, align 4, !dbg !2498, !tbaa !1225
  %134 = load volatile float, ptr %10, align 4, !dbg !2499, !tbaa !1225
  %135 = fsub float %133, %134, !dbg !2500
  %136 = fpext float %135 to double, !dbg !2498
  %137 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.64, double noundef %136), !dbg !2501
  %138 = load volatile float, ptr %9, align 4, !dbg !2502, !tbaa !1225
  %139 = load volatile float, ptr %10, align 4, !dbg !2503, !tbaa !1225
  %140 = fmul float %138, %139, !dbg !2504
  %141 = fpext float %140 to double, !dbg !2502
  %142 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.65, double noundef %141), !dbg !2505
  %143 = load volatile float, ptr %9, align 4, !dbg !2506, !tbaa !1225
  %144 = load volatile float, ptr %10, align 4, !dbg !2507, !tbaa !1225
  %145 = fdiv float %143, %144, !dbg !2508
  %146 = fpext float %145 to double, !dbg !2506
  %147 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.66, double noundef %146), !dbg !2509
  %148 = load volatile float, ptr %9, align 4, !dbg !2510, !tbaa !1225
  %149 = tail call float @llvm.fabs.f32(float %148), !dbg !2511
  %150 = fpext float %149 to double, !dbg !2511
  %151 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.67, double noundef %150), !dbg !2512
  %152 = load volatile float, ptr %10, align 4, !dbg !2513, !tbaa !1225
  %153 = load volatile float, ptr %10, align 4, !dbg !2514, !tbaa !1225
  %154 = fmul float %152, %153, !dbg !2515
  %155 = tail call float @sqrtf(float noundef %154) #19, !dbg !2516
  %156 = fpext float %155 to double, !dbg !2516
  %157 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.68, double noundef %156), !dbg !2517
  %158 = load volatile float, ptr %9, align 4, !dbg !2518, !tbaa !1225
  %159 = load volatile float, ptr %10, align 4, !dbg !2519, !tbaa !1225
  %160 = tail call float @llvm.minnum.f32(float %158, float %159), !dbg !2520
  %161 = fpext float %160 to double, !dbg !2520
  %162 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.69, double noundef %161), !dbg !2521
  %163 = load volatile float, ptr %9, align 4, !dbg !2522, !tbaa !1225
  %164 = load volatile float, ptr %10, align 4, !dbg !2523, !tbaa !1225
  %165 = tail call float @llvm.maxnum.f32(float %163, float %164), !dbg !2524
  %166 = fpext float %165 to double, !dbg !2524
  %167 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.70, double noundef %166), !dbg !2525
  %168 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.414), !dbg !2526
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %11), !dbg !2527
  store volatile double 0xC12E240CA45A1CAC, ptr %11, align 8, !dbg !2528, !tbaa !1230, !DIAssignID !2529
    #dbg_assign(double 0xC12E240CA45A1CAC, !2331, !DIExpression(), !2529, ptr %11, !DIExpression(), !2339)
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %12), !dbg !2530
  store volatile double 2.000000e+00, ptr %12, align 8, !dbg !2531, !tbaa !1230, !DIAssignID !2532
    #dbg_assign(double 2.000000e+00, !2333, !DIExpression(), !2532, ptr %12, !DIExpression(), !2339)
  %169 = load volatile double, ptr %11, align 8, !dbg !2533, !tbaa !1230
  %170 = load volatile double, ptr %12, align 8, !dbg !2534, !tbaa !1230
  %171 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.72, double noundef %169, double noundef %170), !dbg !2535
  %172 = load volatile double, ptr %11, align 8, !dbg !2536, !tbaa !1230
  %173 = load volatile double, ptr %12, align 8, !dbg !2537, !tbaa !1230
  %174 = fadd double %172, %173, !dbg !2538
  %175 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.73, double noundef %174), !dbg !2539
  %176 = load volatile double, ptr %11, align 8, !dbg !2540, !tbaa !1230
  %177 = load volatile double, ptr %12, align 8, !dbg !2541, !tbaa !1230
  %178 = fsub double %176, %177, !dbg !2542
  %179 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.74, double noundef %178), !dbg !2543
  %180 = load volatile double, ptr %11, align 8, !dbg !2544, !tbaa !1230
  %181 = load volatile double, ptr %12, align 8, !dbg !2545, !tbaa !1230
  %182 = fmul double %180, %181, !dbg !2546
  %183 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.75, double noundef %182), !dbg !2547
  %184 = load volatile double, ptr %11, align 8, !dbg !2548, !tbaa !1230
  %185 = load volatile double, ptr %12, align 8, !dbg !2549, !tbaa !1230
  %186 = fdiv double %184, %185, !dbg !2550
  %187 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.76, double noundef %186), !dbg !2551
  %188 = load volatile double, ptr %11, align 8, !dbg !2552, !tbaa !1230
  %189 = tail call double @llvm.fabs.f64(double %188), !dbg !2553
  %190 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.77, double noundef %189), !dbg !2554
  %191 = load volatile double, ptr %12, align 8, !dbg !2555, !tbaa !1230
  %192 = load volatile double, ptr %12, align 8, !dbg !2556, !tbaa !1230
  %193 = fmul double %191, %192, !dbg !2557
  %194 = tail call double @sqrt(double noundef %193) #19, !dbg !2558
  %195 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.78, double noundef %194), !dbg !2559
  %196 = load volatile double, ptr %11, align 8, !dbg !2560, !tbaa !1230
  %197 = load volatile double, ptr %12, align 8, !dbg !2561, !tbaa !1230
  %198 = tail call double @llvm.minnum.f64(double %196, double %197), !dbg !2562
  %199 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.79, double noundef %198), !dbg !2563
  %200 = load volatile double, ptr %11, align 8, !dbg !2564, !tbaa !1230
  %201 = load volatile double, ptr %12, align 8, !dbg !2565, !tbaa !1230
  %202 = tail call double @llvm.maxnum.f64(double %200, double %201), !dbg !2566
  %203 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.80, double noundef %202), !dbg !2567
  %204 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.415), !dbg !2568
  %205 = load volatile double, ptr %11, align 8, !dbg !2569, !tbaa !1230
  %206 = fptosi double %205 to i32, !dbg !2570
    #dbg_value(i32 %206, !2334, !DIExpression(), !2339)
  %207 = load volatile i64, ptr %6, align 8, !dbg !2571, !tbaa !1220
  %208 = sitofp i64 %207 to float, !dbg !2572
    #dbg_value(float %208, !2335, !DIExpression(), !2339)
    #dbg_value(i16 -7616, !2336, !DIExpression(), !2339)
    #dbg_value(i32 -5, !2337, !DIExpression(), !2339)
  %209 = load volatile double, ptr %11, align 8, !dbg !2573, !tbaa !1230
  %210 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.82, double noundef %209, i32 noundef %206), !dbg !2574
  %211 = load volatile i64, ptr %6, align 8, !dbg !2575, !tbaa !1220
  %212 = fpext float %208 to double, !dbg !2576
  %213 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.83, i64 noundef %211, double noundef %212), !dbg !2577
  %214 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.84, i32 noundef -7616), !dbg !2578
  %215 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.85, i32 noundef -5), !dbg !2579
  %216 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.416), !dbg !2580
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %12), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %11), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %10), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %9), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %8), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %7), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %6), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %5), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %4), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %3), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %2), !dbg !2581
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1), !dbg !2581
  ret void, !dbg !2581
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fabs.f32(float) #11

; Function Attrs: mustprogress nofree nounwind willreturn memory(write)
declare !dbg !2582 dso_local float @sqrtf(float noundef) local_unnamed_addr #12

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.minnum.f32(float, float) #11

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.maxnum.f32(float, float) #11

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fabs.f64(double) #11

; Function Attrs: mustprogress nofree nounwind willreturn memory(write)
declare !dbg !2586 dso_local double @sqrt(double noundef) local_unnamed_addr #12

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.minnum.f64(double, double) #11

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.maxnum.f64(double, double) #11

; Function Attrs: noinline nounwind
define dso_local void @MarchallingTest() local_unnamed_addr #1 !dbg !2589 {
  %1 = alloca i32, align 4, !DIAssignID !2592
    #dbg_assign(i1 undef, !2591, !DIExpression(), !2592, ptr %1, !DIExpression(), !2593)
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.437), !dbg !2594
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1) #19, !dbg !2595
  store i32 0, ptr %1, align 4, !dbg !2596, !tbaa !1170, !DIAssignID !2597
    #dbg_assign(i32 0, !2591, !DIExpression(), !2597, ptr %1, !DIExpression(), !2593)
  %3 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.124, i32 noundef 0, ptr noundef nonnull %1), !dbg !2598
  call void @set_magic_number(ptr noundef nonnull %1) #19, !dbg !2599
  %4 = load i32, ptr %1, align 4, !dbg !2600, !tbaa !1170
  %5 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.125, i32 noundef %4), !dbg !2601
  %6 = load i32, ptr %1, align 4, !dbg !2602, !tbaa !1170
  %7 = icmp eq i32 %6, 42, !dbg !2604
  %8 = select i1 %7, ptr @str.439, ptr @str.438
  %9 = call i32 @puts(ptr nonnull dereferenceable(1) %8), !dbg !2605
  %10 = call i32 @puts(ptr nonnull dereferenceable(1) @str.440), !dbg !2606
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1) #19, !dbg !2607
  ret void, !dbg !2607
}

declare !dbg !2608 dso_local void @set_magic_number(ptr noundef) local_unnamed_addr #8

; Function Attrs: nofree noinline nounwind
define dso_local void @test_select_opcodes() local_unnamed_addr #0 !dbg !2611 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.441), !dbg !2619
    #dbg_value(i32 1, !2613, !DIExpression(), !2620)
    #dbg_value(i32 100, !2614, !DIExpression(), !2620)
  %2 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.130, i32 noundef 100), !dbg !2621
    #dbg_value(i64 9876543210, !2615, !DIExpression(), !2620)
  %3 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.131, i64 noundef 9876543210), !dbg !2622
    #dbg_value(float 0x40123D70A0000000, !2616, !DIExpression(), !2620)
  %4 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.132, double noundef 0x40123D70A0000000), !dbg !2623
    #dbg_value(double 2.710000e+00, !2617, !DIExpression(), !2620)
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.133, double noundef 2.710000e+00), !dbg !2624
    #dbg_value(ptr inttoptr (i32 43690 to ptr), !2618, !DIExpression(), !2620)
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.134, ptr noundef nonnull inttoptr (i32 43690 to ptr)), !dbg !2625
  %7 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.442), !dbg !2626
  ret void, !dbg !2627
}

; Function Attrs: nofree noinline nounwind
define dso_local void @test_pointer_ops() local_unnamed_addr #0 !dbg !2628 {
  %1 = alloca i32, align 4, !DIAssignID !2634
    #dbg_assign(i1 undef, !2630, !DIExpression(), !2634, ptr %1, !DIExpression(), !2635)
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.443), !dbg !2636
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1) #19, !dbg !2637
  store i32 123, ptr %1, align 4, !dbg !2638, !tbaa !1170, !DIAssignID !2639
    #dbg_assign(i32 123, !2630, !DIExpression(), !2639, ptr %1, !DIExpression(), !2635)
    #dbg_value(ptr %1, !2631, !DIExpression(), !2635)
  %3 = ptrtoint ptr %1 to i32, !dbg !2640
    #dbg_value(i32 %3, !2632, !DIExpression(), !2635)
  %4 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.137, i32 noundef %3), !dbg !2641
    #dbg_value(ptr %1, !2633, !DIExpression(), !2635)
  %5 = load i32, ptr %1, align 4, !dbg !2642, !tbaa !1170
  %6 = icmp eq i32 %5, 123, !dbg !2644
  br i1 %6, label %7, label %9, !dbg !2645

7:                                                ; preds = %0
  %8 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.138, i32 noundef 123), !dbg !2646
  br label %11, !dbg !2648

9:                                                ; preds = %0
  %10 = call i32 @puts(ptr nonnull dereferenceable(1) @str.444), !dbg !2649
  br label %11

11:                                               ; preds = %9, %7
  %12 = call i32 @puts(ptr nonnull dereferenceable(1) @str.445), !dbg !2651
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1) #19, !dbg !2652
  ret void, !dbg !2652
}

; Function Attrs: noinline nounwind
define dso_local void @test_atomics() local_unnamed_addr #1 !dbg !2653 {
  %1 = alloca i32, align 4, !DIAssignID !2662
    #dbg_assign(i1 undef, !2655, !DIExpression(), !2662, ptr %1, !DIExpression(), !2663)
  %2 = alloca i64, align 8, !DIAssignID !2664
    #dbg_assign(i1 undef, !2659, !DIExpression(), !2664, ptr %2, !DIExpression(), !2663)
  %3 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.446), !dbg !2665
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.447), !dbg !2666
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %1) #19, !dbg !2667
  store volatile i32 10, ptr %1, align 4, !dbg !2668, !tbaa !1170, !DIAssignID !2669
    #dbg_assign(i32 10, !2655, !DIExpression(), !2669, ptr %1, !DIExpression(), !2663)
  %5 = load volatile i32, ptr %1, align 4, !dbg !2670, !tbaa !1170
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.143, i32 noundef %5), !dbg !2671
  %7 = atomicrmw volatile add ptr %1, i32 5 seq_cst, align 4, !dbg !2672
    #dbg_value(i32 %7, !2656, !DIExpression(), !2663)
  %8 = load volatile i32, ptr %1, align 4, !dbg !2673, !tbaa !1170
  %9 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.144, i32 noundef %7, i32 noundef %8), !dbg !2674
  %10 = atomicrmw volatile xchg ptr %1, i32 42 seq_cst, align 4, !dbg !2675
    #dbg_value(i32 %10, !2657, !DIExpression(), !2663)
  %11 = load volatile i32, ptr %1, align 4, !dbg !2676, !tbaa !1170
  %12 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.145, i32 noundef %10, i32 noundef %11), !dbg !2677
    #dbg_value(i32 42, !2658, !DIExpression(), !2663)
  %13 = cmpxchg volatile ptr %1, i32 42, i32 100 seq_cst seq_cst, align 4, !dbg !2678
  %14 = load volatile i32, ptr %1, align 4, !dbg !2679, !tbaa !1170
  %15 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.146, i32 noundef %14), !dbg !2680
    #dbg_value(i32 99, !2658, !DIExpression(), !2663)
  %16 = cmpxchg volatile ptr %1, i32 99, i32 200 seq_cst seq_cst, align 4, !dbg !2681
  %17 = extractvalue { i32, i1 } %16, 0, !dbg !2681
    #dbg_value(i32 %17, !2658, !DIExpression(), !2663)
  %18 = load volatile i32, ptr %1, align 4, !dbg !2682, !tbaa !1170
  %19 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.147, i32 noundef %17, i32 noundef %18), !dbg !2683
  %20 = call i32 @puts(ptr nonnull dereferenceable(1) @str.448), !dbg !2684
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %2) #19, !dbg !2685
  store volatile i64 1000, ptr %2, align 8, !dbg !2686, !tbaa !1220, !DIAssignID !2687
    #dbg_assign(i64 1000, !2659, !DIExpression(), !2687, ptr %2, !DIExpression(), !2663)
  %21 = load volatile i64, ptr %2, align 8, !dbg !2688, !tbaa !1220
  %22 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.149, i64 noundef %21), !dbg !2689
  %23 = call i64 @__atomic_fetch_sub_8(ptr noundef nonnull %2, i64 noundef 100, i32 noundef 5), !dbg !2690
    #dbg_value(i64 %23, !2660, !DIExpression(), !2663)
  %24 = load volatile i64, ptr %2, align 8, !dbg !2691, !tbaa !1220
  %25 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.150, i64 noundef %23, i64 noundef %24), !dbg !2692
  call void @__atomic_store_8(ptr noundef nonnull %2, i64 noundef 5555, i32 noundef 5), !dbg !2693
  %26 = call i64 @__atomic_load_8(ptr noundef nonnull %2, i32 noundef 5), !dbg !2694
    #dbg_value(i64 %26, !2661, !DIExpression(), !2663)
  %27 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.151, i64 noundef %26), !dbg !2695
  %28 = call i32 @puts(ptr nonnull dereferenceable(1) @str.449), !dbg !2696
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %2) #19, !dbg !2697
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %1) #19, !dbg !2697
  ret void, !dbg !2697
}

; Function Attrs: mustprogress nounwind willreturn
declare dso_local i64 @__atomic_fetch_sub_8(ptr, i64, i32) local_unnamed_addr #13

; Function Attrs: mustprogress nounwind willreturn
declare dso_local void @__atomic_store_8(ptr, i64, i32) local_unnamed_addr #13

; Function Attrs: mustprogress nounwind willreturn
declare dso_local i64 @__atomic_load_8(ptr, i32) local_unnamed_addr #13

; Function Attrs: noinline nounwind
define dso_local void @test_heap_and_memory_operations() local_unnamed_addr #1 !dbg !2698 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.450), !dbg !2712
  %2 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.451), !dbg !2713
  %3 = tail call noalias dereferenceable_or_null(128) ptr @malloc(i32 noundef 128) #20, !dbg !2714
    #dbg_value(ptr %3, !2700, !DIExpression(), !2715)
  %4 = icmp eq ptr %3, null, !dbg !2716
  br i1 %4, label %27, label %5, !dbg !2718

5:                                                ; preds = %0
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.156, ptr noundef nonnull %3), !dbg !2719
  tail call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 1 dereferenceable(12) %3, ptr noundef nonnull align 1 dereferenceable(12) @.str.157, i32 12, i1 false), !dbg !2721
  %7 = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %3, ptr noundef nonnull dereferenceable(12) @.str.157), !dbg !2722
  %8 = icmp eq i32 %7, 0, !dbg !2724
  %9 = select i1 %8, ptr @str.453, ptr @str.452
  %10 = tail call i32 @puts(ptr nonnull dereferenceable(1) %9), !dbg !2725
  %11 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.455), !dbg !2726
    #dbg_value(i32 58, !2701, !DIExpression(), !2727)
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.161, i32 noundef 58), !dbg !2728
  tail call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 1 dereferenceable(58) %3, ptr noundef nonnull align 1 dereferenceable(58) @STATIC_DATA_FOR_INIT, i32 58, i1 false), !dbg !2729
  %13 = tail call i32 @memcmp(ptr noundef nonnull dereferenceable(58) %3, ptr noundef nonnull dereferenceable(58) @STATIC_DATA_FOR_INIT, i32 noundef 58), !dbg !2730
  %14 = icmp eq i32 %13, 0, !dbg !2732
  %15 = select i1 %14, ptr @str.457, ptr @str.456
  %16 = tail call i32 @puts(ptr nonnull dereferenceable(1) %15), !dbg !2733
  %17 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.458), !dbg !2734
  %18 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.459), !dbg !2735
  tail call void @llvm.memset.p0.i32(ptr noundef nonnull align 1 dereferenceable(32) %3, i8 88, i32 32, i1 false), !dbg !2736
    #dbg_value(i8 1, !2704, !DIExpression(), !2737)
    #dbg_value(i32 0, !2707, !DIExpression(), !2738)
  br label %22, !dbg !2739

19:                                               ; preds = %22
  %20 = add nuw nsw i32 %23, 1, !dbg !2740
    #dbg_value(i32 %20, !2707, !DIExpression(), !2738)
    #dbg_value(i32 %20, !2707, !DIExpression(), !2738)
  %21 = icmp eq i32 %20, 32, !dbg !2742
  br i1 %21, label %30, label %22, !dbg !2739, !llvm.loop !2743

22:                                               ; preds = %5, %19
  %23 = phi i32 [ 0, %5 ], [ %20, %19 ]
    #dbg_value(i32 %23, !2707, !DIExpression(), !2738)
  %24 = getelementptr inbounds i8, ptr %3, i32 %23, !dbg !2745
  %25 = load i8, ptr %24, align 1, !dbg !2745, !tbaa !1235
  %26 = icmp eq i8 %25, 88, !dbg !2748
    #dbg_value(i32 %23, !2707, !DIExpression(DW_OP_plus_uconst, 1, DW_OP_stack_value), !2738)
  br i1 %26, label %19, label %30, !dbg !2749

27:                                               ; preds = %0
  %28 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.454), !dbg !2725
  %29 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.455), !dbg !2726
  br label %30, !dbg !2750

30:                                               ; preds = %22, %19, %27
  %31 = phi ptr [ @str.458, %27 ], [ @str.460, %22 ], [ @str.461, %19 ]
  %32 = tail call i32 @puts(ptr nonnull dereferenceable(1) %31), !dbg !2715
  %33 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.462), !dbg !2751
  br i1 %4, label %42, label %34, !dbg !2752

34:                                               ; preds = %30
  tail call void @free(ptr noundef %3), !dbg !2753
  %35 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.463), !dbg !2754
  %36 = tail call noalias dereferenceable_or_null(120) ptr @malloc(i32 noundef 120) #20, !dbg !2755
    #dbg_value(ptr %36, !2709, !DIExpression(), !2756)
  %37 = icmp eq ptr %36, null, !dbg !2757
  br i1 %37, label %40, label %38, !dbg !2759

38:                                               ; preds = %34
  %39 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.170, ptr noundef nonnull %36), !dbg !2760
  tail call void @free(ptr noundef %36), !dbg !2762
  br label %42, !dbg !2763

40:                                               ; preds = %34
  %41 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.464), !dbg !2764
  br label %42

42:                                               ; preds = %38, %40, %30
  %43 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.465), !dbg !2766
  ret void, !dbg !2767
}

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite)
declare !dbg !2768 dso_local noalias noundef ptr @malloc(i32 noundef) local_unnamed_addr #14

; Function Attrs: mustprogress nofree nounwind willreturn memory(argmem: read)
declare !dbg !2774 dso_local i32 @strcmp(ptr nocapture noundef, ptr nocapture noundef) local_unnamed_addr #15

; Function Attrs: mustprogress nofree nounwind willreturn memory(argmem: read)
declare !dbg !2778 dso_local i32 @memcmp(ptr nocapture noundef, ptr nocapture noundef, i32 noundef) local_unnamed_addr #15

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i32(ptr nocapture writeonly, i8, i32, i1 immarg) #16

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare !dbg !2783 dso_local void @free(ptr allocptr nocapture noundef) local_unnamed_addr #17

; Function Attrs: nounwind
define dso_local void @app_main() local_unnamed_addr #5 !dbg !2784 {
  %1 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.600), !dbg !2785
  tail call void @test_call_indirect_ptr_unsafe(), !dbg !2786
  tail call void @test_table_operations(), !dbg !2787
  tail call void @test_call_indirect_with_table(), !dbg !2788
  tail call void @test_break_and_continue(), !dbg !2789
  tail call void @test_while_and_do_while_loops(), !dbg !2790
  tail call void @test_complex_data_types(), !dbg !2791
  tail call void @test_math_operations32(), !dbg !2792
  tail call void @test_math_operations(), !dbg !2793
  tail call void @test_i64_imm8_ops(), !dbg !2794
  tail call void @test_heap_and_memory_operations(), !dbg !2795
  tail call void @test_atomics(), !dbg !2796
  tail call void @test_select_opcodes(), !dbg !2797
  tail call void @test_pointer_ops(), !dbg !2798
  tail call void @MarchallingTest(), !dbg !2799
  tail call void @test_globals(), !dbg !2800
  tail call void @test_call_indirect(), !dbg !2801
  tail call void @test_call_indirect_non_optimizable(), !dbg !2802
  tail call void @test_memory_operations(), !dbg !2803
  tail call void @test_control_flow(), !dbg !2804
  tail call void @test_first(), !dbg !2805
  tail call void @test_second(), !dbg !2806
  ret void, !dbg !2807
}

; Function Attrs: nofree nounwind
define dso_local void @test(i32 noundef %0, double noundef %1, ptr noundef %2) local_unnamed_addr #18 !dbg !2808 {
    #dbg_value(i32 %0, !2812, !DIExpression(), !2815)
    #dbg_value(double %1, !2813, !DIExpression(), !2815)
    #dbg_value(ptr %2, !2814, !DIExpression(), !2815)
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.1), !dbg !2816
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.1.2, i32 noundef %0), !dbg !2817
  %6 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.2.3, double noundef %1), !dbg !2818
  %7 = icmp eq ptr %2, null, !dbg !2819
  br i1 %7, label %10, label %8, !dbg !2821

8:                                                ; preds = %3
  %9 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.3.4, ptr noundef nonnull %2), !dbg !2822
  br label %12, !dbg !2824

10:                                               ; preds = %3
  %11 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.6), !dbg !2825
  br label %12

12:                                               ; preds = %10, %8
  %13 = tail call i32 @puts(ptr nonnull dereferenceable(1) @str.7), !dbg !2827
  ret void, !dbg !2828
}

attributes #0 = { nofree noinline nounwind "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #1 = { noinline nounwind "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #2 = { noinline noreturn nounwind "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #3 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(argmem: readwrite) "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #4 = { noinline nounwind optnone "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #5 = { nounwind "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #6 = { nofree nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #7 = { nofree nounwind }
attributes #8 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #9 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #10 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #11 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #12 = { mustprogress nofree nounwind willreturn memory(write) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #13 = { mustprogress nounwind willreturn }
attributes #14 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #15 = { mustprogress nofree nounwind willreturn memory(argmem: read) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #16 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #17 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #18 = { nofree nounwind "no-jump-tables"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv32" "target-features"="+32bit,+a,+c,+m,+relax,+zicsr,+zifencei,-d,-e,-experimental-zacas,-experimental-zcmop,-experimental-zfbfmin,-experimental-zicfilp,-experimental-zicfiss,-experimental-zimop,-experimental-ztso,-experimental-zvfbfmin,-experimental-zvfbfwma,-f,-h,-smaia,-smepmp,-ssaia,-svinval,-svnapot,-svpbmt,-v,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xesppie,-xsfvcp,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xventanacondops,-za128rs,-za64rs,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zca,-zcb,-zcd,-zce,-zcf,-zcmp,-zcmt,-zdinx,-zfa,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccif,-zicclsm,-ziccrse,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-zmmul,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #19 = { nounwind }
attributes #20 = { allocsize(0) }

!llvm.dbg.cu = !{!2, !1124}
!llvm.ident = !{!1133, !1133}
!llvm.module.flags = !{!1134, !1135, !1136, !1137, !1138, !1139}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "g_my_global_int", scope: !2, file: !6, line: 98, type: !25, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "clang version 18.1.2 (https://github.com/espressif/llvm-project.git esp-18.1.2_20240912)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, retainedTypes: !13, globals: !65, splitDebugInlining: false, nameTableKind: None)
!3 = !DIFile(filename: "c:\\andrey\\projects\\vs_code\\espb prodaction\\esp32_prj_to_llvm\\main\\ir_gen_temp/all_tests.c", directory: "C:/Andrey/Projects/VS_Code/ESPB PRODACTION/ESP32_PRJ_TO_LLVM")
!4 = !{!5}
!5 = !DICompositeType(tag: DW_TAG_enumeration_type, file: !6, line: 845, baseType: !7, size: 32, elements: !8)
!6 = !DIFile(filename: "c:\\andrey\\projects\\vs_code\\espb prodaction\\esp32_prj_to_llvm\\main\\ir_gen_temp\\all_tests.c", directory: "")
!7 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!8 = !{!9, !10, !11, !12}
!9 = !DIEnumerator(name: "STATE_IDLE", value: 0)
!10 = !DIEnumerator(name: "STATE_ACTIVE", value: 1)
!11 = !DIEnumerator(name: "STATE_ERROR", value: 2)
!12 = !DIEnumerator(name: "STATE_SLEEP", value: 3)
!13 = !{!14, !20, !25, !27, !28, !31, !34, !35, !36, !40, !41, !43, !44, !48, !49, !53, !55, !56, !58, !59, !60, !61, !7, !24, !64}
!14 = !DIDerivedType(tag: DW_TAG_typedef, name: "TickType_t", file: !15, line: 110, baseType: !16)
!15 = !DIFile(filename: "Programs/Espressif/frameworks/esp-idf-v5.4/components/freertos/FreeRTOS-Kernel/portable/riscv/include/freertos/portmacro.h", directory: "C:/Andrey")
!16 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !17, line: 48, baseType: !18)
!17 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/sys/_stdint.h", directory: "C:/Andrey")
!18 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !19, line: 79, baseType: !7)
!19 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/machine/_default_types.h", directory: "C:/Andrey")
!20 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !21, size: 32)
!21 = !DISubroutineType(types: !22)
!22 = !{!23, !24, !24}
!23 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !24)
!24 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!25 = !DIDerivedType(tag: DW_TAG_typedef, name: "int32_t", file: !17, line: 44, baseType: !26)
!26 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int32_t", file: !19, line: 77, baseType: !24)
!27 = !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)
!28 = !DIDerivedType(tag: DW_TAG_typedef, name: "int16_t", file: !17, line: 32, baseType: !29)
!29 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int16_t", file: !19, line: 55, baseType: !30)
!30 = !DIBasicType(name: "short", size: 16, encoding: DW_ATE_signed)
!31 = !DIDerivedType(tag: DW_TAG_typedef, name: "int8_t", file: !17, line: 20, baseType: !32)
!32 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int8_t", file: !19, line: 41, baseType: !33)
!33 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!34 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_unsigned_char)
!35 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !25, size: 32)
!36 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !37, size: 32)
!37 = !DIDerivedType(tag: DW_TAG_typedef, name: "int64_t", file: !17, line: 56, baseType: !38)
!38 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int64_t", file: !19, line: 103, baseType: !39)
!39 = !DIBasicType(name: "long long", size: 64, encoding: DW_ATE_signed)
!40 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !27, size: 32)
!41 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !42, size: 32)
!42 = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
!43 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !31, size: 32)
!44 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !45, size: 32)
!45 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint8_t", file: !17, line: 24, baseType: !46)
!46 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint8_t", file: !19, line: 43, baseType: !47)
!47 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!48 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !28, size: 32)
!49 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !50, size: 32)
!50 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint16_t", file: !17, line: 36, baseType: !51)
!51 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint16_t", file: !19, line: 57, baseType: !52)
!52 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!53 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !54, size: 32)
!54 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !55)
!55 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 32)
!56 = !DIDerivedType(tag: DW_TAG_typedef, name: "uintptr_t", file: !17, line: 82, baseType: !57)
!57 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uintptr_t", file: !19, line: 232, baseType: !7)
!58 = !DIBasicType(name: "unsigned long", size: 32, encoding: DW_ATE_unsigned)
!59 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !24, size: 32)
!60 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !34, size: 32)
!61 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !62, size: 32)
!62 = !DISubroutineType(types: !63)
!63 = !{!24, !24, !24}
!64 = !DIDerivedType(tag: DW_TAG_typedef, name: "BaseType_t", file: !15, line: 103, baseType: !24)
!65 = !{!0, !66, !68, !71, !76, !81, !86, !88, !93, !95, !100, !105, !107, !112, !117, !122, !127, !132, !137, !139, !141, !146, !151, !156, !158, !163, !168, !173, !175, !180, !182, !184, !189, !191, !196, !198, !200, !205, !207, !209, !211, !213, !218, !220, !222, !224, !226, !228, !230, !232, !237, !242, !244, !246, !251, !253, !255, !257, !259, !261, !263, !265, !267, !269, !271, !273, !278, !280, !282, !284, !286, !288, !290, !292, !294, !296, !301, !303, !305, !307, !309, !311, !313, !315, !317, !322, !324, !326, !328, !333, !338, !340, !345, !350, !352, !354, !356, !358, !360, !362, !364, !366, !371, !373, !375, !377, !379, !381, !383, !388, !390, !392, !394, !396, !398, !400, !402, !404, !406, !408, !410, !412, !414, !419, !421, !426, !428, !430, !435, !437, !439, !444, !446, !448, !450, !452, !454, !456, !458, !460, !462, !464, !466, !468, !470, !472, !474, !476, !481, !483, !485, !490, !492, !494, !496, !498, !500, !502, !507, !512, !517, !519, !524, !529, !531, !533, !535, !540, !542, !547, !549, !551, !553, !555, !560, !562, !567, !572, !577, !579, !581, !583, !585, !587, !589, !591, !596, !601, !606, !608, !610, !612, !614, !616, !618, !620, !622, !624, !629, !631, !633, !635, !637, !639, !641, !643, !645, !650, !652, !654, !656, !661, !663, !668, !670, !672, !674, !676, !678, !680, !682, !684, !686, !688, !690, !692, !694, !696, !701, !703, !708, !710, !712, !714, !716, !718, !720, !722, !724, !726, !731, !733, !735, !737, !742, !744, !746, !748, !750, !752, !757, !762, !764, !769, !771, !776, !778, !780, !782, !784, !786, !788, !790, !792, !794, !796, !798, !800, !802, !804, !806, !808, !813, !815, !820, !822, !824, !826, !828, !830, !832, !834, !836, !838, !840, !842, !844, !846, !851, !853, !855, !857, !859, !861, !863, !865, !867, !869, !871, !873, !875, !877, !879, !881, !883, !885, !887, !889, !891, !893, !895, !897, !902, !904, !906, !908, !910, !912, !914, !916, !918, !920, !922, !924, !926, !931, !933, !935, !937, !939, !941, !943, !945, !947, !949, !951, !953, !955, !957, !959, !961, !963, !965, !967, !969, !971, !973, !975, !977, !979, !981, !983, !985, !987, !989, !991, !993, !995, !997, !999, !1001, !1003, !1008, !1010, !1012, !1014, !1016, !1018, !1020, !1022, !1024, !1026, !1028, !1030, !1032, !1034, !1036, !1038, !1040, !1042, !1044, !1046, !1048, !1050, !1052, !1054, !1056, !1058, !1060, !1062, !1064, !1066, !1068, !1070, !1072, !1074, !1076, !1078, !1080, !1082, !1084, !1086, !1088, !1090, !1093, !1099, !1101, !1103, !1115}
!66 = !DIGlobalVariableExpression(var: !67, expr: !DIExpression())
!67 = distinct !DIGlobalVariable(name: "g_my_global_long", scope: !2, file: !6, line: 99, type: !37, isLocal: false, isDefinition: true)
!68 = !DIGlobalVariableExpression(var: !69, expr: !DIExpression())
!69 = distinct !DIGlobalVariable(name: "g_my_global_bool", scope: !2, file: !6, line: 100, type: !70, isLocal: false, isDefinition: true)
!70 = !DIBasicType(name: "_Bool", size: 8, encoding: DW_ATE_boolean)
!71 = !DIGlobalVariableExpression(var: !72, expr: !DIExpression())
!72 = distinct !DIGlobalVariable(scope: null, file: !6, line: 131, type: !73, isLocal: true, isDefinition: true)
!73 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 288, elements: !74)
!74 = !{!75}
!75 = !DISubrange(count: 36)
!76 = !DIGlobalVariableExpression(var: !77, expr: !DIExpression())
!77 = distinct !DIGlobalVariable(scope: null, file: !6, line: 133, type: !78, isLocal: true, isDefinition: true)
!78 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 104, elements: !79)
!79 = !{!80}
!80 = !DISubrange(count: 13)
!81 = !DIGlobalVariableExpression(var: !82, expr: !DIExpression())
!82 = distinct !DIGlobalVariable(scope: null, file: !6, line: 147, type: !83, isLocal: true, isDefinition: true)
!83 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 248, elements: !84)
!84 = !{!85}
!85 = !DISubrange(count: 31)
!86 = !DIGlobalVariableExpression(var: !87, expr: !DIExpression())
!87 = distinct !DIGlobalVariable(scope: null, file: !6, line: 154, type: !83, isLocal: true, isDefinition: true)
!88 = !DIGlobalVariableExpression(var: !89, expr: !DIExpression())
!89 = distinct !DIGlobalVariable(scope: null, file: !6, line: 162, type: !90, isLocal: true, isDefinition: true)
!90 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 432, elements: !91)
!91 = !{!92}
!92 = !DISubrange(count: 54)
!93 = !DIGlobalVariableExpression(var: !94, expr: !DIExpression())
!94 = distinct !DIGlobalVariable(scope: null, file: !6, line: 170, type: !83, isLocal: true, isDefinition: true)
!95 = !DIGlobalVariableExpression(var: !96, expr: !DIExpression())
!96 = distinct !DIGlobalVariable(scope: null, file: !6, line: 175, type: !97, isLocal: true, isDefinition: true)
!97 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 192, elements: !98)
!98 = !{!99}
!99 = !DISubrange(count: 24)
!100 = !DIGlobalVariableExpression(var: !101, expr: !DIExpression())
!101 = distinct !DIGlobalVariable(scope: null, file: !6, line: 179, type: !102, isLocal: true, isDefinition: true)
!102 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 256, elements: !103)
!103 = !{!104}
!104 = !DISubrange(count: 32)
!105 = !DIGlobalVariableExpression(var: !106, expr: !DIExpression())
!106 = distinct !DIGlobalVariable(scope: null, file: !6, line: 182, type: !102, isLocal: true, isDefinition: true)
!107 = !DIGlobalVariableExpression(var: !108, expr: !DIExpression())
!108 = distinct !DIGlobalVariable(scope: null, file: !6, line: 187, type: !109, isLocal: true, isDefinition: true)
!109 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 296, elements: !110)
!110 = !{!111}
!111 = !DISubrange(count: 37)
!112 = !DIGlobalVariableExpression(var: !113, expr: !DIExpression())
!113 = distinct !DIGlobalVariable(scope: null, file: !6, line: 189, type: !114, isLocal: true, isDefinition: true)
!114 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 320, elements: !115)
!115 = !{!116}
!116 = !DISubrange(count: 40)
!117 = !DIGlobalVariableExpression(var: !118, expr: !DIExpression())
!118 = distinct !DIGlobalVariable(scope: null, file: !6, line: 208, type: !119, isLocal: true, isDefinition: true)
!119 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 424, elements: !120)
!120 = !{!121}
!121 = !DISubrange(count: 53)
!122 = !DIGlobalVariableExpression(var: !123, expr: !DIExpression())
!123 = distinct !DIGlobalVariable(scope: null, file: !6, line: 217, type: !124, isLocal: true, isDefinition: true)
!124 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 360, elements: !125)
!125 = !{!126}
!126 = !DISubrange(count: 45)
!127 = !DIGlobalVariableExpression(var: !128, expr: !DIExpression())
!128 = distinct !DIGlobalVariable(scope: null, file: !6, line: 224, type: !129, isLocal: true, isDefinition: true)
!129 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 376, elements: !130)
!130 = !{!131}
!131 = !DISubrange(count: 47)
!132 = !DIGlobalVariableExpression(var: !133, expr: !DIExpression())
!133 = distinct !DIGlobalVariable(scope: null, file: !6, line: 228, type: !134, isLocal: true, isDefinition: true)
!134 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 272, elements: !135)
!135 = !{!136}
!136 = !DISubrange(count: 34)
!137 = !DIGlobalVariableExpression(var: !138, expr: !DIExpression())
!138 = distinct !DIGlobalVariable(scope: null, file: !6, line: 231, type: !134, isLocal: true, isDefinition: true)
!139 = !DIGlobalVariableExpression(var: !140, expr: !DIExpression())
!140 = distinct !DIGlobalVariable(scope: null, file: !6, line: 237, type: !83, isLocal: true, isDefinition: true)
!141 = !DIGlobalVariableExpression(var: !142, expr: !DIExpression())
!142 = distinct !DIGlobalVariable(scope: null, file: !6, line: 239, type: !143, isLocal: true, isDefinition: true)
!143 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 448, elements: !144)
!144 = !{!145}
!145 = !DISubrange(count: 56)
!146 = !DIGlobalVariableExpression(var: !147, expr: !DIExpression())
!147 = distinct !DIGlobalVariable(scope: null, file: !6, line: 250, type: !148, isLocal: true, isDefinition: true)
!148 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 208, elements: !149)
!149 = !{!150}
!150 = !DISubrange(count: 26)
!151 = !DIGlobalVariableExpression(var: !152, expr: !DIExpression())
!152 = distinct !DIGlobalVariable(scope: null, file: !6, line: 251, type: !153, isLocal: true, isDefinition: true)
!153 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 232, elements: !154)
!154 = !{!155}
!155 = !DISubrange(count: 29)
!156 = !DIGlobalVariableExpression(var: !157, expr: !DIExpression())
!157 = distinct !DIGlobalVariable(scope: null, file: !6, line: 252, type: !102, isLocal: true, isDefinition: true)
!158 = !DIGlobalVariableExpression(var: !159, expr: !DIExpression())
!159 = distinct !DIGlobalVariable(scope: null, file: !6, line: 253, type: !160, isLocal: true, isDefinition: true)
!160 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 240, elements: !161)
!161 = !{!162}
!162 = !DISubrange(count: 30)
!163 = !DIGlobalVariableExpression(var: !164, expr: !DIExpression())
!164 = distinct !DIGlobalVariable(scope: null, file: !6, line: 253, type: !165, isLocal: true, isDefinition: true)
!165 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 40, elements: !166)
!166 = !{!167}
!167 = !DISubrange(count: 5)
!168 = !DIGlobalVariableExpression(var: !169, expr: !DIExpression())
!169 = distinct !DIGlobalVariable(scope: null, file: !6, line: 253, type: !170, isLocal: true, isDefinition: true)
!170 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 48, elements: !171)
!171 = !{!172}
!172 = !DISubrange(count: 6)
!173 = !DIGlobalVariableExpression(var: !174, expr: !DIExpression())
!174 = distinct !DIGlobalVariable(scope: null, file: !6, line: 259, type: !83, isLocal: true, isDefinition: true)
!175 = !DIGlobalVariableExpression(var: !176, expr: !DIExpression())
!176 = distinct !DIGlobalVariable(scope: null, file: !6, line: 260, type: !177, isLocal: true, isDefinition: true)
!177 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 264, elements: !178)
!178 = !{!179}
!179 = !DISubrange(count: 33)
!180 = !DIGlobalVariableExpression(var: !181, expr: !DIExpression())
!181 = distinct !DIGlobalVariable(scope: null, file: !6, line: 261, type: !83, isLocal: true, isDefinition: true)
!182 = !DIGlobalVariableExpression(var: !183, expr: !DIExpression())
!183 = distinct !DIGlobalVariable(scope: null, file: !6, line: 262, type: !177, isLocal: true, isDefinition: true)
!184 = !DIGlobalVariableExpression(var: !185, expr: !DIExpression())
!185 = distinct !DIGlobalVariable(scope: null, file: !6, line: 274, type: !186, isLocal: true, isDefinition: true)
!186 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 200, elements: !187)
!187 = !{!188}
!188 = !DISubrange(count: 25)
!189 = !DIGlobalVariableExpression(var: !190, expr: !DIExpression())
!190 = distinct !DIGlobalVariable(scope: null, file: !6, line: 275, type: !114, isLocal: true, isDefinition: true)
!191 = !DIGlobalVariableExpression(var: !192, expr: !DIExpression())
!192 = distinct !DIGlobalVariable(scope: null, file: !6, line: 276, type: !193, isLocal: true, isDefinition: true)
!193 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 72, elements: !194)
!194 = !{!195}
!195 = !DISubrange(count: 9)
!196 = !DIGlobalVariableExpression(var: !197, expr: !DIExpression())
!197 = distinct !DIGlobalVariable(scope: null, file: !6, line: 277, type: !193, isLocal: true, isDefinition: true)
!198 = !DIGlobalVariableExpression(var: !199, expr: !DIExpression())
!199 = distinct !DIGlobalVariable(scope: null, file: !6, line: 278, type: !193, isLocal: true, isDefinition: true)
!200 = !DIGlobalVariableExpression(var: !201, expr: !DIExpression())
!201 = distinct !DIGlobalVariable(scope: null, file: !6, line: 279, type: !202, isLocal: true, isDefinition: true)
!202 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 80, elements: !203)
!203 = !{!204}
!204 = !DISubrange(count: 10)
!205 = !DIGlobalVariableExpression(var: !206, expr: !DIExpression())
!206 = distinct !DIGlobalVariable(scope: null, file: !6, line: 280, type: !202, isLocal: true, isDefinition: true)
!207 = !DIGlobalVariableExpression(var: !208, expr: !DIExpression())
!208 = distinct !DIGlobalVariable(scope: null, file: !6, line: 281, type: !202, isLocal: true, isDefinition: true)
!209 = !DIGlobalVariableExpression(var: !210, expr: !DIExpression())
!210 = distinct !DIGlobalVariable(scope: null, file: !6, line: 282, type: !202, isLocal: true, isDefinition: true)
!211 = !DIGlobalVariableExpression(var: !212, expr: !DIExpression())
!212 = distinct !DIGlobalVariable(scope: null, file: !6, line: 283, type: !193, isLocal: true, isDefinition: true)
!213 = !DIGlobalVariableExpression(var: !214, expr: !DIExpression())
!214 = distinct !DIGlobalVariable(scope: null, file: !6, line: 284, type: !215, isLocal: true, isDefinition: true)
!215 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 64, elements: !216)
!216 = !{!217}
!217 = !DISubrange(count: 8)
!218 = !DIGlobalVariableExpression(var: !219, expr: !DIExpression())
!219 = distinct !DIGlobalVariable(scope: null, file: !6, line: 285, type: !193, isLocal: true, isDefinition: true)
!220 = !DIGlobalVariableExpression(var: !221, expr: !DIExpression())
!221 = distinct !DIGlobalVariable(scope: null, file: !6, line: 286, type: !193, isLocal: true, isDefinition: true)
!222 = !DIGlobalVariableExpression(var: !223, expr: !DIExpression())
!223 = distinct !DIGlobalVariable(scope: null, file: !6, line: 287, type: !202, isLocal: true, isDefinition: true)
!224 = !DIGlobalVariableExpression(var: !225, expr: !DIExpression())
!225 = distinct !DIGlobalVariable(scope: null, file: !6, line: 288, type: !202, isLocal: true, isDefinition: true)
!226 = !DIGlobalVariableExpression(var: !227, expr: !DIExpression())
!227 = distinct !DIGlobalVariable(scope: null, file: !6, line: 289, type: !193, isLocal: true, isDefinition: true)
!228 = !DIGlobalVariableExpression(var: !229, expr: !DIExpression())
!229 = distinct !DIGlobalVariable(scope: null, file: !6, line: 296, type: !134, isLocal: true, isDefinition: true)
!230 = !DIGlobalVariableExpression(var: !231, expr: !DIExpression())
!231 = distinct !DIGlobalVariable(scope: null, file: !6, line: 323, type: !186, isLocal: true, isDefinition: true)
!232 = !DIGlobalVariableExpression(var: !233, expr: !DIExpression())
!233 = distinct !DIGlobalVariable(scope: null, file: !6, line: 329, type: !234, isLocal: true, isDefinition: true)
!234 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 384, elements: !235)
!235 = !{!236}
!236 = !DISubrange(count: 48)
!237 = !DIGlobalVariableExpression(var: !238, expr: !DIExpression())
!238 = distinct !DIGlobalVariable(scope: null, file: !6, line: 330, type: !239, isLocal: true, isDefinition: true)
!239 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 88, elements: !240)
!240 = !{!241}
!241 = !DISubrange(count: 11)
!242 = !DIGlobalVariableExpression(var: !243, expr: !DIExpression())
!243 = distinct !DIGlobalVariable(scope: null, file: !6, line: 331, type: !239, isLocal: true, isDefinition: true)
!244 = !DIGlobalVariableExpression(var: !245, expr: !DIExpression())
!245 = distinct !DIGlobalVariable(scope: null, file: !6, line: 332, type: !239, isLocal: true, isDefinition: true)
!246 = !DIGlobalVariableExpression(var: !247, expr: !DIExpression())
!247 = distinct !DIGlobalVariable(scope: null, file: !6, line: 333, type: !248, isLocal: true, isDefinition: true)
!248 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 96, elements: !249)
!249 = !{!250}
!250 = !DISubrange(count: 12)
!251 = !DIGlobalVariableExpression(var: !252, expr: !DIExpression())
!252 = distinct !DIGlobalVariable(scope: null, file: !6, line: 334, type: !248, isLocal: true, isDefinition: true)
!253 = !DIGlobalVariableExpression(var: !254, expr: !DIExpression())
!254 = distinct !DIGlobalVariable(scope: null, file: !6, line: 335, type: !248, isLocal: true, isDefinition: true)
!255 = !DIGlobalVariableExpression(var: !256, expr: !DIExpression())
!256 = distinct !DIGlobalVariable(scope: null, file: !6, line: 336, type: !248, isLocal: true, isDefinition: true)
!257 = !DIGlobalVariableExpression(var: !258, expr: !DIExpression())
!258 = distinct !DIGlobalVariable(scope: null, file: !6, line: 337, type: !239, isLocal: true, isDefinition: true)
!259 = !DIGlobalVariableExpression(var: !260, expr: !DIExpression())
!260 = distinct !DIGlobalVariable(scope: null, file: !6, line: 338, type: !202, isLocal: true, isDefinition: true)
!261 = !DIGlobalVariableExpression(var: !262, expr: !DIExpression())
!262 = distinct !DIGlobalVariable(scope: null, file: !6, line: 339, type: !239, isLocal: true, isDefinition: true)
!263 = !DIGlobalVariableExpression(var: !264, expr: !DIExpression())
!264 = distinct !DIGlobalVariable(scope: null, file: !6, line: 340, type: !239, isLocal: true, isDefinition: true)
!265 = !DIGlobalVariableExpression(var: !266, expr: !DIExpression())
!266 = distinct !DIGlobalVariable(scope: null, file: !6, line: 341, type: !248, isLocal: true, isDefinition: true)
!267 = !DIGlobalVariableExpression(var: !268, expr: !DIExpression())
!268 = distinct !DIGlobalVariable(scope: null, file: !6, line: 342, type: !248, isLocal: true, isDefinition: true)
!269 = !DIGlobalVariableExpression(var: !270, expr: !DIExpression())
!270 = distinct !DIGlobalVariable(scope: null, file: !6, line: 343, type: !239, isLocal: true, isDefinition: true)
!271 = !DIGlobalVariableExpression(var: !272, expr: !DIExpression())
!272 = distinct !DIGlobalVariable(scope: null, file: !6, line: 346, type: !186, isLocal: true, isDefinition: true)
!273 = !DIGlobalVariableExpression(var: !274, expr: !DIExpression())
!274 = distinct !DIGlobalVariable(scope: null, file: !6, line: 350, type: !275, isLocal: true, isDefinition: true)
!275 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 160, elements: !276)
!276 = !{!277}
!277 = !DISubrange(count: 20)
!278 = !DIGlobalVariableExpression(var: !279, expr: !DIExpression())
!279 = distinct !DIGlobalVariable(scope: null, file: !6, line: 351, type: !193, isLocal: true, isDefinition: true)
!280 = !DIGlobalVariableExpression(var: !281, expr: !DIExpression())
!281 = distinct !DIGlobalVariable(scope: null, file: !6, line: 352, type: !193, isLocal: true, isDefinition: true)
!282 = !DIGlobalVariableExpression(var: !283, expr: !DIExpression())
!283 = distinct !DIGlobalVariable(scope: null, file: !6, line: 353, type: !193, isLocal: true, isDefinition: true)
!284 = !DIGlobalVariableExpression(var: !285, expr: !DIExpression())
!285 = distinct !DIGlobalVariable(scope: null, file: !6, line: 354, type: !193, isLocal: true, isDefinition: true)
!286 = !DIGlobalVariableExpression(var: !287, expr: !DIExpression())
!287 = distinct !DIGlobalVariable(scope: null, file: !6, line: 355, type: !193, isLocal: true, isDefinition: true)
!288 = !DIGlobalVariableExpression(var: !289, expr: !DIExpression())
!289 = distinct !DIGlobalVariable(scope: null, file: !6, line: 356, type: !202, isLocal: true, isDefinition: true)
!290 = !DIGlobalVariableExpression(var: !291, expr: !DIExpression())
!291 = distinct !DIGlobalVariable(scope: null, file: !6, line: 357, type: !193, isLocal: true, isDefinition: true)
!292 = !DIGlobalVariableExpression(var: !293, expr: !DIExpression())
!293 = distinct !DIGlobalVariable(scope: null, file: !6, line: 358, type: !193, isLocal: true, isDefinition: true)
!294 = !DIGlobalVariableExpression(var: !295, expr: !DIExpression())
!295 = distinct !DIGlobalVariable(scope: null, file: !6, line: 361, type: !186, isLocal: true, isDefinition: true)
!296 = !DIGlobalVariableExpression(var: !297, expr: !DIExpression())
!297 = distinct !DIGlobalVariable(scope: null, file: !6, line: 365, type: !298, isLocal: true, isDefinition: true)
!298 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 176, elements: !299)
!299 = !{!300}
!300 = !DISubrange(count: 22)
!301 = !DIGlobalVariableExpression(var: !302, expr: !DIExpression())
!302 = distinct !DIGlobalVariable(scope: null, file: !6, line: 366, type: !202, isLocal: true, isDefinition: true)
!303 = !DIGlobalVariableExpression(var: !304, expr: !DIExpression())
!304 = distinct !DIGlobalVariable(scope: null, file: !6, line: 367, type: !202, isLocal: true, isDefinition: true)
!305 = !DIGlobalVariableExpression(var: !306, expr: !DIExpression())
!306 = distinct !DIGlobalVariable(scope: null, file: !6, line: 368, type: !202, isLocal: true, isDefinition: true)
!307 = !DIGlobalVariableExpression(var: !308, expr: !DIExpression())
!308 = distinct !DIGlobalVariable(scope: null, file: !6, line: 369, type: !202, isLocal: true, isDefinition: true)
!309 = !DIGlobalVariableExpression(var: !310, expr: !DIExpression())
!310 = distinct !DIGlobalVariable(scope: null, file: !6, line: 370, type: !202, isLocal: true, isDefinition: true)
!311 = !DIGlobalVariableExpression(var: !312, expr: !DIExpression())
!312 = distinct !DIGlobalVariable(scope: null, file: !6, line: 371, type: !239, isLocal: true, isDefinition: true)
!313 = !DIGlobalVariableExpression(var: !314, expr: !DIExpression())
!314 = distinct !DIGlobalVariable(scope: null, file: !6, line: 372, type: !202, isLocal: true, isDefinition: true)
!315 = !DIGlobalVariableExpression(var: !316, expr: !DIExpression())
!316 = distinct !DIGlobalVariable(scope: null, file: !6, line: 373, type: !202, isLocal: true, isDefinition: true)
!317 = !DIGlobalVariableExpression(var: !318, expr: !DIExpression())
!318 = distinct !DIGlobalVariable(scope: null, file: !6, line: 376, type: !319, isLocal: true, isDefinition: true)
!319 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 216, elements: !320)
!320 = !{!321}
!321 = !DISubrange(count: 27)
!322 = !DIGlobalVariableExpression(var: !323, expr: !DIExpression())
!323 = distinct !DIGlobalVariable(scope: null, file: !6, line: 382, type: !97, isLocal: true, isDefinition: true)
!324 = !DIGlobalVariableExpression(var: !325, expr: !DIExpression())
!325 = distinct !DIGlobalVariable(scope: null, file: !6, line: 383, type: !186, isLocal: true, isDefinition: true)
!326 = !DIGlobalVariableExpression(var: !327, expr: !DIExpression())
!327 = distinct !DIGlobalVariable(scope: null, file: !6, line: 384, type: !153, isLocal: true, isDefinition: true)
!328 = !DIGlobalVariableExpression(var: !329, expr: !DIExpression())
!329 = distinct !DIGlobalVariable(scope: null, file: !6, line: 385, type: !330, isLocal: true, isDefinition: true)
!330 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 184, elements: !331)
!331 = !{!332}
!332 = !DISubrange(count: 23)
!333 = !DIGlobalVariableExpression(var: !334, expr: !DIExpression())
!334 = distinct !DIGlobalVariable(scope: null, file: !6, line: 387, type: !335, isLocal: true, isDefinition: true)
!335 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 328, elements: !336)
!336 = !{!337}
!337 = !DISubrange(count: 41)
!338 = !DIGlobalVariableExpression(var: !339, expr: !DIExpression())
!339 = distinct !DIGlobalVariable(scope: null, file: !6, line: 394, type: !73, isLocal: true, isDefinition: true)
!340 = !DIGlobalVariableExpression(var: !341, expr: !DIExpression())
!341 = distinct !DIGlobalVariable(scope: null, file: !6, line: 406, type: !342, isLocal: true, isDefinition: true)
!342 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 336, elements: !343)
!343 = !{!344}
!344 = !DISubrange(count: 42)
!345 = !DIGlobalVariableExpression(var: !346, expr: !DIExpression())
!346 = distinct !DIGlobalVariable(scope: null, file: !6, line: 412, type: !347, isLocal: true, isDefinition: true)
!347 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 304, elements: !348)
!348 = !{!349}
!349 = !DISubrange(count: 38)
!350 = !DIGlobalVariableExpression(var: !351, expr: !DIExpression())
!351 = distinct !DIGlobalVariable(scope: null, file: !6, line: 418, type: !342, isLocal: true, isDefinition: true)
!352 = !DIGlobalVariableExpression(var: !353, expr: !DIExpression())
!353 = distinct !DIGlobalVariable(scope: null, file: !6, line: 424, type: !347, isLocal: true, isDefinition: true)
!354 = !DIGlobalVariableExpression(var: !355, expr: !DIExpression())
!355 = distinct !DIGlobalVariable(scope: null, file: !6, line: 430, type: !114, isLocal: true, isDefinition: true)
!356 = !DIGlobalVariableExpression(var: !357, expr: !DIExpression())
!357 = distinct !DIGlobalVariable(scope: null, file: !6, line: 439, type: !109, isLocal: true, isDefinition: true)
!358 = !DIGlobalVariableExpression(var: !359, expr: !DIExpression())
!359 = distinct !DIGlobalVariable(scope: null, file: !6, line: 440, type: !109, isLocal: true, isDefinition: true)
!360 = !DIGlobalVariableExpression(var: !361, expr: !DIExpression())
!361 = distinct !DIGlobalVariable(scope: null, file: !6, line: 449, type: !347, isLocal: true, isDefinition: true)
!362 = !DIGlobalVariableExpression(var: !363, expr: !DIExpression())
!363 = distinct !DIGlobalVariable(scope: null, file: !6, line: 450, type: !347, isLocal: true, isDefinition: true)
!364 = !DIGlobalVariableExpression(var: !365, expr: !DIExpression())
!365 = distinct !DIGlobalVariable(scope: null, file: !6, line: 458, type: !347, isLocal: true, isDefinition: true)
!366 = !DIGlobalVariableExpression(var: !367, expr: !DIExpression())
!367 = distinct !DIGlobalVariable(scope: null, file: !6, line: 460, type: !368, isLocal: true, isDefinition: true)
!368 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 344, elements: !369)
!369 = !{!370}
!370 = !DISubrange(count: 43)
!371 = !DIGlobalVariableExpression(var: !372, expr: !DIExpression())
!372 = distinct !DIGlobalVariable(scope: null, file: !6, line: 469, type: !83, isLocal: true, isDefinition: true)
!373 = !DIGlobalVariableExpression(var: !374, expr: !DIExpression())
!374 = distinct !DIGlobalVariable(scope: null, file: !6, line: 472, type: !153, isLocal: true, isDefinition: true)
!375 = !DIGlobalVariableExpression(var: !376, expr: !DIExpression())
!376 = distinct !DIGlobalVariable(scope: null, file: !6, line: 477, type: !153, isLocal: true, isDefinition: true)
!377 = !DIGlobalVariableExpression(var: !378, expr: !DIExpression())
!378 = distinct !DIGlobalVariable(scope: null, file: !6, line: 479, type: !330, isLocal: true, isDefinition: true)
!379 = !DIGlobalVariableExpression(var: !380, expr: !DIExpression())
!380 = distinct !DIGlobalVariable(scope: null, file: !6, line: 483, type: !298, isLocal: true, isDefinition: true)
!381 = !DIGlobalVariableExpression(var: !382, expr: !DIExpression())
!382 = distinct !DIGlobalVariable(scope: null, file: !6, line: 485, type: !160, isLocal: true, isDefinition: true)
!383 = !DIGlobalVariableExpression(var: !384, expr: !DIExpression())
!384 = distinct !DIGlobalVariable(scope: null, file: !6, line: 489, type: !385, isLocal: true, isDefinition: true)
!385 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 168, elements: !386)
!386 = !{!387}
!387 = !DISubrange(count: 21)
!388 = !DIGlobalVariableExpression(var: !389, expr: !DIExpression())
!389 = distinct !DIGlobalVariable(scope: null, file: !6, line: 491, type: !186, isLocal: true, isDefinition: true)
!390 = !DIGlobalVariableExpression(var: !391, expr: !DIExpression())
!391 = distinct !DIGlobalVariable(scope: null, file: !6, line: 497, type: !73, isLocal: true, isDefinition: true)
!392 = !DIGlobalVariableExpression(var: !393, expr: !DIExpression())
!393 = distinct !DIGlobalVariable(scope: null, file: !6, line: 499, type: !186, isLocal: true, isDefinition: true)
!394 = !DIGlobalVariableExpression(var: !395, expr: !DIExpression())
!395 = distinct !DIGlobalVariable(scope: null, file: !6, line: 503, type: !160, isLocal: true, isDefinition: true)
!396 = !DIGlobalVariableExpression(var: !397, expr: !DIExpression())
!397 = distinct !DIGlobalVariable(scope: null, file: !6, line: 507, type: !153, isLocal: true, isDefinition: true)
!398 = !DIGlobalVariableExpression(var: !399, expr: !DIExpression())
!399 = distinct !DIGlobalVariable(scope: null, file: !6, line: 509, type: !153, isLocal: true, isDefinition: true)
!400 = !DIGlobalVariableExpression(var: !401, expr: !DIExpression())
!401 = distinct !DIGlobalVariable(scope: null, file: !6, line: 511, type: !83, isLocal: true, isDefinition: true)
!402 = !DIGlobalVariableExpression(var: !403, expr: !DIExpression())
!403 = distinct !DIGlobalVariable(scope: null, file: !6, line: 513, type: !148, isLocal: true, isDefinition: true)
!404 = !DIGlobalVariableExpression(var: !405, expr: !DIExpression())
!405 = distinct !DIGlobalVariable(scope: null, file: !6, line: 517, type: !177, isLocal: true, isDefinition: true)
!406 = !DIGlobalVariableExpression(var: !407, expr: !DIExpression())
!407 = distinct !DIGlobalVariable(scope: null, file: !6, line: 519, type: !160, isLocal: true, isDefinition: true)
!408 = !DIGlobalVariableExpression(var: !409, expr: !DIExpression())
!409 = distinct !DIGlobalVariable(scope: null, file: !6, line: 522, type: !215, isLocal: true, isDefinition: true)
!410 = !DIGlobalVariableExpression(var: !411, expr: !DIExpression())
!411 = distinct !DIGlobalVariable(scope: null, file: !6, line: 525, type: !215, isLocal: true, isDefinition: true)
!412 = !DIGlobalVariableExpression(var: !413, expr: !DIExpression())
!413 = distinct !DIGlobalVariable(scope: null, file: !6, line: 528, type: !215, isLocal: true, isDefinition: true)
!414 = !DIGlobalVariableExpression(var: !415, expr: !DIExpression())
!415 = distinct !DIGlobalVariable(scope: null, file: !6, line: 531, type: !416, isLocal: true, isDefinition: true)
!416 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 112, elements: !417)
!417 = !{!418}
!418 = !DISubrange(count: 14)
!419 = !DIGlobalVariableExpression(var: !420, expr: !DIExpression())
!420 = distinct !DIGlobalVariable(scope: null, file: !6, line: 537, type: !148, isLocal: true, isDefinition: true)
!421 = !DIGlobalVariableExpression(var: !422, expr: !DIExpression())
!422 = distinct !DIGlobalVariable(scope: null, file: !6, line: 541, type: !423, isLocal: true, isDefinition: true)
!423 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 392, elements: !424)
!424 = !{!425}
!425 = !DISubrange(count: 49)
!426 = !DIGlobalVariableExpression(var: !427, expr: !DIExpression())
!427 = distinct !DIGlobalVariable(scope: null, file: !6, line: 543, type: !347, isLocal: true, isDefinition: true)
!428 = !DIGlobalVariableExpression(var: !429, expr: !DIExpression())
!429 = distinct !DIGlobalVariable(scope: null, file: !6, line: 553, type: !102, isLocal: true, isDefinition: true)
!430 = !DIGlobalVariableExpression(var: !431, expr: !DIExpression())
!431 = distinct !DIGlobalVariable(scope: null, file: !6, line: 557, type: !432, isLocal: true, isDefinition: true)
!432 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 512, elements: !433)
!433 = !{!434}
!434 = !DISubrange(count: 64)
!435 = !DIGlobalVariableExpression(var: !436, expr: !DIExpression())
!436 = distinct !DIGlobalVariable(scope: null, file: !6, line: 568, type: !134, isLocal: true, isDefinition: true)
!437 = !DIGlobalVariableExpression(var: !438, expr: !DIExpression())
!438 = distinct !DIGlobalVariable(scope: null, file: !6, line: 572, type: !129, isLocal: true, isDefinition: true)
!439 = !DIGlobalVariableExpression(var: !440, expr: !DIExpression())
!440 = distinct !DIGlobalVariable(scope: null, file: !6, line: 574, type: !441, isLocal: true, isDefinition: true)
!441 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 560, elements: !442)
!442 = !{!443}
!443 = !DISubrange(count: 70)
!444 = !DIGlobalVariableExpression(var: !445, expr: !DIExpression())
!445 = distinct !DIGlobalVariable(scope: null, file: !6, line: 577, type: !160, isLocal: true, isDefinition: true)
!446 = !DIGlobalVariableExpression(var: !447, expr: !DIExpression())
!447 = distinct !DIGlobalVariable(scope: null, file: !6, line: 584, type: !177, isLocal: true, isDefinition: true)
!448 = !DIGlobalVariableExpression(var: !449, expr: !DIExpression())
!449 = distinct !DIGlobalVariable(scope: null, file: !6, line: 589, type: !330, isLocal: true, isDefinition: true)
!450 = !DIGlobalVariableExpression(var: !451, expr: !DIExpression())
!451 = distinct !DIGlobalVariable(scope: null, file: !6, line: 593, type: !186, isLocal: true, isDefinition: true)
!452 = !DIGlobalVariableExpression(var: !453, expr: !DIExpression())
!453 = distinct !DIGlobalVariable(scope: null, file: !6, line: 598, type: !330, isLocal: true, isDefinition: true)
!454 = !DIGlobalVariableExpression(var: !455, expr: !DIExpression())
!455 = distinct !DIGlobalVariable(scope: null, file: !6, line: 602, type: !97, isLocal: true, isDefinition: true)
!456 = !DIGlobalVariableExpression(var: !457, expr: !DIExpression())
!457 = distinct !DIGlobalVariable(scope: null, file: !6, line: 606, type: !330, isLocal: true, isDefinition: true)
!458 = !DIGlobalVariableExpression(var: !459, expr: !DIExpression())
!459 = distinct !DIGlobalVariable(scope: null, file: !6, line: 608, type: !109, isLocal: true, isDefinition: true)
!460 = !DIGlobalVariableExpression(var: !461, expr: !DIExpression())
!461 = distinct !DIGlobalVariable(scope: null, file: !6, line: 616, type: !134, isLocal: true, isDefinition: true)
!462 = !DIGlobalVariableExpression(var: !463, expr: !DIExpression())
!463 = distinct !DIGlobalVariable(scope: null, file: !6, line: 625, type: !347, isLocal: true, isDefinition: true)
!464 = !DIGlobalVariableExpression(var: !465, expr: !DIExpression())
!465 = distinct !DIGlobalVariable(scope: null, file: !6, line: 632, type: !347, isLocal: true, isDefinition: true)
!466 = !DIGlobalVariableExpression(var: !467, expr: !DIExpression())
!467 = distinct !DIGlobalVariable(scope: null, file: !6, line: 634, type: !186, isLocal: true, isDefinition: true)
!468 = !DIGlobalVariableExpression(var: !469, expr: !DIExpression())
!469 = distinct !DIGlobalVariable(scope: null, file: !6, line: 637, type: !239, isLocal: true, isDefinition: true)
!470 = !DIGlobalVariableExpression(var: !471, expr: !DIExpression())
!471 = distinct !DIGlobalVariable(scope: null, file: !6, line: 646, type: !73, isLocal: true, isDefinition: true)
!472 = !DIGlobalVariableExpression(var: !473, expr: !DIExpression())
!473 = distinct !DIGlobalVariable(scope: null, file: !6, line: 649, type: !298, isLocal: true, isDefinition: true)
!474 = !DIGlobalVariableExpression(var: !475, expr: !DIExpression())
!475 = distinct !DIGlobalVariable(scope: null, file: !6, line: 651, type: !160, isLocal: true, isDefinition: true)
!476 = !DIGlobalVariableExpression(var: !477, expr: !DIExpression())
!477 = distinct !DIGlobalVariable(scope: null, file: !6, line: 655, type: !478, isLocal: true, isDefinition: true)
!478 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 224, elements: !479)
!479 = !{!480}
!480 = !DISubrange(count: 28)
!481 = !DIGlobalVariableExpression(var: !482, expr: !DIExpression())
!482 = distinct !DIGlobalVariable(scope: null, file: !6, line: 659, type: !153, isLocal: true, isDefinition: true)
!483 = !DIGlobalVariableExpression(var: !484, expr: !DIExpression())
!484 = distinct !DIGlobalVariable(scope: null, file: !6, line: 664, type: !129, isLocal: true, isDefinition: true)
!485 = !DIGlobalVariableExpression(var: !486, expr: !DIExpression())
!486 = distinct !DIGlobalVariable(scope: null, file: !6, line: 668, type: !487, isLocal: true, isDefinition: true)
!487 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 568, elements: !488)
!488 = !{!489}
!489 = !DISubrange(count: 71)
!490 = !DIGlobalVariableExpression(var: !491, expr: !DIExpression())
!491 = distinct !DIGlobalVariable(scope: null, file: !6, line: 671, type: !298, isLocal: true, isDefinition: true)
!492 = !DIGlobalVariableExpression(var: !493, expr: !DIExpression())
!493 = distinct !DIGlobalVariable(scope: null, file: !6, line: 673, type: !102, isLocal: true, isDefinition: true)
!494 = !DIGlobalVariableExpression(var: !495, expr: !DIExpression())
!495 = distinct !DIGlobalVariable(scope: null, file: !6, line: 677, type: !73, isLocal: true, isDefinition: true)
!496 = !DIGlobalVariableExpression(var: !497, expr: !DIExpression())
!497 = distinct !DIGlobalVariable(scope: null, file: !6, line: 683, type: !423, isLocal: true, isDefinition: true)
!498 = !DIGlobalVariableExpression(var: !499, expr: !DIExpression())
!499 = distinct !DIGlobalVariable(scope: null, file: !6, line: 685, type: !177, isLocal: true, isDefinition: true)
!500 = !DIGlobalVariableExpression(var: !501, expr: !DIExpression())
!501 = distinct !DIGlobalVariable(scope: null, file: !6, line: 693, type: !368, isLocal: true, isDefinition: true)
!502 = !DIGlobalVariableExpression(var: !503, expr: !DIExpression())
!503 = distinct !DIGlobalVariable(scope: null, file: !6, line: 699, type: !504, isLocal: true, isDefinition: true)
!504 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 368, elements: !505)
!505 = !{!506}
!506 = !DISubrange(count: 46)
!507 = !DIGlobalVariableExpression(var: !508, expr: !DIExpression())
!508 = distinct !DIGlobalVariable(scope: null, file: !6, line: 703, type: !509, isLocal: true, isDefinition: true)
!509 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 680, elements: !510)
!510 = !{!511}
!511 = !DISubrange(count: 85)
!512 = !DIGlobalVariableExpression(var: !513, expr: !DIExpression())
!513 = distinct !DIGlobalVariable(scope: null, file: !6, line: 705, type: !514, isLocal: true, isDefinition: true)
!514 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 504, elements: !515)
!515 = !{!516}
!516 = !DISubrange(count: 63)
!517 = !DIGlobalVariableExpression(var: !518, expr: !DIExpression())
!518 = distinct !DIGlobalVariable(scope: null, file: !6, line: 707, type: !248, isLocal: true, isDefinition: true)
!519 = !DIGlobalVariableExpression(var: !520, expr: !DIExpression())
!520 = distinct !DIGlobalVariable(scope: null, file: !6, line: 709, type: !521, isLocal: true, isDefinition: true)
!521 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 488, elements: !522)
!522 = !{!523}
!523 = !DISubrange(count: 61)
!524 = !DIGlobalVariableExpression(var: !525, expr: !DIExpression())
!525 = distinct !DIGlobalVariable(scope: null, file: !6, line: 711, type: !526, isLocal: true, isDefinition: true)
!526 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 456, elements: !527)
!527 = !{!528}
!528 = !DISubrange(count: 57)
!529 = !DIGlobalVariableExpression(var: !530, expr: !DIExpression())
!530 = distinct !DIGlobalVariable(scope: null, file: !6, line: 719, type: !504, isLocal: true, isDefinition: true)
!531 = !DIGlobalVariableExpression(var: !532, expr: !DIExpression())
!532 = distinct !DIGlobalVariable(scope: null, file: !6, line: 722, type: !526, isLocal: true, isDefinition: true)
!533 = !DIGlobalVariableExpression(var: !534, expr: !DIExpression())
!534 = distinct !DIGlobalVariable(scope: null, file: !6, line: 728, type: !526, isLocal: true, isDefinition: true)
!535 = !DIGlobalVariableExpression(var: !536, expr: !DIExpression())
!536 = distinct !DIGlobalVariable(scope: null, file: !6, line: 730, type: !537, isLocal: true, isDefinition: true)
!537 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 472, elements: !538)
!538 = !{!539}
!539 = !DISubrange(count: 59)
!540 = !DIGlobalVariableExpression(var: !541, expr: !DIExpression())
!541 = distinct !DIGlobalVariable(scope: null, file: !6, line: 736, type: !504, isLocal: true, isDefinition: true)
!542 = !DIGlobalVariableExpression(var: !543, expr: !DIExpression())
!543 = distinct !DIGlobalVariable(scope: null, file: !6, line: 738, type: !544, isLocal: true, isDefinition: true)
!544 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 408, elements: !545)
!545 = !{!546}
!546 = !DISubrange(count: 51)
!547 = !DIGlobalVariableExpression(var: !548, expr: !DIExpression())
!548 = distinct !DIGlobalVariable(scope: null, file: !6, line: 751, type: !537, isLocal: true, isDefinition: true)
!549 = !DIGlobalVariableExpression(var: !550, expr: !DIExpression())
!550 = distinct !DIGlobalVariable(scope: null, file: !6, line: 753, type: !537, isLocal: true, isDefinition: true)
!551 = !DIGlobalVariableExpression(var: !552, expr: !DIExpression())
!552 = distinct !DIGlobalVariable(scope: null, file: !6, line: 760, type: !342, isLocal: true, isDefinition: true)
!553 = !DIGlobalVariableExpression(var: !554, expr: !DIExpression())
!554 = distinct !DIGlobalVariable(scope: null, file: !6, line: 763, type: !335, isLocal: true, isDefinition: true)
!555 = !DIGlobalVariableExpression(var: !556, expr: !DIExpression())
!556 = distinct !DIGlobalVariable(scope: null, file: !6, line: 769, type: !557, isLocal: true, isDefinition: true)
!557 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 624, elements: !558)
!558 = !{!559}
!559 = !DISubrange(count: 78)
!560 = !DIGlobalVariableExpression(var: !561, expr: !DIExpression())
!561 = distinct !DIGlobalVariable(scope: null, file: !6, line: 772, type: !124, isLocal: true, isDefinition: true)
!562 = !DIGlobalVariableExpression(var: !563, expr: !DIExpression())
!563 = distinct !DIGlobalVariable(scope: null, file: !6, line: 776, type: !564, isLocal: true, isDefinition: true)
!564 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 400, elements: !565)
!565 = !{!566}
!566 = !DISubrange(count: 50)
!567 = !DIGlobalVariableExpression(var: !568, expr: !DIExpression())
!568 = distinct !DIGlobalVariable(scope: null, file: !6, line: 782, type: !569, isLocal: true, isDefinition: true)
!569 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 440, elements: !570)
!570 = !{!571}
!571 = !DISubrange(count: 55)
!572 = !DIGlobalVariableExpression(var: !573, expr: !DIExpression())
!573 = distinct !DIGlobalVariable(scope: null, file: !6, line: 795, type: !574, isLocal: true, isDefinition: true)
!574 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 352, elements: !575)
!575 = !{!576}
!576 = !DISubrange(count: 44)
!577 = !DIGlobalVariableExpression(var: !578, expr: !DIExpression())
!578 = distinct !DIGlobalVariable(scope: null, file: !6, line: 800, type: !134, isLocal: true, isDefinition: true)
!579 = !DIGlobalVariableExpression(var: !580, expr: !DIExpression())
!580 = distinct !DIGlobalVariable(scope: null, file: !6, line: 804, type: !177, isLocal: true, isDefinition: true)
!581 = !DIGlobalVariableExpression(var: !582, expr: !DIExpression())
!582 = distinct !DIGlobalVariable(scope: null, file: !6, line: 808, type: !134, isLocal: true, isDefinition: true)
!583 = !DIGlobalVariableExpression(var: !584, expr: !DIExpression())
!584 = distinct !DIGlobalVariable(scope: null, file: !6, line: 812, type: !134, isLocal: true, isDefinition: true)
!585 = !DIGlobalVariableExpression(var: !586, expr: !DIExpression())
!586 = distinct !DIGlobalVariable(scope: null, file: !6, line: 816, type: !177, isLocal: true, isDefinition: true)
!587 = !DIGlobalVariableExpression(var: !588, expr: !DIExpression())
!588 = distinct !DIGlobalVariable(scope: null, file: !6, line: 818, type: !124, isLocal: true, isDefinition: true)
!589 = !DIGlobalVariableExpression(var: !590, expr: !DIExpression())
!590 = distinct !DIGlobalVariable(scope: null, file: !6, line: 867, type: !109, isLocal: true, isDefinition: true)
!591 = !DIGlobalVariableExpression(var: !592, expr: !DIExpression())
!592 = distinct !DIGlobalVariable(scope: null, file: !6, line: 869, type: !593, isLocal: true, isDefinition: true)
!593 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 32, elements: !594)
!594 = !{!595}
!595 = !DISubrange(count: 4)
!596 = !DIGlobalVariableExpression(var: !597, expr: !DIExpression())
!597 = distinct !DIGlobalVariable(scope: null, file: !6, line: 871, type: !598, isLocal: true, isDefinition: true)
!598 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 16, elements: !599)
!599 = !{!600}
!600 = !DISubrange(count: 2)
!601 = !DIGlobalVariableExpression(var: !602, expr: !DIExpression())
!602 = distinct !DIGlobalVariable(scope: null, file: !6, line: 879, type: !603, isLocal: true, isDefinition: true)
!603 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 128, elements: !604)
!604 = !{!605}
!605 = !DISubrange(count: 16)
!606 = !DIGlobalVariableExpression(var: !607, expr: !DIExpression())
!607 = distinct !DIGlobalVariable(scope: null, file: !6, line: 885, type: !109, isLocal: true, isDefinition: true)
!608 = !DIGlobalVariableExpression(var: !609, expr: !DIExpression())
!609 = distinct !DIGlobalVariable(scope: null, file: !6, line: 888, type: !478, isLocal: true, isDefinition: true)
!610 = !DIGlobalVariableExpression(var: !611, expr: !DIExpression())
!611 = distinct !DIGlobalVariable(scope: null, file: !6, line: 890, type: !603, isLocal: true, isDefinition: true)
!612 = !DIGlobalVariableExpression(var: !613, expr: !DIExpression())
!613 = distinct !DIGlobalVariable(scope: null, file: !6, line: 897, type: !335, isLocal: true, isDefinition: true)
!614 = !DIGlobalVariableExpression(var: !615, expr: !DIExpression())
!615 = distinct !DIGlobalVariable(scope: null, file: !6, line: 904, type: !109, isLocal: true, isDefinition: true)
!616 = !DIGlobalVariableExpression(var: !617, expr: !DIExpression())
!617 = distinct !DIGlobalVariable(scope: null, file: !6, line: 908, type: !335, isLocal: true, isDefinition: true)
!618 = !DIGlobalVariableExpression(var: !619, expr: !DIExpression())
!619 = distinct !DIGlobalVariable(scope: null, file: !6, line: 912, type: !78, isLocal: true, isDefinition: true)
!620 = !DIGlobalVariableExpression(var: !621, expr: !DIExpression())
!621 = distinct !DIGlobalVariable(scope: null, file: !6, line: 915, type: !129, isLocal: true, isDefinition: true)
!622 = !DIGlobalVariableExpression(var: !623, expr: !DIExpression())
!623 = distinct !DIGlobalVariable(scope: null, file: !6, line: 916, type: !593, isLocal: true, isDefinition: true)
!624 = !DIGlobalVariableExpression(var: !625, expr: !DIExpression())
!625 = distinct !DIGlobalVariable(scope: null, file: !6, line: 916, type: !626, isLocal: true, isDefinition: true)
!626 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 24, elements: !627)
!627 = !{!628}
!628 = !DISubrange(count: 3)
!629 = !DIGlobalVariableExpression(var: !630, expr: !DIExpression())
!630 = distinct !DIGlobalVariable(scope: null, file: !6, line: 921, type: !109, isLocal: true, isDefinition: true)
!631 = !DIGlobalVariableExpression(var: !632, expr: !DIExpression())
!632 = distinct !DIGlobalVariable(scope: null, file: !6, line: 925, type: !526, isLocal: true, isDefinition: true)
!633 = !DIGlobalVariableExpression(var: !634, expr: !DIExpression())
!634 = distinct !DIGlobalVariable(scope: null, file: !6, line: 930, type: !73, isLocal: true, isDefinition: true)
!635 = !DIGlobalVariableExpression(var: !636, expr: !DIExpression())
!636 = distinct !DIGlobalVariable(scope: null, file: !6, line: 932, type: !114, isLocal: true, isDefinition: true)
!637 = !DIGlobalVariableExpression(var: !638, expr: !DIExpression())
!638 = distinct !DIGlobalVariable(scope: null, file: !6, line: 936, type: !275, isLocal: true, isDefinition: true)
!639 = !DIGlobalVariableExpression(var: !640, expr: !DIExpression())
!640 = distinct !DIGlobalVariable(scope: null, file: !6, line: 937, type: !177, isLocal: true, isDefinition: true)
!641 = !DIGlobalVariableExpression(var: !642, expr: !DIExpression())
!642 = distinct !DIGlobalVariable(scope: null, file: !6, line: 938, type: !234, isLocal: true, isDefinition: true)
!643 = !DIGlobalVariableExpression(var: !644, expr: !DIExpression())
!644 = distinct !DIGlobalVariable(scope: null, file: !6, line: 943, type: !330, isLocal: true, isDefinition: true)
!645 = !DIGlobalVariableExpression(var: !646, expr: !DIExpression())
!646 = distinct !DIGlobalVariable(scope: null, file: !6, line: 944, type: !647, isLocal: true, isDefinition: true)
!647 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 480, elements: !648)
!648 = !{!649}
!649 = !DISubrange(count: 60)
!650 = !DIGlobalVariableExpression(var: !651, expr: !DIExpression())
!651 = distinct !DIGlobalVariable(scope: null, file: !6, line: 948, type: !335, isLocal: true, isDefinition: true)
!652 = !DIGlobalVariableExpression(var: !653, expr: !DIExpression())
!653 = distinct !DIGlobalVariable(scope: null, file: !6, line: 950, type: !423, isLocal: true, isDefinition: true)
!654 = !DIGlobalVariableExpression(var: !655, expr: !DIExpression())
!655 = distinct !DIGlobalVariable(scope: null, file: !6, line: 954, type: !603, isLocal: true, isDefinition: true)
!656 = !DIGlobalVariableExpression(var: !657, expr: !DIExpression())
!657 = distinct !DIGlobalVariable(scope: null, file: !6, line: 955, type: !658, isLocal: true, isDefinition: true)
!658 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 144, elements: !659)
!659 = !{!660}
!660 = !DISubrange(count: 18)
!661 = !DIGlobalVariableExpression(var: !662, expr: !DIExpression())
!662 = distinct !DIGlobalVariable(scope: null, file: !6, line: 956, type: !160, isLocal: true, isDefinition: true)
!663 = !DIGlobalVariableExpression(var: !664, expr: !DIExpression())
!664 = distinct !DIGlobalVariable(scope: null, file: !6, line: 957, type: !665, isLocal: true, isDefinition: true)
!665 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 120, elements: !666)
!666 = !{!667}
!667 = !DISubrange(count: 15)
!668 = !DIGlobalVariableExpression(var: !669, expr: !DIExpression())
!669 = distinct !DIGlobalVariable(scope: null, file: !6, line: 962, type: !347, isLocal: true, isDefinition: true)
!670 = !DIGlobalVariableExpression(var: !671, expr: !DIExpression())
!671 = distinct !DIGlobalVariable(scope: null, file: !6, line: 964, type: !603, isLocal: true, isDefinition: true)
!672 = !DIGlobalVariableExpression(var: !673, expr: !DIExpression())
!673 = distinct !DIGlobalVariable(scope: null, file: !6, line: 965, type: !603, isLocal: true, isDefinition: true)
!674 = !DIGlobalVariableExpression(var: !675, expr: !DIExpression())
!675 = distinct !DIGlobalVariable(scope: null, file: !6, line: 967, type: !109, isLocal: true, isDefinition: true)
!676 = !DIGlobalVariableExpression(var: !677, expr: !DIExpression())
!677 = distinct !DIGlobalVariable(scope: null, file: !6, line: 969, type: !160, isLocal: true, isDefinition: true)
!678 = !DIGlobalVariableExpression(var: !679, expr: !DIExpression())
!679 = distinct !DIGlobalVariable(scope: null, file: !6, line: 974, type: !342, isLocal: true, isDefinition: true)
!680 = !DIGlobalVariableExpression(var: !681, expr: !DIExpression())
!681 = distinct !DIGlobalVariable(scope: null, file: !6, line: 976, type: !569, isLocal: true, isDefinition: true)
!682 = !DIGlobalVariableExpression(var: !683, expr: !DIExpression())
!683 = distinct !DIGlobalVariable(scope: null, file: !6, line: 984, type: !129, isLocal: true, isDefinition: true)
!684 = !DIGlobalVariableExpression(var: !685, expr: !DIExpression())
!685 = distinct !DIGlobalVariable(scope: null, file: !6, line: 988, type: !177, isLocal: true, isDefinition: true)
!686 = !DIGlobalVariableExpression(var: !687, expr: !DIExpression())
!687 = distinct !DIGlobalVariable(scope: null, file: !6, line: 990, type: !102, isLocal: true, isDefinition: true)
!688 = !DIGlobalVariableExpression(var: !689, expr: !DIExpression())
!689 = distinct !DIGlobalVariable(scope: null, file: !6, line: 993, type: !574, isLocal: true, isDefinition: true)
!690 = !DIGlobalVariableExpression(var: !691, expr: !DIExpression())
!691 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1006, type: !368, isLocal: true, isDefinition: true)
!692 = !DIGlobalVariableExpression(var: !693, expr: !DIExpression())
!693 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1009, type: !102, isLocal: true, isDefinition: true)
!694 = !DIGlobalVariableExpression(var: !695, expr: !DIExpression())
!695 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1014, type: !119, isLocal: true, isDefinition: true)
!696 = !DIGlobalVariableExpression(var: !697, expr: !DIExpression())
!697 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1017, type: !698, isLocal: true, isDefinition: true)
!698 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 280, elements: !699)
!699 = !{!700}
!700 = !DISubrange(count: 35)
!701 = !DIGlobalVariableExpression(var: !702, expr: !DIExpression())
!702 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1020, type: !83, isLocal: true, isDefinition: true)
!703 = !DIGlobalVariableExpression(var: !704, expr: !DIExpression())
!704 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1022, type: !705, isLocal: true, isDefinition: true)
!705 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 312, elements: !706)
!706 = !{!707}
!707 = !DISubrange(count: 39)
!708 = !DIGlobalVariableExpression(var: !709, expr: !DIExpression())
!709 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1024, type: !234, isLocal: true, isDefinition: true)
!710 = !DIGlobalVariableExpression(var: !711, expr: !DIExpression())
!711 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1028, type: !544, isLocal: true, isDefinition: true)
!712 = !DIGlobalVariableExpression(var: !713, expr: !DIExpression())
!713 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1033, type: !234, isLocal: true, isDefinition: true)
!714 = !DIGlobalVariableExpression(var: !715, expr: !DIExpression())
!715 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1036, type: !143, isLocal: true, isDefinition: true)
!716 = !DIGlobalVariableExpression(var: !717, expr: !DIExpression())
!717 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1038, type: !119, isLocal: true, isDefinition: true)
!718 = !DIGlobalVariableExpression(var: !719, expr: !DIExpression())
!719 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1042, type: !698, isLocal: true, isDefinition: true)
!720 = !DIGlobalVariableExpression(var: !721, expr: !DIExpression())
!721 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1047, type: !143, isLocal: true, isDefinition: true)
!722 = !DIGlobalVariableExpression(var: !723, expr: !DIExpression())
!723 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1055, type: !342, isLocal: true, isDefinition: true)
!724 = !DIGlobalVariableExpression(var: !725, expr: !DIExpression())
!725 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1057, type: !544, isLocal: true, isDefinition: true)
!726 = !DIGlobalVariableExpression(var: !727, expr: !DIExpression())
!727 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1061, type: !728, isLocal: true, isDefinition: true)
!728 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 496, elements: !729)
!729 = !{!730}
!730 = !DISubrange(count: 62)
!731 = !DIGlobalVariableExpression(var: !732, expr: !DIExpression())
!732 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1064, type: !521, isLocal: true, isDefinition: true)
!733 = !DIGlobalVariableExpression(var: !734, expr: !DIExpression())
!734 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1068, type: !148, isLocal: true, isDefinition: true)
!735 = !DIGlobalVariableExpression(var: !736, expr: !DIExpression())
!736 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1070, type: !119, isLocal: true, isDefinition: true)
!737 = !DIGlobalVariableExpression(var: !738, expr: !DIExpression())
!738 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1072, type: !739, isLocal: true, isDefinition: true)
!739 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 576, elements: !740)
!740 = !{!741}
!741 = !DISubrange(count: 72)
!742 = !DIGlobalVariableExpression(var: !743, expr: !DIExpression())
!743 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1075, type: !564, isLocal: true, isDefinition: true)
!744 = !DIGlobalVariableExpression(var: !745, expr: !DIExpression())
!745 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1087, type: !234, isLocal: true, isDefinition: true)
!746 = !DIGlobalVariableExpression(var: !747, expr: !DIExpression())
!747 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1091, type: !504, isLocal: true, isDefinition: true)
!748 = !DIGlobalVariableExpression(var: !749, expr: !DIExpression())
!749 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1092, type: !90, isLocal: true, isDefinition: true)
!750 = !DIGlobalVariableExpression(var: !751, expr: !DIExpression())
!751 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1103, type: !83, isLocal: true, isDefinition: true)
!752 = !DIGlobalVariableExpression(var: !753, expr: !DIExpression())
!753 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1105, type: !754, isLocal: true, isDefinition: true)
!754 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 528, elements: !755)
!755 = !{!756}
!756 = !DISubrange(count: 66)
!757 = !DIGlobalVariableExpression(var: !758, expr: !DIExpression())
!758 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1107, type: !759, isLocal: true, isDefinition: true)
!759 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 416, elements: !760)
!760 = !{!761}
!761 = !DISubrange(count: 52)
!762 = !DIGlobalVariableExpression(var: !763, expr: !DIExpression())
!763 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1112, type: !124, isLocal: true, isDefinition: true)
!764 = !DIGlobalVariableExpression(var: !765, expr: !DIExpression())
!765 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1113, type: !766, isLocal: true, isDefinition: true)
!766 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 712, elements: !767)
!767 = !{!768}
!768 = !DISubrange(count: 89)
!769 = !DIGlobalVariableExpression(var: !770, expr: !DIExpression())
!770 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1122, type: !119, isLocal: true, isDefinition: true)
!771 = !DIGlobalVariableExpression(var: !772, expr: !DIExpression())
!772 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1124, type: !773, isLocal: true, isDefinition: true)
!773 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 552, elements: !774)
!774 = !{!775}
!775 = !DISubrange(count: 69)
!776 = !DIGlobalVariableExpression(var: !777, expr: !DIExpression())
!777 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1126, type: !441, isLocal: true, isDefinition: true)
!778 = !DIGlobalVariableExpression(var: !779, expr: !DIExpression())
!779 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1131, type: !504, isLocal: true, isDefinition: true)
!780 = !DIGlobalVariableExpression(var: !781, expr: !DIExpression())
!781 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1146, type: !335, isLocal: true, isDefinition: true)
!782 = !DIGlobalVariableExpression(var: !783, expr: !DIExpression())
!783 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1147, type: !368, isLocal: true, isDefinition: true)
!784 = !DIGlobalVariableExpression(var: !785, expr: !DIExpression())
!785 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1150, type: !423, isLocal: true, isDefinition: true)
!786 = !DIGlobalVariableExpression(var: !787, expr: !DIExpression())
!787 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1152, type: !335, isLocal: true, isDefinition: true)
!788 = !DIGlobalVariableExpression(var: !789, expr: !DIExpression())
!789 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1155, type: !569, isLocal: true, isDefinition: true)
!790 = !DIGlobalVariableExpression(var: !791, expr: !DIExpression())
!791 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1174, type: !83, isLocal: true, isDefinition: true)
!792 = !DIGlobalVariableExpression(var: !793, expr: !DIExpression())
!793 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1180, type: !73, isLocal: true, isDefinition: true)
!794 = !DIGlobalVariableExpression(var: !795, expr: !DIExpression())
!795 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1186, type: !73, isLocal: true, isDefinition: true)
!796 = !DIGlobalVariableExpression(var: !797, expr: !DIExpression())
!797 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1192, type: !698, isLocal: true, isDefinition: true)
!798 = !DIGlobalVariableExpression(var: !799, expr: !DIExpression())
!799 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1195, type: !759, isLocal: true, isDefinition: true)
!800 = !DIGlobalVariableExpression(var: !801, expr: !DIExpression())
!801 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1197, type: !83, isLocal: true, isDefinition: true)
!802 = !DIGlobalVariableExpression(var: !803, expr: !DIExpression())
!803 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1199, type: !368, isLocal: true, isDefinition: true)
!804 = !DIGlobalVariableExpression(var: !805, expr: !DIExpression())
!805 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1201, type: !347, isLocal: true, isDefinition: true)
!806 = !DIGlobalVariableExpression(var: !807, expr: !DIExpression())
!807 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1205, type: !432, isLocal: true, isDefinition: true)
!808 = !DIGlobalVariableExpression(var: !809, expr: !DIExpression())
!809 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1206, type: !810, isLocal: true, isDefinition: true)
!810 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 760, elements: !811)
!811 = !{!812}
!812 = !DISubrange(count: 95)
!813 = !DIGlobalVariableExpression(var: !814, expr: !DIExpression())
!814 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1208, type: !97, isLocal: true, isDefinition: true)
!815 = !DIGlobalVariableExpression(var: !816, expr: !DIExpression())
!816 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1209, type: !817, isLocal: true, isDefinition: true)
!817 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 696, elements: !818)
!818 = !{!819}
!819 = !DISubrange(count: 87)
!820 = !DIGlobalVariableExpression(var: !821, expr: !DIExpression())
!821 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1212, type: !569, isLocal: true, isDefinition: true)
!822 = !DIGlobalVariableExpression(var: !823, expr: !DIExpression())
!823 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1213, type: !124, isLocal: true, isDefinition: true)
!824 = !DIGlobalVariableExpression(var: !825, expr: !DIExpression())
!825 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1215, type: !97, isLocal: true, isDefinition: true)
!826 = !DIGlobalVariableExpression(var: !827, expr: !DIExpression())
!827 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1216, type: !90, isLocal: true, isDefinition: true)
!828 = !DIGlobalVariableExpression(var: !829, expr: !DIExpression())
!829 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1219, type: !90, isLocal: true, isDefinition: true)
!830 = !DIGlobalVariableExpression(var: !831, expr: !DIExpression())
!831 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1220, type: !647, isLocal: true, isDefinition: true)
!832 = !DIGlobalVariableExpression(var: !833, expr: !DIExpression())
!833 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1222, type: !97, isLocal: true, isDefinition: true)
!834 = !DIGlobalVariableExpression(var: !835, expr: !DIExpression())
!835 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1223, type: !728, isLocal: true, isDefinition: true)
!836 = !DIGlobalVariableExpression(var: !837, expr: !DIExpression())
!837 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1226, type: !569, isLocal: true, isDefinition: true)
!838 = !DIGlobalVariableExpression(var: !839, expr: !DIExpression())
!839 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1228, type: !153, isLocal: true, isDefinition: true)
!840 = !DIGlobalVariableExpression(var: !841, expr: !DIExpression())
!841 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1230, type: !754, isLocal: true, isDefinition: true)
!842 = !DIGlobalVariableExpression(var: !843, expr: !DIExpression())
!843 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1232, type: !124, isLocal: true, isDefinition: true)
!844 = !DIGlobalVariableExpression(var: !845, expr: !DIExpression())
!845 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1236, type: !569, isLocal: true, isDefinition: true)
!846 = !DIGlobalVariableExpression(var: !847, expr: !DIExpression())
!847 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1237, type: !848, isLocal: true, isDefinition: true)
!848 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 640, elements: !849)
!849 = !{!850}
!850 = !DISubrange(count: 80)
!851 = !DIGlobalVariableExpression(var: !852, expr: !DIExpression())
!852 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1240, type: !478, isLocal: true, isDefinition: true)
!853 = !DIGlobalVariableExpression(var: !854, expr: !DIExpression())
!854 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1242, type: !347, isLocal: true, isDefinition: true)
!855 = !DIGlobalVariableExpression(var: !856, expr: !DIExpression())
!856 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1244, type: !698, isLocal: true, isDefinition: true)
!857 = !DIGlobalVariableExpression(var: !858, expr: !DIExpression())
!858 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1248, type: !658, isLocal: true, isDefinition: true)
!859 = !DIGlobalVariableExpression(var: !860, expr: !DIExpression())
!860 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1250, type: !544, isLocal: true, isDefinition: true)
!861 = !DIGlobalVariableExpression(var: !862, expr: !DIExpression())
!862 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1251, type: !102, isLocal: true, isDefinition: true)
!863 = !DIGlobalVariableExpression(var: !864, expr: !DIExpression())
!864 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1252, type: !564, isLocal: true, isDefinition: true)
!865 = !DIGlobalVariableExpression(var: !866, expr: !DIExpression())
!866 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1253, type: !698, isLocal: true, isDefinition: true)
!867 = !DIGlobalVariableExpression(var: !868, expr: !DIExpression())
!868 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1254, type: !73, isLocal: true, isDefinition: true)
!869 = !DIGlobalVariableExpression(var: !870, expr: !DIExpression())
!870 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1255, type: !73, isLocal: true, isDefinition: true)
!871 = !DIGlobalVariableExpression(var: !872, expr: !DIExpression())
!872 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1257, type: !728, isLocal: true, isDefinition: true)
!873 = !DIGlobalVariableExpression(var: !874, expr: !DIExpression())
!874 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1259, type: !342, isLocal: true, isDefinition: true)
!875 = !DIGlobalVariableExpression(var: !876, expr: !DIExpression())
!876 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1263, type: !368, isLocal: true, isDefinition: true)
!877 = !DIGlobalVariableExpression(var: !878, expr: !DIExpression())
!878 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1266, type: !526, isLocal: true, isDefinition: true)
!879 = !DIGlobalVariableExpression(var: !880, expr: !DIExpression())
!880 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1267, type: !705, isLocal: true, isDefinition: true)
!881 = !DIGlobalVariableExpression(var: !882, expr: !DIExpression())
!882 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1270, type: !574, isLocal: true, isDefinition: true)
!883 = !DIGlobalVariableExpression(var: !884, expr: !DIExpression())
!884 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1273, type: !574, isLocal: true, isDefinition: true)
!885 = !DIGlobalVariableExpression(var: !886, expr: !DIExpression())
!886 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1277, type: !537, isLocal: true, isDefinition: true)
!887 = !DIGlobalVariableExpression(var: !888, expr: !DIExpression())
!888 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1279, type: !160, isLocal: true, isDefinition: true)
!889 = !DIGlobalVariableExpression(var: !890, expr: !DIExpression())
!890 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1282, type: !160, isLocal: true, isDefinition: true)
!891 = !DIGlobalVariableExpression(var: !892, expr: !DIExpression())
!892 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1285, type: !160, isLocal: true, isDefinition: true)
!893 = !DIGlobalVariableExpression(var: !894, expr: !DIExpression())
!894 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1289, type: !521, isLocal: true, isDefinition: true)
!895 = !DIGlobalVariableExpression(var: !896, expr: !DIExpression())
!896 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1291, type: !73, isLocal: true, isDefinition: true)
!897 = !DIGlobalVariableExpression(var: !898, expr: !DIExpression())
!898 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1295, type: !899, isLocal: true, isDefinition: true)
!899 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 616, elements: !900)
!900 = !{!901}
!901 = !DISubrange(count: 77)
!902 = !DIGlobalVariableExpression(var: !903, expr: !DIExpression())
!903 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1296, type: !569, isLocal: true, isDefinition: true)
!904 = !DIGlobalVariableExpression(var: !905, expr: !DIExpression())
!905 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1300, type: !342, isLocal: true, isDefinition: true)
!906 = !DIGlobalVariableExpression(var: !907, expr: !DIExpression())
!907 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1303, type: !129, isLocal: true, isDefinition: true)
!908 = !DIGlobalVariableExpression(var: !909, expr: !DIExpression())
!909 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1306, type: !129, isLocal: true, isDefinition: true)
!910 = !DIGlobalVariableExpression(var: !911, expr: !DIExpression())
!911 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1309, type: !705, isLocal: true, isDefinition: true)
!912 = !DIGlobalVariableExpression(var: !913, expr: !DIExpression())
!913 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1312, type: !564, isLocal: true, isDefinition: true)
!914 = !DIGlobalVariableExpression(var: !915, expr: !DIExpression())
!915 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1314, type: !478, isLocal: true, isDefinition: true)
!916 = !DIGlobalVariableExpression(var: !917, expr: !DIExpression())
!917 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1317, type: !569, isLocal: true, isDefinition: true)
!918 = !DIGlobalVariableExpression(var: !919, expr: !DIExpression())
!919 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1319, type: !478, isLocal: true, isDefinition: true)
!920 = !DIGlobalVariableExpression(var: !921, expr: !DIExpression())
!921 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1322, type: !569, isLocal: true, isDefinition: true)
!922 = !DIGlobalVariableExpression(var: !923, expr: !DIExpression())
!923 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1324, type: !319, isLocal: true, isDefinition: true)
!924 = !DIGlobalVariableExpression(var: !925, expr: !DIExpression())
!925 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1327, type: !754, isLocal: true, isDefinition: true)
!926 = !DIGlobalVariableExpression(var: !927, expr: !DIExpression())
!927 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1328, type: !928, isLocal: true, isDefinition: true)
!928 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 672, elements: !929)
!929 = !{!930}
!930 = !DISubrange(count: 84)
!931 = !DIGlobalVariableExpression(var: !932, expr: !DIExpression())
!932 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1332, type: !423, isLocal: true, isDefinition: true)
!933 = !DIGlobalVariableExpression(var: !934, expr: !DIExpression())
!934 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1335, type: !102, isLocal: true, isDefinition: true)
!935 = !DIGlobalVariableExpression(var: !936, expr: !DIExpression())
!936 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1343, type: !564, isLocal: true, isDefinition: true)
!937 = !DIGlobalVariableExpression(var: !938, expr: !DIExpression())
!938 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1350, type: !119, isLocal: true, isDefinition: true)
!939 = !DIGlobalVariableExpression(var: !940, expr: !DIExpression())
!940 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1352, type: !143, isLocal: true, isDefinition: true)
!941 = !DIGlobalVariableExpression(var: !942, expr: !DIExpression())
!942 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1360, type: !544, isLocal: true, isDefinition: true)
!943 = !DIGlobalVariableExpression(var: !944, expr: !DIExpression())
!944 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1361, type: !129, isLocal: true, isDefinition: true)
!945 = !DIGlobalVariableExpression(var: !946, expr: !DIExpression())
!946 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1362, type: !441, isLocal: true, isDefinition: true)
!947 = !DIGlobalVariableExpression(var: !948, expr: !DIExpression())
!948 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1363, type: !773, isLocal: true, isDefinition: true)
!949 = !DIGlobalVariableExpression(var: !950, expr: !DIExpression())
!950 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1364, type: !423, isLocal: true, isDefinition: true)
!951 = !DIGlobalVariableExpression(var: !952, expr: !DIExpression())
!952 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1365, type: !335, isLocal: true, isDefinition: true)
!953 = !DIGlobalVariableExpression(var: !954, expr: !DIExpression())
!954 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1367, type: !423, isLocal: true, isDefinition: true)
!955 = !DIGlobalVariableExpression(var: !956, expr: !DIExpression())
!956 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1369, type: !754, isLocal: true, isDefinition: true)
!957 = !DIGlobalVariableExpression(var: !958, expr: !DIExpression())
!958 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1373, type: !544, isLocal: true, isDefinition: true)
!959 = !DIGlobalVariableExpression(var: !960, expr: !DIExpression())
!960 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1377, type: !544, isLocal: true, isDefinition: true)
!961 = !DIGlobalVariableExpression(var: !962, expr: !DIExpression())
!962 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1408, type: !143, isLocal: true, isDefinition: true)
!963 = !DIGlobalVariableExpression(var: !964, expr: !DIExpression())
!964 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1420, type: !124, isLocal: true, isDefinition: true)
!965 = !DIGlobalVariableExpression(var: !966, expr: !DIExpression())
!966 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1421, type: !544, isLocal: true, isDefinition: true)
!967 = !DIGlobalVariableExpression(var: !968, expr: !DIExpression())
!968 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1424, type: !544, isLocal: true, isDefinition: true)
!969 = !DIGlobalVariableExpression(var: !970, expr: !DIExpression())
!970 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1431, type: !330, isLocal: true, isDefinition: true)
!971 = !DIGlobalVariableExpression(var: !972, expr: !DIExpression())
!972 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1432, type: !330, isLocal: true, isDefinition: true)
!973 = !DIGlobalVariableExpression(var: !974, expr: !DIExpression())
!974 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1433, type: !478, isLocal: true, isDefinition: true)
!975 = !DIGlobalVariableExpression(var: !976, expr: !DIExpression())
!976 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1434, type: !478, isLocal: true, isDefinition: true)
!977 = !DIGlobalVariableExpression(var: !978, expr: !DIExpression())
!978 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1436, type: !114, isLocal: true, isDefinition: true)
!979 = !DIGlobalVariableExpression(var: !980, expr: !DIExpression())
!980 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1439, type: !347, isLocal: true, isDefinition: true)
!981 = !DIGlobalVariableExpression(var: !982, expr: !DIExpression())
!982 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1441, type: !564, isLocal: true, isDefinition: true)
!983 = !DIGlobalVariableExpression(var: !984, expr: !DIExpression())
!984 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1443, type: !368, isLocal: true, isDefinition: true)
!985 = !DIGlobalVariableExpression(var: !986, expr: !DIExpression())
!986 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1445, type: !423, isLocal: true, isDefinition: true)
!987 = !DIGlobalVariableExpression(var: !988, expr: !DIExpression())
!988 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1447, type: !368, isLocal: true, isDefinition: true)
!989 = !DIGlobalVariableExpression(var: !990, expr: !DIExpression())
!990 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1449, type: !423, isLocal: true, isDefinition: true)
!991 = !DIGlobalVariableExpression(var: !992, expr: !DIExpression())
!992 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1452, type: !658, isLocal: true, isDefinition: true)
!993 = !DIGlobalVariableExpression(var: !994, expr: !DIExpression())
!994 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1454, type: !521, isLocal: true, isDefinition: true)
!995 = !DIGlobalVariableExpression(var: !996, expr: !DIExpression())
!996 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1456, type: !432, isLocal: true, isDefinition: true)
!997 = !DIGlobalVariableExpression(var: !998, expr: !DIExpression())
!998 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1457, type: !385, isLocal: true, isDefinition: true)
!999 = !DIGlobalVariableExpression(var: !1000, expr: !DIExpression())
!1000 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1458, type: !186, isLocal: true, isDefinition: true)
!1001 = !DIGlobalVariableExpression(var: !1002, expr: !DIExpression())
!1002 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1461, type: !574, isLocal: true, isDefinition: true)
!1003 = !DIGlobalVariableExpression(var: !1004, expr: !DIExpression())
!1004 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1488, type: !1005, isLocal: true, isDefinition: true)
!1005 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 152, elements: !1006)
!1006 = !{!1007}
!1007 = !DISubrange(count: 19)
!1008 = !DIGlobalVariableExpression(var: !1009, expr: !DIExpression())
!1009 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1492, type: !330, isLocal: true, isDefinition: true)
!1010 = !DIGlobalVariableExpression(var: !1011, expr: !DIExpression())
!1011 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1497, type: !1005, isLocal: true, isDefinition: true)
!1012 = !DIGlobalVariableExpression(var: !1013, expr: !DIExpression())
!1013 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1499, type: !330, isLocal: true, isDefinition: true)
!1014 = !DIGlobalVariableExpression(var: !1015, expr: !DIExpression())
!1015 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1503, type: !275, isLocal: true, isDefinition: true)
!1016 = !DIGlobalVariableExpression(var: !1017, expr: !DIExpression())
!1017 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1505, type: !97, isLocal: true, isDefinition: true)
!1018 = !DIGlobalVariableExpression(var: !1019, expr: !DIExpression())
!1019 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1510, type: !83, isLocal: true, isDefinition: true)
!1020 = !DIGlobalVariableExpression(var: !1021, expr: !DIExpression())
!1021 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1512, type: !153, isLocal: true, isDefinition: true)
!1022 = !DIGlobalVariableExpression(var: !1023, expr: !DIExpression())
!1023 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1517, type: !347, isLocal: true, isDefinition: true)
!1024 = !DIGlobalVariableExpression(var: !1025, expr: !DIExpression())
!1025 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1519, type: !347, isLocal: true, isDefinition: true)
!1026 = !DIGlobalVariableExpression(var: !1027, expr: !DIExpression())
!1027 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1521, type: !319, isLocal: true, isDefinition: true)
!1028 = !DIGlobalVariableExpression(var: !1029, expr: !DIExpression())
!1029 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1525, type: !330, isLocal: true, isDefinition: true)
!1030 = !DIGlobalVariableExpression(var: !1031, expr: !DIExpression())
!1031 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1528, type: !658, isLocal: true, isDefinition: true)
!1032 = !DIGlobalVariableExpression(var: !1033, expr: !DIExpression())
!1033 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1529, type: !416, isLocal: true, isDefinition: true)
!1034 = !DIGlobalVariableExpression(var: !1035, expr: !DIExpression())
!1035 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1530, type: !416, isLocal: true, isDefinition: true)
!1036 = !DIGlobalVariableExpression(var: !1037, expr: !DIExpression())
!1037 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1531, type: !416, isLocal: true, isDefinition: true)
!1038 = !DIGlobalVariableExpression(var: !1039, expr: !DIExpression())
!1039 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1532, type: !665, isLocal: true, isDefinition: true)
!1040 = !DIGlobalVariableExpression(var: !1041, expr: !DIExpression())
!1041 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1533, type: !478, isLocal: true, isDefinition: true)
!1042 = !DIGlobalVariableExpression(var: !1043, expr: !DIExpression())
!1043 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1538, type: !97, isLocal: true, isDefinition: true)
!1044 = !DIGlobalVariableExpression(var: !1045, expr: !DIExpression())
!1045 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1541, type: !275, isLocal: true, isDefinition: true)
!1046 = !DIGlobalVariableExpression(var: !1047, expr: !DIExpression())
!1047 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1542, type: !665, isLocal: true, isDefinition: true)
!1048 = !DIGlobalVariableExpression(var: !1049, expr: !DIExpression())
!1049 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1543, type: !665, isLocal: true, isDefinition: true)
!1050 = !DIGlobalVariableExpression(var: !1051, expr: !DIExpression())
!1051 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1544, type: !665, isLocal: true, isDefinition: true)
!1052 = !DIGlobalVariableExpression(var: !1053, expr: !DIExpression())
!1053 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1545, type: !603, isLocal: true, isDefinition: true)
!1054 = !DIGlobalVariableExpression(var: !1055, expr: !DIExpression())
!1055 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1546, type: !153, isLocal: true, isDefinition: true)
!1056 = !DIGlobalVariableExpression(var: !1057, expr: !DIExpression())
!1057 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1564, type: !102, isLocal: true, isDefinition: true)
!1058 = !DIGlobalVariableExpression(var: !1059, expr: !DIExpression())
!1059 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1567, type: !177, isLocal: true, isDefinition: true)
!1060 = !DIGlobalVariableExpression(var: !1061, expr: !DIExpression())
!1061 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1574, type: !705, isLocal: true, isDefinition: true)
!1062 = !DIGlobalVariableExpression(var: !1063, expr: !DIExpression())
!1063 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1575, type: !593, isLocal: true, isDefinition: true)
!1064 = !DIGlobalVariableExpression(var: !1065, expr: !DIExpression())
!1065 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1583, type: !478, isLocal: true, isDefinition: true)
!1066 = !DIGlobalVariableExpression(var: !1067, expr: !DIExpression())
!1067 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1587, type: !347, isLocal: true, isDefinition: true)
!1068 = !DIGlobalVariableExpression(var: !1069, expr: !DIExpression())
!1069 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1588, type: !78, isLocal: true, isDefinition: true)
!1070 = !DIGlobalVariableExpression(var: !1071, expr: !DIExpression())
!1071 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1593, type: !148, isLocal: true, isDefinition: true)
!1072 = !DIGlobalVariableExpression(var: !1073, expr: !DIExpression())
!1073 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1596, type: !234, isLocal: true, isDefinition: true)
!1074 = !DIGlobalVariableExpression(var: !1075, expr: !DIExpression())
!1075 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1601, type: !102, isLocal: true, isDefinition: true)
!1076 = !DIGlobalVariableExpression(var: !1077, expr: !DIExpression())
!1077 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1604, type: !705, isLocal: true, isDefinition: true)
!1078 = !DIGlobalVariableExpression(var: !1079, expr: !DIExpression())
!1079 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1609, type: !148, isLocal: true, isDefinition: true)
!1080 = !DIGlobalVariableExpression(var: !1081, expr: !DIExpression())
!1081 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1611, type: !177, isLocal: true, isDefinition: true)
!1082 = !DIGlobalVariableExpression(var: !1083, expr: !DIExpression())
!1083 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1616, type: !102, isLocal: true, isDefinition: true)
!1084 = !DIGlobalVariableExpression(var: !1085, expr: !DIExpression())
!1085 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1620, type: !170, isLocal: true, isDefinition: true)
!1086 = !DIGlobalVariableExpression(var: !1087, expr: !DIExpression())
!1087 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1624, type: !248, isLocal: true, isDefinition: true)
!1088 = !DIGlobalVariableExpression(var: !1089, expr: !DIExpression())
!1089 = distinct !DIGlobalVariable(scope: null, file: !6, line: 1643, type: !665, isLocal: true, isDefinition: true)
!1090 = !DIGlobalVariableExpression(var: !1091, expr: !DIExpression())
!1091 = distinct !DIGlobalVariable(name: "operations", scope: !2, file: !6, line: 203, type: !1092, isLocal: true, isDefinition: true)
!1092 = !DICompositeType(tag: DW_TAG_array_type, baseType: !61, size: 64, elements: !599)
!1093 = !DIGlobalVariableExpression(var: !1094, expr: !DIExpression())
!1094 = distinct !DIGlobalVariable(name: "STATIC_DATA_FOR_INIT", scope: !2, file: !6, line: 95, type: !1095, isLocal: true, isDefinition: true)
!1095 = !DICompositeType(tag: DW_TAG_array_type, baseType: !1096, size: 464, elements: !1097)
!1096 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !34)
!1097 = !{!1098}
!1098 = !DISubrange(count: 58)
!1099 = !DIGlobalVariableExpression(var: !1100, expr: !DIExpression())
!1100 = distinct !DIGlobalVariable(scope: null, file: !6, line: 108, type: !97, isLocal: true, isDefinition: true)
!1101 = !DIGlobalVariableExpression(var: !1102, expr: !DIExpression())
!1102 = distinct !DIGlobalVariable(scope: null, file: !6, line: 113, type: !177, isLocal: true, isDefinition: true)
!1103 = !DIGlobalVariableExpression(var: !1104, expr: !DIExpression())
!1104 = distinct !DIGlobalVariable(name: "tick", scope: !1105, file: !6, line: 121, type: !24, isLocal: true, isDefinition: true)
!1105 = distinct !DISubprogram(name: "test_timer_cb", scope: !6, file: !6, line: 119, type: !1106, scopeLine: 119, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1112)
!1106 = !DISubroutineType(types: !1107)
!1107 = !{null, !1108}
!1108 = !DIDerivedType(tag: DW_TAG_typedef, name: "TimerHandle_t", file: !1109, line: 87, baseType: !1110)
!1109 = !DIFile(filename: "Programs/Espressif/frameworks/esp-idf-v5.4/components/freertos/FreeRTOS-Kernel/include/freertos/timers.h", directory: "C:/Andrey")
!1110 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1111, size: 32)
!1111 = !DICompositeType(tag: DW_TAG_structure_type, name: "tmrTimerControl", file: !1109, line: 86, flags: DIFlagFwdDecl)
!1112 = !{!1113, !1114}
!1113 = !DILocalVariable(name: "xTimer", arg: 1, scope: !1105, file: !6, line: 119, type: !1108)
!1114 = !DILocalVariable(name: "ud", scope: !1105, file: !6, line: 120, type: !55)
!1115 = !DIGlobalVariableExpression(var: !1116, expr: !DIExpression())
!1116 = distinct !DIGlobalVariable(scope: null, file: !6, line: 122, type: !109, isLocal: true, isDefinition: true)
!1117 = !DIGlobalVariableExpression(var: !1118, expr: !DIExpression())
!1118 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 8, type: !298, isLocal: true, isDefinition: true)
!1119 = !DIFile(filename: "c:\\andrey\\projects\\vs_code\\espb prodaction\\esp32_prj_to_llvm\\main\\ir_gen_temp\\test.c", directory: "")
!1120 = !DIGlobalVariableExpression(var: !1121, expr: !DIExpression())
!1121 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 9, type: !298, isLocal: true, isDefinition: true)
!1122 = !DIGlobalVariableExpression(var: !1123, expr: !DIExpression())
!1123 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 12, type: !97, isLocal: true, isDefinition: true)
!1124 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1125, producer: "clang version 18.1.2 (https://github.com/espressif/llvm-project.git esp-18.1.2_20240912)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !1126, splitDebugInlining: false, nameTableKind: None)
!1125 = !DIFile(filename: "c:\\andrey\\projects\\vs_code\\espb prodaction\\esp32_prj_to_llvm\\main\\ir_gen_temp/test.c", directory: "C:/Andrey/Projects/VS_Code/ESPB PRODACTION/ESP32_PRJ_TO_LLVM")
!1126 = !{!1127, !1117, !1120, !1122, !1129, !1131}
!1127 = !DIGlobalVariableExpression(var: !1128, expr: !DIExpression())
!1128 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 5, type: !347, isLocal: true, isDefinition: true)
!1129 = !DIGlobalVariableExpression(var: !1130, expr: !DIExpression())
!1130 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 14, type: !148, isLocal: true, isDefinition: true)
!1131 = !DIGlobalVariableExpression(var: !1132, expr: !DIExpression())
!1132 = distinct !DIGlobalVariable(scope: null, file: !1119, line: 17, type: !73, isLocal: true, isDefinition: true)
!1133 = !{!"clang version 18.1.2 (https://github.com/espressif/llvm-project.git esp-18.1.2_20240912)"}
!1134 = !{i32 7, !"Dwarf Version", i32 4}
!1135 = !{i32 2, !"Debug Info Version", i32 3}
!1136 = !{i32 1, !"wchar_size", i32 4}
!1137 = !{i32 1, !"target-abi", !"ilp32"}
!1138 = !{i32 8, !"SmallDataLimit", i32 8}
!1139 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!1140 = distinct !DISubprogram(name: "add_function_1", scope: !6, file: !6, line: 1173, type: !62, scopeLine: 1173, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1141)
!1141 = !{!1142, !1143}
!1142 = !DILocalVariable(name: "a", arg: 1, scope: !1140, file: !6, line: 1173, type: !24)
!1143 = !DILocalVariable(name: "b", arg: 2, scope: !1140, file: !6, line: 1173, type: !24)
!1144 = !DILocation(line: 0, scope: !1140)
!1145 = !DILocation(line: 1174, column: 5, scope: !1140)
!1146 = !DILocation(line: 1175, column: 14, scope: !1140)
!1147 = !DILocation(line: 1175, column: 5, scope: !1140)
!1148 = distinct !DISubprogram(name: "print_int_array", scope: !6, file: !6, line: 866, type: !1149, scopeLine: 866, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1155)
!1149 = !DISubroutineType(types: !1150)
!1150 = !{null, !59, !1151}
!1151 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !1152, line: 40, baseType: !1153)
!1152 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/strings.h", directory: "C:/Andrey")
!1153 = !DIDerivedType(tag: DW_TAG_typedef, name: "__size_t", file: !1154, line: 131, baseType: !7)
!1154 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/sys/_types.h", directory: "C:/Andrey")
!1155 = !{!1156, !1157, !1158}
!1156 = !DILocalVariable(name: "arr", arg: 1, scope: !1148, file: !6, line: 866, type: !59)
!1157 = !DILocalVariable(name: "size", arg: 2, scope: !1148, file: !6, line: 866, type: !1151)
!1158 = !DILocalVariable(name: "i", scope: !1159, file: !6, line: 868, type: !1151)
!1159 = distinct !DILexicalBlock(scope: !1148, file: !6, line: 868, column: 5)
!1160 = !DILocation(line: 0, scope: !1148)
!1161 = !DILocation(line: 867, column: 5, scope: !1148)
!1162 = !DILocation(line: 0, scope: !1159)
!1163 = !DILocation(line: 868, column: 26, scope: !1164)
!1164 = distinct !DILexicalBlock(scope: !1159, file: !6, line: 868, column: 5)
!1165 = !DILocation(line: 868, column: 5, scope: !1159)
!1166 = !DILocation(line: 871, column: 5, scope: !1148)
!1167 = !DILocation(line: 872, column: 1, scope: !1148)
!1168 = !DILocation(line: 869, column: 23, scope: !1169)
!1169 = distinct !DILexicalBlock(scope: !1164, file: !6, line: 868, column: 39)
!1170 = !{!1171, !1171, i64 0}
!1171 = !{!"int", !1172, i64 0}
!1172 = !{!"omnipotent char", !1173, i64 0}
!1173 = !{!"Simple C/C++ TBAA"}
!1174 = !DILocation(line: 869, column: 9, scope: !1169)
!1175 = !DILocation(line: 868, column: 35, scope: !1164)
!1176 = distinct !{!1176, !1165, !1177, !1178, !1179}
!1177 = !DILocation(line: 870, column: 5, scope: !1159)
!1178 = !{!"llvm.loop.mustprogress"}
!1179 = !{!"llvm.loop.unroll.disable"}
!1180 = distinct !DISubprogram(name: "test_memory_operations", scope: !6, file: !6, line: 393, type: !1181, scopeLine: 393, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1183)
!1181 = !DISubroutineType(types: !1182)
!1182 = !{null}
!1183 = !{!1184, !1188, !1190, !1191, !1192, !1193, !1194, !1195, !1196, !1197, !1198, !1199, !1200, !1201, !1202, !1203, !1204, !1205, !1206, !1207, !1208}
!1184 = !DILocalVariable(name: "buffer", scope: !1180, file: !6, line: 399, type: !1185)
!1185 = !DICompositeType(tag: DW_TAG_array_type, baseType: !34, size: 800, elements: !1186)
!1186 = !{!1187}
!1187 = !DISubrange(count: 100)
!1188 = !DILocalVariable(name: "i", scope: !1189, file: !6, line: 402, type: !24)
!1189 = distinct !DILexicalBlock(scope: !1180, file: !6, line: 402, column: 5)
!1190 = !DILocalVariable(name: "p_i32", scope: !1180, file: !6, line: 409, type: !35)
!1191 = !DILocalVariable(name: "loaded_i32", scope: !1180, file: !6, line: 411, type: !25)
!1192 = !DILocalVariable(name: "p_i64", scope: !1180, file: !6, line: 415, type: !36)
!1193 = !DILocalVariable(name: "loaded_i64", scope: !1180, file: !6, line: 417, type: !37)
!1194 = !DILocalVariable(name: "p_f32", scope: !1180, file: !6, line: 421, type: !40)
!1195 = !DILocalVariable(name: "loaded_f32", scope: !1180, file: !6, line: 423, type: !27)
!1196 = !DILocalVariable(name: "p_f64", scope: !1180, file: !6, line: 427, type: !41)
!1197 = !DILocalVariable(name: "loaded_f64", scope: !1180, file: !6, line: 429, type: !42)
!1198 = !DILocalVariable(name: "p_i8", scope: !1180, file: !6, line: 433, type: !43)
!1199 = !DILocalVariable(name: "loaded_i8", scope: !1180, file: !6, line: 435, type: !31)
!1200 = !DILocalVariable(name: "p_u8", scope: !1180, file: !6, line: 436, type: !44)
!1201 = !DILocalVariable(name: "loaded_u8", scope: !1180, file: !6, line: 438, type: !45)
!1202 = !DILocalVariable(name: "p_i16", scope: !1180, file: !6, line: 443, type: !48)
!1203 = !DILocalVariable(name: "loaded_i16", scope: !1180, file: !6, line: 445, type: !28)
!1204 = !DILocalVariable(name: "p_u16", scope: !1180, file: !6, line: 446, type: !49)
!1205 = !DILocalVariable(name: "loaded_u16", scope: !1180, file: !6, line: 448, type: !50)
!1206 = !DILocalVariable(name: "p_ptr", scope: !1180, file: !6, line: 454, type: !53)
!1207 = !DILocalVariable(name: "ptr_to_store", scope: !1180, file: !6, line: 455, type: !55)
!1208 = !DILocalVariable(name: "loaded_ptr", scope: !1180, file: !6, line: 457, type: !55)
!1209 = distinct !DIAssignID()
!1210 = !DILocation(line: 0, scope: !1180)
!1211 = !DILocation(line: 394, column: 5, scope: !1180)
!1212 = !DILocation(line: 399, column: 5, scope: !1180)
!1213 = !DILocation(line: 0, scope: !1189)
!1214 = !DILocation(line: 402, column: 5, scope: !1189)
!1215 = !DILocation(line: 406, column: 5, scope: !1180)
!1216 = !DILocation(line: 410, column: 12, scope: !1180)
!1217 = !DILocation(line: 412, column: 5, scope: !1180)
!1218 = !DILocation(line: 415, column: 40, scope: !1180)
!1219 = !DILocation(line: 416, column: 12, scope: !1180)
!1220 = !{!1221, !1221, i64 0}
!1221 = !{!"long long", !1172, i64 0}
!1222 = !DILocation(line: 418, column: 5, scope: !1180)
!1223 = !DILocation(line: 421, column: 36, scope: !1180)
!1224 = !DILocation(line: 422, column: 12, scope: !1180)
!1225 = !{!1226, !1226, i64 0}
!1226 = !{!"float", !1172, i64 0}
!1227 = !DILocation(line: 424, column: 5, scope: !1180)
!1228 = !DILocation(line: 427, column: 38, scope: !1180)
!1229 = !DILocation(line: 428, column: 12, scope: !1180)
!1230 = !{!1231, !1231, i64 0}
!1231 = !{!"double", !1172, i64 0}
!1232 = !DILocation(line: 430, column: 5, scope: !1180)
!1233 = !DILocation(line: 433, column: 37, scope: !1180)
!1234 = !DILocation(line: 434, column: 11, scope: !1180)
!1235 = !{!1172, !1172, i64 0}
!1236 = !DILocation(line: 436, column: 39, scope: !1180)
!1237 = !DILocation(line: 437, column: 11, scope: !1180)
!1238 = !DILocation(line: 439, column: 5, scope: !1180)
!1239 = !DILocation(line: 440, column: 5, scope: !1180)
!1240 = !DILocation(line: 443, column: 40, scope: !1180)
!1241 = !DILocation(line: 444, column: 12, scope: !1180)
!1242 = !{!1243, !1243, i64 0}
!1243 = !{!"short", !1172, i64 0}
!1244 = !DILocation(line: 446, column: 42, scope: !1180)
!1245 = !DILocation(line: 447, column: 12, scope: !1180)
!1246 = !DILocation(line: 449, column: 5, scope: !1180)
!1247 = !DILocation(line: 450, column: 5, scope: !1180)
!1248 = !DILocation(line: 454, column: 57, scope: !1180)
!1249 = !DILocation(line: 456, column: 12, scope: !1180)
!1250 = !{!1251, !1251, i64 0}
!1251 = !{!"any pointer", !1172, i64 0}
!1252 = !DILocation(line: 457, column: 24, scope: !1180)
!1253 = !DILocation(line: 458, column: 5, scope: !1180)
!1254 = !DILocation(line: 460, column: 5, scope: !1180)
!1255 = !DILocation(line: 461, column: 1, scope: !1180)
!1256 = !DILocation(line: 403, column: 21, scope: !1257)
!1257 = distinct !DILexicalBlock(scope: !1258, file: !6, line: 402, column: 34)
!1258 = distinct !DILexicalBlock(scope: !1189, file: !6, line: 402, column: 5)
!1259 = !DILocation(line: 403, column: 9, scope: !1257)
!1260 = !DILocation(line: 403, column: 19, scope: !1257)
!1261 = !DILocation(line: 402, column: 30, scope: !1258)
!1262 = !DILocation(line: 402, column: 22, scope: !1258)
!1263 = distinct !{!1263, !1214, !1264, !1178, !1179}
!1264 = !DILocation(line: 404, column: 5, scope: !1189)
!1265 = distinct !DISubprogram(name: "test_i64_imm8_ops", scope: !6, file: !6, line: 781, type: !1181, scopeLine: 781, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1266)
!1266 = !{!1267, !1269, !1274, !1275, !1276, !1277, !1278, !1279, !1280}
!1267 = !DILocalVariable(name: "i64_a_volatile", scope: !1265, file: !6, line: 786, type: !1268)
!1268 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !37)
!1269 = !DILocalVariable(name: "u64_a_volatile", scope: !1265, file: !6, line: 787, type: !1270)
!1270 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !1271)
!1271 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !17, line: 60, baseType: !1272)
!1272 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint64_t", file: !19, line: 105, baseType: !1273)
!1273 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!1274 = !DILocalVariable(name: "i64_a", scope: !1265, file: !6, line: 792, type: !37)
!1275 = !DILocalVariable(name: "u64_a", scope: !1265, file: !6, line: 793, type: !1271)
!1276 = !DILocalVariable(name: "div_res", scope: !1265, file: !6, line: 799, type: !37)
!1277 = !DILocalVariable(name: "divu_res", scope: !1265, file: !6, line: 803, type: !1271)
!1278 = !DILocalVariable(name: "rem_res", scope: !1265, file: !6, line: 807, type: !37)
!1279 = !DILocalVariable(name: "remu_res", scope: !1265, file: !6, line: 811, type: !1271)
!1280 = !DILocalVariable(name: "shru_res", scope: !1265, file: !6, line: 815, type: !1271)
!1281 = distinct !DIAssignID()
!1282 = !DILocation(line: 0, scope: !1265)
!1283 = distinct !DIAssignID()
!1284 = !DILocation(line: 782, column: 5, scope: !1265)
!1285 = !DILocation(line: 786, column: 5, scope: !1265)
!1286 = !DILocation(line: 786, column: 22, scope: !1265)
!1287 = distinct !DIAssignID()
!1288 = !DILocation(line: 787, column: 5, scope: !1265)
!1289 = !DILocation(line: 787, column: 23, scope: !1265)
!1290 = distinct !DIAssignID()
!1291 = !DILocation(line: 792, column: 21, scope: !1265)
!1292 = !DILocation(line: 793, column: 22, scope: !1265)
!1293 = !DILocation(line: 795, column: 5, scope: !1265)
!1294 = !DILocation(line: 799, column: 29, scope: !1265)
!1295 = !DILocation(line: 800, column: 5, scope: !1265)
!1296 = !DILocation(line: 803, column: 31, scope: !1265)
!1297 = !DILocation(line: 804, column: 5, scope: !1265)
!1298 = !DILocation(line: 807, column: 29, scope: !1265)
!1299 = !DILocation(line: 808, column: 5, scope: !1265)
!1300 = !DILocation(line: 811, column: 31, scope: !1265)
!1301 = !DILocation(line: 812, column: 5, scope: !1265)
!1302 = !DILocation(line: 815, column: 31, scope: !1265)
!1303 = !DILocation(line: 816, column: 5, scope: !1265)
!1304 = !DILocation(line: 818, column: 5, scope: !1265)
!1305 = !DILocation(line: 819, column: 1, scope: !1265)
!1306 = distinct !DISubprogram(name: "test_table_operations", scope: !6, file: !6, line: 1191, type: !1181, scopeLine: 1191, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1307)
!1307 = !{!1308, !1309, !1310}
!1308 = !DILocalVariable(name: "initial_size", scope: !1306, file: !6, line: 1196, type: !24)
!1309 = !DILocalVariable(name: "final_size", scope: !1306, file: !6, line: 1227, type: !24)
!1310 = !DILocalVariable(name: "expanded_size", scope: !1306, file: !6, line: 1239, type: !24)
!1311 = !DILocation(line: 1192, column: 5, scope: !1306)
!1312 = !DILocation(line: 1195, column: 5, scope: !1306)
!1313 = !DILocation(line: 1196, column: 24, scope: !1306)
!1314 = !DILocation(line: 0, scope: !1306)
!1315 = !DILocation(line: 1197, column: 5, scope: !1306)
!1316 = !DILocation(line: 1198, column: 22, scope: !1317)
!1317 = distinct !DILexicalBlock(scope: !1306, file: !6, line: 1198, column: 9)
!1318 = !DILocation(line: 1198, column: 9, scope: !1306)
!1319 = !DILocation(line: 1199, column: 9, scope: !1320)
!1320 = distinct !DILexicalBlock(scope: !1317, file: !6, line: 1198, column: 29)
!1321 = !DILocation(line: 1200, column: 5, scope: !1320)
!1322 = !DILocation(line: 1201, column: 9, scope: !1323)
!1323 = distinct !DILexicalBlock(scope: !1317, file: !6, line: 1200, column: 12)
!1324 = !DILocation(line: 1205, column: 5, scope: !1306)
!1325 = !DILocation(line: 1206, column: 5, scope: !1306)
!1326 = !DILocation(line: 1207, column: 5, scope: !1306)
!1327 = !DILocation(line: 1208, column: 5, scope: !1306)
!1328 = !DILocation(line: 1209, column: 5, scope: !1306)
!1329 = !DILocation(line: 1212, column: 5, scope: !1306)
!1330 = !DILocation(line: 1213, column: 5, scope: !1306)
!1331 = !DILocation(line: 1214, column: 5, scope: !1306)
!1332 = !DILocation(line: 1215, column: 5, scope: !1306)
!1333 = !DILocation(line: 1216, column: 5, scope: !1306)
!1334 = !DILocation(line: 1219, column: 5, scope: !1306)
!1335 = !DILocation(line: 1220, column: 5, scope: !1306)
!1336 = !DILocation(line: 1221, column: 5, scope: !1306)
!1337 = !DILocation(line: 1222, column: 5, scope: !1306)
!1338 = !DILocation(line: 1223, column: 5, scope: !1306)
!1339 = !DILocation(line: 1226, column: 5, scope: !1306)
!1340 = !DILocation(line: 1227, column: 22, scope: !1306)
!1341 = !DILocation(line: 1228, column: 5, scope: !1306)
!1342 = !DILocation(line: 1229, column: 20, scope: !1343)
!1343 = distinct !DILexicalBlock(scope: !1306, file: !6, line: 1229, column: 9)
!1344 = !DILocation(line: 0, scope: !1343)
!1345 = !DILocation(line: 1236, column: 5, scope: !1306)
!1346 = !DILocation(line: 1237, column: 5, scope: !1306)
!1347 = !DILocation(line: 1238, column: 5, scope: !1306)
!1348 = !DILocation(line: 1239, column: 25, scope: !1306)
!1349 = !DILocation(line: 1240, column: 5, scope: !1306)
!1350 = !DILocation(line: 1241, column: 23, scope: !1351)
!1351 = distinct !DILexicalBlock(scope: !1306, file: !6, line: 1241, column: 9)
!1352 = !DILocation(line: 1241, column: 9, scope: !1306)
!1353 = !DILocation(line: 0, scope: !1351)
!1354 = !DILocation(line: 1248, column: 5, scope: !1306)
!1355 = !DILocation(line: 1249, column: 28, scope: !1356)
!1356 = distinct !DILexicalBlock(scope: !1306, file: !6, line: 1249, column: 9)
!1357 = !DILocation(line: 1250, column: 9, scope: !1358)
!1358 = distinct !DILexicalBlock(scope: !1356, file: !6, line: 1249, column: 71)
!1359 = !DILocation(line: 1251, column: 9, scope: !1358)
!1360 = !DILocation(line: 1252, column: 9, scope: !1358)
!1361 = !DILocation(line: 1253, column: 9, scope: !1358)
!1362 = !DILocation(line: 1254, column: 9, scope: !1358)
!1363 = !DILocation(line: 1256, column: 5, scope: !1358)
!1364 = !DILocation(line: 0, scope: !1356)
!1365 = !DILocation(line: 1259, column: 5, scope: !1306)
!1366 = !DILocation(line: 1260, column: 1, scope: !1306)
!1367 = distinct !DISubprogram(name: "add_function", scope: !6, file: !6, line: 1387, type: !62, scopeLine: 1387, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1368)
!1368 = !{!1369, !1370}
!1369 = !DILocalVariable(name: "a", arg: 1, scope: !1367, file: !6, line: 1387, type: !24)
!1370 = !DILocalVariable(name: "b", arg: 2, scope: !1367, file: !6, line: 1387, type: !24)
!1371 = !DILocation(line: 0, scope: !1367)
!1372 = !DILocation(line: 1388, column: 5, scope: !1367)
!1373 = !DILocation(line: 1389, column: 14, scope: !1367)
!1374 = !DILocation(line: 1389, column: 5, scope: !1367)
!1375 = distinct !DISubprogram(name: "counter_task", scope: !6, file: !6, line: 128, type: !1376, scopeLine: 129, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1378)
!1376 = !DISubroutineType(types: !1377)
!1377 = !{null, !55}
!1378 = !{!1379, !1380}
!1379 = !DILocalVariable(name: "pvParameter", arg: 1, scope: !1375, file: !6, line: 128, type: !55)
!1380 = !DILocalVariable(name: "num", scope: !1375, file: !6, line: 130, type: !24)
!1381 = !DILocation(line: 0, scope: !1375)
!1382 = !DILocation(line: 131, column: 5, scope: !1375)
!1383 = !DILocation(line: 132, column: 5, scope: !1375)
!1384 = !DILocation(line: 133, column: 36, scope: !1385)
!1385 = distinct !DILexicalBlock(scope: !1375, file: !6, line: 132, column: 15)
!1386 = !DILocation(line: 133, column: 9, scope: !1385)
!1387 = !DILocation(line: 134, column: 9, scope: !1385)
!1388 = distinct !{!1388, !1383, !1389, !1179}
!1389 = !DILocation(line: 135, column: 5, scope: !1375)
!1390 = distinct !DISubprogram(name: "call_volatile_function", scope: !6, file: !6, line: 216, type: !1391, scopeLine: 216, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1393)
!1391 = !DISubroutineType(types: !1392)
!1392 = !{!23, !20, !24, !24}
!1393 = !{!1394, !1395, !1396}
!1394 = !DILocalVariable(name: "func", arg: 1, scope: !1390, file: !6, line: 216, type: !20)
!1395 = !DILocalVariable(name: "x", arg: 2, scope: !1390, file: !6, line: 216, type: !24)
!1396 = !DILocalVariable(name: "y", arg: 3, scope: !1390, file: !6, line: 216, type: !24)
!1397 = !DILocation(line: 0, scope: !1390)
!1398 = !DILocation(line: 217, column: 5, scope: !1390)
!1399 = !DILocation(line: 218, column: 12, scope: !1390)
!1400 = !DILocation(line: 218, column: 5, scope: !1390)
!1401 = distinct !DISubprogram(name: "multiply_function_1", scope: !6, file: !6, line: 1179, type: !62, scopeLine: 1179, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1402)
!1402 = !{!1403, !1404}
!1403 = !DILocalVariable(name: "a", arg: 1, scope: !1401, file: !6, line: 1179, type: !24)
!1404 = !DILocalVariable(name: "b", arg: 2, scope: !1401, file: !6, line: 1179, type: !24)
!1405 = !DILocation(line: 0, scope: !1401)
!1406 = !DILocation(line: 1180, column: 5, scope: !1401)
!1407 = !DILocation(line: 1181, column: 14, scope: !1401)
!1408 = !DILocation(line: 1181, column: 5, scope: !1401)
!1409 = distinct !DISubprogram(name: "test_call_indirect_non_optimizable", scope: !6, file: !6, line: 223, type: !1181, scopeLine: 223, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1410)
!1410 = !{!1411, !1412, !1413, !1414}
!1411 = !DILocalVariable(name: "result1", scope: !1409, file: !6, line: 227, type: !24)
!1412 = !DILocalVariable(name: "result2", scope: !1409, file: !6, line: 230, type: !24)
!1413 = !DILocalVariable(name: "volatile_func_ptr", scope: !1409, file: !6, line: 235, type: !20)
!1414 = !DILocalVariable(name: "result3", scope: !1409, file: !6, line: 236, type: !24)
!1415 = !DILocation(line: 224, column: 5, scope: !1409)
!1416 = !DILocation(line: 227, column: 19, scope: !1409)
!1417 = !DILocation(line: 0, scope: !1409)
!1418 = !DILocation(line: 228, column: 5, scope: !1409)
!1419 = !DILocation(line: 230, column: 19, scope: !1409)
!1420 = !DILocation(line: 231, column: 5, scope: !1409)
!1421 = !DILocation(line: 236, column: 19, scope: !1409)
!1422 = !DILocation(line: 237, column: 5, scope: !1409)
!1423 = !DILocation(line: 239, column: 5, scope: !1409)
!1424 = !DILocation(line: 240, column: 1, scope: !1409)
!1425 = distinct !DISubprogram(name: "multiply_function", scope: !6, file: !6, line: 1393, type: !62, scopeLine: 1393, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1426)
!1426 = !{!1427, !1428}
!1427 = !DILocalVariable(name: "a", arg: 1, scope: !1425, file: !6, line: 1393, type: !24)
!1428 = !DILocalVariable(name: "b", arg: 2, scope: !1425, file: !6, line: 1393, type: !24)
!1429 = !DILocation(line: 0, scope: !1425)
!1430 = !DILocation(line: 1394, column: 5, scope: !1425)
!1431 = !DILocation(line: 1395, column: 14, scope: !1425)
!1432 = !DILocation(line: 1395, column: 5, scope: !1425)
!1433 = distinct !DISubprogram(name: "test_while_and_do_while_loops", scope: !6, file: !6, line: 1005, type: !1181, scopeLine: 1005, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1434)
!1434 = !{!1435, !1436, !1437, !1438, !1439, !1440}
!1435 = !DILocalVariable(name: "while_counter", scope: !1433, file: !6, line: 1012, type: !24)
!1436 = !DILocalVariable(name: "while_sum", scope: !1433, file: !6, line: 1013, type: !24)
!1437 = !DILocalVariable(name: "zero_trip_counter", scope: !1433, file: !6, line: 1029, type: !24)
!1438 = !DILocalVariable(name: "do_while_counter", scope: !1433, file: !6, line: 1045, type: !24)
!1439 = !DILocalVariable(name: "do_while_sum", scope: !1433, file: !6, line: 1046, type: !24)
!1440 = !DILocalVariable(name: "one_trip_counter", scope: !1433, file: !6, line: 1062, type: !24)
!1441 = !DILocation(line: 1006, column: 5, scope: !1433)
!1442 = !DILocation(line: 1009, column: 5, scope: !1433)
!1443 = !DILocation(line: 0, scope: !1433)
!1444 = !DILocation(line: 1014, column: 5, scope: !1433)
!1445 = !DILocation(line: 1015, column: 5, scope: !1433)
!1446 = !DILocation(line: 1016, column: 19, scope: !1447)
!1447 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1015, column: 31)
!1448 = !DILocation(line: 1017, column: 9, scope: !1447)
!1449 = !DILocation(line: 1018, column: 22, scope: !1447)
!1450 = !DILocation(line: 1015, column: 26, scope: !1433)
!1451 = distinct !{!1451, !1445, !1452, !1178, !1179}
!1452 = !DILocation(line: 1019, column: 5, scope: !1433)
!1453 = !DILocation(line: 1020, column: 5, scope: !1433)
!1454 = !DILocation(line: 1022, column: 9, scope: !1455)
!1455 = distinct !DILexicalBlock(scope: !1456, file: !6, line: 1021, column: 26)
!1456 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1021, column: 9)
!1457 = !DILocation(line: 1028, column: 5, scope: !1433)
!1458 = !DILocation(line: 1036, column: 9, scope: !1459)
!1459 = distinct !DILexicalBlock(scope: !1460, file: !6, line: 1035, column: 34)
!1460 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1035, column: 9)
!1461 = !DILocation(line: 1042, column: 5, scope: !1433)
!1462 = !DILocation(line: 1047, column: 5, scope: !1433)
!1463 = !DILocation(line: 1048, column: 5, scope: !1433)
!1464 = !DILocation(line: 1049, column: 22, scope: !1465)
!1465 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1048, column: 8)
!1466 = !DILocation(line: 1050, column: 9, scope: !1465)
!1467 = !DILocation(line: 1051, column: 25, scope: !1465)
!1468 = !DILocation(line: 1052, column: 31, scope: !1433)
!1469 = !DILocation(line: 1052, column: 5, scope: !1465)
!1470 = distinct !{!1470, !1463, !1471, !1178, !1179}
!1471 = !DILocation(line: 1052, column: 34, scope: !1433)
!1472 = !DILocation(line: 1053, column: 5, scope: !1433)
!1473 = !DILocation(line: 1055, column: 9, scope: !1474)
!1474 = distinct !DILexicalBlock(scope: !1475, file: !6, line: 1054, column: 29)
!1475 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1054, column: 9)
!1476 = !DILocation(line: 1061, column: 5, scope: !1433)
!1477 = !DILocation(line: 1064, column: 9, scope: !1478)
!1478 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1063, column: 8)
!1479 = !DILocation(line: 1068, column: 5, scope: !1433)
!1480 = !DILocation(line: 1070, column: 9, scope: !1481)
!1481 = distinct !DILexicalBlock(scope: !1482, file: !6, line: 1069, column: 33)
!1482 = distinct !DILexicalBlock(scope: !1433, file: !6, line: 1069, column: 9)
!1483 = !DILocation(line: 1075, column: 5, scope: !1433)
!1484 = !DILocation(line: 1076, column: 1, scope: !1433)
!1485 = distinct !DISubprogram(name: "test_break_and_continue", scope: !6, file: !6, line: 1086, type: !1181, scopeLine: 1086, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1486)
!1486 = !{!1487, !1488, !1490, !1491, !1492, !1493, !1495}
!1487 = !DILocalVariable(name: "continue_sum", scope: !1485, file: !6, line: 1093, type: !24)
!1488 = !DILocalVariable(name: "i", scope: !1489, file: !6, line: 1094, type: !24)
!1489 = distinct !DILexicalBlock(scope: !1485, file: !6, line: 1094, column: 5)
!1490 = !DILocalVariable(name: "break_counter", scope: !1485, file: !6, line: 1114, type: !24)
!1491 = !DILocalVariable(name: "outer_loop_runs", scope: !1485, file: !6, line: 1132, type: !24)
!1492 = !DILocalVariable(name: "total_inner_loop_runs", scope: !1485, file: !6, line: 1133, type: !24)
!1493 = !DILocalVariable(name: "i", scope: !1494, file: !6, line: 1134, type: !24)
!1494 = distinct !DILexicalBlock(scope: !1485, file: !6, line: 1134, column: 5)
!1495 = !DILocalVariable(name: "j", scope: !1496, file: !6, line: 1136, type: !24)
!1496 = distinct !DILexicalBlock(scope: !1497, file: !6, line: 1136, column: 9)
!1497 = distinct !DILexicalBlock(scope: !1498, file: !6, line: 1134, column: 33)
!1498 = distinct !DILexicalBlock(scope: !1494, file: !6, line: 1134, column: 5)
!1499 = !DILocation(line: 1087, column: 5, scope: !1485)
!1500 = !DILocation(line: 1091, column: 5, scope: !1485)
!1501 = !DILocation(line: 1092, column: 5, scope: !1485)
!1502 = !DILocation(line: 0, scope: !1485)
!1503 = !DILocation(line: 0, scope: !1489)
!1504 = !DILocation(line: 1103, column: 5, scope: !1485)
!1505 = !DILocation(line: 1105, column: 9, scope: !1506)
!1506 = distinct !DILexicalBlock(scope: !1507, file: !6, line: 1104, column: 29)
!1507 = distinct !DILexicalBlock(scope: !1485, file: !6, line: 1104, column: 9)
!1508 = !DILocation(line: 1112, column: 5, scope: !1485)
!1509 = !DILocation(line: 1113, column: 5, scope: !1485)
!1510 = !DILocation(line: 1122, column: 5, scope: !1485)
!1511 = !DILocation(line: 1124, column: 9, scope: !1512)
!1512 = distinct !DILexicalBlock(scope: !1513, file: !6, line: 1123, column: 29)
!1513 = distinct !DILexicalBlock(scope: !1485, file: !6, line: 1123, column: 9)
!1514 = !DILocation(line: 1131, column: 5, scope: !1485)
!1515 = !DILocation(line: 0, scope: !1494)
!1516 = !DILocation(line: 1146, column: 5, scope: !1485)
!1517 = !DILocation(line: 1147, column: 5, scope: !1485)
!1518 = !DILocation(line: 1150, column: 9, scope: !1519)
!1519 = distinct !DILexicalBlock(scope: !1520, file: !6, line: 1149, column: 61)
!1520 = distinct !DILexicalBlock(scope: !1485, file: !6, line: 1149, column: 9)
!1521 = !DILocation(line: 1155, column: 5, scope: !1485)
!1522 = !DILocation(line: 1156, column: 1, scope: !1485)
!1523 = distinct !DISubprogram(name: "subtract_function", scope: !6, file: !6, line: 1399, type: !62, scopeLine: 1399, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1524)
!1524 = !{!1525, !1526}
!1525 = !DILocalVariable(name: "a", arg: 1, scope: !1523, file: !6, line: 1399, type: !24)
!1526 = !DILocalVariable(name: "b", arg: 2, scope: !1523, file: !6, line: 1399, type: !24)
!1527 = !DILocation(line: 0, scope: !1523)
!1528 = !DILocation(line: 1400, column: 5, scope: !1523)
!1529 = !DILocation(line: 1401, column: 14, scope: !1523)
!1530 = !DILocation(line: 1401, column: 5, scope: !1523)
!1531 = distinct !DISubprogram(name: "test_call_indirect_ptr_unsafe", scope: !6, file: !6, line: 1419, type: !1181, scopeLine: 1419, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1532)
!1532 = !{!1533, !1534, !1535, !1536, !1537, !1538}
!1533 = !DILocalVariable(name: "addr_add", scope: !1531, file: !6, line: 1427, type: !55)
!1534 = !DILocalVariable(name: "addr_mul", scope: !1531, file: !6, line: 1428, type: !55)
!1535 = !DILocalVariable(name: "addr_sub", scope: !1531, file: !6, line: 1429, type: !55)
!1536 = !DILocalVariable(name: "result1", scope: !1531, file: !6, line: 1440, type: !24)
!1537 = !DILocalVariable(name: "result2", scope: !1531, file: !6, line: 1444, type: !24)
!1538 = !DILocalVariable(name: "result3", scope: !1531, file: !6, line: 1448, type: !24)
!1539 = !DILocation(line: 1420, column: 5, scope: !1531)
!1540 = !DILocation(line: 1421, column: 5, scope: !1531)
!1541 = !DILocation(line: 1424, column: 5, scope: !1531)
!1542 = !DILocation(line: 0, scope: !1531)
!1543 = !DILocation(line: 1431, column: 5, scope: !1531)
!1544 = !DILocation(line: 1432, column: 5, scope: !1531)
!1545 = !DILocation(line: 1433, column: 5, scope: !1531)
!1546 = !DILocation(line: 1434, column: 5, scope: !1531)
!1547 = !DILocation(line: 1436, column: 5, scope: !1531)
!1548 = !DILocation(line: 1439, column: 5, scope: !1531)
!1549 = !DILocation(line: 1440, column: 19, scope: !1531)
!1550 = !DILocation(line: 1441, column: 5, scope: !1531)
!1551 = !DILocation(line: 1443, column: 5, scope: !1531)
!1552 = !DILocation(line: 1444, column: 19, scope: !1531)
!1553 = !DILocation(line: 1445, column: 5, scope: !1531)
!1554 = !DILocation(line: 1447, column: 5, scope: !1531)
!1555 = !DILocation(line: 1448, column: 19, scope: !1531)
!1556 = !DILocation(line: 1449, column: 5, scope: !1531)
!1557 = !DILocation(line: 1452, column: 5, scope: !1531)
!1558 = !DILocation(line: 1453, column: 17, scope: !1559)
!1559 = distinct !DILexicalBlock(scope: !1531, file: !6, line: 1453, column: 9)
!1560 = !DILocation(line: 1453, column: 23, scope: !1559)
!1561 = !DILocation(line: 1456, column: 9, scope: !1562)
!1562 = distinct !DILexicalBlock(scope: !1559, file: !6, line: 1455, column: 12)
!1563 = !DILocation(line: 1457, column: 9, scope: !1562)
!1564 = !DILocation(line: 0, scope: !1559)
!1565 = !DILocation(line: 1461, column: 5, scope: !1531)
!1566 = !DILocation(line: 1462, column: 1, scope: !1531)
!1567 = distinct !DISubprogram(name: "test_complex_data_types", scope: !6, file: !6, line: 884, type: !1181, scopeLine: 884, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1568)
!1568 = !{!1569, !1571, !1573, !1576, !1584, !1586, !1594, !1596, !1598, !1600}
!1569 = !DILocalVariable(name: "simple_array", scope: !1567, file: !6, line: 889, type: !1570)
!1570 = !DICompositeType(tag: DW_TAG_array_type, baseType: !24, size: 160, elements: !166)
!1571 = !DILocalVariable(name: "i", scope: !1572, file: !6, line: 891, type: !24)
!1572 = distinct !DILexicalBlock(scope: !1567, file: !6, line: 891, column: 5)
!1573 = !DILocalVariable(name: "matrix", scope: !1567, file: !6, line: 903, type: !1574)
!1574 = !DICompositeType(tag: DW_TAG_array_type, baseType: !24, size: 192, elements: !1575)
!1575 = !{!600, !628}
!1576 = !DILocalVariable(name: "sensor1", scope: !1567, file: !6, line: 909, type: !1577)
!1577 = !DIDerivedType(tag: DW_TAG_typedef, name: "SensorData", file: !6, line: 835, baseType: !1578)
!1578 = distinct !DICompositeType(tag: DW_TAG_structure_type, file: !6, line: 830, size: 224, elements: !1579)
!1579 = !{!1580, !1581, !1582, !1583}
!1580 = !DIDerivedType(tag: DW_TAG_member, name: "id", scope: !1578, file: !6, line: 831, baseType: !25, size: 32)
!1581 = !DIDerivedType(tag: DW_TAG_member, name: "temperature", scope: !1578, file: !6, line: 832, baseType: !27, size: 32, offset: 32)
!1582 = !DIDerivedType(tag: DW_TAG_member, name: "name", scope: !1578, file: !6, line: 833, baseType: !603, size: 128, offset: 64)
!1583 = !DIDerivedType(tag: DW_TAG_member, name: "is_active", scope: !1578, file: !6, line: 834, baseType: !70, size: 8, offset: 192)
!1584 = !DILocalVariable(name: "p_sensor", scope: !1567, file: !6, line: 919, type: !1585)
!1585 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1577, size: 32)
!1586 = !DILocalVariable(name: "converter", scope: !1567, file: !6, line: 931, type: !1587)
!1587 = !DIDerivedType(tag: DW_TAG_typedef, name: "DataConverter", file: !6, line: 842, baseType: !1588)
!1588 = distinct !DICompositeType(tag: DW_TAG_union_type, file: !6, line: 838, size: 32, elements: !1589)
!1589 = !{!1590, !1591, !1592}
!1590 = !DIDerivedType(tag: DW_TAG_member, name: "i_val", scope: !1588, file: !6, line: 839, baseType: !25, size: 32)
!1591 = !DIDerivedType(tag: DW_TAG_member, name: "f_val", scope: !1588, file: !6, line: 840, baseType: !27, size: 32)
!1592 = !DIDerivedType(tag: DW_TAG_member, name: "bytes", scope: !1588, file: !6, line: 841, baseType: !1593, size: 32)
!1593 = !DICompositeType(tag: DW_TAG_array_type, baseType: !45, size: 32, elements: !594)
!1594 = !DILocalVariable(name: "current_state", scope: !1567, file: !6, line: 949, type: !1595)
!1595 = !DIDerivedType(tag: DW_TAG_typedef, name: "DeviceState", file: !6, line: 850, baseType: !5)
!1596 = !DILocalVariable(name: "sensor_inventory", scope: !1567, file: !6, line: 963, type: !1597)
!1597 = !DICompositeType(tag: DW_TAG_array_type, baseType: !1577, size: 448, elements: !599)
!1598 = !DILocalVariable(name: "i", scope: !1599, file: !6, line: 968, type: !24)
!1599 = distinct !DILexicalBlock(scope: !1567, file: !6, line: 968, column: 5)
!1600 = !DILocalVariable(name: "status", scope: !1567, file: !6, line: 975, type: !1601)
!1601 = !DIDerivedType(tag: DW_TAG_typedef, name: "DeviceStatus", file: !6, line: 858, baseType: !1602)
!1602 = distinct !DICompositeType(tag: DW_TAG_structure_type, file: !6, line: 853, size: 8, elements: !1603)
!1603 = !{!1604, !1605, !1606, !1607}
!1604 = !DIDerivedType(tag: DW_TAG_member, name: "power_on", scope: !1602, file: !6, line: 854, baseType: !45, size: 1, flags: DIFlagBitField, extraData: i64 0)
!1605 = !DIDerivedType(tag: DW_TAG_member, name: "wifi_connected", scope: !1602, file: !6, line: 855, baseType: !45, size: 1, offset: 1, flags: DIFlagBitField, extraData: i64 0)
!1606 = !DIDerivedType(tag: DW_TAG_member, name: "error_code", scope: !1602, file: !6, line: 856, baseType: !45, size: 4, offset: 2, flags: DIFlagBitField, extraData: i64 0)
!1607 = !DIDerivedType(tag: DW_TAG_member, name: "reserved", scope: !1602, file: !6, line: 857, baseType: !45, size: 2, offset: 6, flags: DIFlagBitField, extraData: i64 0)
!1608 = distinct !DIAssignID()
!1609 = !DILocation(line: 0, scope: !1567)
!1610 = distinct !DIAssignID()
!1611 = distinct !DIAssignID()
!1612 = !DILocation(line: 885, column: 5, scope: !1567)
!1613 = !DILocation(line: 888, column: 5, scope: !1567)
!1614 = !DILocation(line: 889, column: 5, scope: !1567)
!1615 = !DILocation(line: 889, column: 9, scope: !1567)
!1616 = distinct !DIAssignID()
!1617 = distinct !DIAssignID()
!1618 = distinct !DIAssignID()
!1619 = distinct !DIAssignID()
!1620 = distinct !DIAssignID()
!1621 = !DILocation(line: 890, column: 5, scope: !1567)
!1622 = !DILocation(line: 0, scope: !1572)
!1623 = !DILocation(line: 891, column: 5, scope: !1572)
!1624 = !DILocation(line: 894, column: 5, scope: !1567)
!1625 = !DILocation(line: 896, column: 21, scope: !1567)
!1626 = distinct !DIAssignID()
!1627 = !DILocation(line: 897, column: 5, scope: !1567)
!1628 = !DILocation(line: 900, column: 5, scope: !1567)
!1629 = !DILocation(line: 904, column: 5, scope: !1567)
!1630 = !DILocation(line: 908, column: 5, scope: !1567)
!1631 = !DILocation(line: 909, column: 5, scope: !1567)
!1632 = !DILocation(line: 910, column: 16, scope: !1567)
!1633 = !{!1634, !1171, i64 0}
!1634 = !{!"", !1171, i64 0, !1226, i64 4, !1172, i64 8, !1635, i64 24}
!1635 = !{!"_Bool", !1172, i64 0}
!1636 = distinct !DIAssignID()
!1637 = !DILocation(line: 911, column: 13, scope: !1567)
!1638 = !DILocation(line: 911, column: 25, scope: !1567)
!1639 = !{!1634, !1226, i64 4}
!1640 = distinct !DIAssignID()
!1641 = !DILocation(line: 912, column: 20, scope: !1567)
!1642 = !DILocation(line: 912, column: 5, scope: !1567)
!1643 = !DILocation(line: 913, column: 13, scope: !1567)
!1644 = !DILocation(line: 913, column: 23, scope: !1567)
!1645 = !{!1634, !1635, i64 24}
!1646 = distinct !DIAssignID()
!1647 = !DILocation(line: 915, column: 5, scope: !1567)
!1648 = !DILocation(line: 920, column: 27, scope: !1567)
!1649 = !DILocation(line: 921, column: 5, scope: !1567)
!1650 = !DILocation(line: 924, column: 5, scope: !1567)
!1651 = !DILocation(line: 926, column: 34, scope: !1567)
!1652 = !DILocation(line: 926, column: 26, scope: !1567)
!1653 = !DILocation(line: 926, column: 55, scope: !1567)
!1654 = !{i8 0, i8 2}
!1655 = !{}
!1656 = !DILocation(line: 926, column: 47, scope: !1567)
!1657 = !DILocation(line: 925, column: 5, scope: !1567)
!1658 = !DILocation(line: 930, column: 5, scope: !1567)
!1659 = !DILocation(line: 932, column: 5, scope: !1567)
!1660 = !DILocation(line: 936, column: 5, scope: !1567)
!1661 = !DILocation(line: 937, column: 5, scope: !1567)
!1662 = !DILocation(line: 938, column: 5, scope: !1567)
!1663 = !DILocation(line: 943, column: 5, scope: !1567)
!1664 = !DILocation(line: 944, column: 5, scope: !1567)
!1665 = !DILocation(line: 948, column: 5, scope: !1567)
!1666 = !DILocation(line: 950, column: 5, scope: !1567)
!1667 = !DILocation(line: 956, column: 28, scope: !1668)
!1668 = distinct !DILexicalBlock(scope: !1567, file: !6, line: 953, column: 28)
!1669 = !DILocation(line: 962, column: 5, scope: !1567)
!1670 = !DILocation(line: 963, column: 5, scope: !1567)
!1671 = !DILocation(line: 964, column: 27, scope: !1567)
!1672 = !{i64 0, i64 4, !1170, i64 4, i64 4, !1225, i64 8, i64 16, !1235, i64 24, i64 1, !1673}
!1673 = !{!1635, !1635, i64 0}
!1674 = distinct !DIAssignID()
!1675 = !{i64 0, i64 4, !1225, i64 4, i64 16, !1235, i64 20, i64 1, !1673}
!1676 = distinct !DIAssignID()
!1677 = !{i64 0, i64 16, !1235, i64 16, i64 1, !1673}
!1678 = distinct !DIAssignID()
!1679 = !{i64 0, i64 1, !1673}
!1680 = distinct !DIAssignID()
!1681 = distinct !DIAssignID()
!1682 = !DILocation(line: 965, column: 5, scope: !1567)
!1683 = !DILocation(line: 965, column: 27, scope: !1567)
!1684 = distinct !DIAssignID()
!1685 = distinct !DIAssignID()
!1686 = distinct !DIAssignID()
!1687 = distinct !DIAssignID()
!1688 = distinct !DIAssignID()
!1689 = !DILocation(line: 967, column: 5, scope: !1567)
!1690 = !DILocation(line: 0, scope: !1599)
!1691 = !DILocation(line: 968, column: 5, scope: !1599)
!1692 = !DILocation(line: 892, column: 23, scope: !1693)
!1693 = distinct !DILexicalBlock(scope: !1694, file: !6, line: 891, column: 33)
!1694 = distinct !DILexicalBlock(scope: !1572, file: !6, line: 891, column: 5)
!1695 = !DILocation(line: 892, column: 9, scope: !1693)
!1696 = !DILocation(line: 891, column: 29, scope: !1694)
!1697 = !DILocation(line: 891, column: 23, scope: !1694)
!1698 = distinct !{!1698, !1623, !1699, !1178, !1179}
!1699 = !DILocation(line: 893, column: 5, scope: !1572)
!1700 = !DILocation(line: 974, column: 5, scope: !1567)
!1701 = !DILocation(line: 976, column: 5, scope: !1567)
!1702 = !DILocation(line: 984, column: 5, scope: !1567)
!1703 = !DILocation(line: 988, column: 9, scope: !1704)
!1704 = distinct !DILexicalBlock(scope: !1705, file: !6, line: 987, column: 52)
!1705 = distinct !DILexicalBlock(scope: !1567, file: !6, line: 987, column: 9)
!1706 = !DILocation(line: 993, column: 5, scope: !1567)
!1707 = !DILocation(line: 994, column: 1, scope: !1567)
!1708 = !DILocation(line: 969, column: 53, scope: !1709)
!1709 = distinct !DILexicalBlock(scope: !1710, file: !6, line: 968, column: 33)
!1710 = distinct !DILexicalBlock(scope: !1599, file: !6, line: 968, column: 5)
!1711 = !DILocation(line: 969, column: 73, scope: !1709)
!1712 = !DILocation(line: 969, column: 97, scope: !1709)
!1713 = !DILocation(line: 969, column: 9, scope: !1709)
!1714 = !DILocation(line: 968, column: 29, scope: !1710)
!1715 = !DILocation(line: 968, column: 23, scope: !1710)
!1716 = distinct !{!1716, !1691, !1717, !1178, !1179}
!1717 = !DILocation(line: 970, column: 5, scope: !1599)
!1718 = distinct !DISubprogram(name: "mul_func", scope: !6, file: !6, line: 153, type: !62, scopeLine: 153, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1719)
!1719 = !{!1720, !1721}
!1720 = !DILocalVariable(name: "a", arg: 1, scope: !1718, file: !6, line: 153, type: !24)
!1721 = !DILocalVariable(name: "b", arg: 2, scope: !1718, file: !6, line: 153, type: !24)
!1722 = !DILocation(line: 0, scope: !1718)
!1723 = !DILocation(line: 154, column: 55, scope: !1718)
!1724 = !DILocation(line: 154, column: 5, scope: !1718)
!1725 = !DILocation(line: 155, column: 5, scope: !1718)
!1726 = distinct !DISubprogram(name: "cb_with_int", scope: !6, file: !6, line: 112, type: !1727, scopeLine: 112, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1729)
!1727 = !DISubroutineType(types: !1728)
!1728 = !{null, !24, !55}
!1729 = !{!1730, !1731}
!1730 = !DILocalVariable(name: "x", arg: 1, scope: !1726, file: !6, line: 112, type: !24)
!1731 = !DILocalVariable(name: "ud", arg: 2, scope: !1726, file: !6, line: 112, type: !55)
!1732 = !DILocation(line: 0, scope: !1726)
!1733 = !DILocation(line: 113, column: 5, scope: !1726)
!1734 = !DILocation(line: 114, column: 1, scope: !1726)
!1735 = distinct !DISubprogram(name: "modify_sensor_by_ref", scope: !6, file: !6, line: 877, type: !1736, scopeLine: 877, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1738)
!1736 = !DISubroutineType(types: !1737)
!1737 = !{null, !1585}
!1738 = !{!1739}
!1739 = !DILocalVariable(name: "sensor", arg: 1, scope: !1735, file: !6, line: 877, type: !1585)
!1740 = !DILocation(line: 0, scope: !1735)
!1741 = !DILocation(line: 878, column: 13, scope: !1735)
!1742 = !DILocation(line: 878, column: 25, scope: !1735)
!1743 = !DILocation(line: 879, column: 20, scope: !1735)
!1744 = !DILocation(line: 879, column: 5, scope: !1735)
!1745 = !DILocation(line: 880, column: 13, scope: !1735)
!1746 = !DILocation(line: 880, column: 23, scope: !1735)
!1747 = !DILocation(line: 881, column: 1, scope: !1735)
!1748 = distinct !DISubprogram(name: "call_function_by_address", scope: !6, file: !6, line: 1407, type: !1749, scopeLine: 1407, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1751)
!1749 = !DISubroutineType(types: !1750)
!1750 = !{!24, !55, !24, !24}
!1751 = !{!1752, !1753, !1754, !1755}
!1752 = !DILocalVariable(name: "func_addr", arg: 1, scope: !1748, file: !6, line: 1407, type: !55)
!1753 = !DILocalVariable(name: "arg1", arg: 2, scope: !1748, file: !6, line: 1407, type: !24)
!1754 = !DILocalVariable(name: "arg2", arg: 3, scope: !1748, file: !6, line: 1407, type: !24)
!1755 = !DILocalVariable(name: "unsafe_func", scope: !1748, file: !6, line: 1412, type: !61)
!1756 = !DILocation(line: 1407, column: 36, scope: !1748)
!1757 = !DILocation(line: 1407, column: 51, scope: !1748)
!1758 = !DILocation(line: 1407, column: 61, scope: !1748)
!1759 = !DILocation(line: 1408, column: 72, scope: !1748)
!1760 = !DILocation(line: 1408, column: 83, scope: !1748)
!1761 = !DILocation(line: 1408, column: 89, scope: !1748)
!1762 = !DILocation(line: 1408, column: 5, scope: !1748)
!1763 = !DILocation(line: 1412, column: 5, scope: !1748)
!1764 = !DILocation(line: 1412, column: 11, scope: !1748)
!1765 = !DILocation(line: 1412, column: 54, scope: !1748)
!1766 = !DILocation(line: 1414, column: 12, scope: !1748)
!1767 = !DILocation(line: 1414, column: 24, scope: !1748)
!1768 = !DILocation(line: 1414, column: 30, scope: !1748)
!1769 = !DILocation(line: 1415, column: 1, scope: !1748)
!1770 = !DILocation(line: 1414, column: 5, scope: !1748)
!1771 = distinct !DISubprogram(name: "add_func", scope: !6, file: !6, line: 146, type: !62, scopeLine: 146, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1772)
!1772 = !{!1773, !1774}
!1773 = !DILocalVariable(name: "a", arg: 1, scope: !1771, file: !6, line: 146, type: !24)
!1774 = !DILocalVariable(name: "b", arg: 2, scope: !1771, file: !6, line: 146, type: !24)
!1775 = !DILocation(line: 0, scope: !1771)
!1776 = !DILocation(line: 147, column: 55, scope: !1771)
!1777 = !DILocation(line: 147, column: 5, scope: !1771)
!1778 = !DILocation(line: 148, column: 5, scope: !1771)
!1779 = distinct !DISubprogram(name: "call_operation_by_index", scope: !6, file: !6, line: 207, type: !1780, scopeLine: 207, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1782)
!1780 = !DISubroutineType(types: !1781)
!1781 = !{!24, !24, !24, !24}
!1782 = !{!1783, !1784, !1785}
!1783 = !DILocalVariable(name: "op_index", arg: 1, scope: !1779, file: !6, line: 207, type: !24)
!1784 = !DILocalVariable(name: "x", arg: 2, scope: !1779, file: !6, line: 207, type: !24)
!1785 = !DILocalVariable(name: "y", arg: 3, scope: !1779, file: !6, line: 207, type: !24)
!1786 = !DILocation(line: 0, scope: !1779)
!1787 = !DILocation(line: 208, column: 5, scope: !1779)
!1788 = !DILocation(line: 211, column: 32, scope: !1779)
!1789 = !DILocation(line: 211, column: 12, scope: !1779)
!1790 = !DILocation(line: 211, column: 5, scope: !1779)
!1791 = distinct !DISubprogram(name: "test_control_flow", scope: !6, file: !6, line: 468, type: !1181, scopeLine: 468, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1792)
!1792 = !{!1793, !1794, !1795, !1796, !1797, !1798, !1800, !1802}
!1793 = !DILocalVariable(name: "a", scope: !1791, file: !6, line: 473, type: !24)
!1794 = !DILocalVariable(name: "b", scope: !1791, file: !6, line: 474, type: !24)
!1795 = !DILocalVariable(name: "d1", scope: !1791, file: !6, line: 494, type: !42)
!1796 = !DILocalVariable(name: "d2", scope: !1791, file: !6, line: 495, type: !42)
!1797 = !DILocalVariable(name: "sum", scope: !1791, file: !6, line: 504, type: !24)
!1798 = !DILocalVariable(name: "i", scope: !1799, file: !6, line: 505, type: !24)
!1799 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 505, column: 5)
!1800 = !DILocalVariable(name: "i", scope: !1801, file: !6, line: 518, type: !24)
!1801 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 518, column: 5)
!1802 = !DILabel(scope: !1791, name: "skip_label", file: !6, line: 540)
!1803 = !DILocation(line: 469, column: 5, scope: !1791)
!1804 = !DILocation(line: 472, column: 5, scope: !1791)
!1805 = !DILocation(line: 0, scope: !1791)
!1806 = !DILocation(line: 477, column: 9, scope: !1807)
!1807 = distinct !DILexicalBlock(scope: !1808, file: !6, line: 476, column: 16)
!1808 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 476, column: 9)
!1809 = !DILocation(line: 485, column: 9, scope: !1810)
!1810 = distinct !DILexicalBlock(scope: !1811, file: !6, line: 484, column: 12)
!1811 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 482, column: 9)
!1812 = !DILocation(line: 489, column: 9, scope: !1813)
!1813 = distinct !DILexicalBlock(scope: !1814, file: !6, line: 488, column: 18)
!1814 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 488, column: 9)
!1815 = !DILocation(line: 497, column: 9, scope: !1816)
!1816 = distinct !DILexicalBlock(scope: !1817, file: !6, line: 496, column: 18)
!1817 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 496, column: 9)
!1818 = !DILocation(line: 503, column: 5, scope: !1791)
!1819 = !DILocation(line: 0, scope: !1799)
!1820 = !DILocation(line: 505, column: 5, scope: !1799)
!1821 = !DILocation(line: 509, column: 5, scope: !1791)
!1822 = !DILocation(line: 513, column: 9, scope: !1823)
!1823 = distinct !DILexicalBlock(scope: !1824, file: !6, line: 512, column: 12)
!1824 = distinct !DILexicalBlock(scope: !1791, file: !6, line: 510, column: 9)
!1825 = !DILocation(line: 517, column: 5, scope: !1791)
!1826 = !DILocation(line: 0, scope: !1801)
!1827 = !DILocation(line: 518, column: 5, scope: !1801)
!1828 = !DILocation(line: 506, column: 13, scope: !1829)
!1829 = distinct !DILexicalBlock(scope: !1830, file: !6, line: 505, column: 34)
!1830 = distinct !DILexicalBlock(scope: !1799, file: !6, line: 505, column: 5)
!1831 = !DILocation(line: 507, column: 9, scope: !1829)
!1832 = !DILocation(line: 505, column: 30, scope: !1830)
!1833 = !DILocation(line: 505, column: 23, scope: !1830)
!1834 = distinct !{!1834, !1820, !1835, !1178, !1179}
!1835 = !DILocation(line: 508, column: 5, scope: !1799)
!1836 = !DILocation(line: 537, column: 5, scope: !1791)
!1837 = !DILocation(line: 540, column: 1, scope: !1791)
!1838 = !DILocation(line: 541, column: 5, scope: !1791)
!1839 = !DILocation(line: 543, column: 5, scope: !1791)
!1840 = !DILocation(line: 544, column: 1, scope: !1791)
!1841 = !DILocation(line: 519, column: 9, scope: !1842)
!1842 = distinct !DILexicalBlock(scope: !1843, file: !6, line: 518, column: 33)
!1843 = distinct !DILexicalBlock(scope: !1801, file: !6, line: 518, column: 5)
!1844 = !DILocation(line: 520, column: 9, scope: !1842)
!1845 = !DILocation(line: 526, column: 17, scope: !1846)
!1846 = distinct !DILexicalBlock(scope: !1842, file: !6, line: 520, column: 20)
!1847 = !DILocation(line: 529, column: 17, scope: !1846)
!1848 = !DILocation(line: 532, column: 17, scope: !1846)
!1849 = !DILocation(line: 0, scope: !1846)
!1850 = !DILocation(line: 518, column: 29, scope: !1843)
!1851 = !DILocation(line: 518, column: 23, scope: !1843)
!1852 = distinct !{!1852, !1827, !1853, !1178, !1179}
!1853 = !DILocation(line: 534, column: 5, scope: !1801)
!1854 = !DILocation(line: 0, scope: !1105)
!1855 = !DILocation(line: 120, column: 16, scope: !1105)
!1856 = !DILocation(line: 122, column: 57, scope: !1105)
!1857 = !DILocation(line: 122, column: 5, scope: !1105)
!1858 = !DILocation(line: 123, column: 1, scope: !1105)
!1859 = distinct !DISubprogram(name: "test_second", scope: !6, file: !6, line: 1553, type: !1181, scopeLine: 1554, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1860)
!1860 = !{!1861, !1862, !1867}
!1861 = !DILocalVariable(name: "t", scope: !1859, file: !6, line: 1557, type: !1108)
!1862 = !DILocalVariable(name: "counter_task_handle", scope: !1859, file: !6, line: 1558, type: !1863)
!1863 = !DIDerivedType(tag: DW_TAG_typedef, name: "TaskHandle_t", file: !1864, line: 93, baseType: !1865)
!1864 = !DIFile(filename: "Programs/Espressif/frameworks/esp-idf-v5.4/components/freertos/FreeRTOS-Kernel/include/freertos/task.h", directory: "C:/Andrey")
!1865 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1866, size: 32)
!1866 = !DICompositeType(tag: DW_TAG_structure_type, name: "tskTaskControlBlock", file: !1864, line: 92, flags: DIFlagFwdDecl)
!1867 = !DILocalVariable(name: "i", scope: !1868, file: !6, line: 1592, type: !24)
!1868 = distinct !DILexicalBlock(scope: !1859, file: !6, line: 1592, column: 5)
!1869 = distinct !DIAssignID()
!1870 = !DILocation(line: 0, scope: !1859)
!1871 = !DILocation(line: 1558, column: 5, scope: !1859)
!1872 = !DILocation(line: 1558, column: 18, scope: !1859)
!1873 = distinct !DIAssignID()
!1874 = !DILocation(line: 1564, column: 5, scope: !1859)
!1875 = !DILocation(line: 1565, column: 5, scope: !1859)
!1876 = !DILocation(line: 1567, column: 5, scope: !1859)
!1877 = !DILocation(line: 1568, column: 5, scope: !1859)
!1878 = !DILocation(line: 1574, column: 5, scope: !1859)
!1879 = !DILocation(line: 1575, column: 9, scope: !1859)
!1880 = !DILocation(line: 1580, column: 9, scope: !1881)
!1881 = distinct !DILexicalBlock(scope: !1859, file: !6, line: 1580, column: 9)
!1882 = !DILocation(line: 1580, column: 9, scope: !1859)
!1883 = !DILocation(line: 1581, column: 9, scope: !1884)
!1884 = distinct !DILexicalBlock(scope: !1881, file: !6, line: 1580, column: 12)
!1885 = !DILocation(line: 1582, column: 5, scope: !1884)
!1886 = !DILocation(line: 1583, column: 9, scope: !1887)
!1887 = distinct !DILexicalBlock(scope: !1881, file: !6, line: 1582, column: 12)
!1888 = !DILocation(line: 1587, column: 5, scope: !1859)
!1889 = !DILocalVariable(name: "pxTaskCode", arg: 1, scope: !1890, file: !1864, line: 371, type: !1893)
!1890 = distinct !DISubprogram(name: "xTaskCreate", scope: !1864, file: !1864, line: 371, type: !1891, scopeLine: 377, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1903)
!1891 = !DISubroutineType(types: !1892)
!1892 = !{!64, !1893, !1896, !1898, !1899, !1900, !1901}
!1893 = !DIDerivedType(tag: DW_TAG_typedef, name: "TaskFunction_t", file: !1894, line: 40, baseType: !1895)
!1894 = !DIFile(filename: "Programs/Espressif/frameworks/esp-idf-v5.4/components/freertos/FreeRTOS-Kernel/include/freertos/projdefs.h", directory: "C:/Andrey")
!1895 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1376, size: 32)
!1896 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !1897)
!1897 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1096, size: 32)
!1898 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !16)
!1899 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !55)
!1900 = !DIDerivedType(tag: DW_TAG_typedef, name: "UBaseType_t", file: !15, line: 104, baseType: !7)
!1901 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !1902)
!1902 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1863, size: 32)
!1903 = !{!1889, !1904, !1905, !1906, !1907, !1908}
!1904 = !DILocalVariable(name: "pcName", arg: 2, scope: !1890, file: !1864, line: 372, type: !1896)
!1905 = !DILocalVariable(name: "usStackDepth", arg: 3, scope: !1890, file: !1864, line: 373, type: !1898)
!1906 = !DILocalVariable(name: "pvParameters", arg: 4, scope: !1890, file: !1864, line: 374, type: !1899)
!1907 = !DILocalVariable(name: "uxPriority", arg: 5, scope: !1890, file: !1864, line: 375, type: !1900)
!1908 = !DILocalVariable(name: "pxCreatedTask", arg: 6, scope: !1890, file: !1864, line: 376, type: !1901)
!1909 = !DILocation(line: 0, scope: !1890, inlinedAt: !1910)
!1910 = distinct !DILocation(line: 1588, column: 4, scope: !1859)
!1911 = !DILocation(line: 394, column: 16, scope: !1890, inlinedAt: !1910)
!1912 = !DILocation(line: 0, scope: !1868)
!1913 = !DILocation(line: 1592, column: 5, scope: !1868)
!1914 = !DILocation(line: 1596, column: 5, scope: !1859)
!1915 = !DILocation(line: 1598, column: 5, scope: !1859)
!1916 = !DILocation(line: 1600, column: 9, scope: !1859)
!1917 = !DILocation(line: 1593, column: 10, scope: !1918)
!1918 = distinct !DILexicalBlock(scope: !1919, file: !6, line: 1592, column: 33)
!1919 = distinct !DILexicalBlock(scope: !1868, file: !6, line: 1592, column: 5)
!1920 = !DILocation(line: 1594, column: 9, scope: !1918)
!1921 = !DILocation(line: 1592, column: 29, scope: !1919)
!1922 = !DILocation(line: 1592, column: 23, scope: !1919)
!1923 = distinct !{!1923, !1913, !1924, !1178, !1179}
!1924 = !DILocation(line: 1595, column: 5, scope: !1868)
!1925 = !DILocation(line: 1601, column: 9, scope: !1926)
!1926 = distinct !DILexicalBlock(scope: !1927, file: !6, line: 1600, column: 12)
!1927 = distinct !DILexicalBlock(scope: !1859, file: !6, line: 1600, column: 9)
!1928 = !DILocation(line: 1602, column: 9, scope: !1926)
!1929 = !DILocation(line: 1603, column: 9, scope: !1926)
!1930 = !DILocation(line: 1604, column: 9, scope: !1926)
!1931 = !DILocation(line: 1606, column: 5, scope: !1926)
!1932 = !DILocation(line: 1608, column: 9, scope: !1933)
!1933 = distinct !DILexicalBlock(scope: !1859, file: !6, line: 1608, column: 9)
!1934 = !DILocation(line: 1608, column: 9, scope: !1859)
!1935 = !DILocation(line: 1609, column: 9, scope: !1936)
!1936 = distinct !DILexicalBlock(scope: !1933, file: !6, line: 1608, column: 30)
!1937 = !DILocation(line: 1610, column: 21, scope: !1936)
!1938 = !DILocation(line: 1610, column: 9, scope: !1936)
!1939 = !DILocation(line: 1613, column: 5, scope: !1936)
!1940 = !DILocation(line: 0, scope: !1933)
!1941 = !DILocation(line: 1620, column: 5, scope: !1859)
!1942 = !DILocation(line: 1622, column: 5, scope: !1859)
!1943 = !DILocation(line: 1624, column: 5, scope: !1859)
!1944 = !DILocation(line: 1636, column: 1, scope: !1859)
!1945 = distinct !DISubprogram(name: "test_call_indirect_with_table", scope: !6, file: !6, line: 1262, type: !1181, scopeLine: 1262, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !1946)
!1946 = !{!1947, !1948, !1949, !1950, !1953, !1954, !1955, !1956, !1958, !1960, !1963, !1964, !1965, !1967}
!1947 = !DILocalVariable(name: "func_idx_0", scope: !1945, file: !6, line: 1278, type: !24)
!1948 = !DILocalVariable(name: "func_idx_1", scope: !1945, file: !6, line: 1281, type: !24)
!1949 = !DILocalVariable(name: "func_idx_2", scope: !1945, file: !6, line: 1284, type: !24)
!1950 = !DILocalVariable(name: "func_table", scope: !1945, file: !6, line: 1299, type: !1951)
!1951 = !DICompositeType(tag: DW_TAG_array_type, baseType: !1952, size: 96, elements: !627)
!1952 = !DIDerivedType(tag: DW_TAG_typedef, name: "binary_func_t", file: !6, line: 1168, baseType: !61)
!1953 = !DILocalVariable(name: "result_add", scope: !1945, file: !6, line: 1313, type: !24)
!1954 = !DILocalVariable(name: "result_mul", scope: !1945, file: !6, line: 1318, type: !24)
!1955 = !DILocalVariable(name: "result_sub", scope: !1945, file: !6, line: 1323, type: !24)
!1956 = !DILocalVariable(name: "dynamic_results", scope: !1945, file: !6, line: 1330, type: !1957)
!1957 = !DICompositeType(tag: DW_TAG_array_type, baseType: !24, size: 96, elements: !627)
!1958 = !DILocalVariable(name: "i", scope: !1959, file: !6, line: 1331, type: !24)
!1959 = distinct !DILexicalBlock(scope: !1945, file: !6, line: 1331, column: 5)
!1960 = !DILocalVariable(name: "result", scope: !1961, file: !6, line: 1333, type: !24)
!1961 = distinct !DILexicalBlock(scope: !1962, file: !6, line: 1331, column: 33)
!1962 = distinct !DILexicalBlock(scope: !1959, file: !6, line: 1331, column: 5)
!1963 = !DILocalVariable(name: "expected_dynamic", scope: !1945, file: !6, line: 1339, type: !1957)
!1964 = !DILocalVariable(name: "dynamic_pass", scope: !1945, file: !6, line: 1340, type: !70)
!1965 = !DILocalVariable(name: "i", scope: !1966, file: !6, line: 1341, type: !24)
!1966 = distinct !DILexicalBlock(scope: !1945, file: !6, line: 1341, column: 5)
!1967 = !DILocalVariable(name: "all_pass", scope: !1945, file: !6, line: 1357, type: !70)
!1968 = distinct !DIAssignID()
!1969 = !DILocation(line: 0, scope: !1945)
!1970 = distinct !DIAssignID()
!1971 = distinct !DIAssignID()
!1972 = !DILocation(line: 1263, column: 5, scope: !1945)
!1973 = !DILocation(line: 1266, column: 5, scope: !1945)
!1974 = !DILocation(line: 1267, column: 5, scope: !1945)
!1975 = !DILocation(line: 1268, column: 5, scope: !1945)
!1976 = !DILocation(line: 1270, column: 5, scope: !1945)
!1977 = !DILocation(line: 1271, column: 5, scope: !1945)
!1978 = !DILocation(line: 1273, column: 5, scope: !1945)
!1979 = !DILocation(line: 1274, column: 5, scope: !1945)
!1980 = !DILocation(line: 1277, column: 5, scope: !1945)
!1981 = !DILocation(line: 1278, column: 22, scope: !1945)
!1982 = !DILocation(line: 1279, column: 5, scope: !1945)
!1983 = !DILocation(line: 1281, column: 22, scope: !1945)
!1984 = !DILocation(line: 1282, column: 5, scope: !1945)
!1985 = !DILocation(line: 1284, column: 22, scope: !1945)
!1986 = !DILocation(line: 1285, column: 5, scope: !1945)
!1987 = !DILocation(line: 1288, column: 20, scope: !1988)
!1988 = distinct !DILexicalBlock(scope: !1945, file: !6, line: 1288, column: 9)
!1989 = !DILocation(line: 1288, column: 25, scope: !1988)
!1990 = !DILocation(line: 0, scope: !1988)
!1991 = !DILocation(line: 1295, column: 5, scope: !1945)
!1992 = !DILocation(line: 1296, column: 5, scope: !1945)
!1993 = !DILocation(line: 1299, column: 5, scope: !1945)
!1994 = !DILocation(line: 1300, column: 5, scope: !1945)
!1995 = !DILocation(line: 1301, column: 19, scope: !1945)
!1996 = distinct !DIAssignID()
!1997 = !DILocation(line: 1303, column: 5, scope: !1945)
!1998 = !DILocation(line: 1304, column: 5, scope: !1945)
!1999 = !DILocation(line: 1304, column: 19, scope: !1945)
!2000 = distinct !DIAssignID()
!2001 = !DILocation(line: 1306, column: 5, scope: !1945)
!2002 = !DILocation(line: 1307, column: 5, scope: !1945)
!2003 = !DILocation(line: 1307, column: 19, scope: !1945)
!2004 = distinct !DIAssignID()
!2005 = !DILocation(line: 1309, column: 5, scope: !1945)
!2006 = !DILocation(line: 1312, column: 5, scope: !1945)
!2007 = !DILocation(line: 1313, column: 22, scope: !1945)
!2008 = !DILocation(line: 1314, column: 5, scope: !1945)
!2009 = !DILocation(line: 1317, column: 5, scope: !1945)
!2010 = !DILocation(line: 1318, column: 22, scope: !1945)
!2011 = !DILocation(line: 1319, column: 5, scope: !1945)
!2012 = !DILocation(line: 1322, column: 5, scope: !1945)
!2013 = !DILocation(line: 1323, column: 22, scope: !1945)
!2014 = !DILocation(line: 1324, column: 5, scope: !1945)
!2015 = !DILocation(line: 1327, column: 5, scope: !1945)
!2016 = !DILocation(line: 1328, column: 5, scope: !1945)
!2017 = !DILocation(line: 1330, column: 5, scope: !1945)
!2018 = !DILocation(line: 0, scope: !1959)
!2019 = !DILocation(line: 1331, column: 5, scope: !1959)
!2020 = !DILocation(line: 1339, column: 5, scope: !1945)
!2021 = !DILocation(line: 1339, column: 9, scope: !1945)
!2022 = distinct !DIAssignID()
!2023 = distinct !DIAssignID()
!2024 = distinct !DIAssignID()
!2025 = !DILocation(line: 0, scope: !1966)
!2026 = !DILocation(line: 1341, column: 5, scope: !1966)
!2027 = !DILocation(line: 1332, column: 9, scope: !1961)
!2028 = !DILocation(line: 1333, column: 22, scope: !1961)
!2029 = !DILocation(line: 0, scope: !1961)
!2030 = !DILocation(line: 1334, column: 9, scope: !1961)
!2031 = !DILocation(line: 1334, column: 28, scope: !1961)
!2032 = !DILocation(line: 1335, column: 9, scope: !1961)
!2033 = !DILocation(line: 1331, column: 29, scope: !1962)
!2034 = !DILocation(line: 1331, column: 23, scope: !1962)
!2035 = distinct !{!2035, !2019, !2036, !1178, !1179}
!2036 = !DILocation(line: 1336, column: 5, scope: !1959)
!2037 = !DILocation(line: 0, scope: !2038)
!2038 = distinct !DILexicalBlock(scope: !1945, file: !6, line: 1349, column: 9)
!2039 = !DILocation(line: 1356, column: 5, scope: !1945)
!2040 = !DILocation(line: 1357, column: 33, scope: !1945)
!2041 = !DILocation(line: 1357, column: 39, scope: !1945)
!2042 = !DILocation(line: 1359, column: 9, scope: !1945)
!2043 = !DILocation(line: 1342, column: 13, scope: !2044)
!2044 = distinct !DILexicalBlock(scope: !2045, file: !6, line: 1342, column: 13)
!2045 = distinct !DILexicalBlock(scope: !2046, file: !6, line: 1341, column: 33)
!2046 = distinct !DILexicalBlock(scope: !1966, file: !6, line: 1341, column: 5)
!2047 = !DILocation(line: 1342, column: 35, scope: !2044)
!2048 = !DILocation(line: 1342, column: 32, scope: !2044)
!2049 = !DILocation(line: 1342, column: 13, scope: !2045)
!2050 = !DILocation(line: 1343, column: 13, scope: !2051)
!2051 = distinct !DILexicalBlock(scope: !2044, file: !6, line: 1342, column: 56)
!2052 = !DILocation(line: 1346, column: 9, scope: !2051)
!2053 = !DILocation(line: 1341, column: 29, scope: !2046)
!2054 = !DILocation(line: 1341, column: 23, scope: !2046)
!2055 = distinct !{!2055, !2026, !2056, !1178, !1179}
!2056 = !DILocation(line: 1347, column: 5, scope: !1966)
!2057 = !DILocation(line: 1360, column: 9, scope: !2058)
!2058 = distinct !DILexicalBlock(scope: !2059, file: !6, line: 1359, column: 19)
!2059 = distinct !DILexicalBlock(scope: !1945, file: !6, line: 1359, column: 9)
!2060 = !DILocation(line: 1361, column: 9, scope: !2058)
!2061 = !DILocation(line: 1362, column: 9, scope: !2058)
!2062 = !DILocation(line: 1363, column: 9, scope: !2058)
!2063 = !DILocation(line: 1364, column: 9, scope: !2058)
!2064 = !DILocation(line: 1366, column: 5, scope: !2058)
!2065 = !DILocation(line: 1367, column: 9, scope: !2066)
!2066 = distinct !DILexicalBlock(scope: !2059, file: !6, line: 1366, column: 12)
!2067 = !DILocation(line: 1368, column: 24, scope: !2068)
!2068 = distinct !DILexicalBlock(scope: !2066, file: !6, line: 1368, column: 13)
!2069 = !DILocation(line: 1368, column: 30, scope: !2068)
!2070 = !DILocation(line: 1369, column: 13, scope: !2071)
!2071 = distinct !DILexicalBlock(scope: !2068, file: !6, line: 1368, column: 70)
!2072 = !DILocation(line: 1371, column: 9, scope: !2071)
!2073 = !DILocation(line: 1372, column: 13, scope: !2066)
!2074 = !DILocation(line: 0, scope: !2059)
!2075 = !DILocation(line: 1377, column: 5, scope: !1945)
!2076 = !DILocation(line: 1378, column: 1, scope: !1945)
!2077 = distinct !DISubprogram(name: "cb_print", scope: !6, file: !6, line: 107, type: !1376, scopeLine: 107, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2078)
!2078 = !{!2079}
!2079 = !DILocalVariable(name: "ud", arg: 1, scope: !2077, file: !6, line: 107, type: !55)
!2080 = !DILocation(line: 0, scope: !2077)
!2081 = !DILocation(line: 108, column: 5, scope: !2077)
!2082 = !DILocation(line: 109, column: 1, scope: !2077)
!2083 = distinct !DISubprogram(name: "test_call_indirect", scope: !6, file: !6, line: 169, type: !1181, scopeLine: 169, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2084)
!2084 = !{!2085, !2086, !2087, !2088, !2089}
!2085 = !DILocalVariable(name: "func_ptr", scope: !2083, file: !6, line: 173, type: !61)
!2086 = !DILocalVariable(name: "result1", scope: !2083, file: !6, line: 174, type: !24)
!2087 = !DILocalVariable(name: "result2", scope: !2083, file: !6, line: 178, type: !24)
!2088 = !DILocalVariable(name: "result3", scope: !2083, file: !6, line: 181, type: !24)
!2089 = !DILocalVariable(name: "result4", scope: !2083, file: !6, line: 186, type: !24)
!2090 = !DILocation(line: 170, column: 5, scope: !2083)
!2091 = !DILocation(line: 0, scope: !2083)
!2092 = !DILocation(line: 174, column: 19, scope: !2083)
!2093 = !DILocation(line: 175, column: 5, scope: !2083)
!2094 = !DILocation(line: 178, column: 19, scope: !2083)
!2095 = !DILocation(line: 179, column: 5, scope: !2083)
!2096 = !DILocation(line: 181, column: 19, scope: !2083)
!2097 = !DILocation(line: 182, column: 5, scope: !2083)
!2098 = !DILocation(line: 186, column: 19, scope: !2083)
!2099 = !DILocation(line: 187, column: 5, scope: !2083)
!2100 = !DILocation(line: 189, column: 5, scope: !2083)
!2101 = !DILocation(line: 190, column: 1, scope: !2083)
!2102 = distinct !DISubprogram(name: "call_operation", scope: !6, file: !6, line: 161, type: !2103, scopeLine: 161, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2105)
!2103 = !DISubroutineType(types: !2104)
!2104 = !{!24, !61, !24, !24}
!2105 = !{!2106, !2107, !2108}
!2106 = !DILocalVariable(name: "operation", arg: 1, scope: !2102, file: !6, line: 161, type: !61)
!2107 = !DILocalVariable(name: "x", arg: 2, scope: !2102, file: !6, line: 161, type: !24)
!2108 = !DILocalVariable(name: "y", arg: 3, scope: !2102, file: !6, line: 161, type: !24)
!2109 = !DILocation(line: 0, scope: !2102)
!2110 = !DILocation(line: 162, column: 5, scope: !2102)
!2111 = !DILocation(line: 163, column: 12, scope: !2102)
!2112 = !DILocation(line: 163, column: 5, scope: !2102)
!2113 = distinct !DISubprogram(name: "subtract_function_1", scope: !6, file: !6, line: 1185, type: !62, scopeLine: 1185, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2114)
!2114 = !{!2115, !2116}
!2115 = !DILocalVariable(name: "a", arg: 1, scope: !2113, file: !6, line: 1185, type: !24)
!2116 = !DILocalVariable(name: "b", arg: 2, scope: !2113, file: !6, line: 1185, type: !24)
!2117 = !DILocation(line: 0, scope: !2113)
!2118 = !DILocation(line: 1186, column: 5, scope: !2113)
!2119 = !DILocation(line: 1187, column: 14, scope: !2113)
!2120 = !DILocation(line: 1187, column: 5, scope: !2113)
!2121 = distinct !DISubprogram(name: "test_first", scope: !6, file: !6, line: 1468, type: !1181, scopeLine: 1469, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2122)
!2122 = !{!2123, !2124, !2125, !2126, !2127, !2128, !2129, !2130, !2131, !2132}
!2123 = !DILocalVariable(name: "a", scope: !2121, file: !6, line: 1470, type: !39)
!2124 = !DILocalVariable(name: "b", scope: !2121, file: !6, line: 1471, type: !39)
!2125 = !DILocalVariable(name: "c", scope: !2121, file: !6, line: 1472, type: !39)
!2126 = !DILocalVariable(name: "flag1", scope: !2121, file: !6, line: 1493, type: !70)
!2127 = !DILocalVariable(name: "flag2", scope: !2121, file: !6, line: 1494, type: !70)
!2128 = !DILocalVariable(name: "result", scope: !2121, file: !6, line: 1508, type: !70)
!2129 = !DILocalVariable(name: "f1", scope: !2121, file: !6, line: 1526, type: !27)
!2130 = !DILocalVariable(name: "f2", scope: !2121, file: !6, line: 1527, type: !27)
!2131 = !DILocalVariable(name: "d1", scope: !2121, file: !6, line: 1539, type: !42)
!2132 = !DILocalVariable(name: "d2", scope: !2121, file: !6, line: 1540, type: !42)
!2133 = !DILocation(line: 0, scope: !2121)
!2134 = !DILocation(line: 1488, column: 5, scope: !2121)
!2135 = !DILocation(line: 1492, column: 5, scope: !2121)
!2136 = !DILocation(line: 1497, column: 9, scope: !2137)
!2137 = distinct !DILexicalBlock(scope: !2138, file: !6, line: 1496, column: 16)
!2138 = distinct !DILexicalBlock(scope: !2121, file: !6, line: 1496, column: 9)
!2139 = !DILocation(line: 1503, column: 9, scope: !2140)
!2140 = distinct !DILexicalBlock(scope: !2141, file: !6, line: 1502, column: 17)
!2141 = distinct !DILexicalBlock(scope: !2121, file: !6, line: 1502, column: 9)
!2142 = !DILocation(line: 1512, column: 9, scope: !2143)
!2143 = distinct !DILexicalBlock(scope: !2144, file: !6, line: 1511, column: 12)
!2144 = distinct !DILexicalBlock(scope: !2121, file: !6, line: 1509, column: 9)
!2145 = !DILocation(line: 1517, column: 9, scope: !2146)
!2146 = distinct !DILexicalBlock(scope: !2147, file: !6, line: 1516, column: 16)
!2147 = distinct !DILexicalBlock(scope: !2121, file: !6, line: 1516, column: 9)
!2148 = !DILocation(line: 1521, column: 5, scope: !2121)
!2149 = !DILocation(line: 1525, column: 5, scope: !2121)
!2150 = !DILocation(line: 1528, column: 5, scope: !2121)
!2151 = !DILocation(line: 1529, column: 5, scope: !2121)
!2152 = !DILocation(line: 1530, column: 5, scope: !2121)
!2153 = !DILocation(line: 1531, column: 5, scope: !2121)
!2154 = !DILocation(line: 1532, column: 5, scope: !2121)
!2155 = !DILocation(line: 1533, column: 5, scope: !2121)
!2156 = !DILocation(line: 1538, column: 5, scope: !2121)
!2157 = !DILocation(line: 1541, column: 5, scope: !2121)
!2158 = !DILocation(line: 1542, column: 5, scope: !2121)
!2159 = !DILocation(line: 1543, column: 5, scope: !2121)
!2160 = !DILocation(line: 1544, column: 5, scope: !2121)
!2161 = !DILocation(line: 1545, column: 5, scope: !2121)
!2162 = !DILocation(line: 1546, column: 5, scope: !2121)
!2163 = !DILocation(line: 1550, column: 1, scope: !2121)
!2164 = !DISubprogram(name: "printf", scope: !2165, file: !2165, line: 206, type: !2166, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2165 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/stdio.h", directory: "C:/Andrey")
!2166 = !DISubroutineType(types: !2167)
!2167 = !{!24, !2168, null}
!2168 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !1897)
!2169 = !DISubprogram(name: "OP_table_set", scope: !6, file: !6, line: 1165, type: !2170, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2170 = !DISubroutineType(types: !2171)
!2171 = !{null, !24, !24, !24}
!2172 = !DISubprogram(name: "OP_table_get", scope: !6, file: !6, line: 1164, type: !62, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2173 = !DISubprogram(name: "host_invoke_cb", scope: !6, file: !6, line: 56, type: !2174, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2174 = !DISubroutineType(types: !2175)
!2175 = !{null, !1895, !55}
!2176 = !DISubprogram(name: "host_invoke_cb2", scope: !6, file: !6, line: 57, type: !2177, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2177 = !DISubroutineType(types: !2178)
!2178 = !{null, !2179, !24, !55}
!2179 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1727, size: 32)
!2180 = !DISubprogram(name: "xTimerCreate", scope: !1109, file: !1109, line: 234, type: !2181, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2181 = !DISubroutineType(types: !2182)
!2182 = !{!1108, !1896, !2183, !2184, !1899, !2185}
!2183 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !14)
!2184 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !64)
!2185 = !DIDerivedType(tag: DW_TAG_typedef, name: "TimerCallbackFunction_t", file: !1109, line: 92, baseType: !2186)
!2186 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !1106, size: 32)
!2187 = !DISubprogram(name: "xTaskGetTickCount", scope: !1864, file: !1864, line: 1418, type: !2188, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2188 = !DISubroutineType(types: !2189)
!2189 = !{!14}
!2190 = !DISubprogram(name: "xTimerGenericCommand", scope: !1109, file: !1109, line: 1313, type: !2191, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2191 = !DISubroutineType(types: !2192)
!2192 = !{!64, !1108, !2184, !2183, !2193, !2183}
!2193 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !2194)
!2194 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !64, size: 32)
!2195 = !DISubprogram(name: "xTaskCreatePinnedToCore", scope: !1864, file: !1864, line: 382, type: !2196, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2196 = !DISubroutineType(types: !2197)
!2197 = !{!64, !1893, !1896, !1898, !1899, !1900, !1901, !2184}
!2198 = !DISubprogram(name: "vTaskDelay", scope: !1864, file: !1864, line: 834, type: !2199, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2199 = !DISubroutineType(types: !2200)
!2200 = !{null, !2183}
!2201 = !DISubprogram(name: "vTaskDelete", scope: !1864, file: !1864, line: 785, type: !2202, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2202 = !DISubroutineType(types: !2203)
!2203 = !{null, !1863}
!2204 = !DISubprogram(name: "pvTimerGetTimerID", scope: !1109, file: !1109, line: 385, type: !2205, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2205 = !DISubroutineType(types: !2206)
!2206 = !{!55, !2207}
!2207 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !1108)
!2208 = !DISubprogram(name: "OP_table_size", scope: !6, file: !6, line: 1160, type: !2209, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2209 = !DISubroutineType(types: !2210)
!2210 = !{!24, !24}
!2211 = !DISubprogram(name: "OP_table_init", scope: !6, file: !6, line: 1161, type: !2212, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2212 = !DISubroutineType(types: !2213)
!2213 = !{null, !24, !24, !24, !24, !24}
!2214 = !DISubprogram(name: "OP_table_fill", scope: !6, file: !6, line: 1163, type: !2215, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2215 = !DISubroutineType(types: !2216)
!2216 = !{null, !24, !24, !24, !24}
!2217 = !DISubprogram(name: "OP_table_copy", scope: !6, file: !6, line: 1162, type: !2212, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2218 = distinct !DISubprogram(name: "test_globals", scope: !6, file: !6, line: 249, type: !1181, scopeLine: 249, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2)
!2219 = !DILocation(line: 250, column: 5, scope: !2218)
!2220 = !DILocation(line: 251, column: 54, scope: !2218)
!2221 = !DILocation(line: 251, column: 5, scope: !2218)
!2222 = !DILocation(line: 252, column: 55, scope: !2218)
!2223 = !DILocation(line: 252, column: 5, scope: !2218)
!2224 = !DILocation(line: 253, column: 46, scope: !2218)
!2225 = !DILocation(line: 253, column: 5, scope: !2218)
!2226 = !DILocation(line: 255, column: 21, scope: !2218)
!2227 = !DILocation(line: 256, column: 22, scope: !2218)
!2228 = !DILocation(line: 257, column: 25, scope: !2218)
!2229 = !DILocation(line: 257, column: 22, scope: !2218)
!2230 = !DILocation(line: 259, column: 5, scope: !2218)
!2231 = !DILocation(line: 260, column: 56, scope: !2218)
!2232 = !DILocation(line: 260, column: 5, scope: !2218)
!2233 = !DILocation(line: 261, column: 47, scope: !2218)
!2234 = !DILocation(line: 261, column: 5, scope: !2218)
!2235 = !DILocation(line: 262, column: 5, scope: !2218)
!2236 = !DILocation(line: 263, column: 1, scope: !2218)
!2237 = distinct !DISubprogram(name: "test_math_operations32", scope: !6, file: !6, line: 268, type: !1181, scopeLine: 269, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2238)
!2238 = !{!2239, !2241, !2242, !2244}
!2239 = !DILocalVariable(name: "i32_a", scope: !2237, file: !6, line: 270, type: !2240)
!2240 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !25)
!2241 = !DILocalVariable(name: "i32_b", scope: !2237, file: !6, line: 271, type: !2240)
!2242 = !DILocalVariable(name: "u32_a", scope: !2237, file: !6, line: 272, type: !2243)
!2243 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !16)
!2244 = !DILocalVariable(name: "u32_b", scope: !2237, file: !6, line: 273, type: !2243)
!2245 = distinct !DIAssignID()
!2246 = !DILocation(line: 0, scope: !2237)
!2247 = distinct !DIAssignID()
!2248 = distinct !DIAssignID()
!2249 = distinct !DIAssignID()
!2250 = !DILocation(line: 270, column: 5, scope: !2237)
!2251 = !DILocation(line: 270, column: 23, scope: !2237)
!2252 = distinct !DIAssignID()
!2253 = !DILocation(line: 271, column: 5, scope: !2237)
!2254 = !DILocation(line: 271, column: 22, scope: !2237)
!2255 = distinct !DIAssignID()
!2256 = !DILocation(line: 272, column: 5, scope: !2237)
!2257 = !DILocation(line: 272, column: 25, scope: !2237)
!2258 = distinct !DIAssignID()
!2259 = !DILocation(line: 273, column: 5, scope: !2237)
!2260 = !DILocation(line: 273, column: 24, scope: !2237)
!2261 = distinct !DIAssignID()
!2262 = !DILocation(line: 274, column: 5, scope: !2237)
!2263 = !DILocation(line: 275, column: 56, scope: !2237)
!2264 = !DILocation(line: 275, column: 63, scope: !2237)
!2265 = !DILocation(line: 275, column: 70, scope: !2237)
!2266 = !DILocation(line: 275, column: 77, scope: !2237)
!2267 = !DILocation(line: 275, column: 5, scope: !2237)
!2268 = !DILocation(line: 276, column: 25, scope: !2237)
!2269 = !DILocation(line: 276, column: 33, scope: !2237)
!2270 = !DILocation(line: 276, column: 31, scope: !2237)
!2271 = !DILocation(line: 276, column: 5, scope: !2237)
!2272 = !DILocation(line: 277, column: 25, scope: !2237)
!2273 = !DILocation(line: 277, column: 33, scope: !2237)
!2274 = !DILocation(line: 277, column: 31, scope: !2237)
!2275 = !DILocation(line: 277, column: 5, scope: !2237)
!2276 = !DILocation(line: 278, column: 25, scope: !2237)
!2277 = !DILocation(line: 278, column: 33, scope: !2237)
!2278 = !DILocation(line: 278, column: 31, scope: !2237)
!2279 = !DILocation(line: 278, column: 5, scope: !2237)
!2280 = !DILocation(line: 279, column: 26, scope: !2237)
!2281 = !DILocation(line: 279, column: 34, scope: !2237)
!2282 = !DILocation(line: 279, column: 32, scope: !2237)
!2283 = !DILocation(line: 279, column: 5, scope: !2237)
!2284 = !DILocation(line: 280, column: 26, scope: !2237)
!2285 = !DILocation(line: 280, column: 34, scope: !2237)
!2286 = !DILocation(line: 280, column: 32, scope: !2237)
!2287 = !DILocation(line: 280, column: 5, scope: !2237)
!2288 = !DILocation(line: 281, column: 26, scope: !2237)
!2289 = !DILocation(line: 281, column: 34, scope: !2237)
!2290 = !DILocation(line: 281, column: 32, scope: !2237)
!2291 = !DILocation(line: 281, column: 5, scope: !2237)
!2292 = !DILocation(line: 282, column: 26, scope: !2237)
!2293 = !DILocation(line: 282, column: 34, scope: !2237)
!2294 = !DILocation(line: 282, column: 32, scope: !2237)
!2295 = !DILocation(line: 282, column: 5, scope: !2237)
!2296 = !DILocation(line: 283, column: 25, scope: !2237)
!2297 = !DILocation(line: 283, column: 31, scope: !2237)
!2298 = !DILocation(line: 283, column: 5, scope: !2237)
!2299 = !DILocation(line: 284, column: 24, scope: !2237)
!2300 = !DILocation(line: 284, column: 30, scope: !2237)
!2301 = !DILocation(line: 284, column: 5, scope: !2237)
!2302 = !DILocation(line: 285, column: 25, scope: !2237)
!2303 = !DILocation(line: 285, column: 31, scope: !2237)
!2304 = !DILocation(line: 285, column: 5, scope: !2237)
!2305 = !DILocation(line: 286, column: 25, scope: !2237)
!2306 = !DILocation(line: 286, column: 31, scope: !2237)
!2307 = !DILocation(line: 286, column: 5, scope: !2237)
!2308 = !DILocation(line: 287, column: 26, scope: !2237)
!2309 = !DILocation(line: 287, column: 32, scope: !2237)
!2310 = !DILocation(line: 287, column: 5, scope: !2237)
!2311 = !DILocation(line: 288, column: 26, scope: !2237)
!2312 = !DILocation(line: 288, column: 32, scope: !2237)
!2313 = !DILocation(line: 288, column: 5, scope: !2237)
!2314 = !DILocation(line: 289, column: 26, scope: !2237)
!2315 = !DILocation(line: 289, column: 25, scope: !2237)
!2316 = !DILocation(line: 289, column: 5, scope: !2237)
!2317 = !DILocation(line: 292, column: 1, scope: !2237)
!2318 = distinct !DISubprogram(name: "test_math_operations", scope: !6, file: !6, line: 295, type: !1181, scopeLine: 295, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2319)
!2319 = !{!2320, !2321, !2322, !2323, !2324, !2325, !2326, !2327, !2328, !2330, !2331, !2333, !2334, !2335, !2336, !2337}
!2320 = !DILocalVariable(name: "i32_a", scope: !2318, file: !6, line: 300, type: !2240)
!2321 = !DILocalVariable(name: "i32_b", scope: !2318, file: !6, line: 301, type: !2240)
!2322 = !DILocalVariable(name: "u32_a", scope: !2318, file: !6, line: 302, type: !2243)
!2323 = !DILocalVariable(name: "u32_b", scope: !2318, file: !6, line: 303, type: !2243)
!2324 = !DILocalVariable(name: "i64_a", scope: !2318, file: !6, line: 324, type: !1268)
!2325 = !DILocalVariable(name: "i64_b", scope: !2318, file: !6, line: 325, type: !1268)
!2326 = !DILocalVariable(name: "u64_a", scope: !2318, file: !6, line: 326, type: !1270)
!2327 = !DILocalVariable(name: "u64_b", scope: !2318, file: !6, line: 327, type: !1270)
!2328 = !DILocalVariable(name: "f32_a", scope: !2318, file: !6, line: 347, type: !2329)
!2329 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !27)
!2330 = !DILocalVariable(name: "f32_b", scope: !2318, file: !6, line: 348, type: !2329)
!2331 = !DILocalVariable(name: "f64_a", scope: !2318, file: !6, line: 362, type: !2332)
!2332 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !42)
!2333 = !DILocalVariable(name: "f64_b", scope: !2318, file: !6, line: 363, type: !2332)
!2334 = !DILocalVariable(name: "i32_from_f64", scope: !2318, file: !6, line: 377, type: !25)
!2335 = !DILocalVariable(name: "f32_from_i64", scope: !2318, file: !6, line: 378, type: !27)
!2336 = !DILocalVariable(name: "i16_from_i32", scope: !2318, file: !6, line: 379, type: !28)
!2337 = !DILocalVariable(name: "i32_from_i8", scope: !2318, file: !6, line: 380, type: !25)
!2338 = distinct !DIAssignID()
!2339 = !DILocation(line: 0, scope: !2318)
!2340 = distinct !DIAssignID()
!2341 = distinct !DIAssignID()
!2342 = distinct !DIAssignID()
!2343 = distinct !DIAssignID()
!2344 = distinct !DIAssignID()
!2345 = distinct !DIAssignID()
!2346 = distinct !DIAssignID()
!2347 = distinct !DIAssignID()
!2348 = distinct !DIAssignID()
!2349 = distinct !DIAssignID()
!2350 = distinct !DIAssignID()
!2351 = !DILocation(line: 296, column: 5, scope: !2318)
!2352 = !DILocation(line: 300, column: 5, scope: !2318)
!2353 = !DILocation(line: 300, column: 22, scope: !2318)
!2354 = distinct !DIAssignID()
!2355 = !DILocation(line: 301, column: 5, scope: !2318)
!2356 = !DILocation(line: 301, column: 22, scope: !2318)
!2357 = distinct !DIAssignID()
!2358 = !DILocation(line: 302, column: 5, scope: !2318)
!2359 = !DILocation(line: 302, column: 23, scope: !2318)
!2360 = distinct !DIAssignID()
!2361 = !DILocation(line: 303, column: 5, scope: !2318)
!2362 = !DILocation(line: 303, column: 23, scope: !2318)
!2363 = distinct !DIAssignID()
!2364 = !DILocation(line: 306, column: 56, scope: !2318)
!2365 = !DILocation(line: 306, column: 63, scope: !2318)
!2366 = !DILocation(line: 306, column: 70, scope: !2318)
!2367 = !DILocation(line: 306, column: 77, scope: !2318)
!2368 = !DILocation(line: 306, column: 5, scope: !2318)
!2369 = !DILocation(line: 307, column: 25, scope: !2318)
!2370 = !DILocation(line: 307, column: 33, scope: !2318)
!2371 = !DILocation(line: 307, column: 31, scope: !2318)
!2372 = !DILocation(line: 307, column: 5, scope: !2318)
!2373 = !DILocation(line: 308, column: 25, scope: !2318)
!2374 = !DILocation(line: 308, column: 33, scope: !2318)
!2375 = !DILocation(line: 308, column: 31, scope: !2318)
!2376 = !DILocation(line: 308, column: 5, scope: !2318)
!2377 = !DILocation(line: 309, column: 25, scope: !2318)
!2378 = !DILocation(line: 309, column: 33, scope: !2318)
!2379 = !DILocation(line: 309, column: 31, scope: !2318)
!2380 = !DILocation(line: 309, column: 5, scope: !2318)
!2381 = !DILocation(line: 310, column: 26, scope: !2318)
!2382 = !DILocation(line: 310, column: 34, scope: !2318)
!2383 = !DILocation(line: 310, column: 32, scope: !2318)
!2384 = !DILocation(line: 310, column: 5, scope: !2318)
!2385 = !DILocation(line: 311, column: 26, scope: !2318)
!2386 = !DILocation(line: 311, column: 34, scope: !2318)
!2387 = !DILocation(line: 311, column: 32, scope: !2318)
!2388 = !DILocation(line: 311, column: 5, scope: !2318)
!2389 = !DILocation(line: 312, column: 26, scope: !2318)
!2390 = !DILocation(line: 312, column: 34, scope: !2318)
!2391 = !DILocation(line: 312, column: 32, scope: !2318)
!2392 = !DILocation(line: 312, column: 5, scope: !2318)
!2393 = !DILocation(line: 313, column: 26, scope: !2318)
!2394 = !DILocation(line: 313, column: 34, scope: !2318)
!2395 = !DILocation(line: 313, column: 32, scope: !2318)
!2396 = !DILocation(line: 313, column: 5, scope: !2318)
!2397 = !DILocation(line: 314, column: 25, scope: !2318)
!2398 = !DILocation(line: 314, column: 31, scope: !2318)
!2399 = !DILocation(line: 314, column: 5, scope: !2318)
!2400 = !DILocation(line: 315, column: 24, scope: !2318)
!2401 = !DILocation(line: 315, column: 30, scope: !2318)
!2402 = !DILocation(line: 315, column: 5, scope: !2318)
!2403 = !DILocation(line: 316, column: 25, scope: !2318)
!2404 = !DILocation(line: 316, column: 31, scope: !2318)
!2405 = !DILocation(line: 316, column: 5, scope: !2318)
!2406 = !DILocation(line: 317, column: 25, scope: !2318)
!2407 = !DILocation(line: 317, column: 31, scope: !2318)
!2408 = !DILocation(line: 317, column: 5, scope: !2318)
!2409 = !DILocation(line: 318, column: 26, scope: !2318)
!2410 = !DILocation(line: 318, column: 32, scope: !2318)
!2411 = !DILocation(line: 318, column: 5, scope: !2318)
!2412 = !DILocation(line: 319, column: 26, scope: !2318)
!2413 = !DILocation(line: 319, column: 32, scope: !2318)
!2414 = !DILocation(line: 319, column: 5, scope: !2318)
!2415 = !DILocation(line: 320, column: 26, scope: !2318)
!2416 = !DILocation(line: 320, column: 25, scope: !2318)
!2417 = !DILocation(line: 320, column: 5, scope: !2318)
!2418 = !DILocation(line: 323, column: 5, scope: !2318)
!2419 = !DILocation(line: 324, column: 5, scope: !2318)
!2420 = !DILocation(line: 324, column: 22, scope: !2318)
!2421 = distinct !DIAssignID()
!2422 = !DILocation(line: 325, column: 5, scope: !2318)
!2423 = !DILocation(line: 325, column: 22, scope: !2318)
!2424 = distinct !DIAssignID()
!2425 = !DILocation(line: 326, column: 5, scope: !2318)
!2426 = !DILocation(line: 326, column: 23, scope: !2318)
!2427 = distinct !DIAssignID()
!2428 = !DILocation(line: 327, column: 5, scope: !2318)
!2429 = !DILocation(line: 327, column: 23, scope: !2318)
!2430 = distinct !DIAssignID()
!2431 = !DILocation(line: 329, column: 92, scope: !2318)
!2432 = !DILocation(line: 329, column: 99, scope: !2318)
!2433 = !DILocation(line: 329, column: 106, scope: !2318)
!2434 = !DILocation(line: 329, column: 113, scope: !2318)
!2435 = !DILocation(line: 329, column: 5, scope: !2318)
!2436 = !DILocation(line: 330, column: 34, scope: !2318)
!2437 = !DILocation(line: 330, column: 42, scope: !2318)
!2438 = !DILocation(line: 330, column: 40, scope: !2318)
!2439 = !DILocation(line: 330, column: 5, scope: !2318)
!2440 = !DILocation(line: 331, column: 34, scope: !2318)
!2441 = !DILocation(line: 331, column: 42, scope: !2318)
!2442 = !DILocation(line: 331, column: 40, scope: !2318)
!2443 = !DILocation(line: 331, column: 5, scope: !2318)
!2444 = !DILocation(line: 332, column: 34, scope: !2318)
!2445 = !DILocation(line: 332, column: 40, scope: !2318)
!2446 = !DILocation(line: 332, column: 5, scope: !2318)
!2447 = !DILocation(line: 333, column: 35, scope: !2318)
!2448 = !DILocation(line: 333, column: 43, scope: !2318)
!2449 = !DILocation(line: 333, column: 41, scope: !2318)
!2450 = !DILocation(line: 333, column: 5, scope: !2318)
!2451 = !DILocation(line: 334, column: 35, scope: !2318)
!2452 = !DILocation(line: 334, column: 43, scope: !2318)
!2453 = !DILocation(line: 334, column: 41, scope: !2318)
!2454 = !DILocation(line: 334, column: 5, scope: !2318)
!2455 = !DILocation(line: 335, column: 35, scope: !2318)
!2456 = !DILocation(line: 335, column: 43, scope: !2318)
!2457 = !DILocation(line: 335, column: 41, scope: !2318)
!2458 = !DILocation(line: 335, column: 5, scope: !2318)
!2459 = !DILocation(line: 336, column: 35, scope: !2318)
!2460 = !DILocation(line: 336, column: 43, scope: !2318)
!2461 = !DILocation(line: 336, column: 41, scope: !2318)
!2462 = !DILocation(line: 336, column: 5, scope: !2318)
!2463 = !DILocation(line: 337, column: 34, scope: !2318)
!2464 = !DILocation(line: 337, column: 40, scope: !2318)
!2465 = !DILocation(line: 337, column: 5, scope: !2318)
!2466 = !DILocation(line: 338, column: 33, scope: !2318)
!2467 = !DILocation(line: 338, column: 39, scope: !2318)
!2468 = !DILocation(line: 338, column: 5, scope: !2318)
!2469 = !DILocation(line: 339, column: 34, scope: !2318)
!2470 = !DILocation(line: 339, column: 40, scope: !2318)
!2471 = !DILocation(line: 339, column: 5, scope: !2318)
!2472 = !DILocation(line: 340, column: 34, scope: !2318)
!2473 = !DILocation(line: 340, column: 40, scope: !2318)
!2474 = !DILocation(line: 340, column: 5, scope: !2318)
!2475 = !DILocation(line: 341, column: 35, scope: !2318)
!2476 = !DILocation(line: 341, column: 41, scope: !2318)
!2477 = !DILocation(line: 341, column: 5, scope: !2318)
!2478 = !DILocation(line: 342, column: 35, scope: !2318)
!2479 = !DILocation(line: 342, column: 41, scope: !2318)
!2480 = !DILocation(line: 342, column: 5, scope: !2318)
!2481 = !DILocation(line: 343, column: 35, scope: !2318)
!2482 = !DILocation(line: 343, column: 34, scope: !2318)
!2483 = !DILocation(line: 343, column: 5, scope: !2318)
!2484 = !DILocation(line: 346, column: 5, scope: !2318)
!2485 = !DILocation(line: 347, column: 5, scope: !2318)
!2486 = !DILocation(line: 347, column: 20, scope: !2318)
!2487 = distinct !DIAssignID()
!2488 = !DILocation(line: 348, column: 5, scope: !2318)
!2489 = !DILocation(line: 348, column: 20, scope: !2318)
!2490 = distinct !DIAssignID()
!2491 = !DILocation(line: 350, column: 36, scope: !2318)
!2492 = !DILocation(line: 350, column: 43, scope: !2318)
!2493 = !DILocation(line: 350, column: 5, scope: !2318)
!2494 = !DILocation(line: 351, column: 25, scope: !2318)
!2495 = !DILocation(line: 351, column: 33, scope: !2318)
!2496 = !DILocation(line: 351, column: 31, scope: !2318)
!2497 = !DILocation(line: 351, column: 5, scope: !2318)
!2498 = !DILocation(line: 352, column: 25, scope: !2318)
!2499 = !DILocation(line: 352, column: 33, scope: !2318)
!2500 = !DILocation(line: 352, column: 31, scope: !2318)
!2501 = !DILocation(line: 352, column: 5, scope: !2318)
!2502 = !DILocation(line: 353, column: 25, scope: !2318)
!2503 = !DILocation(line: 353, column: 33, scope: !2318)
!2504 = !DILocation(line: 353, column: 31, scope: !2318)
!2505 = !DILocation(line: 353, column: 5, scope: !2318)
!2506 = !DILocation(line: 354, column: 25, scope: !2318)
!2507 = !DILocation(line: 354, column: 33, scope: !2318)
!2508 = !DILocation(line: 354, column: 31, scope: !2318)
!2509 = !DILocation(line: 354, column: 5, scope: !2318)
!2510 = !DILocation(line: 355, column: 31, scope: !2318)
!2511 = !DILocation(line: 355, column: 25, scope: !2318)
!2512 = !DILocation(line: 355, column: 5, scope: !2318)
!2513 = !DILocation(line: 356, column: 32, scope: !2318)
!2514 = !DILocation(line: 356, column: 40, scope: !2318)
!2515 = !DILocation(line: 356, column: 38, scope: !2318)
!2516 = !DILocation(line: 356, column: 26, scope: !2318)
!2517 = !DILocation(line: 356, column: 5, scope: !2318)
!2518 = !DILocation(line: 357, column: 31, scope: !2318)
!2519 = !DILocation(line: 357, column: 38, scope: !2318)
!2520 = !DILocation(line: 357, column: 25, scope: !2318)
!2521 = !DILocation(line: 357, column: 5, scope: !2318)
!2522 = !DILocation(line: 358, column: 31, scope: !2318)
!2523 = !DILocation(line: 358, column: 38, scope: !2318)
!2524 = !DILocation(line: 358, column: 25, scope: !2318)
!2525 = !DILocation(line: 358, column: 5, scope: !2318)
!2526 = !DILocation(line: 361, column: 5, scope: !2318)
!2527 = !DILocation(line: 362, column: 5, scope: !2318)
!2528 = !DILocation(line: 362, column: 21, scope: !2318)
!2529 = distinct !DIAssignID()
!2530 = !DILocation(line: 363, column: 5, scope: !2318)
!2531 = !DILocation(line: 363, column: 21, scope: !2318)
!2532 = distinct !DIAssignID()
!2533 = !DILocation(line: 365, column: 38, scope: !2318)
!2534 = !DILocation(line: 365, column: 45, scope: !2318)
!2535 = !DILocation(line: 365, column: 5, scope: !2318)
!2536 = !DILocation(line: 366, column: 26, scope: !2318)
!2537 = !DILocation(line: 366, column: 34, scope: !2318)
!2538 = !DILocation(line: 366, column: 32, scope: !2318)
!2539 = !DILocation(line: 366, column: 5, scope: !2318)
!2540 = !DILocation(line: 367, column: 26, scope: !2318)
!2541 = !DILocation(line: 367, column: 34, scope: !2318)
!2542 = !DILocation(line: 367, column: 32, scope: !2318)
!2543 = !DILocation(line: 367, column: 5, scope: !2318)
!2544 = !DILocation(line: 368, column: 26, scope: !2318)
!2545 = !DILocation(line: 368, column: 34, scope: !2318)
!2546 = !DILocation(line: 368, column: 32, scope: !2318)
!2547 = !DILocation(line: 368, column: 5, scope: !2318)
!2548 = !DILocation(line: 369, column: 26, scope: !2318)
!2549 = !DILocation(line: 369, column: 34, scope: !2318)
!2550 = !DILocation(line: 369, column: 32, scope: !2318)
!2551 = !DILocation(line: 369, column: 5, scope: !2318)
!2552 = !DILocation(line: 370, column: 31, scope: !2318)
!2553 = !DILocation(line: 370, column: 26, scope: !2318)
!2554 = !DILocation(line: 370, column: 5, scope: !2318)
!2555 = !DILocation(line: 371, column: 32, scope: !2318)
!2556 = !DILocation(line: 371, column: 40, scope: !2318)
!2557 = !DILocation(line: 371, column: 38, scope: !2318)
!2558 = !DILocation(line: 371, column: 27, scope: !2318)
!2559 = !DILocation(line: 371, column: 5, scope: !2318)
!2560 = !DILocation(line: 372, column: 31, scope: !2318)
!2561 = !DILocation(line: 372, column: 38, scope: !2318)
!2562 = !DILocation(line: 372, column: 26, scope: !2318)
!2563 = !DILocation(line: 372, column: 5, scope: !2318)
!2564 = !DILocation(line: 373, column: 31, scope: !2318)
!2565 = !DILocation(line: 373, column: 38, scope: !2318)
!2566 = !DILocation(line: 373, column: 26, scope: !2318)
!2567 = !DILocation(line: 373, column: 5, scope: !2318)
!2568 = !DILocation(line: 376, column: 5, scope: !2318)
!2569 = !DILocation(line: 377, column: 37, scope: !2318)
!2570 = !DILocation(line: 377, column: 28, scope: !2318)
!2571 = !DILocation(line: 378, column: 33, scope: !2318)
!2572 = !DILocation(line: 378, column: 26, scope: !2318)
!2573 = !DILocation(line: 382, column: 40, scope: !2318)
!2574 = !DILocation(line: 382, column: 5, scope: !2318)
!2575 = !DILocation(line: 383, column: 48, scope: !2318)
!2576 = !DILocation(line: 383, column: 55, scope: !2318)
!2577 = !DILocation(line: 383, column: 5, scope: !2318)
!2578 = !DILocation(line: 384, column: 5, scope: !2318)
!2579 = !DILocation(line: 385, column: 5, scope: !2318)
!2580 = !DILocation(line: 387, column: 5, scope: !2318)
!2581 = !DILocation(line: 388, column: 1, scope: !2318)
!2582 = !DISubprogram(name: "sqrtf", scope: !2583, file: !2583, line: 395, type: !2584, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2583 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/math.h", directory: "C:/Andrey")
!2584 = !DISubroutineType(types: !2585)
!2585 = !{!27, !27}
!2586 = !DISubprogram(name: "sqrt", scope: !2583, file: !2583, line: 112, type: !2587, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2587 = !DISubroutineType(types: !2588)
!2588 = !{!42, !42}
!2589 = distinct !DISubprogram(name: "MarchallingTest", scope: !6, file: !6, line: 551, type: !1181, scopeLine: 552, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2590)
!2590 = !{!2591}
!2591 = !DILocalVariable(name: "magic_container", scope: !2589, file: !6, line: 555, type: !24)
!2592 = distinct !DIAssignID()
!2593 = !DILocation(line: 0, scope: !2589)
!2594 = !DILocation(line: 553, column: 5, scope: !2589)
!2595 = !DILocation(line: 555, column: 5, scope: !2589)
!2596 = !DILocation(line: 555, column: 9, scope: !2589)
!2597 = distinct !DIAssignID()
!2598 = !DILocation(line: 557, column: 5, scope: !2589)
!2599 = !DILocation(line: 566, column: 5, scope: !2589)
!2600 = !DILocation(line: 568, column: 50, scope: !2589)
!2601 = !DILocation(line: 568, column: 5, scope: !2589)
!2602 = !DILocation(line: 571, column: 9, scope: !2603)
!2603 = distinct !DILexicalBlock(scope: !2589, file: !6, line: 571, column: 9)
!2604 = !DILocation(line: 571, column: 25, scope: !2603)
!2605 = !DILocation(line: 0, scope: !2603)
!2606 = !DILocation(line: 577, column: 5, scope: !2589)
!2607 = !DILocation(line: 578, column: 1, scope: !2589)
!2608 = !DISubprogram(name: "set_magic_number", scope: !6, file: !6, line: 53, type: !2609, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2609 = !DISubroutineType(types: !2610)
!2610 = !{null, !59}
!2611 = distinct !DISubprogram(name: "test_select_opcodes", scope: !6, file: !6, line: 583, type: !1181, scopeLine: 583, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2612)
!2612 = !{!2613, !2614, !2615, !2616, !2617, !2618}
!2613 = !DILocalVariable(name: "condition", scope: !2611, file: !6, line: 585, type: !24)
!2614 = !DILocalVariable(name: "i32_result", scope: !2611, file: !6, line: 588, type: !24)
!2615 = !DILocalVariable(name: "i64_result", scope: !2611, file: !6, line: 592, type: !37)
!2616 = !DILocalVariable(name: "f32_result", scope: !2611, file: !6, line: 597, type: !27)
!2617 = !DILocalVariable(name: "f64_result", scope: !2611, file: !6, line: 601, type: !42)
!2618 = !DILocalVariable(name: "ptr_result", scope: !2611, file: !6, line: 605, type: !55)
!2619 = !DILocation(line: 584, column: 5, scope: !2611)
!2620 = !DILocation(line: 0, scope: !2611)
!2621 = !DILocation(line: 589, column: 5, scope: !2611)
!2622 = !DILocation(line: 593, column: 5, scope: !2611)
!2623 = !DILocation(line: 598, column: 5, scope: !2611)
!2624 = !DILocation(line: 602, column: 5, scope: !2611)
!2625 = !DILocation(line: 606, column: 5, scope: !2611)
!2626 = !DILocation(line: 608, column: 5, scope: !2611)
!2627 = !DILocation(line: 609, column: 1, scope: !2611)
!2628 = distinct !DISubprogram(name: "test_pointer_ops", scope: !6, file: !6, line: 615, type: !1181, scopeLine: 615, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2629)
!2629 = !{!2630, !2631, !2632, !2633}
!2630 = !DILocalVariable(name: "my_var", scope: !2628, file: !6, line: 618, type: !24)
!2631 = !DILocalVariable(name: "p", scope: !2628, file: !6, line: 621, type: !59)
!2632 = !DILocalVariable(name: "addr_val", scope: !2628, file: !6, line: 624, type: !56)
!2633 = !DILocalVariable(name: "p2", scope: !2628, file: !6, line: 628, type: !59)
!2634 = distinct !DIAssignID()
!2635 = !DILocation(line: 0, scope: !2628)
!2636 = !DILocation(line: 616, column: 5, scope: !2628)
!2637 = !DILocation(line: 618, column: 5, scope: !2628)
!2638 = !DILocation(line: 618, column: 9, scope: !2628)
!2639 = distinct !DIAssignID()
!2640 = !DILocation(line: 624, column: 26, scope: !2628)
!2641 = !DILocation(line: 625, column: 5, scope: !2628)
!2642 = !DILocation(line: 631, column: 9, scope: !2643)
!2643 = distinct !DILexicalBlock(scope: !2628, file: !6, line: 631, column: 9)
!2644 = !DILocation(line: 631, column: 13, scope: !2643)
!2645 = !DILocation(line: 631, column: 9, scope: !2628)
!2646 = !DILocation(line: 632, column: 9, scope: !2647)
!2647 = distinct !DILexicalBlock(scope: !2643, file: !6, line: 631, column: 21)
!2648 = !DILocation(line: 633, column: 5, scope: !2647)
!2649 = !DILocation(line: 634, column: 9, scope: !2650)
!2650 = distinct !DILexicalBlock(scope: !2643, file: !6, line: 633, column: 12)
!2651 = !DILocation(line: 637, column: 6, scope: !2628)
!2652 = !DILocation(line: 638, column: 1, scope: !2628)
!2653 = distinct !DISubprogram(name: "test_atomics", scope: !6, file: !6, line: 645, type: !1181, scopeLine: 645, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2654)
!2654 = !{!2655, !2656, !2657, !2658, !2659, !2660, !2661}
!2655 = !DILocalVariable(name: "i32_atomic_var", scope: !2653, file: !6, line: 650, type: !2240)
!2656 = !DILocalVariable(name: "old_val_add", scope: !2653, file: !6, line: 654, type: !25)
!2657 = !DILocalVariable(name: "old_val_xchg", scope: !2653, file: !6, line: 658, type: !25)
!2658 = !DILocalVariable(name: "expected", scope: !2653, file: !6, line: 662, type: !25)
!2659 = !DILocalVariable(name: "i64_atomic_var", scope: !2653, file: !6, line: 672, type: !1268)
!2660 = !DILocalVariable(name: "old_val_sub64", scope: !2653, file: !6, line: 676, type: !37)
!2661 = !DILocalVariable(name: "loaded_val", scope: !2653, file: !6, line: 680, type: !37)
!2662 = distinct !DIAssignID()
!2663 = !DILocation(line: 0, scope: !2653)
!2664 = distinct !DIAssignID()
!2665 = !DILocation(line: 646, column: 5, scope: !2653)
!2666 = !DILocation(line: 649, column: 5, scope: !2653)
!2667 = !DILocation(line: 650, column: 5, scope: !2653)
!2668 = !DILocation(line: 650, column: 22, scope: !2653)
!2669 = distinct !DIAssignID()
!2670 = !DILocation(line: 651, column: 46, scope: !2653)
!2671 = !DILocation(line: 651, column: 5, scope: !2653)
!2672 = !DILocation(line: 654, column: 27, scope: !2653)
!2673 = !DILocation(line: 655, column: 57, scope: !2653)
!2674 = !DILocation(line: 655, column: 5, scope: !2653)
!2675 = !DILocation(line: 658, column: 28, scope: !2653)
!2676 = !DILocation(line: 659, column: 59, scope: !2653)
!2677 = !DILocation(line: 659, column: 5, scope: !2653)
!2678 = !DILocation(line: 663, column: 5, scope: !2653)
!2679 = !DILocation(line: 664, column: 63, scope: !2653)
!2680 = !DILocation(line: 664, column: 5, scope: !2653)
!2681 = !DILocation(line: 667, column: 5, scope: !2653)
!2682 = !DILocation(line: 668, column: 97, scope: !2653)
!2683 = !DILocation(line: 668, column: 5, scope: !2653)
!2684 = !DILocation(line: 671, column: 5, scope: !2653)
!2685 = !DILocation(line: 672, column: 5, scope: !2653)
!2686 = !DILocation(line: 672, column: 22, scope: !2653)
!2687 = distinct !DIAssignID()
!2688 = !DILocation(line: 673, column: 48, scope: !2653)
!2689 = !DILocation(line: 673, column: 5, scope: !2653)
!2690 = !DILocation(line: 676, column: 29, scope: !2653)
!2691 = !DILocation(line: 677, column: 67, scope: !2653)
!2692 = !DILocation(line: 677, column: 5, scope: !2653)
!2693 = !DILocation(line: 681, column: 5, scope: !2653)
!2694 = !DILocation(line: 682, column: 18, scope: !2653)
!2695 = !DILocation(line: 683, column: 5, scope: !2653)
!2696 = !DILocation(line: 685, column: 5, scope: !2653)
!2697 = !DILocation(line: 686, column: 1, scope: !2653)
!2698 = distinct !DISubprogram(name: "test_heap_and_memory_operations", scope: !6, file: !6, line: 692, type: !1181, scopeLine: 692, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !2699)
!2699 = !{!2700, !2701, !2704, !2707, !2709}
!2700 = !DILocalVariable(name: "dynamic_buffer", scope: !2698, file: !6, line: 700, type: !60)
!2701 = !DILocalVariable(name: "data_len", scope: !2702, file: !6, line: 721, type: !1151)
!2702 = distinct !DILexicalBlock(scope: !2703, file: !6, line: 720, column: 25)
!2703 = distinct !DILexicalBlock(scope: !2698, file: !6, line: 720, column: 9)
!2704 = !DILocalVariable(name: "fill_ok", scope: !2705, file: !6, line: 742, type: !70)
!2705 = distinct !DILexicalBlock(scope: !2706, file: !6, line: 737, column: 25)
!2706 = distinct !DILexicalBlock(scope: !2698, file: !6, line: 737, column: 9)
!2707 = !DILocalVariable(name: "i", scope: !2708, file: !6, line: 743, type: !24)
!2708 = distinct !DILexicalBlock(scope: !2705, file: !6, line: 743, column: 9)
!2709 = !DILocalVariable(name: "reused_buffer", scope: !2710, file: !6, line: 767, type: !60)
!2710 = distinct !DILexicalBlock(scope: !2711, file: !6, line: 761, column: 25)
!2711 = distinct !DILexicalBlock(scope: !2698, file: !6, line: 761, column: 9)
!2712 = !DILocation(line: 693, column: 5, scope: !2698)
!2713 = !DILocation(line: 699, column: 5, scope: !2698)
!2714 = !DILocation(line: 700, column: 35, scope: !2698)
!2715 = !DILocation(line: 0, scope: !2698)
!2716 = !DILocation(line: 702, column: 24, scope: !2717)
!2717 = distinct !DILexicalBlock(scope: !2698, file: !6, line: 702, column: 9)
!2718 = !DILocation(line: 702, column: 9, scope: !2698)
!2719 = !DILocation(line: 705, column: 9, scope: !2720)
!2720 = distinct !DILexicalBlock(scope: !2717, file: !6, line: 704, column: 12)
!2721 = !DILocation(line: 707, column: 9, scope: !2720)
!2722 = !DILocation(line: 708, column: 13, scope: !2723)
!2723 = distinct !DILexicalBlock(scope: !2720, file: !6, line: 708, column: 13)
!2724 = !DILocation(line: 708, column: 51, scope: !2723)
!2725 = !DILocation(line: 0, scope: !2717)
!2726 = !DILocation(line: 719, column: 5, scope: !2698)
!2727 = !DILocation(line: 0, scope: !2702)
!2728 = !DILocation(line: 722, column: 9, scope: !2702)
!2729 = !DILocation(line: 724, column: 9, scope: !2702)
!2730 = !DILocation(line: 727, column: 13, scope: !2731)
!2731 = distinct !DILexicalBlock(scope: !2702, file: !6, line: 727, column: 13)
!2732 = !DILocation(line: 727, column: 68, scope: !2731)
!2733 = !DILocation(line: 0, scope: !2731)
!2734 = !DILocation(line: 736, column: 5, scope: !2698)
!2735 = !DILocation(line: 738, column: 9, scope: !2705)
!2736 = !DILocation(line: 739, column: 9, scope: !2705)
!2737 = !DILocation(line: 0, scope: !2705)
!2738 = !DILocation(line: 0, scope: !2708)
!2739 = !DILocation(line: 743, column: 9, scope: !2708)
!2740 = !DILocation(line: 743, column: 34, scope: !2741)
!2741 = distinct !DILexicalBlock(scope: !2708, file: !6, line: 743, column: 9)
!2742 = !DILocation(line: 743, column: 27, scope: !2741)
!2743 = distinct !{!2743, !2739, !2744, !1178, !1179}
!2744 = !DILocation(line: 748, column: 9, scope: !2708)
!2745 = !DILocation(line: 744, column: 17, scope: !2746)
!2746 = distinct !DILexicalBlock(scope: !2747, file: !6, line: 744, column: 17)
!2747 = distinct !DILexicalBlock(scope: !2741, file: !6, line: 743, column: 38)
!2748 = !DILocation(line: 744, column: 35, scope: !2746)
!2749 = !DILocation(line: 744, column: 17, scope: !2747)
!2750 = !DILocation(line: 720, column: 9, scope: !2698)
!2751 = !DILocation(line: 760, column: 5, scope: !2698)
!2752 = !DILocation(line: 761, column: 9, scope: !2698)
!2753 = !DILocation(line: 762, column: 9, scope: !2710)
!2754 = !DILocation(line: 763, column: 9, scope: !2710)
!2755 = !DILocation(line: 767, column: 38, scope: !2710)
!2756 = !DILocation(line: 0, scope: !2710)
!2757 = !DILocation(line: 768, column: 13, scope: !2758)
!2758 = distinct !DILexicalBlock(scope: !2710, file: !6, line: 768, column: 13)
!2759 = !DILocation(line: 768, column: 13, scope: !2710)
!2760 = !DILocation(line: 769, column: 14, scope: !2761)
!2761 = distinct !DILexicalBlock(scope: !2758, file: !6, line: 768, column: 28)
!2762 = !DILocation(line: 770, column: 14, scope: !2761)
!2763 = !DILocation(line: 771, column: 9, scope: !2761)
!2764 = !DILocation(line: 772, column: 14, scope: !2765)
!2765 = distinct !DILexicalBlock(scope: !2758, file: !6, line: 771, column: 16)
!2766 = !DILocation(line: 776, column: 5, scope: !2698)
!2767 = !DILocation(line: 777, column: 1, scope: !2698)
!2768 = !DISubprogram(name: "malloc", scope: !2769, file: !2769, line: 108, type: !2770, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2769 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/stdlib.h", directory: "C:/Andrey")
!2770 = !DISubroutineType(types: !2771)
!2771 = !{!55, !2772}
!2772 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !2773, line: 18, baseType: !7)
!2773 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/lib/clang/18/include/__stddef_size_t.h", directory: "C:/Andrey")
!2774 = !DISubprogram(name: "strcmp", scope: !2775, file: !2775, line: 36, type: !2776, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2775 = !DIFile(filename: "Programs/Espressif/Tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin/../lib/clang-runtimes/riscv32-esp-unknown-elf/rv32imac-zicsr-zifencei_ilp32/include/string.h", directory: "C:/Andrey")
!2776 = !DISubroutineType(types: !2777)
!2777 = !{!24, !1897, !1897}
!2778 = !DISubprogram(name: "memcmp", scope: !2775, file: !2775, line: 30, type: !2779, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2779 = !DISubroutineType(types: !2780)
!2780 = !{!24, !2781, !2781, !1151}
!2781 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !2782, size: 32)
!2782 = !DIDerivedType(tag: DW_TAG_const_type, baseType: null)
!2783 = !DISubprogram(name: "free", scope: !2769, file: !2769, line: 94, type: !1376, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!2784 = distinct !DISubprogram(name: "app_main", scope: !6, file: !6, line: 1638, type: !1181, scopeLine: 1639, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2)
!2785 = !DILocation(line: 1643, column: 5, scope: !2784)
!2786 = !DILocation(line: 1645, column: 5, scope: !2784)
!2787 = !DILocation(line: 1646, column: 5, scope: !2784)
!2788 = !DILocation(line: 1647, column: 5, scope: !2784)
!2789 = !DILocation(line: 1648, column: 5, scope: !2784)
!2790 = !DILocation(line: 1649, column: 5, scope: !2784)
!2791 = !DILocation(line: 1650, column: 6, scope: !2784)
!2792 = !DILocation(line: 1651, column: 4, scope: !2784)
!2793 = !DILocation(line: 1652, column: 6, scope: !2784)
!2794 = !DILocation(line: 1653, column: 7, scope: !2784)
!2795 = !DILocation(line: 1654, column: 6, scope: !2784)
!2796 = !DILocation(line: 1655, column: 7, scope: !2784)
!2797 = !DILocation(line: 1656, column: 5, scope: !2784)
!2798 = !DILocation(line: 1657, column: 5, scope: !2784)
!2799 = !DILocation(line: 1658, column: 5, scope: !2784)
!2800 = !DILocation(line: 1659, column: 7, scope: !2784)
!2801 = !DILocation(line: 1663, column: 5, scope: !2784)
!2802 = !DILocation(line: 1665, column: 5, scope: !2784)
!2803 = !DILocation(line: 1667, column: 5, scope: !2784)
!2804 = !DILocation(line: 1669, column: 4, scope: !2784)
!2805 = !DILocation(line: 1671, column: 5, scope: !2784)
!2806 = !DILocation(line: 1673, column: 5, scope: !2784)
!2807 = !DILocation(line: 1678, column: 1, scope: !2784)
!2808 = distinct !DISubprogram(name: "test", scope: !1119, file: !1119, line: 3, type: !2809, scopeLine: 4, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !1124, retainedNodes: !2811)
!2809 = !DISubroutineType(types: !2810)
!2810 = !{null, !24, !42, !1897}
!2811 = !{!2812, !2813, !2814}
!2812 = !DILocalVariable(name: "some_int", arg: 1, scope: !2808, file: !1119, line: 3, type: !24)
!2813 = !DILocalVariable(name: "some_double", arg: 2, scope: !2808, file: !1119, line: 3, type: !42)
!2814 = !DILocalVariable(name: "some_string", arg: 3, scope: !2808, file: !1119, line: 3, type: !1897)
!2815 = !DILocation(line: 0, scope: !2808)
!2816 = !DILocation(line: 5, column: 5, scope: !2808)
!2817 = !DILocation(line: 8, column: 5, scope: !2808)
!2818 = !DILocation(line: 9, column: 5, scope: !2808)
!2819 = !DILocation(line: 11, column: 9, scope: !2820)
!2820 = distinct !DILexicalBlock(scope: !2808, file: !1119, line: 11, column: 9)
!2821 = !DILocation(line: 11, column: 9, scope: !2808)
!2822 = !DILocation(line: 12, column: 9, scope: !2823)
!2823 = distinct !DILexicalBlock(scope: !2820, file: !1119, line: 11, column: 22)
!2824 = !DILocation(line: 13, column: 5, scope: !2823)
!2825 = !DILocation(line: 14, column: 9, scope: !2826)
!2826 = distinct !DILexicalBlock(scope: !2820, file: !1119, line: 13, column: 12)
!2827 = !DILocation(line: 17, column: 5, scope: !2808)
!2828 = !DILocation(line: 18, column: 1, scope: !2808)
