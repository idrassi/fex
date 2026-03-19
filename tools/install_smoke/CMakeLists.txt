cmake_minimum_required(VERSION 3.10)
project(fex_install_smoke C)

find_package(fex CONFIG REQUIRED)

add_executable(fex_install_smoke main.c)
target_link_libraries(fex_install_smoke PRIVATE fex::fex)
