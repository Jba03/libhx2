cmake_minimum_required(VERSION 3.1)

project(hx2)

# libhx2
add_library(hx2 SHARED hx2.c hx2.h stream.c stream.h waveformat.c waveformat.h codec.c)
set_property(TARGET hx2 PROPERTY C_STANDARD 99)

# hxtool
add_executable(hxtool hxtool.c)
set_property(TARGET hxtool PROPERTY C_STANDARD 99)
target_link_libraries(hxtool hx2)
