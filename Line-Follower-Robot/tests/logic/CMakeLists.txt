cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(line_follower_logic_test C)

set(app_dir ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_include_directories(app PRIVATE ${app_dir}/src src)

# Mã thật của firmware; phần cứng được thay bằng src/fakes.c
target_sources(app PRIVATE
  ${app_dir}/src/clock_format.c
  ${app_dir}/src/destination_manager.c
  ${app_dir}/src/drive.c
  ${app_dir}/src/events.c
  ${app_dir}/src/json_flat.c
  ${app_dir}/src/line_pid.c
  ${app_dir}/src/obstacle_filter.c
  ${app_dir}/src/path_control.c
  ${app_dir}/src/robot_logic.c
  ${app_dir}/src/robot_state.c
  ${app_dir}/src/route.c
  ${app_dir}/src/wav.c
  ${app_dir}/src/web_protocol.c
  src/fakes.c
  src/test_parsers.c
  src/test_robot.c
  src/test_web.c
)

generate_inc_file_for_target(app ${app_dir}/assets/audio/welcome.wav
  ${ZEPHYR_BINARY_DIR}/include/generated/welcome.wav.inc)
