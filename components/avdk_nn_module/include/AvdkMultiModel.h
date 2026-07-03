#pragma once

#include <stdint.h>

#include "AvdkDetectionModel.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#define AVDK_MULTI_MODEL_OP_RESOLVER_SIZE 5

class AvdkMultiModel {
public:
    AvdkMultiModel();
    ~AvdkMultiModel();

    void setName(const char *name);
    void setModelType(avdk_nn_model_type_t type);
    void setModelLoadType(avdk_nn_model_load_type_t type);
    void setModelRamType(avdk_nn_mem_type_t type);
    void setModelFilePath(const char *path);
    void setFlashModel(uint8_t *data, uint32_t size);
    void setFastRam(avdk_nn_mem_type_t type, uint32_t size);
    void setArenaRam(avdk_nn_mem_type_t type, uint32_t size);

    int init();
    int deinit();
    int prepareInterpreter();
    void releaseInterpreter();

    tflite::MicroInterpreter *interpreter();
    uint8_t *modelData();
    uint32_t modelDataSize() const;

private:
    void *allocMemory(avdk_nn_mem_type_t type, uint32_t size);
    void freeMemory(avdk_nn_mem_type_t type, void *ptr);
    int LoadModel();
    int UnloadModel();
    int npuStartup();
    int npuShutdown();
    int ensureSharedArena(avdk_nn_mem_type_t type, uint32_t size);
    void freeSharedArena();

    const char *name_;
    avdk_nn_model_type_t model_type_;
    avdk_nn_model_load_type_t modelLoadType_;
    avdk_nn_mem_type_t model_ram_type_;
    uint8_t *model_data_;
    uint32_t model_data_size_;
    uint8_t *model_flash_data_;
    uint32_t model_flash_data_size_;
    const char *modelFilePath_;

    avdk_nn_mem_type_t fast_ram_type_;
    uint32_t fast_ram_data_size_;

    avdk_nn_mem_type_t arena_ram_type_;
    uint8_t *arena_ram_data_;
    uint32_t arena_data_size_;

    tflite::MicroInterpreter *pinterpreter_;
    tflite::MicroMutableOpResolver<AVDK_MULTI_MODEL_OP_RESOLVER_SIZE> micro_op_resolver_;
    bool resolver_ready_;
    bool initialized_;

    static bool s_hw_inited_;
    static uint8_t *s_fast_ram_data_;
    static uint32_t s_fast_ram_data_size_;
    static avdk_nn_mem_type_t s_fast_ram_type_;
    static int s_model_count_;

    static uint8_t *s_shared_arena_data_;
    static uint32_t s_shared_arena_size_;
    static avdk_nn_mem_type_t s_shared_arena_type_;
    static AvdkMultiModel *s_active_model_;
};
