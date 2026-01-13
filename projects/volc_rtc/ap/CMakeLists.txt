set(incs include  .)
set(srcs ap_main.c )

if (CONFIG_AUDIO_PARA)

list(APPEND srcs
    audio_para/bk_audio_para.c
)
endif()

set(priv_req  media_service multimedia network_transfer bk_voice_service audio_engine 
              bk_smart_config bk_app_event bk_key_app bk_factory_config bk_led_blink 
              bk_motor bk_nfc bk_batt_monitor bk_bluetooth bk_pan bk_aud_param_ctrl)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
