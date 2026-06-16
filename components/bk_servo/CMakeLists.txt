set(incs .)
set(srcs "")

list(APPEND srcs
	bk_aimi_servo.c
	bk_aimi_palm_tracker.c
	bk_hiwonder_hand_servo.c
)


set(priv_req bk_common media_service bk_vfs
             bk_app_event key ap bk_smart_config audio_engine bk_factory_config)

if (CONFIG_ADC_KEY)
	list(APPEND priv_req adc_key)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES ${priv_req}
	)
