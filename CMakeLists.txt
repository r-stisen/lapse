# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.



cmake_minimum_required(VERSION 3.22)

project(lapse
    VERSION 2.0.0
    DESCRIPTION "Language-agnostic subtitle synchronization engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

option(LAPSE_FETCH_MISSING "download and build the dependencies the system does not have" ON)
option(LAPSE_STATIC_DEPS   "link the dependencies statically, the way the releases do" OFF)
option(LAPSE_LTO           "link time optimisation, worth a few percent, costs build time" ON)
set(LAPSE_DEPS_PREFIX "" CACHE PATH "prefix that .github/scripts/build-*.sh installed into")


if(LAPSE_DEPS_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${LAPSE_DEPS_PREFIX}")
    set(LAPSE_STATIC_DEPS ON)
endif()

include(FetchContent)
find_package(PkgConfig)

if(PkgConfig_FOUND AND LAPSE_STATIC_DEPS)
    set(PKG_CONFIG_ARGN --static)
endif()



add_library(lapse_ffmpeg INTERFACE)

if(LAPSE_DEPS_PREFIX)
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
        PATHS "${LAPSE_DEPS_PREFIX}/include" NO_DEFAULT_PATH)
    
    foreach(lib avformat avcodec swresample avutil)
        find_library(FFMPEG_${lib}_LIBRARY ${lib}
            PATHS "${LAPSE_DEPS_PREFIX}/lib" NO_DEFAULT_PATH)
        list(APPEND FFMPEG_PREFIX_LIBRARIES "${FFMPEG_${lib}_LIBRARY}")
    endforeach()
    if(FFMPEG_INCLUDE_DIR AND NOT "FFMPEG_avformat_LIBRARY-NOTFOUND" IN_LIST FFMPEG_PREFIX_LIBRARIES
        AND NOT "FFMPEG_avcodec_LIBRARY-NOTFOUND" IN_LIST FFMPEG_PREFIX_LIBRARIES
        AND NOT "FFMPEG_swresample_LIBRARY-NOTFOUND" IN_LIST FFMPEG_PREFIX_LIBRARIES
        AND NOT "FFMPEG_avutil_LIBRARY-NOTFOUND" IN_LIST FFMPEG_PREFIX_LIBRARIES)
        set(FFMPEG_FROM_PREFIX TRUE)
    endif()
endif()

if(NOT FFMPEG_FROM_PREFIX AND PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
endif()

if(FFMPEG_FROM_PREFIX)
    target_include_directories(lapse_ffmpeg INTERFACE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(lapse_ffmpeg INTERFACE ${FFMPEG_PREFIX_LIBRARIES})
    set(LAPSE_FFMPEG_FROM "static, from ${LAPSE_DEPS_PREFIX}")
elseif(FFMPEG_FOUND)
    target_link_libraries(lapse_ffmpeg INTERFACE PkgConfig::FFMPEG)
    set(LAPSE_FFMPEG_FROM "system (${FFMPEG_libavformat_VERSION})")
else()
    message(FATAL_ERROR
        "FFmpeg not found. Install the development packages:\n"
        "  macOS          brew install ffmpeg pkg-config\n"
        "  Debian/Ubuntu  sudo apt install libavformat-dev libavcodec-dev "
        "libavutil-dev libswresample-dev pkg-config\n"
        "  Fedora         sudo dnf install ffmpeg-devel pkgconf-pkg-config\n"
        "  MSYS2          pacman -S \${MINGW_PACKAGE_PREFIX}-ffmpeg "
        "\${MINGW_PACKAGE_PREFIX}-pkgconf\n"
        "Or build a static one with .github/scripts/build-ffmpeg.sh \"\$HOME/deps\" "
        "and configure with -DLAPSE_DEPS_PREFIX=\$HOME/deps")
endif()


add_library(lapse_fftw3 INTERFACE)

if(LAPSE_DEPS_PREFIX)
    find_path(FFTW3_INCLUDE_DIR fftw3.h
        PATHS "${LAPSE_DEPS_PREFIX}/include" NO_DEFAULT_PATH)
    find_library(FFTW3_LIBRARY NAMES fftw3 libfftw3
        PATHS "${LAPSE_DEPS_PREFIX}/lib" NO_DEFAULT_PATH)
endif()
if(NOT (FFTW3_INCLUDE_DIR AND FFTW3_LIBRARY) AND PkgConfig_FOUND)
    pkg_check_modules(FFTW3 IMPORTED_TARGET fftw3)
endif()
if(NOT FFTW3_FOUND AND NOT (FFTW3_INCLUDE_DIR AND FFTW3_LIBRARY))
    find_path(FFTW3_INCLUDE_DIR fftw3.h)
    find_library(FFTW3_LIBRARY NAMES fftw3 libfftw3)
endif()

if(FFTW3_FOUND)
    target_link_libraries(lapse_fftw3 INTERFACE PkgConfig::FFTW3)
    set(LAPSE_FFTW3_FROM "system (${FFTW3_VERSION})")
elseif(FFTW3_INCLUDE_DIR AND FFTW3_LIBRARY)
    target_include_directories(lapse_fftw3 INTERFACE "${FFTW3_INCLUDE_DIR}")
    target_link_libraries(lapse_fftw3 INTERFACE "${FFTW3_LIBRARY}")
    set(LAPSE_FFTW3_FROM "${FFTW3_LIBRARY}")
elseif(LAPSE_FETCH_MISSING)

    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(lapse_shared_was "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS      OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENMP    OFF CACHE BOOL "" FORCE)
    set(ENABLE_THREADS   OFF CACHE BOOL "" FORCE)
    set(ENABLE_FLOAT     OFF CACHE BOOL "" FORCE)

    set(lapse_fftw_exclude "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        set(lapse_fftw_exclude EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(fftw3
        URL      https://www.fftw.org/fftw-3.3.10.tar.gz
        URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
        ${lapse_fftw_exclude})
    message(STATUS "FFTW3 not found, building fftw-3.3.10 in the build tree")
    FetchContent_MakeAvailable(fftw3)
    set(BUILD_SHARED_LIBS "${lapse_shared_was}" CACHE BOOL "" FORCE)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    target_include_directories(lapse_fftw3 INTERFACE "${fftw3_SOURCE_DIR}/api")
    target_link_libraries(lapse_fftw3 INTERFACE fftw3)
    set(LAPSE_FFTW3_FROM "built here (3.3.10)")
else()
    message(FATAL_ERROR
        "FFTW3 not found and -DLAPSE_FETCH_MISSING=OFF.\n"
        "  macOS          brew install fftw\n"
        "  Debian/Ubuntu  sudo apt install libfftw3-dev\n"
        "  MSYS2          pacman -S \${MINGW_PACKAGE_PREFIX}-fftw")
endif()


add_library(lapse_fvad INTERFACE)

if(LAPSE_DEPS_PREFIX)
    find_path(FVAD_INCLUDE_DIR fvad.h
        PATHS "${LAPSE_DEPS_PREFIX}/include" NO_DEFAULT_PATH)
    find_library(FVAD_LIBRARY NAMES fvad libfvad
        PATHS "${LAPSE_DEPS_PREFIX}/lib" NO_DEFAULT_PATH)
endif()
if(NOT (FVAD_INCLUDE_DIR AND FVAD_LIBRARY) AND PkgConfig_FOUND)
    pkg_check_modules(FVAD IMPORTED_TARGET libfvad)
endif()
if(NOT FVAD_FOUND AND NOT (FVAD_INCLUDE_DIR AND FVAD_LIBRARY))
    find_path(FVAD_INCLUDE_DIR fvad.h)
    find_library(FVAD_LIBRARY NAMES fvad libfvad)
endif()

if(FVAD_FOUND)
    target_link_libraries(lapse_fvad INTERFACE PkgConfig::FVAD)
    set(LAPSE_FVAD_FROM "system (${FVAD_VERSION})")
elseif(FVAD_INCLUDE_DIR AND FVAD_LIBRARY)
    target_include_directories(lapse_fvad INTERFACE "${FVAD_INCLUDE_DIR}")
    target_link_libraries(lapse_fvad INTERFACE "${FVAD_LIBRARY}")
    set(LAPSE_FVAD_FROM "${FVAD_LIBRARY}")
elseif(LAPSE_FETCH_MISSING)
    FetchContent_Declare(libfvad
        GIT_REPOSITORY https://github.com/dpirch/libfvad.git
        GIT_TAG        532ab666c20d3cfda38bca63abbb0f152706c369
        GIT_SHALLOW    FALSE
        SOURCE_SUBDIR  cmake-does-not-live-here)
    message(STATUS "libfvad not found, building it in the build tree")
    FetchContent_MakeAvailable(libfvad)

    add_library(fvad STATIC
        "${libfvad_SOURCE_DIR}/src/fvad.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/division_operations.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/energy.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/get_scaling_square.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/resample_48khz.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/resample_by_2_internal.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/resample_fractional.c"
        "${libfvad_SOURCE_DIR}/src/signal_processing/spl_inl.c"
        "${libfvad_SOURCE_DIR}/src/vad/vad_core.c"
        "${libfvad_SOURCE_DIR}/src/vad/vad_filterbank.c"
        "${libfvad_SOURCE_DIR}/src/vad/vad_gmm.c"
        "${libfvad_SOURCE_DIR}/src/vad/vad_sp.c")
    target_include_directories(fvad
        PUBLIC  "${libfvad_SOURCE_DIR}/include"
        PRIVATE "${libfvad_SOURCE_DIR}/src")
    set_target_properties(fvad PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

    target_link_libraries(lapse_fvad INTERFACE fvad)
    set(LAPSE_FVAD_FROM "built here (532ab66)")
else()
    message(FATAL_ERROR
        "libfvad not found and -DLAPSE_FETCH_MISSING=OFF.\n"
        "Build it with .github/scripts/build-libfvad.sh \"\$HOME/deps\" and "
        "configure with -DLAPSE_DEPS_PREFIX=\$HOME/deps")
endif()


add_executable(lapse
    engine/main.cpp
    engine/correlate.cpp
    engine/align.cpp
    engine/decoder.cpp
    engine/srt_parser.cpp
    engine/write_subtitle.cpp
    engine/silero.cpp
    engine/cuetext.cpp
    engine/charset.cpp
    engine/log.cpp)

target_compile_features(lapse PRIVATE cxx_std_20)
target_include_directories(lapse PRIVATE engine)
target_link_libraries(lapse PRIVATE lapse_ffmpeg lapse_fftw3 lapse_fvad)

find_package(Threads REQUIRED)
target_link_libraries(lapse PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    target_link_libraries(lapse PRIVATE m)
endif()


if(LAPSE_STATIC_DEPS)
    set(lapse_suffixes_was "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()
find_package(ZLIB)
if(LAPSE_STATIC_DEPS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${lapse_suffixes_was}")
endif()
if(ZLIB_FOUND)
    target_link_libraries(lapse PRIVATE ZLIB::ZLIB)
endif()

if(NOT MSVC)
    target_compile_options(lapse PRIVATE -fno-math-errno)
endif()

if(APPLE)
    target_link_libraries(lapse PRIVATE
        "-framework CoreFoundation" "-framework CoreServices" "-framework Security")
elseif(WIN32)
    enable_language(RC)
    target_sources(lapse PRIVATE engine/lapse.rc)
    target_link_libraries(lapse PRIVATE ws2_32 bcrypt secur32 ole32 user32)
endif()

if(LAPSE_STATIC_DEPS)
    if(MINGW)
        target_link_options(lapse PRIVATE -static)
    elseif(NOT APPLE AND NOT MSVC)
        target_link_options(lapse PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

if(LAPSE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lapse_ipo OUTPUT lapse_ipo_why)
    if(lapse_ipo)
        set_property(TARGET lapse PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(STATUS "link time optimisation unavailable: ${lapse_ipo_why}")
    endif()
endif()

foreach(runtime_file libonnxruntime.dylib libonnxruntime.so onnxruntime.dll silero_vad.onnx)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine/${runtime_file}")
        add_custom_command(TARGET lapse POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/engine/${runtime_file}"
                "$<TARGET_FILE_DIR:lapse>/${runtime_file}"
            VERBATIM)
    endif()
endforeach()

include(GNUInstallDirs)
install(TARGETS lapse RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")



enable_testing()
find_program(BASH_EXECUTABLE bash)
if(BASH_EXECUTABLE)
    add_test(NAME formats COMMAND lapse --formats)
    add_test(NAME smoke
        COMMAND "${BASH_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/.github/scripts/smoke-engine.sh"
                "$<TARGET_FILE:lapse>")
endif()

message(STATUS "")
message(STATUS "lapse ${PROJECT_VERSION} (${CMAKE_BUILD_TYPE})")
message(STATUS "  FFmpeg   ${LAPSE_FFMPEG_FROM}")
message(STATUS "  FFTW3    ${LAPSE_FFTW3_FROM}")
message(STATUS "  libfvad  ${LAPSE_FVAD_FROM}")
message(STATUS "")
