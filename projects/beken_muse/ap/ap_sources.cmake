# AP source list for beken_muse -- standalone Muse product project.
# Trimmed from beken_robot: keeps the Muse gadget service (BLE pairing +
# Noise voice/photo chat), the camera preview overlay that feeds it, the
# shared UI shell (home / demo center / settings) and provisioning.
# Edge-AI (tflite/NPU), Agora cloud demos (ai_chat/vision), LAN robot
# video, BT music and the sound demos are intentionally left out; the
# matching Kconfig switches are off in ap/config/bk7259_ap/defconfig.

if(NOT APP_ROOT)
    set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}")
endif()

set(incs
    ${APP_ROOT}/include
    ${APP_ROOT}/include/demo
    ${APP_ROOT}/include/ui
    ${APP_ROOT}
)

set(_muse_app_rel_srcs
    ap_main.c
    src/common/media_devices.c
    src/common/board_usb_switch.c
    src/ui/ui_nav_router.c
    src/ui/ui_touch_gesture.c
    src/ui/ui_list_menu.c
    src/ui/ui_theme.c
    src/ui/ui_i18n.c
    src/ui/ui_key_bridge.c
    src/ui/ui_overlay_swipe.c
    src/ui/ui_screenshot.c
    src/ui/wifi_status_ui.c
    src/ui/demo_registry.c
    src/ui/pages/demo_catalog.c
    src/ui/pages/volume.c
    src/demo/udisk.c
    src/demo/camera_preview.c
)

if(CONFIG_MUSE_GADGET)
    list(APPEND _muse_app_rel_srcs
        src/demo/muse_gadget.c
        src/ui/pages/muse_status.c
        src/ui/pages/voice_chat_page.c
        src/ui/pages/voice_history_page.c
        src/ui/jolly/jolly_video.c
        src/muse/muse_service.c
        src/muse/muse_pairing.c
        src/muse/muse_ble_pairing.c
        src/muse/muse_commands.c
        src/muse/muse_display.c
        src/muse/muse_tls_probe.c
        src/muse/muse_voice.c
        src/muse/muse_voice_proto.c
        src/muse/muse_asr_volc.c
        src/muse/muse_tts_volc.c
        src/muse/voice_history.c
        src/muse/muse_storage.c
        src/muse/muse_vm_api.c
        src/muse/muse_ca_cert.c
        src/muse/muse_session.cpp
        src/muse/muse_chat.cpp
        src/muse/noise_core/src/ClientSession.cpp
        src/muse/noise_core/src/InitiatorHandshake.cpp
        src/muse/noise_core/src/PsaCryptoBackend.cpp
        src/muse/noise_core/src/ServiceCodec.cpp
        src/muse/noise_core/src/Status.cpp
        src/muse/noise_core/src/Transport.cpp
        src/muse/noise_core/src/TransportFrameCodec.cpp
    )
    list(APPEND incs
        ${APP_ROOT}/src/muse
        ${APP_ROOT}/src/muse/tjpgd
        ${APP_ROOT}/src/muse/noise_core/include
        ${APP_ROOT}/src/ui
        $ENV{ARMINO_AP_DIR}/components/json
        $ENV{ARMINO_AP_DIR}/components/bk_websocket/include
        $ENV{ARMINO_AP_DIR}/components/psa_mbedtls/mbedtls/include
        $ENV{ARMINO_AP_DIR}/components/psa_mbedtls/mbedtls_port/mbedtls/include
        $ENV{ARMINO_AP_DIR}/components/psa_mbedtls/mbedtls_port/inc
    )
endif()

set(srcs)
foreach(_rel ${_muse_app_rel_srcs})
    list(APPEND srcs "${APP_ROOT}/${_rel}")
endforeach()

if(CONFIG_MUSE_GADGET)
    # display.draw_url decodes JPEG with the classic ChaN TJpgDec (same
    # decoder family the upstream esp32 uses via its ROM copy). Vendored
    # under src/muse/tjpgd with an LVGL-free, RGB565+scale configuration.
    list(APPEND srcs
        ${APP_ROOT}/src/muse/tjpgd/tjpgd.c
    )
endif()

include(${APP_ROOT}/beken_generated/beken_generated.cmake)
list(APPEND srcs ${BEKEN_GENERATOR_SOURCES})
list(APPEND incs ${BEKEN_GENERATOR_INCLUDE_DIRS})

set(priv_req media_service audio_engine
              bk_app_event bk_key_app bk_factory_config bk_led_blink
              bk_batt_monitor bk_bluetooth bk_network_provisioning
              bk_display bk_peripheral multimedia_device_service video_engine bk_cli avdk_utils lvgl
              bk_vfs lwip_intf_v2_1
              audio_play bk_video_player)

if(CONFIG_MUSE_GADGET)
    list(APPEND priv_req json psa_mbedtls bk_websocket)
endif()
