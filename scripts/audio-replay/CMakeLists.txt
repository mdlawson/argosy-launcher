cmake_minimum_required(VERSION 3.16)
project(CeresAudioCompare LANGUAGES CXX)

if(NOT SNES9X_SOURCE_DIR OR NOT LIBRETRO_COMMON_DIR)
    message(FATAL_ERROR "Set SNES9X_SOURCE_DIR and LIBRETRO_COMMON_DIR")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(CeresAudioCompare
    CeresAudioCompare.cpp
    ${SNES9X_SOURCE_DIR}/apu/bapu/smp/smp.cpp
    ${SNES9X_SOURCE_DIR}/apu/bapu/smp/smp_state.cpp
    ${SNES9X_SOURCE_DIR}/apu/bapu/dsp/sdsp.cpp
)
target_compile_features(CeresAudioCompare PRIVATE cxx_std_17)
target_compile_definitions(CeresAudioCompare PRIVATE SKIP_STDIO_REDEFINES _CRT_SECURE_NO_WARNINGS)
target_include_directories(CeresAudioCompare PRIVATE ${SNES9X_SOURCE_DIR} ${LIBRETRO_COMMON_DIR}/include)

enable_testing()
add_test(NAME replay-self-test COMMAND CeresAudioCompare --self-test)
