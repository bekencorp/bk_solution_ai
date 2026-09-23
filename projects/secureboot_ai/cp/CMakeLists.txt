# secureboot_ai CP — local cp_main (AP boot vote) + TF-M; keepalive/vnd_cal from base.
set(incs include .)
set(CP_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../beken_robot/cp")
list(APPEND incs "${CP_APP_ROOT}/include" "${CP_APP_ROOT}")

set(srcs
    cp_main.c
    ${CP_APP_ROOT}/src/robot_keepalive.c
)

if (CONFIG_VND_CAL)
if (CONFIG_OVERRIDE_VND_CAL)
    list(APPEND srcs
        ${CP_APP_ROOT}/vnd_cal.c
    )
endif()
endif()

set(priv_req controller_if lwip_intf_v2_1 bk_bluetooth)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    REQUIRES tfm
    PRIV_REQUIRES "${priv_req}"
)

# bk_init and other SDK units include project-root headers such as vnd_cal.h
# via -I<this_project>/cp, not via this component's INCLUDE_DIRS. Keep the
# beken_robot CP tree on the global include path as well.
armino_build_set_property(COMPILE_OPTIONS "-I${CP_APP_ROOT}" APPEND)
armino_build_set_property(COMPILE_OPTIONS "-I${CP_APP_ROOT}/include" APPEND)
