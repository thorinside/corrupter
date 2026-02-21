cmake_minimum_required(VERSION 3.20)
project(corrupter_dsp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(corrupter_dsp STATIC
  src/engine.cpp
  src/c_api.cpp
)

target_include_directories(corrupter_dsp
  PUBLIC
    include
)

target_compile_options(corrupter_dsp
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(corrupter_dsp_smoke
  examples/offline_smoke.cpp
)

target_link_libraries(corrupter_dsp_smoke
  PRIVATE
    corrupter_dsp
)
