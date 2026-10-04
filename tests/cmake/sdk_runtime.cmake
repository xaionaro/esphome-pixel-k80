# The full mode deliberately uses installed ESPHome declarations and behavior.
# Hardware time/storage/lifecycle are the only substituted seams.
if(NOT EXISTS "${ESPHOME_SDK_ROOT}/esphome/components/light/light_state.cpp")
  message(FATAL_ERROR "Full SDK checks require ESPHOME_SDK_ROOT containing the ESPHome package")
endif()
set(YISCAXIA_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

function(yiscaxia_sdk_extract source start stop result)
  string(FIND "${source}" "${start}" begin)
  string(FIND "${source}" "${stop}" end)
  if(begin LESS 0 OR end LESS_EQUAL begin)
    message(FATAL_ERROR "Installed SDK boundary missing or reordered: ${start} -> ${stop}")
  endif()
  math(EXPR length "${end} - ${begin}")
  string(SUBSTRING "${source}" ${begin} ${length} section)
  set(${result} "${section}" PARENT_SCOPE)
endfunction()

file(READ "${ESPHOME_SDK_ROOT}/esphome/components/light/light_state.cpp" light_source)
set(ordering "")
foreach(boundary IN ITEMS
    "void LightState::loop()|void LightState::publish_state()"
    "void LightState::publish_state()|LightOutput *LightState::get_output()"
    "void LightState::add_remote_values_listener(|void LightState::add_effects("
    "void LightState::start_effect_(|void LightState::start_transition_("
    "void LightState::set_immediately_(|void LightState::disable_loop_if_idle_("
    "void LightState::disable_loop_if_idle_(|void LightState::save_remote_values_(")
  string(REPLACE "|" ";" names "${boundary}")
  list(GET names 0 start)
  list(GET names 1 stop)
  yiscaxia_sdk_extract("${light_source}" "${start}" "${stop}" section)
  string(APPEND ordering "${section}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-light-ordering.h" "${ordering}")
set(restore "")
foreach(boundary IN ITEMS
    "void LightState::setup()|void LightState::dump_config()"
    "LightOutput *LightState::get_output()|static constexpr auto EFFECT_NONE_REF"
    "void LightState::save_remote_values_()|}  // namespace esphome::light")
  string(REPLACE "|" ";" names "${boundary}")
  list(GET names 0 start)
  list(GET names 1 stop)
  yiscaxia_sdk_extract("${light_source}" "${start}" "${stop}" section)
  string(APPEND restore "${section}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-light-restore.h" "${restore}")
file(READ "${ESPHOME_SDK_ROOT}/esphome/core/helpers.cpp" helpers_source)
yiscaxia_sdk_extract("${helpers_source}" "void rgb_to_hsv("
    "uint8_t HighFrequencyLoopRequester::" hsv)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-hsv.h" "${hsv}")

add_library(yiscaxia_sdk_runtime STATIC
    "${YISCAXIA_TEST_DIR}/sdk_light_runtime.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/text/text_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/text/text.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/number/number_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/number/number.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/entity_base.cpp")
target_include_directories(yiscaxia_sdk_runtime PUBLIC
    "${YISCAXIA_TEST_DIR}/sdk" "${YISCAXIA_TEST_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(yiscaxia_sdk_runtime SYSTEM PUBLIC "${ESPHOME_SDK_ROOT}")
target_compile_features(yiscaxia_sdk_runtime PUBLIC cxx_std_20)
target_compile_options(yiscaxia_sdk_runtime PRIVATE -Wall -Wextra -Werror
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_state.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/entity_base.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/helpers.cpp")
