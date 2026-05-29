// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.
// MIPI DSI panel + DPU controller helpers.

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <avdk_error.h>

#include "components/bk_frame_buffer.h"
#include "components/bk_display.h"
#include "app_display.h"
#include <avdk_check.h>

#include <sys_types.h>
#include <modules/pm.h>

#define TAG "bkmm-disp"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

typedef struct
{
    uint8_t enable;
    bk_display_ctlr_handle_t dpu_ctlr_handle;
    bk_display_bus_handle_t dis_bus_handle;
    bk_avdk_lcd_panel_handle_t panel_handle;
    bk_display_bus_handle_t cfg_bus_handle;
} display_ctx_t;

static display_ctx_t *s_disp_ctx = NULL;

static display_board_config_t *display_board_config = NULL;

/* Single owner of PM_AUXLDO_USER_DISPLAY -- only used by turn_on/turn_off
 * below. Higher layers MUST NOT vote PM_AUXLDO_USER_DISPLAY directly. */
static avdk_err_t app_display_power_enable(bool enable)
{
    int ldo_en = enable ? PM_AUXLDO_ENABLE : PM_AUXLDO_DISABLE;
    LOGI("%s, vddio enable: %d\n", __func__, ldo_en);

    pm_auxldo_ctrl_cfg_t auxldo_cfg = {0};
    auxldo_cfg.ldo   = AUXLDOS_SEL_1P8V;
    auxldo_cfg.out   = PM_AUXLDO_1P8V_OUT_1P8V;
    auxldo_cfg.user  = PM_AUXLDO_USER_DISPLAY;
    auxldo_cfg.state = ldo_en;
    AVDK_RETURN_ON_ERROR(bk_pm_auxldo_ctrl_vote(&auxldo_cfg), TAG, "display 1p8v ldo vote failed");
    rtos_delay_milliseconds(1);
    return AVDK_ERR_OK;
}

void *app_mipi_lcd_handle_get(void)
{
    AVDK_RETURN_ON_FALSE(s_disp_ctx, NULL, TAG, "s_disp_ctx NULL\n");
    return s_disp_ctx->dpu_ctlr_handle;
}

int app_mipi_lcd_turn_off(void)
{
    bk_err_t ret = BK_OK;

    display_ctx_t *config = s_disp_ctx;
    if (config == NULL)
    {
        LOGI("%s, already turn off\n", __func__);
        return ret;
    }

    if (config->enable == false)
    {
        LOGE("%s, display state already disable, may be turning off now\n", __func__);
        return BK_FAIL;
    }

    config->enable = false;
    if (config->dpu_ctlr_handle)
    {
        ret = bk_display_deinit(config->dpu_ctlr_handle);
        bk_display_delete(config->dpu_ctlr_handle);
        config->dpu_ctlr_handle = NULL;
    }
    if (config->panel_handle)
    {
        bk_lcd_panel_delete(config->panel_handle);
        config->panel_handle = NULL;
    }
    if (config->dis_bus_handle)
    {
        bk_display_bus_delete(config->dis_bus_handle);
        config->dis_bus_handle = NULL;
    }

    os_memset(config, 0, sizeof(display_ctx_t));
    os_free(config);
    s_disp_ctx = NULL;
    /* Release vddio after panel/bus/DPU are torn down. */
    app_display_power_enable(false);
    LOGI("%s complete\n", __func__);
    return BK_OK;
}

int app_mipi_lcd_turn_on(display_board_config_t *config)
{
    int ret = BK_OK;
    display_ctx_t *content = s_disp_ctx;

    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (config->mipi.panel == NULL)
    {
        LOGE("No panel specified\n");
        return BK_FAIL;
    }

    if (content)
    {
        LOGW("%s already turned on\n", __func__);
        if (!content->enable)
        {
            LOGE("%s, display state error, may be turning off now\n", __func__);
            ret = BK_FAIL;
        }
        return ret;
    }

    content = (display_ctx_t *)os_malloc(sizeof(display_ctx_t));
    AVDK_RETURN_ON_FALSE(content, BK_ERR_NO_MEM, TAG, "malloc disp_ctx failed\n");

    os_memset(content, 0, sizeof(display_ctx_t));

    /* Bring up vddio BEFORE · DSI bus / panel: panel reset + I2C bridge
     * write needs 1.8V vddio, otherwise the panel will not respond. */
    app_display_power_enable(true);


    AVDK_GOTO_ON_ERROR(bk_display_dsi_bus_new(&content->dis_bus_handle, NULL), err, TAG, "display dsi bus new err\n");
    bk_lcd_panel_config_t panel_config = {
        .reset_pin = config->mipi.pin_reset,
    };

    AVDK_GOTO_ON_ERROR(bk_lcd_mipi_panel_new(content->dis_bus_handle, &panel_config, config->mipi.panel, &content->panel_handle),
                       err, TAG, "create panel err\n");

    bk_display_dpu_config_t lcd_cfg = {
        .video.enable = config->dpu_video.enable,
        .video.decompress = config->dpu_video.decompress,
        .video.format = config->dpu_video.format,
    };

    AVDK_GOTO_ON_ERROR(bk_display_dpu_ctlr_new(&content->dpu_ctlr_handle, content->panel_handle, &lcd_cfg), err, TAG, "display dpu ctlr new err\n");
    AVDK_GOTO_ON_ERROR(bk_display_init(content->dpu_ctlr_handle), err, TAG, "display init err\n");
    AVDK_GOTO_ON_ERROR(bk_display_open(content->dpu_ctlr_handle), err, TAG, "display open err\n");

    content->enable = true;

    s_disp_ctx = content;
    return BK_OK;

err:
    if (content != NULL) {
        if (content->cfg_bus_handle) {
            bk_display_bus_delete(content->cfg_bus_handle);
            content->cfg_bus_handle = NULL;
        }
        if (content->dis_bus_handle) {
            bk_display_bus_delete(content->dis_bus_handle);
            content->dis_bus_handle = NULL;
        }
        if (content->dpu_ctlr_handle) {
            bk_display_deinit(content->dpu_ctlr_handle);
            bk_display_delete(content->dpu_ctlr_handle);
            content->dpu_ctlr_handle = NULL;
        }
        os_memset(content, 0, sizeof(display_ctx_t));
        os_free(content);
        s_disp_ctx = NULL;
    }
    /* Release vddio on failure -- we voted ON earlier in this function. */
    app_display_power_enable(false);
    LOGE("%s fail\n", __func__);
    return ret;
}

int app_mipi_lcd_flush(void *frame, avdk_err_t (*free_t)(void *args))
{
    if (s_disp_ctx && s_disp_ctx->dpu_ctlr_handle)
    {
        return bk_display_flush(s_disp_ctx->dpu_ctlr_handle, frame, free_t);
    }
    return AVDK_ERR_GENERIC;
}

bool app_mipi_lcd_state_get(void)
{
    return (s_disp_ctx && s_disp_ctx->dpu_ctlr_handle);
}

int app_display_board_config_set(display_board_config_t *config)
{
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (display_board_config == NULL)
    {
        display_board_config = os_malloc(sizeof(display_board_config_t));
        AVDK_RETURN_ON_FALSE(display_board_config, AVDK_ERR_GENERIC, TAG, "display_board_config malloc failed");
    }

    os_memcpy(display_board_config, config, sizeof(display_board_config_t));
    return AVDK_ERR_OK;
}

display_board_config_t *app_display_board_config_get(void)
{
    return display_board_config;
}
