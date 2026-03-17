cmake_minimum_required(VERSION 3.10)
project(fex C)

if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()

include(CTest)

set(CMAKE_C_STANDARD 90)

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
