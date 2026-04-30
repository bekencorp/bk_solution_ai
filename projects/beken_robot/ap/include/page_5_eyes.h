/**
 * @file page_5_eyes.h
 * @brief Page 5 中心表情系统：双眼（眼白 + 瞳孔 + 眨眼 + 凝视 +
 *        颜色随声源方向变化）+ 笑嘴（开口向上的"碗形"弧）。
 *
 * 用 LVGL v9 原生图元拼出来，没有 PNG / 字体依赖：
 *   1) 眼白：两个白色 LV_RADIUS_CIRCLE 的圆形 lv_obj
 *   2) 瞳孔：两个深色 LV_RADIUS_CIRCLE 的子 lv_obj，做凝视偏移
 *   3) 笑嘴：三层 lv_obj 叠加做出"开口向上的弧"（避开 lv_arc 在
 *      ROTATE_90 + vg_lite 加速下角度乱掉的坑）：
 *        - s_mouth_outer ：外圈白色椭圆/圆
 *        - s_mouth_inner ：内圈黑色椭圆/圆，比 outer 缩 INSET 像素
 *        - s_mouth_mask  ：上半遮罩（与 page 同色），盖掉笑嘴上半部分，
 *                          只留下"开口朝上"的下半圆弧
 *      这样既不依赖 lv_arc 的角度旋转，画出来的方向也跟屏幕直接对应。
 *
 * 屏幕坐标提醒（影响所有几何参数）：
 *   ap_main.c 设置 cfg.rotation = ROTATE_90，LVGL 内部把 hor/ver 互换，
 *   应用层逻辑屏 = 390 × 360（横屏），中心 = (195, 180)。
 *   page_5 也同时被覆盖成 390x360，让 page 局部坐标 = 应用层屏坐标。
 *   下面所有 PAGE_5_FACE_NUDGE_X/_Y / PAGE_5_RING_W/_H 都按这个逻辑
 *   屏来度量。
 *
 * 设计折中：
 *   - 凝视偏移是小幅度（≤ EYE_GAZE_RADIUS 像素）的瞳孔位移，不旋转、
 *     不缩放整个眼睛。这样刷新成本最低，并且 LVGL software 渲染稳定。
 *   - 眨眼用 lv_anim 改瞳孔 height / 眼白 clip 实现："瞳孔被压扁"
 *     近似闭眼。比真做眼皮 lv_obj 更省 CPU。
 *   - 笑嘴用静态 lv_obj 叠加，不开动画，保持 CPU 占用接近 0。
 *   - PAGE_5_FACE_NUDGE_X/Y 用经验像素值，把整组（眼睛 + 圆环 + 嘴）
 *     一起做"整体平移"，方便板上视觉对齐时一处改、所有跟随。
 */

#ifndef __PAGE_5_EYES_H__
#define __PAGE_5_EYES_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/*  表情整体在 page_5 上的位置补偿（眼睛 + 圆环 + 嘴一起跟随）                */
/* ------------------------------------------------------------------------- */
/*
 * 物理屏 ≈ 4.6cm × 3.9cm （390 × 360 像素逻辑屏，横屏）
 *   水平：约 8.5 px / mm
 *   竖直：约 9.2 px / mm
 *
 * 用经验测量值 + 反复微调得到下面两个常量（板上效果为准）：
 *   X 方向：整组朝右移 ≈ 3 mm  → +25 px
 *   Y 方向：整组朝上移 ≈ 3 mm  → -28 px
 * 板子坐标系："朝右" = X 正方向；"朝上" = Y 负方向。
 */
#define PAGE_5_FACE_NUDGE_X  25
#define PAGE_5_FACE_NUDGE_Y  (-28)

/* ------------------------------------------------------------------------- */
/*  圆环外接矩形（KNOB 走的那一圈白色 arc）                                   */
/* ------------------------------------------------------------------------- */
/*
 * 之前 Designer 默认是 248 × 220（看起来比脸小一圈）。
 * 客户希望表情整体大一些，所以这里把宽度 / 高度各放大约 8 mm 直径
 *   水平 +68 px ≈ +8 mm   →  248 + 68 = 316
 *   竖直 +74 px ≈ +8 mm   →  220 + 74 = 294
 * 圆环和 page_5_eyes 双眼共用同一个 FACE_CENTER + FACE_NUDGE，
 * 调整这两个常量整组同心放大 / 平移，不会错位。
 */
#define PAGE_5_RING_W  316
#define PAGE_5_RING_H  294

/* ------------------------------------------------------------------------- */
/*  对外 API                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief 创建中心双眼 + 笑嘴所有图元到 parent 上。
 *
 * 调用前提：caller 已经持有 lv_vendor_disp_lock()（在 init_page_page_5
 * 里就是这个状态）。本函数不再加锁。
 *
 * 多次调用是幂等的：内部会先 destroy 已有图元，再重建。
 *
 * Z 序：嘴的 mask 一旦做大，会盖到眼睛上半部分；为了不让 mask 把眼睛
 * 遮住，嘴的三个对象在创建顺序上**先于**眼睛，眼睛 child 后建，自然
 * 浮在最上面。
 */
void page_5_eyes_create(lv_obj_t *parent);

/**
 * @brief 销毁所有图元 + 停掉眨眼动画。
 *        在 destroy_page_page_5 里 lv_obj_del(page_5) 之前必须先调，
 *        否则动画 callback 会写到已释放的 lv_obj 上。
 */
void page_5_eyes_destroy(void);

/**
 * @brief 显隐切换（不销毁，仅 LV_OBJ_FLAG_HIDDEN 翻转）。
 *        真正离开 page_5 用 page_5_eyes_destroy。
 */
void page_5_eyes_set_visible(bool visible);

/**
 * @brief 让瞳孔朝某个方向偏移，做"看向你"的凝视错觉。
 *
 * 角度语义沿用 LVGL arc：
 *   0°   = 屏幕右
 *   90°  = 屏幕下
 *   180° = 屏幕左
 *   270° = 屏幕上
 *
 * 同时根据角度把瞳孔色改成：
 *   - 屏幕左侧（135° ~ 225°） → 蓝
 *   - 屏幕右侧（≤ 45° 或 ≥ 315°） → 红
 *   - 上 / 下区域 → 默认深色
 *
 * 调用线程：内部不加锁，要求 caller 已在 lv_vendor_disp_lock()
 * 临界区里调用（典型场景：page_5_apply_arrow_state_locked 中）。
 */
void page_5_eyes_set_gaze(int degrees);

/**
 * @brief 注册调试 CLI（命令名：eyes，子命令 blink/gaze/show）。
 *
 *   eyes blink                手动触发一次眨眼
 *   eyes gaze <deg>           设置凝视方向（同 page_5_set_arrow_angle）
 *   eyes show 0|1             隐藏 / 显示
 *
 * 串口排查时方便单独验证眼睛状态机，不依赖 KWS 唤醒。
 */
int page_5_eyes_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_5_EYES_H__ */
