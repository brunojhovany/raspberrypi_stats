cmake_minimum_required(VERSION 3.10)
project(raspberrypi_stats_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(raspberrypi_stats_cpp
    src/main.cpp
    src/ssd1306.cpp
    src/stats.cpp
)

target_include_directories(raspberrypi_stats_cpp PRIVATE src)

target_link_libraries(raspberrypi_stats_cpp PRIVATE
    pthread
)

# i2c-dev lives in the kernel; just need headers at build time (libi2c-dev)
# No extra link library required on most systems.
