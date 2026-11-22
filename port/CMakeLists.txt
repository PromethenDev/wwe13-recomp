# WWE '13 PC - native port on the ReXGlue SDK 0.10 (docs/NATIVE_PORT_PLAN.md)
#
# Step 1: the game on the SDK 0.10 with the Xenos GPU plugin. The native renderer (re:Blue + plume,
# from WWE '12 PC) comes in step 3 as a second executable, as in WWE 2K15 PC.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(wwe13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Game fixes and features shared by both executables.
set(WWE13_GAME_SOURCES
    src/crash_report.cpp
    src/present_count.cpp
    src/exit_entry.cpp
    src/cloth_handshake.cpp
    src/text_entry_keyboard.cpp
    src/render_queue_guard.cpp
    src/pin_kickout.cpp
    src/d3d_ring_lock.cpp
    src/renderq_watch.cpp
    src/renderq_sublist.cpp
    src/frame_rate_lock.cpp
    src/xmp_controller_fast.cpp
)
set(WWE13_SOURCES
    src/main.cpp
    ${WWE13_GAME_SOURCES}
)
if(WIN32)
    list(APPEND WWE13_SOURCES res/wwe13.rc)
    set_source_files_properties(res/wwe13.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/wwe13.ico)
endif()

if(WIN32)
    add_executable(wwe13 WIN32 ${WWE13_SOURCES})
else()
    add_executable(wwe13 ${WWE13_SOURCES})
endif()
target_include_directories(wwe13 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Native renderer (docs/NATIVE_PORT_PLAN.md, step 3): executables that draw the game's D3D calls
# through re:Blue + plume, taken from WWE '12 PC through WWE 2K15 PC: wwe13_native.exe (Vulkan),
# wwe13_d3d12.exe (Direct3D 12) and wwe13_d3d11.exe (Direct3D 11), as WWE '12 PC has them
# (cmake/native_renderer.cmake). They link the same recompiled objects; wwe13.exe (Xenos GPU
# emulation) is unchanged. Needs plume and reblue-XenosRecomp in WWE13_NATIVE_DEPS (C:/Dev/native-deps,
# patched as for WWE 2K15 PC) and this project's patched copy (port/tools/prepare_native_deps.ps1).
option(WWE13_NATIVE_RENDERER "Also build the native renderer executables (Vulkan, D3D12, D3D11)" OFF)
set(WWE13_NATIVE_TARGETS "")
if(WWE13_NATIVE_RENDERER)
    include(cmake/native_renderer.cmake)
    # Windows: GUI subsystem + icon/version resource. Linux: plain executable (SDL window, Vulkan only).
    if(WIN32)
        set(_wwe13_exe_kind WIN32)
        set(_wwe13_exe_res res/wwe13.rc)
    else()
        set(_wwe13_exe_kind "")
        set(_wwe13_exe_res "")
    endif()
    add_executable(wwe13_native ${_wwe13_exe_kind} src/main.cpp ${WWE13_GAME_SOURCES} ${_wwe13_exe_res})
    wwe13_add_native_renderer(wwe13_native)
    list(APPEND WWE13_NATIVE_TARGETS wwe13_native)
    if(WWE13_D3D12_EXE)
        add_executable(wwe13_d3d12 ${_wwe13_exe_kind} src/main.cpp ${WWE13_GAME_SOURCES} ${_wwe13_exe_res})
        wwe13_add_native_renderer(wwe13_d3d12 D3D12)
        list(APPEND WWE13_NATIVE_TARGETS wwe13_d3d12)
    endif()
    if(WWE13_D3D11_EXE)
        add_executable(wwe13_d3d11 ${_wwe13_exe_kind} src/main.cpp ${WWE13_GAME_SOURCES} ${_wwe13_exe_res})
        wwe13_add_native_renderer(wwe13_d3d11 D3D11)
        list(APPEND WWE13_NATIVE_TARGETS wwe13_d3d11)
    endif()
endif()

rexglue_setup_target(wwe13 GPU_PLUGINS xenos)

foreach(native_target IN LISTS WWE13_NATIVE_TARGETS)
    # wwe13_recomp exists once codegen has written generated/ (a fresh checkout's first configure
    # doesn't have it; the build runs codegen and CMake configures again).
    if(TARGET wwe13_recomp)
        target_sources(${native_target} PRIVATE $<TARGET_OBJECTS:wwe13_recomp>)
    endif()
    target_include_directories(${native_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${REXGLUE_ENTRYPOINT_INCLUDE_DIR})
    target_link_libraries(${native_target} PRIVATE rex::runtime)
    add_dependencies(${native_target} wwe13_codegen)
    rexglue_apply_target_settings(${native_target})
    rexglue_configure_target(${native_target} GPU_PLUGINS xenos)
    if(MSVC)
        target_link_options(${native_target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endforeach()

# Keep a PDB and a link map beside the exe (not shipped) to symbolize crash and hang reports.
if(MSVC)
    target_link_options(wwe13 PRIVATE /DEBUG /OPT:REF /OPT:ICF "/MAP:$<TARGET_FILE_DIR:wwe13>/wwe13.map")
endif()

# Runtime settings beside the executable, where ReXApp loads wwe13.toml.
set_property(TARGET wwe13 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wwe13_runtime.toml")
add_custom_command(TARGET wwe13 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/wwe13_runtime.toml" "$<TARGET_FILE_DIR:wwe13>/wwe13.toml"
    VERBATIM)
