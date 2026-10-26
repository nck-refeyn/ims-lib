cmake_minimum_required(VERSION 3.15)

option(IMSLIB_SUPPRESS_LIBRARY_BUILD "Do not create library" OFF)

if (NOT IMSLIB_SUPPRESS_LIBRARY_BUILD)
    if(WIN32)
        set(IMS_TARGET_NAME "imsLibrary")
    else()
        set(IMS_TARGET_NAME "ims")
    endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(${IMS_TARGET_NAME} VERSION 2.0.7 DESCRIPTION "Isomet iMS Synthesiser Library" LANGUAGES C CXX)
    message(STATUS "Building Standalone iMS Library ${IMS_TARGET_NAME$} v${PROJECT_VERSION}")
endif()

message(STATUS "Building for Platform: '${CMAKE_GENERATOR_PLATFORM}'")

option(IMSLIB_BUILD_SHARED "Build imslib as a shared library" ON)
if(IMSLIB_BUILD_SHARED)
    message(STATUS "Building ims-lib as shared library")
    set(IMSLIB_LIB_TYPE SHARED)
else()
    message(STATUS "Building ims-lib as static library")
    set(IMSLIB_LIB_TYPE STATIC)
endif()

# Min C++17
set(CMAKE_CXX_STANDARD 17)

include(GNUInstallDirs)

# Conan integration and Dependency Resolution
# Only do these things when building standalone
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
#    include(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake) # Conan Toolchain (CMakeToolchain)
    ##
    ## Check for LibXML2
    ##
    find_package(LibXml2 2.9.9 REQUIRED)
    if (LibXml2_FOUND)
        message("-- Including LibXml2")
        include_directories(${libxml2_INCLUDE_DIRS})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${libxml2_DEFINITIONS}")
    else()
        message("-- LibXml2 not Found")
    endif()

    ##
    ## Get BOOST
    ##
    find_package(Boost REQUIRED COMPONENTS regex log_setup log)
endif()

# add_subdirectory(modules/boost-cmake)
# set_property(
#     TARGET 
#         Boost_regex 
#         Boost_log_setup
#         Boost_log
#         Boost_filesystem
#         Boost_atomic
#         Boost_date_time
#         Boost_thread
#         Boost_chrono    
#     PROPERTY POSITION_INDEPENDENT_CODE ON)

# # Missing compile definition in boost-cmake.  If not included, compilation of init_from_settings.cpp fails with 
# # "Error: boost::condition_variable has no member 'timed_wait'
# target_compile_definitions(Boost_log_setup PRIVATE
#     BOOST_THREAD_USES_DATETIME
# )

set(api_source_dir  	${CMAKE_CURRENT_SOURCE_DIR}/src)
set(api_include_dir 	${CMAKE_CURRENT_SOURCE_DIR}/include)
set(api_resource_dir	${CMAKE_CURRENT_SOURCE_DIR}/res)
set(api_lib_dir	        ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# API Source Files
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ims_api.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ims_api_h.cmake)

# Generate resources
if (NOT WIN32)
    # Run through sh, as build.sh is not marked executable in git; -e stops at the first failing xxd
    execute_process(
        COMMAND sh -e build.sh
        WORKING_DIRECTORY ${api_resource_dir}
        RESULT_VARIABLE XXD_ERR
    )
    if (XXD_ERR)
        message(FATAL_ERROR "Resource generation (res/build.sh) failed: ${XXD_ERR}. Is xxd installed?")
    endif()
    set (api_db_resource ${api_resource_dir}/imshw.h)
else()
    set (api_db_resource ${api_resource_dir}/imshw.rc)
endif()

if (NOT IMSLIB_SUPPRESS_LIBRARY_BUILD)
    add_library(${IMS_TARGET_NAME} ${IMSLIB_LIB_TYPE} "")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set_target_properties(${IMS_TARGET_NAME} PROPERTIES VERSION ${PROJECT_VERSION})
endif()
set_target_properties(${IMS_TARGET_NAME} PROPERTIES PUBLIC_HEADER "${ims_public_api_header_files}")
set_target_properties(${IMS_TARGET_NAME} PROPERTIES DEBUG_POSTFIX "d")


target_include_directories(${IMS_TARGET_NAME} AFTER 
    PRIVATE
    ${api_include_dir}
    ${api_resource_dir}
    ${api_lib_dir}/spline
    ${api_lib_dir}/sqlite3
    ${Boost_INCLUDE_DIRS}
)

if(WIN32)
target_include_directories(${IMS_TARGET_NAME} AFTER 
    PRIVATE
    ${api_lib_dir}/enumser
    ${api_lib_dir}/FTDI
    ${api_lib_dir}/CyAPI/inc
)
endif()

#
# Compiler specific Definitions
#
if(WIN32)
    # The bundled static FTDI library is MSVC-only; MinGW links against ftd2xx.dll instead
    if(NOT MINGW)
        target_compile_definitions(${IMS_TARGET_NAME} PRIVATE FTD2XX_STATIC)
    endif()
    if (${IMSLIB_BUILD_SHARED})
        target_compile_definitions(${IMS_TARGET_NAME} PRIVATE _EXPORTING_IMS)
    endif()
    target_compile_definitions(${IMS_TARGET_NAME} PRIVATE _WIN32_WINNT=0x0A00)
    target_compile_definitions(${IMS_TARGET_NAME} PRIVATE UNICODE)

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Required by Boost::Log
    target_link_libraries(${IMS_TARGET_NAME} PRIVATE synchronization)
   # Set Warning level always to 4
   #if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
   #  string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
   #else ()
   #  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
   #endif ()    
endif()

if (NOT ${IMSLIB_BUILD_SHARED})
    target_compile_definitions(${IMS_TARGET_NAME} PRIVATE _STATIC_IMS)
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3 -Wall -Wextra -Wcast-qual -Wcast-align -Wno-unused-parameter -Wmissing-include-dirs -Wpointer-arith -Wredundant-decls -Wshadow")
    endif()
endif()

#
# Global Definitions
#
# Conan builds libxml2 statically; MSYS2's libxml2 is a DLL
if(NOT MINGW)
    target_compile_definitions(${IMS_TARGET_NAME} PUBLIC LIBXML_STATIC)
endif()

#
# Building and Linking
#
target_sources(${IMS_TARGET_NAME} 
    PRIVATE 
        ${ims_api_source_files}
        ${api_db_resource}
        ${ims_api_header_files}
 )

target_include_directories(${IMS_TARGET_NAME}
    PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(${IMS_TARGET_NAME} 
    PRIVATE 
#        -Wl,--whole-archive
            Boost::regex
            Boost::log_setup
            Boost::log
#        -Wl,--no-whole-archive        
)

# Conan and CMake's FindLibXml2 both provide the LibXml2::LibXml2 target
if(TARGET LibXml2::LibXml2)
    target_link_libraries(${IMS_TARGET_NAME} PRIVATE LibXml2::LibXml2)
else()
    target_link_libraries(${IMS_TARGET_NAME} PRIVATE ${libxml2_LIBRARIES})
endif()

if(WIN32)
# Visual Studio generators set the platform (Win32/x64); single-config generators (Ninja/MinGW) do not
if(CMAKE_GENERATOR_PLATFORM)
    set(FTDI_PLATFORM ${CMAKE_GENERATOR_PLATFORM})
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FTDI_PLATFORM x64)
else()
    set(FTDI_PLATFORM Win32)
endif()

if(MINGW)
    # The static ftd2xx.lib contains MSVC-compiled C++ and cannot be linked by GCC.
    # Generate an import library for ftd2xx.dll (installed with the FTDI D2XX driver) instead.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "MinGW builds of ${IMS_TARGET_NAME} are only supported for 64-bit targets")
    endif()
    find_program(DLLTOOL_EXECUTABLE NAMES ${CMAKE_CXX_COMPILER_TARGET}-dlltool dlltool
        HINTS "${CMAKE_CXX_COMPILER}/.." REQUIRED)
    set(FTD2XX_IMPORT_LIB ${CMAKE_CURRENT_BINARY_DIR}/libftd2xx.dll.a)
    add_custom_command(
        OUTPUT ${FTD2XX_IMPORT_LIB}
        COMMAND ${DLLTOOL_EXECUTABLE} -d ${api_lib_dir}/FTDI/ftd2xx.def -l ${FTD2XX_IMPORT_LIB}
        DEPENDS ${api_lib_dir}/FTDI/ftd2xx.def
        COMMENT "Generating ftd2xx import library"
    )
    add_custom_target(ftd2xx_import DEPENDS ${FTD2XX_IMPORT_LIB})
    add_dependencies(${IMS_TARGET_NAME} ftd2xx_import)

    target_link_libraries(${IMS_TARGET_NAME} PRIVATE ${FTD2XX_IMPORT_LIB} ws2_32 setupapi)
else()
    add_library(ftd2xx STATIC IMPORTED)

    set_target_properties(ftd2xx PROPERTIES
        IMPORTED_LOCATION
        ${api_lib_dir}/FTDI/Static/${FTDI_PLATFORM}/ftd2xx.lib
    )

    target_link_libraries(${IMS_TARGET_NAME} PRIVATE ftd2xx)
endif()
endif()

## Documentation
option(BUILD_DOCS "Build SDK documentation" OFF)

if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    find_package(LATEX REQUIRED)

    set(STAGE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/stage/include")

    configure_file(
        ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in
        ${CMAKE_BINARY_DIR}/Doxyfile
        @ONLY
    )

    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating SDK documentation with Doxygen"
    )

    add_custom_target(docs_pdf ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/docs/latex pdflatex refman.tex
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/docs/latex pdflatex refman.tex
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/docs/latex/refman.pdf
            ${CMAKE_BINARY_DIR}/docs/IMS_SDK.pdf
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating IMS SDK PDF documentation in ${CMAKE_BINARY_DIR}/docs/latex"
    )
endif()

# Only do these things when building standalone
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    #
    # Set Install options
    # https://decovar.dev/blog/2021/03/08/cmake-cpp-library/
    if(DEFINED CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        message(
            STATUS
            "CMAKE_INSTALL_PREFIX is not set\n"
            "Default value: ${CMAKE_INSTALL_PREFIX}\n"
            "Will set it to ${CMAKE_SOURCE_DIR}/install"
        )
        set(CMAKE_INSTALL_PREFIX
            "${CMAKE_SOURCE_DIR}/install"
            CACHE PATH "Where the library will be installed to" FORCE
        )
    else()
        message(
            STATUS
            "CMAKE_INSTALL_PREFIX was already set\n"
            "Current value: ${CMAKE_INSTALL_PREFIX}"
        )
    endif()

    install(
        TARGETS ${IMS_TARGET_NAME}
        EXPORT "${IMS_TARGET_NAME}Targets"
    #    COMPONENT ${IMS_TARGET_NAME} # must be here, not any line lower
        # these get default values from GNUInstallDirs, no need to set them
        #RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # bin
        #LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} # lib
        #ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # lib
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${IMS_TARGET_NAME} # include
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # include
    )

    if (NOT ${BUILD_SHARED_LIBS})
    # If building a static library, we must export Boost::regex and the user must link to it
    # Otherwise we get a configuration error where Boost::regex is not part of the export set
    install(
        TARGETS Boost_regex Boost_log_setup Boost_log Boost_atomic Boost_date_time Boost_filesystem Boost_thread Boost_chrono
        EXPORT "${IMS_TARGET_NAME}Targets"
    )
    endif()

    install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${IMS_TARGET_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${IMS_TARGET_NAME}ConfigVersion.cmake"
        DESTINATION cmake
    #   COMPONENT ${IMS_TARGET_NAME}
    )

    # generate and install export file
    install(EXPORT "${IMS_TARGET_NAME}Targets"
        FILE "${IMS_TARGET_NAME}Targets.cmake"
        DESTINATION cmake
    )
    # generate the export targets for the build tree
    export(EXPORT "${IMS_TARGET_NAME}Targets"
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/${IMS_TARGET_NAME}Targets.cmake"
    )

    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${IMS_TARGET_NAME}ConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY AnyNewerVersion
    )
    # create config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/${IMS_TARGET_NAME}Config.cmake"
        INSTALL_DESTINATION cmake#/${IMS_TARGET_NAME}
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${IMS_TARGET_NAME}ConfigVersion.cmake"
        DESTINATION cmake
    )

    set(CPACK_PROJECT_NAME ${PROJECT_NAME})
    set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging.cmake)
endif()