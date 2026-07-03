set(incs .)
set(srcs "")

if (CONFIG_BK_AUDIO_ENGINE)
list(APPEND srcs
	audio_engine.c
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
	PRIV_REQUIRES bk_common include bk_voice_service network_engine bk_player_service bk_vfs bk_factory_config bk_asr_service bk_app_event fatfs
)

armino_component_get_target(target audio_engine)
set(AUDIO_ENGINE_KWS_MODEL_FROM_UDISK_ENABLED 0)
set(AUDIO_ENGINE_PROJECT_DECONFIG_CANDIDATES
    "$ENV{PROJECT_DIR}/ap/config/bk7259_ap/defconfig"
    "${CMAKE_SOURCE_DIR}/ap/config/bk7259_ap/defconfig"
)
foreach(defconfig_path ${AUDIO_ENGINE_PROJECT_DECONFIG_CANDIDATES})
    if (EXISTS "${defconfig_path}")
        file(STRINGS "${defconfig_path}" defconfig_has_kws_udisk REGEX "^CONFIG_BEKEN_KWS_MODEL_FROM_UDISK=y$")
        if (defconfig_has_kws_udisk)
            set(AUDIO_ENGINE_KWS_MODEL_FROM_UDISK_ENABLED 1)
        endif()
    endif()
endforeach()

if (AUDIO_ENGINE_KWS_MODEL_FROM_UDISK_ENABLED)
    target_compile_definitions(${target} PRIVATE CONFIG_BEKEN_KWS_MODEL_FROM_UDISK=1)
endif()