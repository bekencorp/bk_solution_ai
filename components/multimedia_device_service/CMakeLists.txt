set(incs include)
set(srcs
    src/app_camera.c
    src/app_display.c
    src/app_gpu.c
    src/bk_camera_lvgl_blend.c
)

if(CONFIG_MULTIMEDIA_H264_ENCODE)
    list(APPEND srcs
        src/app_codec.c
        src/multimedia_img_manager.c
    )
endif()

set(priv_req bk_common bk_pm media_service bk_peripheral avdk_driver lvgl)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

add_definitions(-Wno-unused-variable
                -Wno-unused-but-set-variable
                -Wno-int-to-pointer-cast
                -Wno-pointer-to-int-cast)
