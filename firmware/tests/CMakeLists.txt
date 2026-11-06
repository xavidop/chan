cmake_minimum_required(VERSION 3.16)

project(chan_firmware_tests LANGUAGES CXX)

enable_testing()

# ---------------------------------------------------------------- upstream test
add_executable(motion_math_test
    motion_math_test.cpp
    ../main/stackchan/motion/motion_math.cpp
)
target_include_directories(motion_math_test PRIVATE ../main)
target_compile_features(motion_math_test PRIVATE cxx_std_17)
add_test(NAME motion_math_test COMMAND $<TARGET_FILE:motion_math_test>)

# ---------------------------------------------------------------- Chan core tests
# Pure C++17 logic from main/chan/core, compiled for the host with doctest.
# ArduinoJson comes from the pinned checkout made by `python3 fetch_repos.py`
# (firmware/components/ArduinoJson); override with -DARDUINOJSON_INCLUDE_DIR.
set(ARDUINOJSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../components/ArduinoJson/src"
    CACHE PATH "Directory containing ArduinoJson.h")
if(NOT EXISTS "${ARDUINOJSON_INCLUDE_DIR}/ArduinoJson.h")
    message(FATAL_ERROR "ArduinoJson.h not found in ${ARDUINOJSON_INCLUDE_DIR}; run `python3 fetch_repos.py` in firmware/")
endif()

file(GLOB CHAN_CORE_SOURCES CONFIGURE_DEPENDS ../main/chan/core/*.cpp)
file(GLOB CHAN_CORE_TESTS CONFIGURE_DEPENDS *_test.cpp)
list(REMOVE_ITEM CHAN_CORE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/motion_math_test.cpp")

add_executable(chan_core_tests test_main.cpp ${CHAN_CORE_TESTS} ${CHAN_CORE_SOURCES})
target_include_directories(chan_core_tests PRIVATE ../main "${ARDUINOJSON_INCLUDE_DIR}" .)
target_compile_features(chan_core_tests PRIVATE cxx_std_17)
target_compile_definitions(chan_core_tests PRIVATE
    CHAN_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    CHAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_compile_options(chan_core_tests PRIVATE -Wall -Wextra)
add_test(NAME chan_core_tests COMMAND $<TARGET_FILE:chan_core_tests>)
