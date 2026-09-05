set(incs .)

set(srcs
    ap_main.c
    tf_card.c
    baf_file.c
    assets/hello_baf.c
    assets/background_baf.c
    assets/curtain_baf.c
    )

# Requirements are resolved by the build system in an early pass without config,
# so they must be unconditional. lvgl self-gates to an empty component when
# CONFIG_LVGL is off (RAW mode), so it adds no code/flash there; bk_gpu is always
# lightweight. Only the backend source is switched by CONFIG_LVGL.
set(priv_req  media_service bk_network_provisioning bk_display bk_peripheral bk_cli avdk_utils bk_baf lvgl bk_gpu avdk_driver fatfs)

# Playback backend is selected at build time by CONFIG_LVGL (menuconfig
# "Enable Lvgl"). LVGL on: render through the lv_baf widget. LVGL off (default):
# drive bk_baf directly + GPU compose + DPU flush (raw, no LVGL).
if (CONFIG_LVGL)
    list(APPEND srcs baf_page.c)
else()
    list(APPEND srcs baf_raw.c)
endif()

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -O3)
