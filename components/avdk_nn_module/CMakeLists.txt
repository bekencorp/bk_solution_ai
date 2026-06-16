set(incs
    include
    ../../tflite_micro/gesture_detection/ap/ui/beken_generated
)
set(srcs
	src/core/box.c
	src/core/AvdkDetectionModel.cc
	src/core/AvdkVideoReatorOSD.cc
)

if (CONFIG_TFLM_YOLOFACE_V1)
	list(APPEND srcs
		src/tflm_yoloface_detection/YolofaceDetectionModel.cc
	)
	if (NOT CONFIG_SDCARD)
		list(APPEND srcs
			src/tflm_yoloface_detection/yoloface_detect_model_data.cc
		)
	endif()
endif()

if (CONFIG_TFLM_PALM_DETECTION_V1)
	list(APPEND srcs
		src/tflm_palm_detection/PalmDetectionModel.cc
	)
	if (NOT CONFIG_SDCARD)
		list(APPEND srcs
			src/tflm_palm_detection/palm_detect_model_data.cc
		)
	endif()
endif()

if (CONFIG_TFLM_HAND_GESTURE_DETECTION_V1)
	list(APPEND srcs
		src/tflm_hand_gesture_detection/HandGestureDetectionModel.cc
	)
	if (NOT CONFIG_SDCARD)
		list(APPEND srcs
			src/tflm_hand_gesture_detection/hand_gesture_detect_model_data.cc
		)
	endif()
endif()

set(priv_req bk_common unity include avdk_utils bk_tflite_micro multimedia_device_service bk_peripheral)

if (CONFIG_SDCARD)
	list(APPEND priv_req fatfs)
endif()


armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

add_definitions(
	-DTF_LITE_STATIC_MEMORY
	-DTF_LITE_DISABLE_X86_NEON
	-DTF_LITE_USE_CTIME
	-DTF_LITE_USE_GLOBAL_CMATH_FUNCTIONS
	-DCMSIS_NN
	-DKERNELS_OPTIMIZED_FOR_SPEED
)
