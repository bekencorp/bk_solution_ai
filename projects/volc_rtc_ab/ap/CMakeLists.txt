set(incs include  .)
set(srcs ap_main.c )

set(priv_req  media_service multimedia network_transfer bk_voice_service audio_engine bk_smart_config bk_app_event bk_key_app bk_factory_config bk_led_blink bk_motor bk_nfc)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
