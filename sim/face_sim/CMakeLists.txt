# PetBot face simulator: firmware/lib/face on LVGL v9 + SDL2.
cmake_minimum_required(VERSION 3.16)
project(face_sim C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  message(FATAL_ERROR "SDL2 development files not found. Install them, then re-run cmake:\n"
                      "  Debian/Ubuntu: sudo apt install libsdl2-dev\n"
                      "  Termux:        pkg install sdl2")
endif()

# LVGL, pinned to an exact release tag.
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES} m)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(face_sim
  main.cpp
  ${REPO}/firmware/lib/face/src/face.c
  ${REPO}/firmware/lib/face/src/face_draw.c
  ${REPO}/firmware/src/protocol.cpp)
target_include_directories(face_sim PRIVATE ${REPO}/firmware/lib/face/src ${REPO}/firmware/src)
target_compile_options(face_sim PRIVATE -Wall -Wextra)
target_link_libraries(face_sim PRIVATE lvgl)
