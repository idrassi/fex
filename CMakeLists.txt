cmake_minimum_required(VERSION 3.10)
project(fex C)

include(CTest)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

add_executable(fex src/fex.c src/fex_span.c src/fe.c src/main.c src/fex_builtins.c src/sfc32.c)

# Link math library on Unix systems, not needed on Windows
if(UNIX)
    target_link_libraries(fex m)
endif()

if(BUILD_TESTING)
    find_package(PythonInterp QUIET)
    if(PYTHONINTERP_FOUND)
        add_test(
            NAME fex_regressions
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run_regression_tests.py --exe $<TARGET_FILE:fex>
        )
    endif()
endif()
