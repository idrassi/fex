cmake_minimum_required(VERSION 3.10)
project(fex C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

add_executable(fex src/fex.c src/fex_span.c src/fe.c src/main.c)