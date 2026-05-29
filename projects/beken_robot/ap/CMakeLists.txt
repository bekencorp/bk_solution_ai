set(incs include .)
set(srcs
    ap_main.c
    src/media_devices.c
    src/ui_nav_router.c
    src/ui_key_bridge.c
    src/wifi_status_ui.c
    src/page_5_eyes.c
    src/page_chat_anim.c
    src/ai_debug_cli.c
    src/board_usb_switch.c
    beken_generated/page_6_init.c
    beken_generated/page_7_init.c
    beken_generated/page_10_init.c
    src/palm_detection.cc
    src/camera_preview.c
)

include(${CMAKE_CURRENT_LIST_DIR}/beken_generated/beken_generated.cmake)
list(APPEND srcs ${BEKEN_GENERATOR_SOURCES})
list(APPEND incs ${BEKEN_GENERATOR_INCLUDE_DIRS})

set(priv_req media_service network_transfer bk_voice_service audio_engine
              bk_smart_config bk_app_event bk_key_app bk_factory_config bk_led_blink
              bk_motor bk_nfc bk_batt_monitor bk_bluetooth bk_pan avdk_nn_module bk_servo
              bk_display bk_peripheral multimedia_device_service video_engine bk_cli avdk_utils lvgl
              bk_tflite_micro)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

# Match Kconfig CONFIG_ROBOT_TEST: preprocessor ROBOT_TEST for #if ROBOT_TEST in generated pages
if(CONFIG_ROBOT_TEST)
    component_compile_definitions(ROBOT_TEST=1)
endif()
