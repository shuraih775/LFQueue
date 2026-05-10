cmake_minimum_required(VERSION 3.10)
project(LockfreeRingBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mavx2 -ftree-vectorize -fno-tree-slp-vectorize -flto -DNDEBUG")

set(CMAKE_CXX_FLAGS_TSAN "-O2 -g -fsanitize=thread")
set(CMAKE_EXE_LINKER_FLAGS_TSAN "-fsanitize=thread")

find_package(Threads REQUIRED)

include_directories(include)
include_directories(third_party/drogalis/include)
include_directories(third_party/moodycamel)
include_directories(third_party/rigtorp/include/)
include_directories(benchmark)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Main benchmark
add_executable(bench benchmark/run_benchmark.cpp)
target_link_libraries(bench PRIVATE Threads::Threads)

#  DRO ASM 

add_executable(dro_asm_bench benchmark/dro_asm_bench.cpp)
target_link_libraries(dro_asm_bench PRIVATE Threads::Threads)

target_compile_options(dro_asm_bench PRIVATE -fno-inline)

add_custom_command(
    TARGET dro_asm_bench POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -d -M intel
            $<TARGET_FILE:dro_asm_bench>
            > ${CMAKE_BINARY_DIR}/dro_dump.asm
    COMMENT "Generating Drogalis assembly dump"
)

#  LFQUEUE ASM 

add_executable(lfqueue_asm_bench benchmark/lfqueue_asm_bench.cpp)
target_link_libraries(lfqueue_asm_bench PRIVATE Threads::Threads)

target_compile_options(lfqueue_asm_bench PRIVATE -fno-inline)

add_custom_command(
    TARGET lfqueue_asm_bench POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -d -M intel
            $<TARGET_FILE:lfqueue_asm_bench>
            > ${CMAKE_BINARY_DIR}/lfqueue_dump.asm
    COMMENT "Generating LFQueue assembly dump"
)

# Rigtorp ASM

add_executable(rigtorp_asm_bench benchmark/rigtorp_asm_bench.cpp)
target_link_libraries(rigtorp_asm_bench PRIVATE Threads::Threads)

target_compile_options(rigtorp_asm_bench PRIVATE -fno-inline)

add_custom_command(
    TARGET rigtorp_asm_bench POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -d -M intel
            $<TARGET_FILE:rigtorp_asm_bench>
            > ${CMAKE_BINARY_DIR}/rigtorp_dump.asm
    COMMENT "Generating Rigtorp assembly dump"
)
