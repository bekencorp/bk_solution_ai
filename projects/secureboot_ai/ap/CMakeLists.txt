# secureboot_ai AP — compile beken_robot sources.
# Overlay: only board_usb_switch.c (NS/secure-boot USB mux vs base MIPI/PLL path).
set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../beken_robot/ap")
set(ROBOT_APP_OVERLAY_DIR "${CMAKE_CURRENT_LIST_DIR}")
include(${APP_ROOT}/ap_sources.cmake)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

if(CONFIG_ROBOT_TEST)
    component_compile_definitions(ROBOT_TEST=1)
endif()
