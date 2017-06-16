cmake_minimum_required(VERSION 3.7)
project(UnrealUWFTesting)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES main.cpp CircularBuffer.h.h Vector.h.h)
add_executable(UnrealUWFTesting ${SOURCE_FILES})

if(MINGW)
    target_link_libraries(UnrealUWFTesting ws2_32)
endif()