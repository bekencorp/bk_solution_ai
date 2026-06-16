set(incs include .)
set(srcs
    cp_main.c
    src/robot_keepalive.c
)
set(priv_req controller_if lwip_intf_v2_1 bt)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
