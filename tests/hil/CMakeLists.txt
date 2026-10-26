# Copyright 2026 Isomet (UK) Ltd. All rights reserved.
#
# Hardware-in-the-loop tests and benchmarks, each built in two flavours from the same sources:
#   ims_hil_tests / ims_bench          real transports (ConnectionList scan); run on a rig with hardware
#   ims_hil_tests_sim / ims_bench_sim  simulated device; registered with ctest so CI keeps them working

find_package(benchmark CONFIG QUIET)
if(NOT benchmark_FOUND)
    message(STATUS "Google Benchmark not found, fetching v1.9.1")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
        URL_HASH SHA256=32131c08ee31eeff2c8968d7e874f3cb648034377dfc32a4c377fa8796d84981
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(benchmark)
endif()

#
# ims_core_hw: the hardware transports and ConnectionList on top of ims_core
#
set(ims_transport_sources ${ims_api_source_files})
list(FILTER ims_transport_sources INCLUDE REGEX ${ims_transport_sources_regex})

add_library(ims_core_hw STATIC ${ims_transport_sources})
target_link_libraries(ims_core_hw PUBLIC ims_core)
if(WIN32)
    target_include_directories(ims_core_hw PRIVATE
        ${api_lib_dir}/enumser
        ${api_lib_dir}/FTDI
        ${api_lib_dir}/CyAPI/inc
    )
    target_link_libraries(ims_core_hw PUBLIC setupapi)
    if(MINGW)
        # Same handling as the library target: vendor narrowing in CyAPI, ftd2xx via its DLL
        target_compile_options(ims_core_hw PRIVATE -Wno-narrowing)
        add_dependencies(ims_core_hw ftd2xx_import)
        target_link_libraries(ims_core_hw PUBLIC ${FTD2XX_IMPORT_LIB})
    else()
        target_compile_definitions(ims_core_hw PRIVATE FTD2XX_STATIC)
        target_link_libraries(ims_core_hw PUBLIC ftd2xx)
    endif()
endif()

set(hil_test_sources HilTestMain.cpp HilCommon.cpp test_hil.cpp)
set(hil_bench_sources BenchMain.cpp HilCommon.cpp bench_hil.cpp)

ims_add_test_executable(ims_hil_tests ${hil_test_sources} HilTarget_hw.cpp)
target_link_libraries(ims_hil_tests PRIVATE ims_core_hw ims_test_support GTest::gtest)

ims_add_test_executable(ims_bench ${hil_bench_sources} HilTarget_hw.cpp)
target_link_libraries(ims_bench PRIVATE ims_core_hw ims_test_support benchmark::benchmark)

ims_add_test_executable(ims_hil_tests_sim ${hil_test_sources} HilTarget_sim.cpp)
target_link_libraries(ims_hil_tests_sim PRIVATE ims_sim GTest::gtest)

ims_add_test_executable(ims_bench_sim ${hil_bench_sources} HilTarget_sim.cpp)
target_link_libraries(ims_bench_sim PRIVATE ims_sim benchmark::benchmark)

gtest_discover_tests(ims_hil_tests_sim
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    TEST_PREFIX "sim:"
    PROPERTIES LABELS "hil-sim" ENVIRONMENT "IMS_HIL_ALLOW_WRITE=1" TIMEOUT 120
)
# One iteration of each benchmark: checks they run, not how fast
add_test(NAME sim:bench_smoke COMMAND ims_bench_sim --benchmark_min_time=1x)
set_tests_properties(sim:bench_smoke PROPERTIES LABELS "hil-sim" ENVIRONMENT "IMS_HIL_ALLOW_WRITE=1" TIMEOUT 300)

# Differential probe: diff the output of the two to see where the simulator's firmware model differs
ims_add_test_executable(ims_hil_probe hil_probe.cpp HilCommon.cpp HilTarget_hw.cpp)
target_link_libraries(ims_hil_probe PRIVATE ims_core_hw ims_test_support)
ims_add_test_executable(ims_hil_probe_sim hil_probe.cpp HilCommon.cpp HilTarget_sim.cpp)
target_link_libraries(ims_hil_probe_sim PRIVATE ims_sim)
