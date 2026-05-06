add_executable(corrupter_dsp_spectral_tests
  spectral_helpers.cpp
  engine_driver.cpp
  test_runner.cpp
  test_smoke.cpp
  test_aliasing.cpp
  test_fra_per_module.cpp
  test_thd.cpp
  test_clicks.cpp
  test_freeze_transitions.cpp
  test_smoothing.cpp
)

target_link_libraries(corrupter_dsp_spectral_tests
  PRIVATE
    corrupter_dsp
)

target_include_directories(corrupter_dsp_spectral_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(corrupter_dsp_spectral_tests
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set(CORRUPTER_SPECTRAL_OUTPUT_DIR ${CMAKE_BINARY_DIR}/spectral-output)
file(MAKE_DIRECTORY ${CORRUPTER_SPECTRAL_OUTPUT_DIR})

target_compile_definitions(corrupter_dsp_spectral_tests
  PRIVATE
    CORRUPTER_SPECTRAL_OUTPUT_DIR="${CORRUPTER_SPECTRAL_OUTPUT_DIR}"
)

add_test(
  NAME corrupter_dsp_spectral_tests
  COMMAND corrupter_dsp_spectral_tests
)

set_tests_properties(corrupter_dsp_spectral_tests PROPERTIES
  LABELS "spectral"
  TIMEOUT 600
)
