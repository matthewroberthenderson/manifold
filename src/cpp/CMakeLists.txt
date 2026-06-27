cmake_minimum_required(VERSION 3.10)
project(WasmRenderer)


set(CMAKE_CXX_STANDARD 17)

# Emscripten specific flags
set(EMSCRIPTEN_FLAGS 
    "-s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='createRenderer' \
    -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
    -s EXPORTED_FUNCTIONS=['_update_input', '_add_cam_yaw', '_clear_map', '_add_wall', '_set_player_pos', '_main'] \ 
    -lGL")

add_executable(renderer main.cpp)

set_target_properties(renderer PROPERTIES 
    LINK_FLAGS "${EMSCRIPTEN_FLAGS}"
    SUFFIX ".js")