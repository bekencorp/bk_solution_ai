# Shared AP source list for beken_robot (base) and secureboot_ai (flavor).
#
# APP_ROOT: directory that contains ap_main.c / src / include / beken_generated.
#           Defaults to this file's directory (beken_robot/ap).
# ROBOT_APP_OVERLAY_DIR: optional flavor directory. If a relative source exists
#           there, that file is compiled instead of the base copy. Used by
#           secureboot_ai for the USB mux path that differs under secure boot.

if(NOT APP_ROOT)
    set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}")
endif()

set(incs
    ${APP_ROOT}/include
    ${APP_ROOT}/include/demo
    ${APP_ROOT}
)

# Overlay headers first so a flavor-local .h would win. Current overlay is
# .c/.cc only; include still points at the base tree.
if(ROBOT_APP_OVERLAY_DIR)
    list(INSERT incs 0
        ${ROBOT_APP_OVERLAY_DIR}/include
        ${ROBOT_APP_OVERLAY_DIR}/include/demo
        ${ROBOT_APP_OVERLAY_DIR}
    )
endif()

set(_robot_app_rel_srcs
    ap_main.c
    src/common/media_devices.c
    src/common/ui_nav_router.c
    src/common/ui_touch_gesture.c
    src/common/ui_list_menu.c
    src/common/ui_theme.c
    src/common/ui_i18n.c
    src/common/ui_key_bridge.c
    src/common/ui_overlay_swipe.c
    src/common/ui_screenshot.c
    src/common/wifi_status_ui.c
    src/common/board_usb_switch.c
    src/demo/demo_registry.c
    src/demo/demo_catalog.c
    src/demo/provisioning.c
    src/demo/ai_chat.c
    src/demo/vision.c
    src/demo/asr.c
    src/demo/music.c
    src/demo/volume.c
    src/demo/sound_localization.c
    src/demo/edge_ai/palm_tracking.cc
    src/demo/edge_ai/yoloface_tracking.cc
    src/demo/edge_ai/car_tracking.cc
    src/demo/edge_ai/hand_gesture.cc
    src/demo/camera_preview.c
    src/demo/udisk.c
    src/demo/robot_video.c
    src/demo/bt_music/bt_music.c
    src/demo/bt_music/a2dp_sink.c
    src/demo/bt_music/bt_rhythm.c
)

set(srcs)
foreach(_rel ${_robot_app_rel_srcs})
    if(ROBOT_APP_OVERLAY_DIR AND EXISTS "${ROBOT_APP_OVERLAY_DIR}/${_rel}")
        list(APPEND srcs "${ROBOT_APP_OVERLAY_DIR}/${_rel}")
    else()
        list(APPEND srcs "${APP_ROOT}/${_rel}")
    endif()
endforeach()

include(${APP_ROOT}/beken_generated/beken_generated.cmake)
list(APPEND srcs ${BEKEN_GENERATOR_SOURCES})
list(APPEND incs ${BEKEN_GENERATOR_INCLUDE_DIRS})

set(priv_req media_service network_engine bk_voice_service audio_engine
              bk_smart_config bk_app_event bk_key_app bk_factory_config bk_led_blink
              bk_motor bk_nfc bk_batt_monitor bk_bluetooth bk_pan avdk_nn_module bk_servo
              bk_display bk_peripheral multimedia_device_service video_engine bk_cli avdk_utils lvgl
              bk_tflite_micro bk_network_transfer robot_lan_net robot_video_service robot_ctrl_service bk_vfs
              audio_play)
