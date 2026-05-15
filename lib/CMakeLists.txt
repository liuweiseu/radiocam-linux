# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Project name and language
project(SDR_ADC_Control C)

# Specify C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Enable compiler warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# 1. Create a library out of ALL your core SDR files
# We simply add sdr_transfer.c to the list of source files here.
add_library(sdr_lib STATIC 
    sdr_config.c
    sdr_transfer.c
)

# 2. Build the FIRST executable (Your config test)
add_executable(adc_config_test adc_config_test.c)
target_link_libraries(adc_config_test sdr_lib)

# 3. Build the SECOND executable (e.g., your actual data capture app)
add_executable(sdr_transfer_test sdr_test.c)
target_link_libraries(sdr_transfer_test sdr_lib)