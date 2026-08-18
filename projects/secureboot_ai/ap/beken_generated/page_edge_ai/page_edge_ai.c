/**
 * @file page_edge_ai.c
 * @brief End-side AI entry shim and solution-example page.
 *
 * The End-side AI menu is rendered by demo_catalog. This file keeps the
 * legacy page_edge_ai entry points while hosting the LVGL overlay solution
 * page introduced by the camera/LVGL blending flow.
 */
#include "page_edge_ai.h"

#ifdef ROBOT_TEST

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <os/os.h>
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "demo/demo_catalog.h"
#include "demo/yoloface_tracking.h"
#include "page_hooks.h"
#include "ui_i18n.h"
#include "ui_nav_router.h"

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#define SOL_SCREEN_W           LOGICAL_SCREEN_WIDTH
#define SOL_SCREEN_H           LOGICAL_SCREEN_HEIGHT
#define SOL_CAMERA_VIEW_X      25
#define SOL_CAMERA_VIEW_Y      20
#define SOL_CAMERA_VIEW_W      256
#define SOL_CAMERA_VIEW_H      256
#define SOL_TEXT_H             20
#define SOL_CAMERA_BORDER_W    2
#define SOL_CAMERA_RADIUS      0
#define SOL_CAMERA_PANEL_W     (SOL_CAMERA_VIEW_W + SOL_CAMERA_BORDER_W * 2)
#define SOL_CAMERA_PANEL_H     (SOL_CAMERA_VIEW_H + SOL_TEXT_H + SOL_CAMERA_BORDER_W * 2)
#define SOL_BUTTON_X           292
#define SOL_BUTTON_W           82
#define SOL_BUTTON_H           38
#define SOL_BUTTON_GAP         18
#define SOL_BUTTON_Y0          50
#define ARCH_ROW_H             34
#define ARCH_ROW_GAP           10
#define ARCH_PANEL_H           178
#define ARCH_TASK_STACK_SIZE   (1024 * 4)
#define ARCH_TASK_NAME         "face_archive"
#define RESET_CONFIRM_MS       6000
#define RESET_TASK_STACK_SIZE  (1024 * 4)
#define RESET_TASK_NAME        "face_reset"
#define STATUS_CLEAR_MS        5000
#define STATUS_CLEAR_TASK_SIZE (1024 * 2)
#define STATUS_CLEAR_TASK_NAME "solution_status_clear"
#define FACE_PROMPT_DEBOUNCE_MS 800
#define SOLUTION_BUTTON_COUNT 4

typedef enum {
    ARCH_OP_QUERY = 0,
    ARCH_OP_DELETE,
} archive_op_t;

static lv_obj_t *s_solution_screen;
static lv_obj_t *s_solution_status_label;
static lv_obj_t *s_solution_buttons[SOLUTION_BUTTON_COUNT];
static uint32_t s_solution_selected;
static lv_obj_t *s_archive_screen;
static lv_obj_t *s_archive_status_label;
static lv_obj_t *s_archive_panel;
static lv_obj_t *s_archive_rows[YOLOFACE_ARCHIVE_MAX_ITEMS];
static lv_obj_t *s_archive_delete_btn;
static lv_obj_t *s_archive_return_btn;
static yoloface_archive_info_t s_archive_info;
static uint32_t s_archive_selected;
static uint32_t s_archive_focus;
static beken_thread_t s_archive_thread;
static volatile bool s_archive_busy;
static volatile bool s_archive_active;
static archive_op_t s_archive_op;
static uint32_t s_archive_delete_profile;
static yoloface_archive_info_t s_archive_worker_info;
static int s_archive_worker_rc;
static bool s_archive_worker_deleted;
static archive_op_t s_archive_worker_op;
static beken_thread_t s_reset_thread;
static volatile bool s_reset_busy;
static uint32_t s_reset_confirm_deadline;
static int s_reset_worker_rc;
static char s_solution_status_pending[64];
static volatile uint32_t s_solution_status_seq;
static volatile uint32_t s_solution_status_clear_seq;
static ui_lang_t s_archive_lang = UI_LANG_COUNT;
#if CONFIG_APP_EVT
static bool s_face_prompt_last_valid;
static app_evt_type_t s_face_prompt_last_event;
static uint32_t s_face_prompt_last_ms;
#endif

int page_edge_ai_archive_enter(void);
static const ui_page_nav_ops_t s_solution_nav_ops;
static void archive_row_click_cb(lv_event_t *e);
static void solution_status_apply_async(void *arg);
static void solution_status_clear_async(void *arg);

typedef enum {
    SOL_STR_ENROLL = 0,
    SOL_STR_VERIFY,
    SOL_STR_QUERY,
    SOL_STR_RESET,
    SOL_STR_ARCHIVE_TITLE,
    SOL_STR_ARCHIVE_DELETE,
    SOL_STR_ARCHIVE_RETURN,
    SOL_STR_ARCHIVE_EMPTY,
    SOL_STR_ARCHIVE_STATS_FMT,
    SOL_STR_ARCHIVE_ROW_FMT,
    SOL_STR_QUERY_FAILED,
    SOL_STR_DELETE_SUCCESS,
    SOL_STR_DELETE_FAILED,
    SOL_STR_BUSY,
    SOL_STR_QUERY_THREAD_FAILED,
    SOL_STR_NO_DELETABLE_PROFILE,
    SOL_STR_DELETING,
    SOL_STR_DB_CLEARED,
    SOL_STR_BUSY_WAIT,
    SOL_STR_CLEAR_FAILED,
    SOL_STR_CLEAR_THREAD_FAILED,
    SOL_STR_CLEAR_CONFIRM,
    SOL_STR_CLEARING,
    SOL_STR_NO_FACE,
    SOL_STR_ENROLL_FAILED_RETRY,
    SOL_STR_VERIFYING,
    SOL_STR_VERIFY_FAILED_RETRY,
    SOL_STR_QUERYING,
    SOL_STR_CAMERA_PREVIEW,
    SOL_STR_KEEP_FRONTAL,
    SOL_STR_VERIFY_PASSED,
    SOL_STR_VERIFY_FAILED,
    SOL_STR_ENROLL_COMPLETE_FMT,
    SOL_STR_ENROLL_SUCCESS_CONTINUE_FMT,
    SOL_STR_SAVING,
    SOL_STR_STORAGE_THREAD_NOT_READY,
    SOL_STR_STORAGE_NOT_READY,
    SOL_STR_ENROLLING,
    SOL_STR_ENROLL_CANCELED,
    SOL_STR_COUNT,
} solution_str_id_t;

static const char *const s_solution_tr[SOL_STR_COUNT][UI_LANG_COUNT] = {
    [SOL_STR_ENROLL] = { "录入", "Enroll" },
    [SOL_STR_VERIFY] = { "验证", "Verify" },
    [SOL_STR_QUERY] = { "查询", "Query" },
    [SOL_STR_RESET] = { "重置", "Reset" },
    [SOL_STR_ARCHIVE_TITLE] = { "人脸档案", "Face Archive" },
    [SOL_STR_ARCHIVE_DELETE] = { "删除", "Delete" },
    [SOL_STR_ARCHIVE_RETURN] = { "返回", "Back" },
    [SOL_STR_ARCHIVE_EMPTY] = { "暂无人脸档案", "No face profiles" },
    [SOL_STR_ARCHIVE_STATS_FMT] = { "档案:%u 样本:%u 图片:%u",
                                    "Profiles:%u Samples:%u Images:%u" },
    [SOL_STR_ARCHIVE_ROW_FMT] = { "%sface_%04u  样本:%u",
                                  "%sface_%04u  Samples:%u" },
    [SOL_STR_QUERY_FAILED] = { "查询失败", "Query failed" },
    [SOL_STR_DELETE_SUCCESS] = { "删除成功", "Delete success" },
    [SOL_STR_DELETE_FAILED] = { "删除失败", "Delete failed" },
    [SOL_STR_BUSY] = { "操作中", "Busy" },
    [SOL_STR_QUERY_THREAD_FAILED] = { "查询线程失败", "Query thread failed" },
    [SOL_STR_NO_DELETABLE_PROFILE] = { "暂无可删除档案", "No deletable profile" },
    [SOL_STR_DELETING] = { "删除中", "Deleting" },
    [SOL_STR_DB_CLEARED] = { "人脸库已清空", "Face DB cleared" },
    [SOL_STR_BUSY_WAIT] = { "操作中,  请稍后", "Busy, please wait" },
    [SOL_STR_CLEAR_FAILED] = { "清空失败,  请检查存储", "Clear failed, check storage" },
    [SOL_STR_CLEAR_THREAD_FAILED] = { "清空线程失败", "Clear thread failed" },
    [SOL_STR_CLEAR_CONFIRM] = { "再次点击清空人脸库", "Tap again to clear Face DB" },
    [SOL_STR_CLEARING] = { "清空中", "Clearing" },
    [SOL_STR_NO_FACE] = { "未检测到人脸", "No face detected" },
    [SOL_STR_ENROLL_FAILED_RETRY] = { "录入失败,  请重新录入", "Enroll failed, retry" },
    [SOL_STR_VERIFYING] = { "验证中", "Verifying" },
    [SOL_STR_VERIFY_FAILED_RETRY] = { "验证失败,  请重试", "Verify failed, retry" },
    [SOL_STR_QUERYING] = { "查询中", "Querying" },
    [SOL_STR_CAMERA_PREVIEW] = { "摄像头预览", "Camera preview" },
    [SOL_STR_KEEP_FRONTAL] = { "请保持正脸", "Keep face frontal" },
    [SOL_STR_VERIFY_PASSED] = { "验证通过", "Verify passed" },
    [SOL_STR_VERIFY_FAILED] = { "验证失败", "Verify failed" },
    [SOL_STR_ENROLL_COMPLETE_FMT] = { "录入完成 %u/%u", "Enroll complete %u/%u" },
    [SOL_STR_ENROLL_SUCCESS_CONTINUE_FMT] = { "录入成功 %u/%u,  请继续录入",
                                              "Enroll success %u/%u, continue" },
    [SOL_STR_SAVING] = { "保存中", "Saving" },
    [SOL_STR_STORAGE_THREAD_NOT_READY] = { "存储线程未就绪", "Storage thread not ready" },
    [SOL_STR_STORAGE_NOT_READY] = { "存储未就绪", "Storage not ready" },
    [SOL_STR_ENROLLING] = { "录入中", "Enrolling" },
    [SOL_STR_ENROLL_CANCELED] = { "已取消录入", "Enroll canceled" },
};

static const solution_str_id_t s_solution_btn_title_ids[SOLUTION_BUTTON_COUNT] = {
    SOL_STR_ENROLL,
    SOL_STR_VERIFY,
    SOL_STR_QUERY,
    SOL_STR_RESET,
};

#if CONFIG_APP_EVT
static bool face_prompt_event_for_text(const char *text, app_evt_type_t *event)
{
    if (text == NULL || event == NULL) {
        return false;
    }

    if (strcmp(text, "未检测到人脸") == 0) {
        *event = APP_EVT_FACE_NO_FACE_DETECTED;
    } else if (strstr(text, "录入失败") != NULL) {
        *event = APP_EVT_FACE_ENROLLMENT_FAILED;
    } else if (strstr(text, "录入成功") != NULL) {
        *event = APP_EVT_FACE_ENROLLMENT_SUCCESSFUL;
    } else if (strstr(text, "录入完成") != NULL) {
        *event = APP_EVT_FACE_ENROLLMENT_COMPLETED;
    } else if (strcmp(text, "验证通过") == 0) {
        *event = APP_EVT_FACE_VERIFICATION_PASSED;
    } else if (strstr(text, "验证失败") != NULL) {
        *event = APP_EVT_FACE_VERIFICATION_FAILED;
    } else if (strcmp(text, "请先录入") == 0) {
        *event = APP_EVT_FACE_ENROLLMENT_REQUIRED;
    } else if (strcmp(text, "再次点击清空人脸库") == 0) {
        *event = APP_EVT_FACE_CLEAR_CONFIRM;
    } else if (strcmp(text, "人脸库已清空") == 0) {
        *event = APP_EVT_FACE_DATABASE_CLEARED;
    } else if (strcmp(text, "删除成功") == 0) {
        *event = APP_EVT_FACE_DELETE_SUCCESS;
    } else if (strcmp(text, "删除失败") == 0) {
        *event = APP_EVT_FACE_DELETE_FAILED;
    } else if (strcmp(text, "暂无可删除档案") == 0) {
        *event = APP_EVT_FACE_NO_DELETABLE_PROFILE;
    } else {
        return false;
    }

    return true;
}

static void face_prompt_play_for_text(const char *text)
{
    app_evt_type_t event;
    uint32_t now;

    if (!face_prompt_event_for_text(text, &event)) {
        return;
    }

    now = (uint32_t)rtos_get_time();
    if (s_face_prompt_last_valid &&
        s_face_prompt_last_event == event &&
        (int32_t)(now - s_face_prompt_last_ms) < FACE_PROMPT_DEBOUNCE_MS) {
        return;
    }

    s_face_prompt_last_valid = true;
    s_face_prompt_last_event = event;
    s_face_prompt_last_ms = now;
    (void)app_event_send_msg(event, 0);
}
#else
static void face_prompt_play_for_text(const char *text)
{
    (void)text;
}
#endif

static const char *solution_tr(solution_str_id_t id)
{
    ui_lang_t lang = ui_i18n_get_lang();

    if (id >= SOL_STR_COUNT || lang >= UI_LANG_COUNT) {
        return "";
    }
    return s_solution_tr[id][lang];
}

static const char *solution_status_display_text(const char *text, char *buf, size_t buf_len)
{
    unsigned done;
    unsigned total;

    if (text == NULL || ui_i18n_get_lang() != UI_LANG_EN) {
        return text;
    }

    if (strcmp(text, "暂无人脸档案") == 0) {
        return solution_tr(SOL_STR_ARCHIVE_EMPTY);
    } else if (strcmp(text, "查询失败") == 0) {
        return solution_tr(SOL_STR_QUERY_FAILED);
    } else if (strcmp(text, "删除成功") == 0) {
        return solution_tr(SOL_STR_DELETE_SUCCESS);
    } else if (strcmp(text, "删除失败") == 0) {
        return solution_tr(SOL_STR_DELETE_FAILED);
    } else if (strcmp(text, "操作中") == 0) {
        return solution_tr(SOL_STR_BUSY);
    } else if (strcmp(text, "查询线程失败") == 0) {
        return solution_tr(SOL_STR_QUERY_THREAD_FAILED);
    } else if (strcmp(text, "暂无可删除档案") == 0) {
        return solution_tr(SOL_STR_NO_DELETABLE_PROFILE);
    } else if (strcmp(text, "删除中") == 0) {
        return solution_tr(SOL_STR_DELETING);
    } else if (strcmp(text, "人脸库已清空") == 0) {
        return solution_tr(SOL_STR_DB_CLEARED);
    } else if (strcmp(text, "操作中,  请稍后") == 0) {
        return solution_tr(SOL_STR_BUSY_WAIT);
    } else if (strcmp(text, "清空失败,  请检查存储") == 0) {
        return solution_tr(SOL_STR_CLEAR_FAILED);
    } else if (strcmp(text, "清空线程失败") == 0) {
        return solution_tr(SOL_STR_CLEAR_THREAD_FAILED);
    } else if (strcmp(text, "再次点击清空人脸库") == 0) {
        return solution_tr(SOL_STR_CLEAR_CONFIRM);
    } else if (strcmp(text, "清空中") == 0) {
        return solution_tr(SOL_STR_CLEARING);
    } else if (strcmp(text, "未检测到人脸") == 0) {
        return solution_tr(SOL_STR_NO_FACE);
    } else if (strcmp(text, "录入失败,  请重新录入") == 0) {
        return solution_tr(SOL_STR_ENROLL_FAILED_RETRY);
    } else if (strcmp(text, "验证中") == 0) {
        return solution_tr(SOL_STR_VERIFYING);
    } else if (strcmp(text, "验证失败,  请重试") == 0) {
        return solution_tr(SOL_STR_VERIFY_FAILED_RETRY);
    } else if (strcmp(text, "查询中") == 0) {
        return solution_tr(SOL_STR_QUERYING);
    } else if (strcmp(text, "摄像头预览") == 0) {
        return solution_tr(SOL_STR_CAMERA_PREVIEW);
    } else if (strcmp(text, "请保持正脸") == 0) {
        return solution_tr(SOL_STR_KEEP_FRONTAL);
    } else if (strcmp(text, "验证通过") == 0) {
        return solution_tr(SOL_STR_VERIFY_PASSED);
    } else if (strcmp(text, "验证失败") == 0) {
        return solution_tr(SOL_STR_VERIFY_FAILED);
    } else if (strcmp(text, "请先录入") == 0) {
        return "Enroll first";
    } else if (strcmp(text, "保存中") == 0) {
        return solution_tr(SOL_STR_SAVING);
    } else if (strcmp(text, "存储线程未就绪") == 0) {
        return solution_tr(SOL_STR_STORAGE_THREAD_NOT_READY);
    } else if (strcmp(text, "存储未就绪") == 0) {
        return solution_tr(SOL_STR_STORAGE_NOT_READY);
    } else if (strcmp(text, "录入中") == 0) {
        return solution_tr(SOL_STR_ENROLLING);
    } else if (strcmp(text, "已取消录入") == 0) {
        return solution_tr(SOL_STR_ENROLL_CANCELED);
    }

    if (sscanf(text, "录入完成 %u/%u", &done, &total) == 2) {
        snprintf(buf, buf_len, solution_tr(SOL_STR_ENROLL_COMPLETE_FMT), done, total);
        return buf;
    }
    if (sscanf(text, "录入成功 %u/%u,  请继续录入", &done, &total) == 2) {
        snprintf(buf, buf_len, solution_tr(SOL_STR_ENROLL_SUCCESS_CONTINUE_FMT),
                 done, total);
        return buf;
    }

    return text;
}

static void set_status_label_text(lv_obj_t *label, const char *text)
{
    char buf[64];

    if (label == NULL || text == NULL || !lv_obj_is_valid(label)) {
        return;
    }

    lv_label_set_text(label, solution_status_display_text(text, buf, sizeof(buf)));
    face_prompt_play_for_text(text);
}

static void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
    if (label != NULL) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              int x, int y, int w, int h,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, font);
    return label;
}

static lv_obj_t *create_solution_button(lv_obj_t *parent, const char *text,
                                        int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, &lv_font_ali_16);
    return btn;
}

static void set_nav_button_selected(lv_obj_t *btn, bool selected)
{
    if (btn == NULL || !lv_obj_is_valid(btn)) {
        return;
    }

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2d75b9),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, selected ? 2 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x32d5ff),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x32d5ff),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, selected ? 8 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, selected ? LV_OPA_30 : LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void solution_refresh_nav(void)
{
    for (uint32_t i = 0; i < SOLUTION_BUTTON_COUNT; i++) {
        set_nav_button_selected(s_solution_buttons[i], i == s_solution_selected);
    }
}

static uint32_t archive_visible_count(void)
{
    return s_archive_info.profile_count > YOLOFACE_ARCHIVE_MAX_ITEMS ?
           YOLOFACE_ARCHIVE_MAX_ITEMS : s_archive_info.profile_count;
}

static uint32_t archive_focus_count(void)
{
    return archive_visible_count() + 2;
}

static void archive_refresh_controls(void)
{
    uint32_t visible = archive_visible_count();

    set_nav_button_selected(s_archive_delete_btn, s_archive_focus == visible);
    set_nav_button_selected(s_archive_return_btn, s_archive_focus == visible + 1);
}

static void archive_clear_rows(void)
{
    for (uint32_t i = 0; i < YOLOFACE_ARCHIVE_MAX_ITEMS; i++) {
        if (s_archive_rows[i] != NULL && lv_obj_is_valid(s_archive_rows[i])) {
            lv_obj_del(s_archive_rows[i]);
        }
        s_archive_rows[i] = NULL;
    }
}

static void archive_ensure_rows(uint32_t visible)
{
    if (s_archive_panel == NULL || !lv_obj_is_valid(s_archive_panel)) {
        return;
    }
    for (uint32_t i = 0; i < visible && i < YOLOFACE_ARCHIVE_MAX_ITEMS; i++) {
        if (s_archive_rows[i] != NULL && lv_obj_is_valid(s_archive_rows[i])) {
            continue;
        }
        s_archive_rows[i] = create_solution_button(s_archive_panel, "--",
                                                   0, (int)i * (ARCH_ROW_H + ARCH_ROW_GAP),
                                                   236, ARCH_ROW_H);
        lv_obj_set_style_radius(s_archive_rows[i], 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(s_archive_rows[i], archive_row_click_cb,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void archive_refresh_view(void)
{
    char text[64];
    uint32_t visible = archive_visible_count();

    if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
        if (s_archive_info.profile_count == 0) {
            set_status_label_text(s_archive_status_label, "暂无人脸档案");
        } else {
            snprintf(text, sizeof(text), solution_tr(SOL_STR_ARCHIVE_STATS_FMT),
                     (unsigned)s_archive_info.profile_count,
                     (unsigned)s_archive_info.total_features,
                     (unsigned)s_archive_info.total_ppm);
            lv_label_set_text(s_archive_status_label, text);
        }
    }

    archive_ensure_rows(visible);
    for (uint32_t i = 0; i < YOLOFACE_ARCHIVE_MAX_ITEMS; i++) {
        if (s_archive_rows[i] == NULL || !lv_obj_is_valid(s_archive_rows[i])) {
            continue;
        }
        if (i >= visible) {
            lv_obj_add_flag(s_archive_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_archive_rows[i], LV_OBJ_FLAG_HIDDEN);
        yoloface_archive_item_t *item = &s_archive_info.items[i];
        snprintf(text, sizeof(text), solution_tr(SOL_STR_ARCHIVE_ROW_FMT),
                 i == s_archive_selected ? "> " : "  ",
                 (unsigned)item->profile_id,
                 (unsigned)item->feature_count);
        lv_label_set_text(lv_obj_get_child(s_archive_rows[i], 0), text);
        lv_obj_set_style_bg_color(s_archive_rows[i],
                                  i == s_archive_focus ? lv_color_hex(0x32d5ff) :
                                  lv_color_hex(0x1a2230),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_archive_rows[i], i == s_archive_focus ? 2 : 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(s_archive_rows[i], lv_color_hex(0xffffff),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_archive_panel != NULL && lv_obj_is_valid(s_archive_panel)) {
        int content_h = (int)visible * (ARCH_ROW_H + ARCH_ROW_GAP);
        lv_obj_set_scrollbar_mode(s_archive_panel,
                                  content_h > ARCH_PANEL_H ? LV_SCROLLBAR_MODE_ON :
                                  LV_SCROLLBAR_MODE_OFF);
    }
    archive_refresh_controls();
}

static void archive_apply_query_result(const yoloface_archive_info_t *info, const char *fail_text)
{
    if (info == NULL) {
        memset(&s_archive_info, 0, sizeof(s_archive_info));
        archive_refresh_view();
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label,
                                  fail_text != NULL ? fail_text : "查询失败");
        }
        return;
    }
    s_archive_info = *info;
    if (s_archive_selected >= archive_visible_count()) {
        s_archive_selected = 0;
    }
    if (s_archive_focus >= archive_focus_count()) {
        s_archive_focus = 0;
    }
    archive_refresh_view();
}

static void archive_apply_async(void *arg)
{
    (void)arg;

    if (!s_archive_active || s_archive_screen == NULL ||
        !lv_obj_is_valid(s_archive_screen)) {
        return;
    }

    if (s_archive_worker_rc == 0) {
        archive_apply_query_result(&s_archive_worker_info, NULL);
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label) &&
            s_archive_worker_op == ARCH_OP_DELETE) {
            set_status_label_text(s_archive_status_label,
                                  s_archive_worker_deleted ? "删除成功" : "删除失败");
        }
    } else {
        archive_apply_query_result(NULL, "查询失败");
    }
}

static void archive_task(void *arg)
{
    (void)arg;
    bool deleted = false;

    memset(&s_archive_worker_info, 0, sizeof(s_archive_worker_info));
    if (s_archive_op == ARCH_OP_DELETE) {
        deleted = (yoloface_solution_archive_delete(s_archive_delete_profile) == 0);
    }
    s_archive_worker_op = s_archive_op;
    s_archive_worker_deleted = deleted;
    s_archive_worker_rc = yoloface_solution_archive_query(&s_archive_worker_info);
    (void)lv_async_call(archive_apply_async, NULL);

    s_archive_busy = false;
    s_archive_thread = NULL;
    rtos_delete_thread(NULL);
}

static int archive_start_task(archive_op_t op, uint32_t profile_id)
{
    if (s_archive_busy) {
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label, "操作中");
        }
        return 0;
    }

    s_archive_busy = true;
    s_archive_op = op;
    s_archive_delete_profile = profile_id;
    if (s_archive_screen != NULL && lv_obj_is_valid(s_archive_screen)) {
        archive_clear_rows();
    }
    bk_err_t ret = rtos_create_thread(&s_archive_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      ARCH_TASK_NAME,
                                      (beken_thread_function_t)archive_task,
                                      ARCH_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_archive_thread = NULL;
        s_archive_busy = false;
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label, "查询线程失败");
        }
        return -1;
    }
    return 0;
}

static void archive_row_click_cb(lv_event_t *e)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    if (index < archive_visible_count()) {
        s_archive_selected = (uint32_t)index;
        s_archive_focus = (uint32_t)index;
        archive_refresh_view();
    }
}

static void archive_return_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_archive_busy) {
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label, "操作中");
        }
        return;
    }
    s_archive_active = false;
    if (s_archive_screen != NULL && lv_obj_is_valid(s_archive_screen)) {
        ui_nav_unregister_screen(s_archive_screen);
    }
    (void)page_edge_ai_solution_enter();
    yoloface_tracking_set_return_to_edge_ai(true);
    yoloface_tracking_set_lvgl_camera_blend(true);
    (void)yoloface_tracking_start();
}

static void archive_delete_click_cb(lv_event_t *e)
{
    (void)e;
    uint32_t visible = archive_visible_count();
    if (visible == 0 || s_archive_selected >= visible) {
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label, "暂无可删除档案");
        }
        return;
    }

    uint32_t profile_id = s_archive_info.items[s_archive_selected].profile_id;
    if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
        set_status_label_text(s_archive_status_label, "删除中");
    }
    if (archive_start_task(ARCH_OP_DELETE, profile_id) != 0 &&
        s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
        set_status_label_text(s_archive_status_label, "删除失败");
    }
}

static void reset_apply_async(void *arg)
{
    (void)arg;
    s_reset_busy = false;
    s_reset_confirm_deadline = 0;

    if (s_solution_status_label == NULL || !lv_obj_is_valid(s_solution_status_label)) {
        return;
    }

    if (s_reset_worker_rc >= 0) {
        set_status_label_text(s_solution_status_label, "人脸库已清空");
    } else if (s_reset_worker_rc == YOLOFACE_SOLUTION_ERR_BUSY) {
        set_status_label_text(s_solution_status_label, "操作中,  请稍后");
    } else {
        set_status_label_text(s_solution_status_label, "清空失败,  请检查存储");
    }
}

static void reset_task(void *arg)
{
    (void)arg;
    s_reset_worker_rc = yoloface_solution_archive_clear_all();
    (void)lv_async_call(reset_apply_async, NULL);
    s_reset_thread = NULL;
    rtos_delete_thread(NULL);
}

static int reset_start_task(void)
{
    if (s_reset_busy) {
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        }
        return 0;
    }

    s_reset_busy = true;
    s_reset_worker_rc = -1;
    bk_err_t ret = rtos_create_thread(&s_reset_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      RESET_TASK_NAME,
                                      (beken_thread_function_t)reset_task,
                                      RESET_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_reset_thread = NULL;
        s_reset_busy = false;
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "清空线程失败");
        }
        return -1;
    }
    return 0;
}

static void solution_reset_click_cb(lv_event_t *e)
{
    (void)e;

    if (yoloface_solution_enroll_is_active()) {
        (void)yoloface_solution_enroll_cancel();
        return;
    }

    if (s_reset_busy) {
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        }
        return;
    }

    uint32_t now = (uint32_t)rtos_get_time();
    if ((int32_t)(s_reset_confirm_deadline - now) <= 0) {
        s_reset_confirm_deadline = now + RESET_CONFIRM_MS;
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "再次点击清空人脸库");
        }
        return;
    }

    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        set_status_label_text(s_solution_status_label, "清空中");
    }
    (void)reset_start_task();
}

static void solution_enroll_click_cb(lv_event_t *e)
{
    (void)e;
    int rc;
    if (s_reset_busy) {
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        }
        return;
    }
    rc = yoloface_solution_enroll_request();
    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        if (rc == YOLOFACE_SOLUTION_ERR_BUSY) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        } else if (rc == YOLOFACE_SOLUTION_ERR_NO_FACE) {
            set_status_label_text(s_solution_status_label, "未检测到人脸");
        } else if (rc != 0) {
            set_status_label_text(s_solution_status_label, "录入失败,  请重新录入");
        }
    }
}

static void solution_verify_click_cb(lv_event_t *e)
{
    (void)e;
    int rc;
    if (s_reset_busy) {
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        }
        return;
    }
    rc = yoloface_solution_verify_request();
    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        if (rc == 0) {
            set_status_label_text(s_solution_status_label, "验证中");
        } else if (rc == YOLOFACE_SOLUTION_ERR_BUSY) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        } else if (rc == YOLOFACE_SOLUTION_ERR_NO_FACE) {
            set_status_label_text(s_solution_status_label, "未检测到人脸");
        } else {
            set_status_label_text(s_solution_status_label, "验证失败,  请重试");
        }
    }
}

static void solution_query_click_cb(lv_event_t *e)
{
    (void)e;
    int rc;
    if (s_reset_busy) {
        if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        }
        return;
    }
    rc = yoloface_solution_archive_enter_request();
    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        if (rc == 0) {
            set_status_label_text(s_solution_status_label, "查询中");
        } else if (rc == YOLOFACE_SOLUTION_ERR_BUSY) {
            set_status_label_text(s_solution_status_label, "操作中,  请稍后");
        } else {
            set_status_label_text(s_solution_status_label, "查询失败");
        }
    }
}

int page_edge_ai_enter(void)
{
    return demo_category_edge_enter();
}

static bool solution_status_should_auto_clear(const char *text)
{
    if (text == NULL || strcmp(text, "摄像头预览") == 0) {
        return false;
    }
    if (strstr(text, "请继续录入") != NULL) {
        return false;
    }
    if (strstr(text, "中") != NULL) {
        return false;
    }
    return true;
}

static void solution_status_clear_task(void *arg)
{
    uint32_t seq = (uint32_t)(uintptr_t)arg;

    rtos_delay_milliseconds(STATUS_CLEAR_MS);
    if (seq == s_solution_status_seq) {
        s_solution_status_clear_seq = seq;
        (void)lv_async_call(solution_status_clear_async, NULL);
    }
    rtos_delete_thread(NULL);
}

void page_edge_ai_solution_set_status(const char *text)
{
    if (text == NULL) {
        return;
    }

    snprintf(s_solution_status_pending, sizeof(s_solution_status_pending), "%s", text);
    s_solution_status_seq++;
    (void)lv_async_call(solution_status_apply_async, NULL);

    if (solution_status_should_auto_clear(text)) {
        beken_thread_t thread = NULL;
        (void)rtos_create_thread(&thread,
                                 BEKEN_DEFAULT_WORKER_PRIORITY,
                                 STATUS_CLEAR_TASK_NAME,
                                 (beken_thread_function_t)solution_status_clear_task,
                                 STATUS_CLEAR_TASK_SIZE,
                                 (void *)(uintptr_t)s_solution_status_seq);
    }
}

static void solution_status_apply_async(void *arg)
{
    (void)arg;

    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        set_status_label_text(s_solution_status_label, s_solution_status_pending);
        lv_obj_invalidate(s_solution_status_label);
    }
}

static void solution_status_clear_async(void *arg)
{
    (void)arg;

    if (s_solution_status_clear_seq != s_solution_status_seq) {
        return;
    }
    if (s_solution_status_label != NULL && lv_obj_is_valid(s_solution_status_label)) {
        set_status_label_text(s_solution_status_label, "摄像头预览");
        lv_obj_invalidate(s_solution_status_label);
    }
}

static void solution_on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;

    if (yoloface_solution_enroll_is_active()) {
        (void)yoloface_solution_enroll_cancel();
    }

    if (yoloface_detection_is_active()) {
        (void)yoloface_detection_exit_to_menu();
    } else {
        (void)page_edge_ai_enter();
    }
}

static void solution_on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;

    s_solution_selected = (s_solution_selected + SOLUTION_BUTTON_COUNT - 1) %
                          SOLUTION_BUTTON_COUNT;
    solution_refresh_nav();
}

static void solution_on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;

    s_solution_selected = (s_solution_selected + 1) % SOLUTION_BUTTON_COUNT;
    solution_refresh_nav();
}

static void solution_on_confirm(bk_lv_ui_t *ui)
{
    (void)ui;

    switch (s_solution_selected) {
    case 0:
        solution_enroll_click_cb(NULL);
        break;
    case 1:
        solution_verify_click_cb(NULL);
        break;
    case 2:
        solution_query_click_cb(NULL);
        break;
    case 3:
        solution_reset_click_cb(NULL);
        break;
    default:
        break;
    }
}

static const ui_page_nav_ops_t s_solution_nav_ops = {
    .on_focus_prev = solution_on_focus_prev,
    .on_focus_next = solution_on_focus_next,
    .on_screen_prev = solution_on_screen_prev,
    .on_screen_next = solution_on_confirm,
    .on_confirm_long = solution_on_confirm,
};

static void archive_on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    archive_return_click_cb(NULL);
}

static void archive_on_focus_prev(bk_lv_ui_t *ui)
{
    uint32_t count;

    (void)ui;
    count = archive_focus_count();
    if (count == 0) {
        return;
    }

    s_archive_focus = (s_archive_focus + count - 1) % count;
    if (s_archive_focus < archive_visible_count()) {
        s_archive_selected = s_archive_focus;
        if (s_archive_rows[s_archive_focus] != NULL &&
            lv_obj_is_valid(s_archive_rows[s_archive_focus])) {
            lv_obj_scroll_to_view(s_archive_rows[s_archive_focus], LV_ANIM_OFF);
        }
    }
    archive_refresh_view();
}

static void archive_on_focus_next(bk_lv_ui_t *ui)
{
    uint32_t count;

    (void)ui;
    count = archive_focus_count();
    if (count == 0) {
        return;
    }

    s_archive_focus = (s_archive_focus + 1) % count;
    if (s_archive_focus < archive_visible_count()) {
        s_archive_selected = s_archive_focus;
        if (s_archive_rows[s_archive_focus] != NULL &&
            lv_obj_is_valid(s_archive_rows[s_archive_focus])) {
            lv_obj_scroll_to_view(s_archive_rows[s_archive_focus], LV_ANIM_OFF);
        }
    }
    archive_refresh_view();
}

static void archive_on_confirm(bk_lv_ui_t *ui)
{
    uint32_t visible;

    (void)ui;
    visible = archive_visible_count();
    if (s_archive_focus < visible) {
        s_archive_selected = s_archive_focus;
        archive_refresh_view();
    } else if (s_archive_focus == visible) {
        archive_delete_click_cb(NULL);
    } else {
        archive_return_click_cb(NULL);
    }
}

static const ui_page_nav_ops_t s_archive_nav_ops = {
    .on_focus_prev = archive_on_focus_prev,
    .on_focus_next = archive_on_focus_next,
    .on_screen_prev = archive_on_screen_prev,
    .on_screen_next = archive_on_confirm,
    .on_confirm_long = archive_on_confirm,
};

void page_edge_ai_solution_destroy(void)
{
    s_archive_active = false;
    s_archive_busy = false;
    s_archive_thread = NULL;
    s_reset_busy = false;
    s_reset_thread = NULL;
    s_reset_confirm_deadline = 0;
    s_solution_status_seq++;
    s_solution_status_clear_seq = s_solution_status_seq;

    if (s_archive_screen != NULL && lv_obj_is_valid(s_archive_screen)) {
        ui_nav_unregister_screen(s_archive_screen);
        lv_obj_del(s_archive_screen);
    }
    s_archive_screen = NULL;
    s_archive_status_label = NULL;
    s_archive_panel = NULL;
    s_archive_delete_btn = NULL;
    s_archive_return_btn = NULL;
    for (uint32_t i = 0; i < YOLOFACE_ARCHIVE_MAX_ITEMS; i++) {
        s_archive_rows[i] = NULL;
    }
    memset(&s_archive_info, 0, sizeof(s_archive_info));
    s_archive_selected = 0;
    s_archive_focus = 0;

    if (s_solution_screen != NULL && lv_obj_is_valid(s_solution_screen)) {
        ui_nav_unregister_screen(s_solution_screen);
        lv_obj_del(s_solution_screen);
    }
    s_solution_screen = NULL;
    s_solution_status_label = NULL;
    for (uint32_t i = 0; i < SOLUTION_BUTTON_COUNT; i++) {
        s_solution_buttons[i] = NULL;
    }
    s_solution_selected = 0;
}

int page_edge_ai_archive_enter(void)
{
    s_archive_active = true;
    memset(&s_archive_info, 0, sizeof(s_archive_info));

    if (s_archive_screen != NULL && lv_obj_is_valid(s_archive_screen) &&
        s_archive_lang != ui_i18n_get_lang()) {
        ui_nav_unregister_screen(s_archive_screen);
        lv_obj_del(s_archive_screen);
        s_archive_screen = NULL;
        s_archive_status_label = NULL;
        s_archive_panel = NULL;
        s_archive_delete_btn = NULL;
        s_archive_return_btn = NULL;
        for (uint32_t i = 0; i < YOLOFACE_ARCHIVE_MAX_ITEMS; i++) {
            s_archive_rows[i] = NULL;
        }
    }

    if (s_archive_screen != NULL && lv_obj_is_valid(s_archive_screen)) {
        ui_nav_unregister_screen(s_archive_screen);
        archive_clear_rows();
        if (s_archive_status_label != NULL && lv_obj_is_valid(s_archive_status_label)) {
            set_status_label_text(s_archive_status_label, "查询中");
        }
        if (s_archive_panel != NULL && lv_obj_is_valid(s_archive_panel)) {
            lv_obj_scroll_to_y(s_archive_panel, 0, LV_ANIM_OFF);
        }
        bk_page_attach_right_swipe_gesture(s_archive_screen);
        lv_screen_load(s_archive_screen);
        (void)ui_nav_register_screen(s_archive_screen, &s_archive_nav_ops);
        s_archive_selected = 0;
        s_archive_focus = 0;
        (void)archive_start_task(ARCH_OP_QUERY, 0);
        return 0;
    }

    s_archive_lang = ui_i18n_get_lang();
    s_archive_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_archive_screen, SOL_SCREEN_W, SOL_SCREEN_H);
    lv_obj_set_scrollbar_mode(s_archive_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_archive_screen, lv_color_hex(0x050910), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_archive_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    (void)create_label(s_archive_screen, solution_tr(SOL_STR_ARCHIVE_TITLE),
                       20, 14, 180, 28,
                       &lv_font_ali_25, 0xffffff);
    s_archive_status_label = create_label(s_archive_screen, solution_tr(SOL_STR_QUERYING),
                                          20, 48, 340, 24,
                                          &lv_font_ali_16, 0x32d5ff);

    s_archive_panel = lv_obj_create(s_archive_screen);
    lv_obj_remove_style_all(s_archive_panel);
    lv_obj_set_pos(s_archive_panel, 20, 80);
    lv_obj_set_size(s_archive_panel, 248, ARCH_PANEL_H);
    lv_obj_set_scroll_dir(s_archive_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_archive_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_archive_panel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(s_archive_panel, ARCH_ROW_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_archive_delete_btn = create_solution_button(s_archive_screen,
                                                  solution_tr(SOL_STR_ARCHIVE_DELETE),
                                                  286, 104, 78, 40);
    lv_obj_add_event_cb(s_archive_delete_btn, archive_delete_click_cb, LV_EVENT_CLICKED, NULL);

    s_archive_return_btn = create_solution_button(s_archive_screen,
                                                  solution_tr(SOL_STR_ARCHIVE_RETURN),
                                                  286, 166, 78, 40);
    lv_obj_add_event_cb(s_archive_return_btn, archive_return_click_cb, LV_EVENT_CLICKED, NULL);

    bk_page_attach_right_swipe_gesture(s_archive_screen);
    lv_screen_load(s_archive_screen);
    (void)ui_nav_register_screen(s_archive_screen, &s_archive_nav_ops);

    s_archive_selected = 0;
    s_archive_focus = 0;
    (void)archive_start_task(ARCH_OP_QUERY, 0);
    return 0;
}

int page_edge_ai_solution_enter(void)
{
    if (s_solution_screen != NULL && lv_obj_is_valid(s_solution_screen)) {
        ui_nav_unregister_screen(s_solution_screen);
        lv_obj_del(s_solution_screen);
    }
    for (uint32_t i = 0; i < SOLUTION_BUTTON_COUNT; i++) {
        s_solution_buttons[i] = NULL;
    }
    s_solution_selected = 0;

    s_solution_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_solution_screen, SOL_SCREEN_W, SOL_SCREEN_H);
    lv_obj_set_scrollbar_mode(s_solution_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_solution_screen, lv_color_hex(0x050910), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_solution_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_panel = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_panel);
    lv_obj_set_pos(cam_panel, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_panel, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_radius(cam_panel, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(cam_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cam_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cam_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *preview = lv_obj_create(cam_panel);
    lv_obj_remove_style_all(preview);
    lv_obj_set_pos(preview, SOL_CAMERA_BORDER_W, SOL_CAMERA_BORDER_W);
    lv_obj_set_size(preview, SOL_CAMERA_VIEW_W, SOL_CAMERA_VIEW_H);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

    s_solution_status_label = create_label(cam_panel, solution_tr(SOL_STR_CAMERA_PREVIEW),
                                           SOL_CAMERA_BORDER_W,
                                           SOL_CAMERA_BORDER_W + SOL_CAMERA_VIEW_H,
                                           SOL_CAMERA_VIEW_W, SOL_TEXT_H,
                                           &lv_font_ali_16, 0xffffff);
    lv_obj_set_style_bg_color(s_solution_status_label, lv_color_hex(0x10151f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_solution_status_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_border = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_border);
    lv_obj_set_pos(cam_border, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_border, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_bg_opa(cam_border, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cam_border, SOL_CAMERA_BORDER_W, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(cam_border, lv_color_hex(0x32d5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(cam_border, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cam_border, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_border, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        int y = SOL_BUTTON_Y0 + i * (SOL_BUTTON_H + SOL_BUTTON_GAP);
        lv_obj_t *btn = create_solution_button(s_solution_screen,
                                               solution_tr(s_solution_btn_title_ids[i]),
                                               SOL_BUTTON_X, y,
                                               SOL_BUTTON_W, SOL_BUTTON_H);
        s_solution_buttons[i] = btn;
        if (i == 0) {
            lv_obj_add_event_cb(btn, solution_enroll_click_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 1) {
            lv_obj_add_event_cb(btn, solution_verify_click_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 2) {
            lv_obj_add_event_cb(btn, solution_query_click_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 3) {
            lv_obj_add_event_cb(btn, solution_reset_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    bk_page_attach_right_swipe_gesture(s_solution_screen);
    lv_screen_load(s_solution_screen);
    solution_refresh_nav();
    (void)ui_nav_register_screen(s_solution_screen, &s_solution_nav_ops);
    return 0;
}

#else  /* !ROBOT_TEST */

int page_edge_ai_enter(void) { return 0; }
int page_edge_ai_solution_enter(void) { return 0; }
int page_edge_ai_archive_enter(void) { return 0; }
void page_edge_ai_solution_set_status(const char *text) { (void)text; }
void page_edge_ai_solution_destroy(void) {}

#endif /* ROBOT_TEST */
