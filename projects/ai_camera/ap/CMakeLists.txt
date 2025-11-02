set(incs include  .)
set(srcs ap_main.c )

set(priv_req  media_service multimedia network_transfer bk_voice_service audio_engine bk_smart_config bk_app_event)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
