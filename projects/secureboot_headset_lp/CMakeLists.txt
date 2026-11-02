# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{ARMINO_TOOLS_PATH}/build_tools/cmake/project.cmake)

# Display helpers (app_display.c: MIPI panel + DPU bring-up) live in the
# solution components tree. Point at the single component dir only, so the
# other robot components (video_engine, audio_engine, robot_*...) stay out.
set(EXTRA_COMPONENTS_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/multimedia_device_service
)

project(secureboot_headset_lp)
