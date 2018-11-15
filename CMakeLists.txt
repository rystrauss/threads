cmake_minimum_required(VERSION 3.12)
project(threads C)

set(CMAKE_C_STANDARD 11)

add_executable(producer-consumer1 producer-consumer1.c threads.c synchronization.c ll_double.c)
add_executable(producer-consumer2 producer-consumer2.c threads.c synchronization.c ll_double.c)