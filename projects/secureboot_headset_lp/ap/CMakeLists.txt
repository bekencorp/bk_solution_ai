# Same warning suppression as the reference headset_lp project: the BT demo
# sources keep unused locals inside disabled #if branches (e.g. number/accept
# in a2dp_sink_demo_cli.c), which -Werror would reject.
function(set_local_library_compile_option input_target)
        set(local_compile_option )

        set(TMP_WARNING_LIST)
        list(APPEND TMP_WARNING_LIST
                                unused-but-set-variable
                                unused-variable
                                unused-function
                                unused-but-set-variable
                                unused-label
                                #uninitialized
        )

        foreach(tmp_loopvar IN LISTS TMP_WARNING_LIST)
                list(APPEND local_compile_option -Wno-error=${tmp_loopvar} -Wno-${tmp_loopvar})
        endforeach()

        target_compile_options(${input_target} PRIVATE ${local_compile_option})
endfunction()

set(incs . include)

set(srcs
    ap_main.c
	src/common/media_devices.c
	src/common/board_usb_switch.c
)

# LVGL Designer-generated UI tree (page_*_init.c + fonts + event_runtime +
# page_hooks.c). beken_generated.cmake exports BEKEN_GENERATOR_SOURCES and
# BEKEN_GENERATOR_INCLUDE_DIRS; appending them here keeps the rest of the
# file untouched.
include(${CMAKE_CURRENT_LIST_DIR}/beken_generated/beken_generated.cmake)
list(APPEND srcs ${BEKEN_GENERATOR_SOURCES})
list(APPEND incs ${BEKEN_GENERATOR_INCLUDE_DIRS})

if (CONFIG_BT)
	list(APPEND incs
		hfp_hf
		a2dp_sink
	)

	list(APPEND srcs
		a2dp_sink/a2dp_sink_demo.c
		a2dp_sink_demo_cli.c
		hfp_hf/hfp_hf_demo.c
		)

	if (CONFIG_AUD_PARAM_CTRL)
		list(APPEND incs
			audio_param
		)
		list(APPEND srcs
			audio_param/bt_audio_param.c
			audio_param/a2dp_audio_para.c
			audio_param/hfp_audio_para.c
			)
	endif()
endif()

set(priv_req bk_init media_service bk_cli audio_play audio_record lwip_intf_v2_1 bk_bluetooth lvgl bk_display bk_peripheral multimedia_device_service)

if (CONFIG_AUD_PARAM_CTRL)
    list(APPEND priv_req bk_aud_param_ctrl)
endif()

armino_component_register(
	SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES "${priv_req}"
)

# Suppress unused-variable warnings in the LVGL Designer-generated tree
# (page_1_init.c has unused 'target' in every event callback). Keep the
# same list as the AP component so we don't turn -Werror back on globally.
get_filename_component(config_component_name ${CMAKE_CURRENT_LIST_DIR} NAME)
armino_component_get_target(target "${config_component_name}")
set_local_library_compile_option(${target})
