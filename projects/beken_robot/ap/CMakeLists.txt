# beken_robot AP — application base. Source list lives in ap_sources.cmake so
# secureboot_ai can compile the same tree with APP_ROOT pointed here.
set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}")
include(${CMAKE_CURRENT_LIST_DIR}/ap_sources.cmake)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

if(CONFIG_ROBOT_TEST)
    component_compile_definitions(ROBOT_TEST=1)
endif()
