cmake_minimum_required(VERSION 4.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(f3d)

# Global
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/libs)
set (emdawnwepbgpu_port
    "${CMAKE_SOURCE_DIR}/libs/emdawnwebgpu-v20261002.154047.remoteport.py"
)

set(GLOBAL_COMPILE_FLAGS "")
set(GLOBAL_LINK_FLAGS "")

add_compile_options(${GLOBAL_COMPILE_FLAGS})
add_link_options(${GLOBAL_LINK_FLAGS})


# emdawnwebgpu
add_library(emdawnwebgpu INTERFACE)
set(emdawnwebgpu_remoteport "emdawnwebgpu-v20261002.154047.remoteport")
set (emdawnwebgpu_port_file "${LIBS_DIR}/${emdawnwebgpu_remoteport}.py")
target_compile_options(emdawnwebgpu INTERFACE "--use-port=${emdawnwebgpu_port_file}")
target_link_options(emdawnwebgpu INTERFACE "--use-port=${emdawnwebgpu_port_file}")

set(emdawnwebgpu_pkg  "$ENV{EMSDK}/upstream/emscripten/cache/ports/${emdawnwebgpu_remoteport}/emdawnwebgpu_pkg")
if( IS_DIRECTORY ${emdawnwebgpu_pkg})
    message(STATUS "emdawnwebgpu pkg: ${emdawnwebgpu_pkg}")
    target_include_directories(
        emdawnwebgpu
        SYSTEM
        INTERFACE
        "${emdawnwebgpu_pkg}/webgpu/include"
        "${emdawnwebgpu_pkg}/webgpu_cpp/include"

    )
else()
    message(FATAL_ERROR "emdawnwebgpu_pkg haven't existed.  Got ${emdawnwebgpu_pkg}")
endif()

# SDL3
add_subdirectory(libs/SDL EXCLUDE_FROM_ALL)
add_library(sdl3 INTERFACE)
target_link_libraries(sdl3 INTERFACE SDL3::SDL3)

# ImGui
add_library(imgui INTERFACE)
target_include_directories(imgui INTERFACE "${LIBS_DIR}/imgui" "${LIBS_DIR}/imgui/backends")
target_sources(imgui INTERFACE
            "${LIBS_DIR}/imgui/imgui.cpp"
            "${LIBS_DIR}/imgui/imgui_draw.cpp"
            "${LIBS_DIR}/imgui/imgui_demo.cpp"
            "${LIBS_DIR}/imgui/imgui_tables.cpp"
            "${LIBS_DIR}/imgui/imgui_widgets.cpp"
            "${LIBS_DIR}/imgui/backends/imgui_impl_sdl3.cpp"
            "${LIBS_DIR}/imgui/backends/imgui_impl_wgpu.cpp"
)



# Targets
add_executable(main)
target_sources(main PRIVATE src/main.cpp)
target_link_libraries(main PRIVATE emdawnwebgpu sdl3 imgui)
target_compile_features(main PRIVATE cxx_std_23)
target_compile_options(main PRIVATE -Wno-c23-extensions)

target_link_options(main PRIVATE "-sJSPI" "$<$<CONFIG:Release>:--closure=1>")

set_target_properties(main PROPERTIES SUFFIX ".html")

