# Copyright 2026 Isomet (UK) Ltd. All rights reserved.
#
# Test targets
#   ims_core        Library sources minus the hardware transports, as a static library so tests
#                   can reach internal classes (CM_Common, HostReport, ...) that the DLL does not export
#   ims_sim         Simulated hardware (SimDevice + CM_Sim), on top of ims_test_support
#   ims_unit_tests  Unit and integration tests, all against simulated hardware
#   hil/            Hardware-in-the-loop tests and benchmarks (IMSLIB_BUILD_HIL)

# The Conan toolchain turns BUILD_SHARED_LIBS on (the package is shared); keep the fetched
# GoogleTest and Benchmark static, so the test executables have no DLLs to find at run time
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found, fetching v1.15.2")
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

#
# ims_core
#
set(ims_transport_sources_regex "/(CM_FTDI|CM_CYUSB|CM_RS422|CM_ENET|tftp_client|tftp_packet|ConnectionList|CyAPI|enumser)\\.cpp$")
set(ims_core_sources ${ims_api_source_files})
list(FILTER ims_core_sources EXCLUDE REGEX ${ims_transport_sources_regex})

add_library(ims_core STATIC ${ims_core_sources})
target_include_directories(ims_core PUBLIC
    ${api_include_dir}
    ${api_resource_dir}
    ${api_lib_dir}/spline
    ${api_lib_dir}/sqlite3
)
target_compile_definitions(ims_core PUBLIC _STATIC_IMS)
if(WIN32)
    target_compile_definitions(ims_core PUBLIC _WIN32_WINNT=0x0A00 UNICODE)
    # synchronization for Boost.Log, ws2_32 for Boost.Asio (CS_ETH)
    target_link_libraries(ims_core PUBLIC synchronization ws2_32)
endif()
if(NOT MINGW)
    target_compile_definitions(ims_core PUBLIC LIBXML_STATIC)
endif()
target_link_libraries(ims_core PUBLIC Boost::regex Boost::log_setup Boost::log)
if(TARGET LibXml2::LibXml2)
    target_link_libraries(ims_core PUBLIC LibXml2::LibXml2)
else()
    target_link_libraries(ims_core PUBLIC ${libxml2_LIBRARIES})
endif()

#
# ims_test_support (logging control, event recorder) and ims_sim (simulated hardware)
#
add_library(ims_test_support STATIC support/TestSupport.cpp)
target_include_directories(ims_test_support PUBLIC support)
target_link_libraries(ims_test_support PUBLIC ims_core)

add_library(ims_sim STATIC
    sim/SimDevice.cpp
    sim/CM_Sim.cpp
)
target_include_directories(ims_sim PUBLIC sim)
target_link_libraries(ims_sim PUBLIC ims_test_support)

# The hardware database is embedded as a Windows resource and located through __ImageBase,
# so it must be linked into the executable itself; a resource object inside a static library
# would never be pulled in by the linker.
function(ims_add_test_executable name)
    add_executable(${name} ${ARGN})
    if(WIN32)
        target_sources(${name} PRIVATE ${api_db_resource})
    endif()
endfunction()

#
# ims_unit_tests
#
ims_add_test_executable(ims_unit_tests
    support/TestMain.cpp
    unit/test_protocol.cpp
    unit/test_types.cpp
    unit/test_sim_device.cpp
    unit/test_connection_manager.cpp
    unit/test_memory_transfer.cpp
    unit/test_ims_system.cpp
    unit/test_signal_path.cpp
    unit/test_image_download.cpp
    unit/test_eeprom.cpp
)
target_include_directories(ims_unit_tests PRIVATE unit)
target_link_libraries(ims_unit_tests PRIVATE ims_sim GTest::gtest)
# Per-test timeout so a hang fails the run instead of blocking it
# Discovery runs the executable; the default 5 s limit can be exceeded by a virus scan of a new build
gtest_discover_tests(ims_unit_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit" TIMEOUT 120
)

if(IMSLIB_BUILD_HIL)
    add_subdirectory(hil)
endif()
