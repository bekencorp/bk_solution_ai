set(incs include .)
set(srcs
    cp_main.c
    src/robot_keepalive.c
)

if (CONFIG_VND_CAL)
if (CONFIG_OVERRIDE_VND_CAL)
    list(APPEND srcs
        vnd_cal.c
    )
endif()
endif()

set(priv_req controller_if lwip_intf_v2_1 bt)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
