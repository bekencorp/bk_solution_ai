set(incs .)
set(srcs "")

if (CONFIG_BK_VIDEO_ENGINE)
list(APPEND srcs
    video_engine.c
    video_frame_que.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	REQUIRES bk_common bk_camera driver multimedia
	PRIV_REQUIRES include bk_voice_service network_transfer avdk_utils multimedia bk_display bk_player_service
)
