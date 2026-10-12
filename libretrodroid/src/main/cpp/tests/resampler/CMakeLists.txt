cmake_minimum_required(VERSION 3.13)
project(audio_resampler_tests LANGUAGES CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(audio_resampler_tests
    streaminglinearresampler_test.cpp
    ../../resamplers/streaminglinearresampler.cpp
)
target_include_directories(audio_resampler_tests PRIVATE ../../resamplers)
add_test(NAME audio_resampler_tests COMMAND audio_resampler_tests)
