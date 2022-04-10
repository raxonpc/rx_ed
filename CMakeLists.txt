cmake_minimum_required(VERSION 3.21)
project(rx_ed C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_OSX_ARCHITECTURES arm64)

add_executable(rx_ed main.c)
