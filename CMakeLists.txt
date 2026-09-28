cmake_minimum_required(VERSION 3.20.0)
# The application requires the motor and encoder nodes from this overlay.
if(NOT DTC_OVERLAY_FILE)
  set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f401re.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab2)

target_sources(app PRIVATE
  src/blinker.c
  src/main.c
  src/encoder.c
  src/encoder_monitor.c
  src/motor.c
  src/motor_controller.c
  src/pedal_control.c
  src/quadrature.c
  src/uart_receiver.c
)
target_include_directories(app PRIVATE include)
