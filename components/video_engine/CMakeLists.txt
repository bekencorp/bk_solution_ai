set(incs .)
set(srcs "")

if (CONFIG_BK_VIDEO_ENGINE)
list(APPEND srcs
    video_engine.c
)

if (CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA)
list(APPEND srcs
    video_frame_que.c
)
endif()
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	REQUIRES bk_common bk_camera driver
	PRIV_REQUIRES include bk_voice_service network_engine avdk_utils bk_display bk_player_service multimedia_device_service bk_peripheral
)
