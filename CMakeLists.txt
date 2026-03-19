cmake_minimum_required(VERSION 3.10)
project(fex VERSION 1.0 LANGUAGES C)

if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 90)
option(FEX_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FEX_ENABLE_EXTRA_WARNINGS "Enable extra compiler warnings on supported compilers" ON)
option(FEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on supported compilers" OFF)

function(fex_apply_target_defaults target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/O2>)
    else()
        target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:-O2>)
    endif()

    if(FEX_ENABLE_EXTRA_WARNINGS)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE -Wall -Wextra)
            if(FEX_WARNINGS_AS_ERRORS)
                target_compile_options(${target_name} PRIVATE -Werror)
            endif()
        endif()
    endif()
endfunction()

if(FEX_ENABLE_SANITIZERS)
    if(WIN32)
        message(FATAL_ERROR "FEX_ENABLE_SANITIZERS is currently supported only on non-Windows GCC or Clang builds")
    endif()
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "FEX_ENABLE_SANITIZERS requires a GCC- or Clang-compatible compiler")
    endif()

    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address,undefined -fno-omit-frame-pointer")
endif()

add_library(fexlib STATIC
    src/fex.c
    src/fex_span.c
    src/fe.c
    src/fex_builtins.c
    src/sfc32.c
)
set_target_properties(fexlib PROPERTIES
    OUTPUT_NAME fex
    EXPORT_NAME fex
)
add_library(fex::fex ALIAS fexlib)
target_include_directories(
    fexlib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fex>
)
fex_apply_target_defaults(fexlib)

# Link math library on Unix systems, not needed on Windows
if(UNIX)
    target_link_libraries(fexlib PUBLIC m)
endif()

add_executable(fex src/main.c)
target_link_libraries(fex PRIVATE fexlib)
fex_apply_target_defaults(fex)

if(BUILD_TESTING)
    find_package(PythonInterp QUIET)
    add_executable(fex_try_api_test tools/test_try_api.c)
    target_link_libraries(fex_try_api_test PRIVATE fexlib)
    fex_apply_target_defaults(fex_try_api_test)
    if(PYTHONINTERP_FOUND)
        target_compile_definitions(
            fex_try_api_test PRIVATE
            FEX_TEST_PYTHON_EXECUTABLE="${PYTHON_EXECUTABLE}"
        )
    endif()

    add_test(NAME fex_try_api COMMAND $<TARGET_FILE:fex_try_api_test>)

    if(PYTHONINTERP_FOUND)
        add_test(
            NAME fex_regressions
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run_regression_tests.py --exe $<TARGET_FILE:fex>
        )
    endif()
endif()

install(
    TARGETS fexlib
    EXPORT fexTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fex
)

install(
    TARGETS fex
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    FILES
        src/fe.h
        src/fex.h
        src/fex_builtins.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fex
)

install(
    FILES
        src/fe.c
        src/fex.c
        src/fex_span.c
        src/fex_builtins.c
        src/sfc32.c
        src/sfc32.h
        src/fe_fixnum.h
        src/fex_internal.h
        src/fex_span.h
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fex/src
)

install(
    FILES
        README.md
        LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fex
)

install(
    DIRECTORY doc/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fex/doc
    FILES_MATCHING
    PATTERN "*.md"
)

install(
    FILES
        scripts/test.fex
        scripts/fib.fex
        scripts/life.fex
        scripts/mandelbrot.fex
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fex/examples
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fexConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/fexConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fex
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/fexConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT fexTargets
    FILE fexTargets.cmake
    NAMESPACE fex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fex
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/fexConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/fexConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fex
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fex.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/fex.pc
    @ONLY
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/fex.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

set(CPACK_PACKAGE_NAME "fex")
set(CPACK_PACKAGE_VENDOR "Mounir IDRASSI")
set(CPACK_PACKAGE_CONTACT "mounir.idrassi@amcrypto.jp")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "FeX embeddable scripting language")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "fex")
set(CPACK_PACKAGE_FILE_NAME "fex-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
