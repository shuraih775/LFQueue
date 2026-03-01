cmake_minimum_required(VERSION 3.10)
project(LockfreeRingBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")

set(CMAKE_CXX_FLAGS_TSAN "-O2 -g -fsanitize=thread")
set(CMAKE_EXE_LINKER_FLAGS_TSAN "-fsanitize=thread")

find_package(Threads REQUIRED)

include_directories(include)
include_directories(third_party/drogalis/include)
include_directories(third_party/moodycamel)
include_directories(third_party/rigtorp/include/)
include_directories(benchmark)

add_executable(bench benchmark/run_benchmark.cpp)
target_link_libraries(bench PRIVATE Threads::Threads)

# Set build type to Release by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()