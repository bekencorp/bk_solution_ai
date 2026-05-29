set(incs include .)
set(srcs "")

if (CONFIG_BK_ROBOT_VIDEO_SERVICE)
list(APPEND srcs
    src/robot_video_service.c
    src/robot_video_config.c
    src/robot_video_cli.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common video_engine bk_cli
)
