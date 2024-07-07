cmake_minimum_required(VERSION 3.1)

project(hx2)

# libhx2
add_library(hx2 SHARED hx2.c hx2.h)
set_property(TARGET hx2 PROPERTY C_STANDARD 99)

# hxtool
add_executable(hxtool hxtool.c)
target_link_libraries(hxtool hx2)