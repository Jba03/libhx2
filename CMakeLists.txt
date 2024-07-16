cmake_minimum_required(VERSION 3.1)

project(hx2)

# libhx2
add_library(hx2 SHARED hx2.c hx2.h stream.c stream.h waveformat.c waveformat.h codec.c)
set_property(TARGET hx2 PROPERTY C_STANDARD 99)

# hxtool
add_executable(hxtool hxtool.c)
set_property(TARGET hxtool PROPERTY C_STANDARD 99)
target_link_libraries(hxtool hx2)

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(AVCODEC libavcodec)
  pkg_check_modules(AVFORMAT libavformat)
  pkg_check_modules(AVUTIL libavutil)
  
  set(LIBAV_INCLUDE_PATHS /usr/include /usr/local/include /opt/local/include)
  set(LIBAV_LIBRARY_PATHS /usr/lib /usr/local/lib /opt/local/lib)
  set(LIBAV_PATH_SUFFIX libav ffmpeg)
  
  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS ${LIBAV_INCLUDE_PATHS} ${AVCODEC_INCLUDE_DIRS} PATH_SUFFIXES ${LIBAV_PATH_SUFFIX})
  find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h PATHS ${LIBAV_INCLUDE_PATHS} ${AVFORMAT_INCLUDE_DIRS} PATH_SUFFIXES ${LIBAV_PATH_SUFFIX})
  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS ${LIBAV_INCLUDE_PATHS} ${AVUTIL_INCLUDE_DIRS} PATH_SUFFIXES ${LIBAV_PATH_SUFFIX})
  set(LIBAV_INCLUDE_DIRS ${AVCODEC_INCLUDE_DIR} ${AVFORMAT_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR})

  if (AVCODEC_LIBRARIES)
    string(ASCII 27 Esc)
    message("${Esc}[1;32mUsing libavcodec for hxtool")
    target_include_directories(hxtool PRIVATE ${LIBAV_INCLUDE_DIRS})
    target_link_directories(hxtool PRIVATE ${AVCODEC_LIBRARY_DIRS})
    target_compile_definitions(hxtool PRIVATE USE_LIBAV=1)
    target_link_libraries(hxtool avcodec)
  endif(AVCODEC_INCLUDE_DIR)
endif()
