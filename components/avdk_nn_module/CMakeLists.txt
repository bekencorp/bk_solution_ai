set(incs
    include
    ../../tflite_micro/gesture_detection/ap/ui/beken_generated
)
# The whole module is a TFLite Micro consumer: with TFLITE_MICRO off (e.g.
# beken_muse) the tensorflow headers are not on the include path and none of
# these sources can compile. Register the component empty in that case.
set(srcs "")
if (CONFIG_TFLITE_MICRO)
list(APPEND srcs
	src/core/box.c
	src/core/AvdkDetectionModel.cc
	src/core/AvdkMultiModel.cc
	src/core/AvdkVideoReatorOSD.cc
)
endif()

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

if (CONFIG_TFLM_PET_DETECTION_V1)
	list(APPEND srcs
		src/tflm_pet_detection/PetDetectionModel.cc
	)
	if (NOT CONFIG_SDCARD)
		list(APPEND srcs
			src/tflm_pet_detection/pet_detect_model_data.cc
		)
	endif()
endif()

if (CONFIG_TFLM_FACE_RECOG_V1)
	list(APPEND srcs
		src/tflm_face_recog/FaceRecognitionModel.cc
		src/tflm_face_recog/FaceDetectionRuntime.cc
		src/tflm_face_recog/FaceVerifyRuntime.cc
	)
	if (NOT CONFIG_SDCARD)
		list(APPEND srcs
			src/tflm_face_recog/face_detect_model_data.cc
			src/tflm_face_recog/face_verify_model_data.cc
		)
	endif()
endif()

set(priv_req bk_common unity include avdk_utils bk_tflite_micro multimedia_device_service bk_peripheral)

if (CONFIG_TFLM_FACE_RECOG_V1)
	list(APPEND priv_req bk_gpu media_service avdk_driver)
endif()

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
