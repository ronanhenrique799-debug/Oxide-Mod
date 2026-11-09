# CMakeLists.txt
cmake_minimum_required(VERSION 3.18.1)
project("kanha_oxide")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fvisibility=hidden")

add_library(kanha_oxide SHARED jni/Main.cpp)

find_library(log-lib log)
target_link_libraries(kanha_oxide ${log-lib})
