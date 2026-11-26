cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(line_follower_robot C)

target_sources(app PRIVATE
  src/fatal.c
  src/main.c
  src/motor_control.c
  src/robot_state.c
)
target_sources_ifdef(CONFIG_APP_BRINGUP_SHELL app PRIVATE src/bringup_shell.c)

if(NOT CONFIG_APP_BRINGUP_SHELL)
  target_sources(app PRIVATE
    src/audio.c
    src/clock_display.c
    src/clock_format.c
    src/control_thread.c
    src/destination_manager.c
    src/drive.c
    src/events.c
    src/json_flat.c
    src/line_pid.c
    src/network.c
    src/obstacle_filter.c
    src/obstacle_monitor.c
    src/path_control.c
    src/robot_logic.c
    src/route.c
    src/safety.c
    src/tables_storage.c
    src/web_protocol.c
    src/wav.c
    src/web_server.c
  )

  # Chỉ để dùng websocket_internal.h (xem web_server.c)
  target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/net/lib/websocket)

  # Iterable section cho resource của service "robot_http"
  zephyr_linker_sources(SECTIONS sections-rom.ld)
  zephyr_linker_section(NAME http_resource_desc_robot_http
    KVMA RAM_REGION GROUP RODATA_REGION
  )
endif()

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app assets/tables.json ${gen_dir}/tables.json.inc)

generate_inc_file_for_target(app assets/audio/welcome.wav
  ${gen_dir}/welcome.wav.inc)

foreach(web_file index.html app.js style.css)
  generate_inc_file_for_target(app assets/web/${web_file}
    ${gen_dir}/${web_file}.gz.inc --gzip)
endforeach()
