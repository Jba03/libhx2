cmake_minimum_required(VERSION 3.1)

project(hx2)

add_library(hx2 SHARED hx2.c hx2.h stream.c stream.h waveformat.c waveformat.h)
set_property(TARGET hx2 PROPERTY C_STANDARD 99)