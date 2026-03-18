cmake_minimum_required(VERSION 3.10)
project(fex C)

if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()

include(CTest)

set(CMAKE_C_STANDARD 90)
option(FEX_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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

add_executable(fex src/fex.c src/fex_span.c src/fe.c src/main.c src/fex_builtins.c src/sfc32.c)
target_include_directories(fex PRIVATE src)

if(MSVC)
    target_compile_options(fex PRIVATE $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(fex PRIVATE $<$<CONFIG:Release>:-O2>)
endif()

# Link math library on Unix systems, not needed on Windows
if(UNIX)
    target_link_libraries(fex m)
endif()

if(BUILD_TESTING)
    add_executable(fex_try_api_test src/fex.c src/fex_span.c src/fe.c src/fex_builtins.c src/sfc32.c tools/test_try_api.c)
    target_include_directories(fex_try_api_test PRIVATE src)
    if(MSVC)
        target_compile_options(fex_try_api_test PRIVATE $<$<CONFIG:Release>:/O2>)
    else()
        target_compile_options(fex_try_api_test PRIVATE $<$<CONFIG:Release>:-O2>)
    endif()
    if(UNIX)
        target_link_libraries(fex_try_api_test m)
    endif()

    add_test(NAME fex_try_api COMMAND $<TARGET_FILE:fex_try_api_test>)

    find_package(PythonInterp QUIET)
    if(PYTHONINTERP_FOUND)
        add_test(
            NAME fex_regressions
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run_regression_tests.py --exe $<TARGET_FILE:fex>
        )
    endif()
endif()
