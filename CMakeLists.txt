cmake_minimum_required(VERSION 3.20)

project(
    PerfLens
    VERSION 0.1.0
    DESCRIPTION "Controlled Linux performance regression measurements"
    LANGUAGES CXX
)

include(FetchContent)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "PerfLens currently supports Linux only")
endif()

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cli11 nlohmann_json)

add_library(perflens_core
    src/analysis/benchmark_runner.cpp
    src/analysis/comparison.cpp
    src/analysis/statistics.cpp
    src/model.cpp
    src/perf/perf_counter.cpp
    src/perf/perf_sampler.cpp
    src/process/process_runner.cpp
    src/process/workload_metrics.cpp
    src/report/terminal_report.cpp
    src/symbols/symbolizer.cpp
)

target_include_directories(perflens_core PUBLIC include)
target_link_libraries(perflens_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(perflens_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(perflens src/cli/main.cpp)
target_link_libraries(perflens PRIVATE perflens_core CLI11::CLI11)
target_compile_options(perflens PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

install(TARGETS perflens RUNTIME DESTINATION bin)

add_executable(allocation examples/allocation.cpp)
target_compile_options(allocation PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(branch_behavior examples/branch_behavior.cpp)
target_compile_options(branch_behavior PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(cpu_hotspot examples/cpu_hotspot.cpp)
target_compile_options(cpu_hotspot PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(cpu_work examples/cpu_work.cpp)
target_compile_options(cpu_work PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(example_server examples/example_server.cpp)
target_compile_options(example_server PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(example_workload examples/example_workload.cpp)
target_compile_options(example_workload PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(lock_contention examples/lock_contention.cpp)
target_compile_options(lock_contention PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
add_executable(memory_access examples/memory_access.cpp)
target_compile_options(memory_access PRIVATE -O2 -g -Wall -Wextra -Wpedantic)
target_link_libraries(lock_contention PRIVATE Threads::Threads)

include(CTest)

if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(perflens_tests
        tests/analysis_test.cpp
        tests/model_test.cpp
        tests/perf_test.cpp
        tests/process_runner_test.cpp
        tests/service_lifecycle_test.cpp
        tests/symbolizer_test.cpp
        tests/workload_metrics_test.cpp
    )
    target_link_libraries(perflens_tests PRIVATE perflens_core Catch2::Catch2WithMain)
    target_compile_options(perflens_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(perflens_tests)
endif()

if(BUILD_TESTING)
    add_executable(symbolizer_nopie_tests tests/symbolizer_test.cpp)
    target_link_libraries(symbolizer_nopie_tests PRIVATE perflens_core Catch2::Catch2WithMain)
    target_compile_options(symbolizer_nopie_tests PRIVATE -fno-pie)
    target_link_options(symbolizer_nopie_tests PRIVATE -no-pie)
    catch_discover_tests(symbolizer_nopie_tests TEST_PREFIX "non-PIE: ")
endif()
