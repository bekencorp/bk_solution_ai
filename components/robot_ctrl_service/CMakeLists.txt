set(incs include .)
set(srcs "")

if (CONFIG_BK_ROBOT_CTRL_SERVICE)
list(APPEND srcs
    src/robot_ctrl_service.c
    src/robot_ctrl_jsonrpc.c
    src/robot_ctrl_session.c
    src/robot_ctrl_solution.c
    src/robot_ctrl_camera.c
    src/robot_ctrl_motion.c
    src/robot_ctrl_audio.c
    src/robot_ctrl_notify.c
    src/robot_ctrl_sleep.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common json robot_lan_net robot_video_service audio_engine bk_motor bk_app_event
)
