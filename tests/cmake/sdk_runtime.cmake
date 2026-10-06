# The full mode deliberately uses installed ESPHome declarations and behavior.
# Hardware time/storage/lifecycle are the only substituted seams.
if(NOT EXISTS "${ESPHOME_SDK_ROOT}/esphome/components/light/light_state.cpp")
  message(FATAL_ERROR "Full SDK checks require ESPHOME_SDK_ROOT containing the ESPHome package")
endif()
set(PIXEL_K80_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

function(pixel_k80_sdk_extract source start stop result)
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
    "void LightState::start_transition_(|void LightState::set_immediately_("
    "void LightState::set_immediately_(|void LightState::disable_loop_if_idle_("
    "void LightState::disable_loop_if_idle_(|void LightState::save_remote_values_(")
  string(REPLACE "|" ";" names "${boundary}")
  list(GET names 0 start)
  list(GET names 1 stop)
  pixel_k80_sdk_extract("${light_source}" "${start}" "${stop}" section)
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
  pixel_k80_sdk_extract("${light_source}" "${start}" "${stop}" section)
  string(APPEND restore "${section}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-light-restore.h" "${restore}")
file(READ "${ESPHOME_SDK_ROOT}/esphome/core/helpers.cpp" helpers_source)
pixel_k80_sdk_extract("${helpers_source}" "void rgb_to_hsv("
    "uint8_t HighFrequencyLoopRequester::" hsv)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-hsv.h" "${hsv}")
pixel_k80_sdk_extract("${helpers_source}" "__attribute__((noinline, cold)) void *callback_manager_grow("
    "static const uint16_t CRC16_A001_LE_LUT_L" callbacks)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk-callbacks.h" "${callbacks}")

add_library(pixel_k80_sdk_runtime STATIC
    "${PIXEL_K80_TEST_DIR}/sdk_light_runtime.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_color_values.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_output.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/controller_registry.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/text/text_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/text/text.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/number/number_call.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/components/number/number.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/entity_base.cpp")
target_include_directories(pixel_k80_sdk_runtime PUBLIC
    "${PIXEL_K80_TEST_DIR}/../components/pixel_k80"
    "${PIXEL_K80_TEST_DIR}/sdk" "${PIXEL_K80_TEST_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(pixel_k80_sdk_runtime SYSTEM PUBLIC "${ESPHOME_SDK_ROOT}")
target_compile_features(pixel_k80_sdk_runtime PUBLIC cxx_std_20)
target_compile_definitions(pixel_k80_sdk_runtime PUBLIC USE_LIGHT USE_CONTROLLER_REGISTRY CONTROLLER_REGISTRY_MAX=4 ESPHOME_ENTITY_LIGHT_COUNT=0)
target_compile_options(pixel_k80_sdk_runtime PRIVATE -Wall -Wextra -Werror
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ESPHOME_SDK_ROOT}/esphome/components/light/light_state.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/entity_base.cpp"
    "${ESPHOME_SDK_ROOT}/esphome/core/helpers.cpp")
