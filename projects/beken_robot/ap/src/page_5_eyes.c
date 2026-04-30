/**
 * @file page_5_eyes.c
 * @brief 实现 page_5 的中心表情：双眼（眼白 + 瞳孔 + 眨眼 + 凝视 +
 *        颜色随声源方向变化）+ 笑嘴（开口向上的"碗形"弧）。
 *
 * 详见 page_5_eyes.h 头部注释。
 */

#include "page_5_eyes.h"

#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"

#include "components/log.h"
#include "bk_cli.h"
#include "cli.h"
#include "os/os.h"
#include "os/str.h"

#include <stdlib.h>

#define TAG "page5_eyes"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */
/*  Geometry constants                                                        */
/* ------------------------------------------------------------------------- */
/*
 * 应用层逻辑屏：cfg.rotation = ROTATE_90 之后 hor/ver 互换
 *   逻辑屏 = APP_LOGICAL_W × APP_LOGICAL_H = 390 × 360
 *   逻辑屏中心 = (195, 180)
 * 表情中心（圆环 / 双眼 / 嘴 共用）= 逻辑屏中心 + 经验位移
 */
#define APP_LOGICAL_W   390
#define APP_LOGICAL_H   360

#define FACE_CENTER_X   ((APP_LOGICAL_W / 2) + PAGE_5_FACE_NUDGE_X)
#define FACE_CENTER_Y   ((APP_LOGICAL_H / 2) + PAGE_5_FACE_NUDGE_Y)

/* ----- 双眼几何 -----
 * 单眼眼白半径 50 px，双眼水平间距 110 px（瞳孔中心到中心）。
 * 眼睛整体相对 face 中心向上 EYE_OFFSET_FROM_FACE_Y px ≈ 4 mm，留出
 * 下方画"笑嘴"的空间。
 * 凝视用瞳孔在眼白内 ±EYE_GAZE_RADIUS px 偏移；偏移量必须 <
 * (EYE_RADIUS - PUPIL_RADIUS) 才不会戳出眼白边缘。
 */
#define EYE_RADIUS               50
#define EYE_GAP                  110
#define EYE_OFFSET_FROM_FACE_Y   (-37)
#define PUPIL_RADIUS             24
#define EYE_GAZE_RADIUS          18

#define BLINK_PERIOD_MS          3500
#define BLINK_DURATION_MS        180   /* 单次眨眼"压扁→恢复"的总时长 */

/* ----- 笑嘴几何（基于 outer 矩形 + inner 矩形 + 上半 mask 三层叠加） -----
 *   - MOUTH_W / MOUTH_H_TOTAL  ：outer 椭圆/圆 外接矩形大小
 *   - MOUTH_INSET              ：inner 比 outer 每边内缩
 *   - MOUTH_GAP                ：眼底（眼白下边缘）到嘴顶（outer 顶）的距离
 *   - MOUTH_VISIBLE_H = (MOUTH_H_TOTAL/2) * 2/3  ：露出的弧高度（下半 2/3）
 *   - MOUTH_MASK_H              ：从嘴顶往下盖多少像素（盖到只露 VISIBLE_H）
 */
#define MOUTH_W           160
#define MOUTH_H_TOTAL     160
#define MOUTH_INSET       10
/*
 * 嘴位置微调（独立于 PAGE_5_FACE_NUDGE_*，让嘴在眼睛/圆环固定的前
 * 提下还能小幅再微调）：
 *   - MOUTH_GAP      ：眼白下边缘到嘴 outer 矩形顶的距离（像素）。
 *                      可以为负——为负只表示嘴 outer 矩形的顶比眼底
 *                      还高；眼睛 lv_obj 在 z 序最上层（嘴的 3 个对
 *                      象在 page_5_eyes_create 里先建，眼睛后建），
 *                      mask 不会盖到眼睛，可见的"碗形"弧仍在眼下方。
 *   - MOUTH_OFFSET_X ：嘴整体在水平方向相对 FACE_CENTER_X 的偏移；
 *                      只影响嘴 3 个对象，眼睛 / 圆环不跟随。
 *
 * 当前位置经验值（板上视觉对齐结果）：
 *   - GAP        从 30 → -53： 相对原始 +30 总共上移约 9 mm
 *                              （8 mm + 1 mm，9.2 px/mm）
 *   - OFFSET_X   = -9        ： 整组朝左 1 mm（8.5 px/mm）
 */
#define MOUTH_GAP         (-53)
#define MOUTH_OFFSET_X    (-9)
#define MOUTH_VISIBLE_H   ((MOUTH_H_TOTAL / 2) * 2 / 3)
#define MOUTH_MASK_H      (MOUTH_H_TOTAL - MOUTH_VISIBLE_H)

/* ----- 颜色 ----- */
#define COLOR_BG                lv_color_hex(0x000000)
#define COLOR_EYE_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_PUPIL_DARK        lv_color_hex(0x8B4513)
#define COLOR_PUPIL_LEFT_BLUE   lv_color_hex(0x2195F6)
#define COLOR_PUPIL_RIGHT_RED   lv_color_hex(0xFFBF00)
#define COLOR_MOUTH_OUTER       lv_color_hex(0xFFFFFF)

/* ------------------------------------------------------------------------- */
/*  State                                                                     */
/* ------------------------------------------------------------------------- */

static lv_obj_t *s_eye_left;
static lv_obj_t *s_eye_right;
static lv_obj_t *s_pupil_left;
static lv_obj_t *s_pupil_right;

static lv_obj_t *s_mouth_outer;
static lv_obj_t *s_mouth_inner;
static lv_obj_t *s_mouth_mask;

static lv_anim_t s_blink_anim;
static bool      s_blink_anim_running;

/* 首帧默认看向下方（90°），与 page_5 初始化蓝色小球位置保持一致。 */
static int s_last_gaze_deg = 90;

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static lv_obj_t *make_solid(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                            lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t diameter, lv_color_t color)
{
    return make_solid(parent, diameter, diameter, color, LV_RADIUS_CIRCLE);
}

static lv_obj_t *make_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                           lv_color_t color)
{
    return make_solid(parent, w, h, color, 0);
}

/*
 * 瞳孔在眼白里的偏移量。
 *
 * LVGL trigo 约定：lv_trigo_sin(0)=0，lv_trigo_sin(90)=LV_TRIGO_SIN_MAX
 * (32767)。屏幕坐标 x 向右、y 向下；KNOB / 箭头度数语义沿用 lv_arc：
 *   0° = 右， 90° = 下， 180° = 左， 270° = 上。
 * 因此：
 *   dx = cos(deg) * R
 *   dy = sin(deg) * R   （直接对应屏幕 y 正方向）
 */
static void compute_gaze_offset(int deg, lv_coord_t *dx, lv_coord_t *dy)
{
    int32_t s = lv_trigo_sin(deg);
    int32_t c = lv_trigo_cos(deg);
    *dx = (lv_coord_t)((c * EYE_GAZE_RADIUS) / LV_TRIGO_SIN_MAX);
    *dy = (lv_coord_t)((s * EYE_GAZE_RADIUS) / LV_TRIGO_SIN_MAX);
}

static lv_color_t deg_to_pupil_color(int deg)
{
    int d = ((deg % 360) + 360) % 360;
    if (d >= 135 && d <= 225) {
        return COLOR_PUPIL_LEFT_BLUE;     /* 屏幕左侧：蓝 */
    }
    if (d <= 45 || d >= 315) {
        return COLOR_PUPIL_RIGHT_RED;     /* 屏幕右侧：红 */
    }
    return COLOR_PUPIL_DARK;              /* 上 / 下区：默认深色 */
}

static void apply_gaze_locked(int deg)
{
    if (s_pupil_left == NULL || s_pupil_right == NULL) {
        return;
    }
    lv_coord_t dx, dy;
    compute_gaze_offset(deg, &dx, &dy);
    lv_obj_set_pos(s_pupil_left,  dx, dy);
    lv_obj_set_pos(s_pupil_right, dx, dy);

    lv_color_t c = deg_to_pupil_color(deg);
    lv_obj_set_style_bg_color(s_pupil_left,  c, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pupil_right, c, LV_PART_MAIN);
}

/* ------------------------------------------------------------------------- */
/*  Blink animation                                                           */
/* ------------------------------------------------------------------------- */

static void blink_anim_set_h(void *var, int32_t v)
{
    (void)var;
    /* destroy 后回调可能还在跑一次，先 guard 一下避免空指针 */
    if (s_pupil_left == NULL || s_pupil_right == NULL) {
        return;
    }
    /* 用瞳孔高度变化 + LV_ALIGN_CENTER 自动重对齐做"压扁→恢复"。
     * 比建一个真的眼皮 lv_obj 省事，重绘量也更小。 */
    lv_obj_set_height(s_pupil_left,  v);
    lv_obj_set_height(s_pupil_right, v);
}

static void blink_anim_start(void)
{
    if (s_pupil_left == NULL) {
        return;
    }
    /* 先停掉旧的，避免 anim 列表里残留 */
    lv_anim_delete(NULL, blink_anim_set_h);

    lv_anim_init(&s_blink_anim);
    /* var 当 marker 用，回调里通过全局 s_pupil_* 找瞳孔 */
    lv_anim_set_var(&s_blink_anim, s_pupil_left);
    lv_anim_set_exec_cb(&s_blink_anim, blink_anim_set_h);
    lv_anim_set_values(&s_blink_anim, PUPIL_RADIUS * 2, 4);
    lv_anim_set_duration(&s_blink_anim, BLINK_DURATION_MS / 2);
    lv_anim_set_reverse_duration(&s_blink_anim, BLINK_DURATION_MS / 2);
    lv_anim_set_repeat_count(&s_blink_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&s_blink_anim, BLINK_PERIOD_MS - BLINK_DURATION_MS);
    lv_anim_start(&s_blink_anim);
    s_blink_anim_running = true;
}

static void blink_anim_stop(void)
{
    if (!s_blink_anim_running) {
        return;
    }
    lv_anim_delete(NULL, blink_anim_set_h);
    s_blink_anim_running = false;
}

/* ------------------------------------------------------------------------- */
/*  Create / destroy                                                          */
/* ------------------------------------------------------------------------- */

void page_5_eyes_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return;
    }
    page_5_eyes_destroy();    /* 多次调用幂等 */

    const lv_coord_t eye_y  = FACE_CENTER_Y + EYE_OFFSET_FROM_FACE_Y;
    const lv_coord_t eye_lx = FACE_CENTER_X - EYE_GAP / 2;
    const lv_coord_t eye_rx = FACE_CENTER_X + EYE_GAP / 2;

    /* 嘴 outer 顶 = 眼白下边缘 + MOUTH_GAP。整组 outer 高度 = MOUTH_H_TOTAL，
     * 下半 MOUTH_VISIBLE_H 露出来；上半被 mask 盖到 BG 色。
     * MOUTH_OFFSET_X 只作用在 mouth_left / mask / inner，眼睛不跟随。 */
    const lv_coord_t mouth_top    = eye_y + EYE_RADIUS + MOUTH_GAP;
    const lv_coord_t mouth_left   = FACE_CENTER_X - MOUTH_W / 2 + MOUTH_OFFSET_X;
    const lv_coord_t mouth_in_top = mouth_top + MOUTH_INSET;
    const lv_coord_t mouth_in_lx  = mouth_left + MOUTH_INSET;

    /* ---------- 嘴：先建 ----------
     * mask 是 BG 色矩形，盖在 outer / inner 之上；高度比 EYE_RADIUS 大，
     * 会盖到眼睛区域。所以**必须先建嘴，再建眼睛**，让眼睛在 z 序最上面。
     */
    s_mouth_outer = make_solid(parent, MOUTH_W, MOUTH_H_TOTAL,
                               COLOR_MOUTH_OUTER, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_mouth_outer, mouth_left, mouth_top);

    s_mouth_inner = make_solid(parent,
                               MOUTH_W - 2 * MOUTH_INSET,
                               MOUTH_H_TOTAL - 2 * MOUTH_INSET,
                               COLOR_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_mouth_inner, mouth_in_lx, mouth_in_top);

    s_mouth_mask = make_rect(parent, MOUTH_W, MOUTH_MASK_H, COLOR_BG);
    lv_obj_set_pos(s_mouth_mask, mouth_left, mouth_top);

    /* ---------- 眼白 + 瞳孔（child） ---------- */
    s_eye_left  = make_circle(parent, EYE_RADIUS * 2, COLOR_EYE_WHITE);
    lv_obj_set_pos(s_eye_left,  eye_lx - EYE_RADIUS, eye_y - EYE_RADIUS);

    s_eye_right = make_circle(parent, EYE_RADIUS * 2, COLOR_EYE_WHITE);
    lv_obj_set_pos(s_eye_right, eye_rx - EYE_RADIUS, eye_y - EYE_RADIUS);

    /* 瞳孔挂成眼白的子对象，LV_ALIGN_CENTER + 后续 set_pos(dx,dy) 做凝视偏移 */
    s_pupil_left = make_circle(s_eye_left, PUPIL_RADIUS * 2, COLOR_PUPIL_DARK);
    lv_obj_set_align(s_pupil_left, LV_ALIGN_CENTER);

    s_pupil_right = make_circle(s_eye_right, PUPIL_RADIUS * 2, COLOR_PUPIL_DARK);
    lv_obj_set_align(s_pupil_right, LV_ALIGN_CENTER);

    apply_gaze_locked(s_last_gaze_deg);
    blink_anim_start();

    LOGI("eyes+mouth created: face=(%d,%d) eye_y=%d mouth_top=%d\r\n",
         FACE_CENTER_X, FACE_CENTER_Y, eye_y, mouth_top);
}

void page_5_eyes_destroy(void)
{
    blink_anim_stop();

    /* 子瞳孔随眼白自动 lv_obj_del，不必单独删；只删顶层 5 个对象。 */
    lv_obj_t *objs[] = {
        s_eye_left, s_eye_right,
        s_mouth_outer, s_mouth_inner, s_mouth_mask,
    };
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); ++i) {
        if (objs[i] != NULL && lv_obj_is_valid(objs[i])) {
            lv_obj_del(objs[i]);
        }
    }
    s_eye_left   = NULL;
    s_eye_right  = NULL;
    s_pupil_left = NULL;
    s_pupil_right = NULL;
    s_mouth_outer = NULL;
    s_mouth_inner = NULL;
    s_mouth_mask  = NULL;
}

void page_5_eyes_set_visible(bool visible)
{
    lv_obj_t *objs[] = {
        s_eye_left, s_eye_right,
        s_mouth_outer, s_mouth_inner, s_mouth_mask,
    };
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); ++i) {
        if (objs[i] == NULL) {
            continue;
        }
        if (visible) {
            lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void page_5_eyes_set_gaze(int degrees)
{
    s_last_gaze_deg = degrees;
    apply_gaze_locked(degrees);
}

/* ------------------------------------------------------------------------- */
/*  CLI: eyes blink|gaze|show                                                 */
/* ------------------------------------------------------------------------- */

static void eyes_cli_usage(void)
{
    LOGI("Usage:\r\n"
         "  eyes blink                trigger one blink animation cycle\r\n"
         "  eyes gaze <deg>           set pupil gaze direction (0~360)\r\n"
         "  eyes show 0|1             hide / show all expression objects\r\n");
}

static void eyes_cli_handler(char *out, int out_len, int argc, char **argv)
{
    (void)out;
    (void)out_len;

    if (argc < 2) {
        eyes_cli_usage();
        return;
    }

    if (os_strcmp(argv[1], "blink") == 0) {
        lv_vendor_disp_lock();
        blink_anim_start();
        lv_vendor_disp_unlock();
        LOGI("eyes blink (re)started\r\n");
        return;
    }

    if (os_strcmp(argv[1], "gaze") == 0 && argc >= 3) {
        int deg = atoi(argv[2]);
        lv_vendor_disp_lock();
        page_5_eyes_set_gaze(deg);
        lv_vendor_disp_unlock();
        LOGI("eyes gaze=%d\r\n", deg);
        return;
    }

    if (os_strcmp(argv[1], "show") == 0 && argc >= 3) {
        bool v = atoi(argv[2]) != 0;
        lv_vendor_disp_lock();
        page_5_eyes_set_visible(v);
        lv_vendor_disp_unlock();
        LOGI("eyes show=%d\r\n", (int)v);
        return;
    }

    eyes_cli_usage();
}

static const struct cli_command s_eyes_cli_cmd[] = {
    {
        "eyes",
        "eyes [blink|gaze <deg>|show 0|1]",
        eyes_cli_handler,
    },
};

int page_5_eyes_cli_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return 0;
    }
    int ret = cli_register_commands(s_eyes_cli_cmd,
                                    sizeof(s_eyes_cli_cmd) / sizeof(s_eyes_cli_cmd[0]));
    if (ret == 0) {
        s_inited = true;
        LOGI("eyes CLI registered\r\n");
    } else {
        LOGW("eyes CLI register failed: ret=%d\r\n", ret);
    }
    return ret;
}
