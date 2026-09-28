cmake_minimum_required(VERSION 3.20.0)
# The application requires the motor and encoder nodes from this overlay.
if(NOT DTC_OVERLAY_FILE)
  set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f401re.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab2)

target_sources(app PRIVATE src/main.c src/encoder.c src/motor.c)
target_include_directories(app PRIVATE include)

option(LAB_MOTOR_TEST "Enable USER-button one-shot left motor test" OFF)
if(LAB_MOTOR_TEST)
  target_sources(app PRIVATE src/motor_test.c)
  target_compile_definitions(app PRIVATE LAB_MOTOR_TEST=1)
endif()
