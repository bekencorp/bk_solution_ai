set(incs .)
set(srcs "")

if (CONFIG_BK_AUDIO_ENGINE)
list(APPEND srcs
	audio_engine.c
	audio_engine_cli.c
)
endif()

if (CONFIG_AE_SUPPORT_PROMPT_TONE)
list(APPEND srcs
	audio_engine_prompt_tone.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common include bk_voice_service network_engine bk_player_service bk_vfs bk_factory_config bk_asr_service bk_app_event
)