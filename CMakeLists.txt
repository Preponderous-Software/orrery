cmake_minimum_required(VERSION 3.16)
project(orrery VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(EMSCRIPTEN)
  # The browser build (web/README.md): the app only. The tests run natively.
  set(ORRERY_BUILD_TESTS_DEFAULT OFF)
else()
  set(ORRERY_BUILD_TESTS_DEFAULT ON)
endif()
option(ORRERY_BUILD_TESTS "Build the tests" ${ORRERY_BUILD_TESTS_DEFAULT})
option(ORRERY_WITH_STRATA "Build the renderer app on the Strata engine" ON)
option(ORRERY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# The Strata commit the app is built against (strata.ref: the v0.6.0 tag).
# Pinned; bump deliberately.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/strata.ref" ORRERY_STRATA_REF_DEFAULT)
string(STRIP "${ORRERY_STRATA_REF_DEFAULT}" ORRERY_STRATA_REF_DEFAULT)
set(ORRERY_STRATA_REF "${ORRERY_STRATA_REF_DEFAULT}" CACHE STRING "Strata git ref (a full commit SHA)")

if(EMSCRIPTEN)
  # One exception mode for the whole module: Strata's (STRATA_WASM_EXCEPTIONS,
  # wasm by default; docs/WEB.md in Strata). orrery throws and catches too.
  set(STRATA_WASM_EXCEPTIONS "wasm" CACHE STRING "C++ exceptions in WebAssembly: wasm or js (Strata's option)")
  if(STRATA_WASM_EXCEPTIONS STREQUAL "wasm")
    set(ORRERY_WASM_EH_FLAG -fwasm-exceptions)
  else()
    set(ORRERY_WASM_EH_FLAG -fexceptions)
  endif()
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ORRERY_WASM_EH_FLAG}>")
  add_link_options(${ORRERY_WASM_EH_FLAG})
  # The data the page preloads (tools/make_web_data.py).
  set(ORRERY_WEB_DATA "${CMAKE_CURRENT_SOURCE_DIR}/build-web-data" CACHE PATH
      "Directory packaged as the browser build's /data (tools/make_web_data.py)")
endif()

function(orrery_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(ORRERY_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Ephemerides, orbits, the catalogue and the view maths: no GPU, no Strata.
add_library(orrery_core STATIC
  src/orrery/csv.cpp
  src/orrery/time.cpp
  src/orrery/spk.cpp
  src/orrery/kepler.cpp
  src/orrery/element_table.cpp
  src/orrery/system.cpp
  src/orrery/scale.cpp
  src/orrery/atmosphere.cpp
  src/orrery/flight.cpp
  src/orrery/missions.cpp)
target_include_directories(orrery_core PUBLIC src)
orrery_warnings(orrery_core)

if(ORRERY_BUILD_TESTS)
  enable_testing()
  foreach(t time kepler scale rotation csv flight atmosphere element_table)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE orrery_core)
    orrery_warnings(test_${t})
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
  # Needs data/kernels/de440s.bsp and tests/data/vectors (tools/fetch_data.py all, then
  # tools/build_catalog.py).
  add_executable(test_ephemeris tests/test_ephemeris.cpp)
  target_link_libraries(test_ephemeris PRIVATE orrery_core)
  target_compile_definitions(test_ephemeris PRIVATE ORRERY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  orrery_warnings(test_ephemeris)
  add_test(NAME ephemeris COMMAND test_ephemeris)
  # Also needs the catalogue: every mission's bodies must exist at its date.
  add_executable(test_missions tests/test_missions.cpp)
  target_link_libraries(test_missions PRIVATE orrery_core)
  target_compile_definitions(test_missions PRIVATE ORRERY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  orrery_warnings(test_missions)
  add_test(NAME missions COMMAND test_missions)
endif()

if(ORRERY_WITH_STRATA)
  cmake_minimum_required(VERSION 3.24)
  include(FetchContent)
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${ORRERY_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  # stb_image decodes the planet maps (third_party/stb/README.md); its own
  # translation unit, without the project's warning flags.
  add_library(orrery_stb STATIC third_party/stb/image_file.cpp)
  target_include_directories(orrery_stb SYSTEM PUBLIC third_party/stb)

  add_executable(orrery app/main.cpp app/renderer.cpp app/usda.cpp)
  target_include_directories(orrery PRIVATE app)
  target_link_libraries(orrery PRIVATE orrery_core orrery_stb strata::static)
  target_compile_definitions(orrery PRIVATE ORRERY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  orrery_warnings(orrery)

  if(EMSCRIPTEN)
    # index.html + index.js + index.wasm + index.data: the whole site.
    if(NOT EXISTS "${ORRERY_WEB_DATA}/catalog/bodies.csv")
      message(FATAL_ERROR "no web data in ${ORRERY_WEB_DATA}: run tools/make_web_data.py first (web/README.md)")
    endif()
    set_target_properties(orrery PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(orrery PRIVATE
      -sALLOW_MEMORY_GROWTH=1
      -sSTACK_SIZE=1MB
      -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,ENV
      "--preload-file=${ORRERY_WEB_DATA}@/data"
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set_property(TARGET orrery APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  endif()

  if(ORRERY_BUILD_TESTS)
    # Headless render smoke test: renders four views and checks the frames
    # are not blank. Needs a GL 4.5 context (llvmpipe is fine).
    add_test(NAME render_smoke COMMAND orrery --data "${CMAKE_CURRENT_SOURCE_DIR}/data" --smoke-test
             "${CMAKE_CURRENT_BINARY_DIR}/smoke")
    # A mission end to end in the app: the Moon run on the landing autopilot
    # (about 25 s of flight; 800 frames fly 26.7 s) completes.
    add_test(NAME mission_moon_run COMMAND orrery --data "${CMAKE_CURRENT_SOURCE_DIR}/data" --headless
             --mission moon-run --land --no-small-bodies --width 160 --height 90
             --frames "${CMAKE_CURRENT_BINARY_DIR}/mission_moon_run" --count 800 --step 0)
    set_tests_properties(mission_moon_run PROPERTIES PASS_REGULAR_EXPRESSION "mission moon-run complete")
    # The landing autopilot comes down through Earth's air softly and stays cool (heat under 50%),
    # and under its daytime sky no labels are drawn (Mercury and Venus are in view).
    add_test(NAME land_through_air COMMAND orrery --data "${CMAKE_CURRENT_SOURCE_DIR}/data" --headless
             --mode true --enlarge 1 --date 2026-11-24 --ship --target Earth --land --no-small-bodies
             --width 640 --height 360 --frames "${CMAKE_CURRENT_BINARY_DIR}/land_through_air" --count 1500 --step 0)
    set_tests_properties(land_through_air PROPERTIES PASS_REGULAR_EXPRESSION "touchdown soft on Earth, hottest [1-4]?[0-9]%, 0 labels")
  endif()
endif()
