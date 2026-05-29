set(incs include .)
set(srcs "")

if (CONFIG_BK_ROBOT_LAN_NET)
list(APPEND srcs
    src/robot_lan_net.c
    src/robot_lan_identity.c
    src/robot_lan_discovery.c
    src/robot_lan_transfer.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common bk_init lwip_intf_v2_1 json bk_factory_config bk_network_transfer
)
