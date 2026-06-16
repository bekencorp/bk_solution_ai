SDK_DIR = /home/long.chen/workspace/armino/bk7259_dev_ai/bk_avdk_smp_dev_7259v2_bringup_25W4801

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not exist, please check sdk directory.")
endif
include $(PROJECT_MAKE_FILE)
