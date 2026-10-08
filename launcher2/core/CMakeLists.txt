cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(wwe13_launcher_core LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(WWE13_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT WWE13_REXGLUE_THIRDPARTY)
  get_filename_component(WWE13_REXGLUE_ROOT "${WWE13_PROJECT_ROOT}/../tools/rexglue" ABSOLUTE)
  if(NOT EXISTS "${WWE13_REXGLUE_ROOT}/thirdparty/volk/volk.c")
    # Nested worktrees sit below .worktrees/<name>; use the shared sibling tools directory.
    get_filename_component(WWE13_REXGLUE_ROOT "${WWE13_PROJECT_ROOT}/../../../tools/rexglue" ABSOLUTE)
  endif()
  set(WWE13_REXGLUE_THIRDPARTY "${WWE13_REXGLUE_ROOT}/thirdparty")
endif()

set(WWE13_MINIZ_VENDOR "${WWE13_PROJECT_ROOT}/launcher2/thirdparty/miniz/miniz.h")
if(NOT EXISTS "${WWE13_MINIZ_VENDOR}")
  message(FATAL_ERROR "launcher2/thirdparty/miniz/miniz.h is required by the core")
endif()
set(WWE13_MINIZ_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/miniz-core")
file(MAKE_DIRECTORY "${WWE13_MINIZ_BUILD_DIR}")
file(READ "${WWE13_MINIZ_VENDOR}" WWE13_MINIZ_CONFIGURED)
# The SDK copies miniz into SDL with its archive/inflate APIs disabled and
# SDL-specific allocator names. Generate a build-tree copy with the standalone
# ZIP/deflate API enabled; keep the vendored third-party file unmodified.
foreach(_miniz_macro MINIZ_NO_INFLATE_APIS MINIZ_NO_ARCHIVE_APIS
                     MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_APIS
                     MINIZ_SDL_MALLOC MINIZ_STATIC_FUNCTIONS)
  string(REPLACE "#define ${_miniz_macro}\n" "" WWE13_MINIZ_CONFIGURED "${WWE13_MINIZ_CONFIGURED}")
endforeach()
file(WRITE "${WWE13_MINIZ_BUILD_DIR}/miniz.h" "${WWE13_MINIZ_CONFIGURED}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WWE13_MINIZ_VENDOR}")

add_library(wwe13_launcher_miniz STATIC internal/miniz_impl.c)
target_include_directories(wwe13_launcher_miniz PUBLIC
  "${WWE13_MINIZ_BUILD_DIR}")

add_library(wwe13_launcher_volk STATIC
  "${WWE13_REXGLUE_THIRDPARTY}/volk/volk.c")
target_include_directories(wwe13_launcher_volk PUBLIC
  "${WWE13_REXGLUE_THIRDPARTY}/volk"
  "${WWE13_REXGLUE_THIRDPARTY}/vulkan-headers/include")
if(CMAKE_DL_LIBS)
  target_link_libraries(wwe13_launcher_volk PUBLIC "${CMAKE_DL_LIBS}")
endif()

file(GLOB WWE13_LAUNCHER_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/internal/*.cpp")
add_library(wwe13_launcher_core STATIC ${WWE13_LAUNCHER_CORE_SOURCES})
target_include_directories(wwe13_launcher_core
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
  PRIVATE "${WWE13_REXGLUE_THIRDPARTY}/decoders"
          "${WWE13_PROJECT_ROOT}/launcher2/thirdparty/picosha2")
target_link_libraries(wwe13_launcher_core
  PUBLIC wwe13_launcher_volk wwe13_launcher_miniz)
if(WIN32)
  # The auto-updater uses WinHTTP (a system DLL) for HTTPS; no extra dependency to ship.
  target_link_libraries(wwe13_launcher_core PRIVATE shell32 winhttp)
endif()

if(MSVC)
  target_compile_options(wwe13_launcher_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(wwe13_launcher_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

file(GLOB WWE13_LAUNCHER_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/tests/*_test.cpp")
enable_testing()
foreach(test_source IN LISTS WWE13_LAUNCHER_TEST_SOURCES)
  get_filename_component(test_name "${test_source}" NAME_WE)
  add_executable("${test_name}" "${test_source}")
  target_link_libraries("${test_name}" PRIVATE wwe13_launcher_core)
  if(WIN32 AND CMAKE_CROSSCOMPILING)
    # The xwin/Wine cross-toolchain exposes the host filesystem as Wine's Z:.
    set(WWE13_TESTDATA_PATH "Z:${WWE13_PROJECT_ROOT}/out/l1/testdata")
  else()
    set(WWE13_TESTDATA_PATH "${WWE13_PROJECT_ROOT}/out/l1/testdata")
  endif()
  target_compile_definitions("${test_name}" PRIVATE
    WWE13_LAUNCHER_TESTDATA="${WWE13_TESTDATA_PATH}")
  add_test(NAME "${test_name}" COMMAND "${test_name}")
endforeach()
