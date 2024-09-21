cmake_minimum_required(VERSION 3.15)
project(filesniper)

# Set C++ standard and compile options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

# Define the executable target
add_executable(${PROJECT_NAME} main.cpp)

# Link the ncurses library
target_link_libraries(${PROJECT_NAME} ncurses)
