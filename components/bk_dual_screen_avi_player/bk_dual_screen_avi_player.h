#pragma once

#ifndef __BK_DUAL_SCREEN_AVI_PLAYER_H__
#define __BK_DUAL_SCREEN_AVI_PLAYER_H__



#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_dual_screen_avi_player_start(char *file_path);

bk_err_t bk_dual_screen_avi_player_stop(void);

typedef enum {
    BK_DUAL_SCREEN_AVI_PLAYER_UI_TEXT = 0,
    BK_DUAL_SCREEN_AVI_PLAYER_UI_TEXT_AND_IMAGE,
} bk_dual_screen_avi_player_ui_t;

bk_err_t bk_dual_screen_avi_player_switch_ui(bk_dual_screen_avi_player_ui_t ui_index);

#ifdef __cplusplus
}
#endif

#endif /* __BK_DUAL_SCREEN_AVI_PLAYER_H__ */