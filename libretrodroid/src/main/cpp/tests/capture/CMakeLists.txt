cmake_minimum_required(VERSION 3.13)
project(audio_capture_tests LANGUAGES CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(audio_capture_fixture
    capture_fixture.cpp
    ../../audiocapture.cpp
    ../../audiodiagnostics.cpp
)
target_include_directories(audio_capture_fixture PRIVATE stubs ../..)
target_link_libraries(audio_capture_fixture PRIVATE Threads::Threads)
add_test(NAME audio_capture_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_capture.py
            $<TARGET_FILE:audio_capture_fixture>
)
