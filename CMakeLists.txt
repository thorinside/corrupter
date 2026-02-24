cmake_minimum_required(VERSION 3.20)
project(corrupter_dsp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
option(CORRUPTER_ENABLE_COVERAGE "Enable coverage instrumentation for tests" OFF)

if(CORRUPTER_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-O0 -g -fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
  else()
    message(WARNING "Coverage requested but compiler is unsupported: ${CMAKE_CXX_COMPILER_ID}")
  endif()
endif()

add_library(corrupter_dsp STATIC
  src/engine.cpp
  src/c_api.cpp
  src/pitch_quantizer.cpp
  src/internal/clock_engine.cpp
  src/internal/corrupt_engine.cpp
)

target_include_directories(corrupter_dsp
  PUBLIC
    include
  PRIVATE
    src
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

add_executable(corrupter_dsp_regression
  examples/regression_tests.cpp
)

target_link_libraries(corrupter_dsp_regression
  PRIVATE
    corrupter_dsp
)

target_include_directories(corrupter_dsp_regression
  PRIVATE
    src
)

add_test(
  NAME corrupter_dsp_regression
  COMMAND corrupter_dsp_regression
)

add_executable(corrupter_dsp_benchmark
  examples/benchmark.cpp
)

target_link_libraries(corrupter_dsp_benchmark
  PRIVATE
    corrupter_dsp
)

if(CORRUPTER_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  find_program(CORRUPTER_LLVM_PROFDATA llvm-profdata)
  find_program(CORRUPTER_LLVM_COV llvm-cov)
  find_program(CORRUPTER_XCRUN xcrun)
  set(CORRUPTER_LLVM_PROFDATA_CMD "")
  set(CORRUPTER_LLVM_COV_CMD "")
  if(CORRUPTER_LLVM_PROFDATA AND CORRUPTER_LLVM_COV)
    set(CORRUPTER_LLVM_PROFDATA_CMD ${CORRUPTER_LLVM_PROFDATA})
    set(CORRUPTER_LLVM_COV_CMD ${CORRUPTER_LLVM_COV})
  elseif(CORRUPTER_XCRUN)
    set(CORRUPTER_LLVM_PROFDATA_CMD ${CORRUPTER_XCRUN} llvm-profdata)
    set(CORRUPTER_LLVM_COV_CMD ${CORRUPTER_XCRUN} llvm-cov)
  endif()

  if(CORRUPTER_LLVM_PROFDATA_CMD AND CORRUPTER_LLVM_COV_CMD)
    add_custom_target(corrupter_dsp_coverage
      COMMAND ${CMAKE_COMMAND} -E rm -f
        ${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profraw
        ${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profdata
      COMMAND ${CMAKE_COMMAND} -E env
        LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profraw
        $<TARGET_FILE:corrupter_dsp_regression>
      COMMAND ${CORRUPTER_LLVM_PROFDATA_CMD} merge -sparse
        ${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profraw
        -o ${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profdata
      COMMAND ${CORRUPTER_LLVM_COV_CMD} report
        $<TARGET_FILE:corrupter_dsp_regression>
        -instr-profile=${CMAKE_BINARY_DIR}/corrupter_dsp_regression.profdata
      DEPENDS corrupter_dsp_regression
      USES_TERMINAL
      VERBATIM
    )
  else()
    message(WARNING "Coverage target disabled: llvm-profdata or llvm-cov not found")
  endif()
endif()
