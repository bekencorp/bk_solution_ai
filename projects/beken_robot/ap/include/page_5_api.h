/**
 * @file page_5_api.h
 * @brief Page 5（声源定位）对外控制接口
 *
 * Page 5 上有一个箭头图（page_5_image_1），用来指示声源方位。
 * 业务侧（音频 DOA 算法、拾音方向估计等）通过本接口设置箭头角度。
 *
 * 线程安全：所有接口内部都持 lv_vendor_disp_lock，可以从任何任务调用。
 */

 #ifndef __PAGE_5_API_H__
 #define __PAGE_5_API_H__
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /**
  * @brief 设置 page_5 箭头的指向角度
  *
  * 角度定义遵循 LVGL 旋转约定：
  *   - 0°   = 不旋转（设计器默认朝向）
  *   - 顺时针递增
  *   - 90°  = 顺时针 90°
  *   - 180° = 上下翻转
  *   - 270° = 逆时针 90°
  *
  * @param degrees  目标角度，单位"度"。可以是任意整数，函数内部会
  *                 做 mod 360 归一化（含负数处理）。
  *
  * 行为：
  *   - 角度被缓存到内部静态变量。即使此刻 page_5 还没 init，下次
  *     init_page_page_5() 也会自动应用最新角度。
  *   - 如果 page_5 当前已经创建（无论是否在前台显示），立刻更新
  *     图像旋转，下一帧刷新生效。
  */
 void page_5_set_arrow_angle(int degrees);
 
 /**
  * @brief 读回当前缓存的箭头角度（已归一化到 [0, 360) ）
  */
 int page_5_get_arrow_angle(void);
 
 /**
  * @brief 调整箭头图的旋转中心（pivot），单位是图像本地像素
  *
  * 设计器默认 pivot 是 (128, 64)，对 128×128 的箭头图来说
  * 不在几何中心，肉眼看到的旋转效果会偏移。如果业务需要"绕图
  * 像中心旋转"，调用 page_5_set_arrow_pivot(64, 64) 即可。
  *
  * 同样支持 page_5 未创建时调用——值会被缓存，下次 init 时生效。
  */
 void page_5_set_arrow_pivot(int x, int y);
 
 /**
  * @brief 注册 page_5 调试相关的 CLI 命令（命令名：arrow）
  *
  * 在 ap_main 里启动阶段调用一次即可。注册后串口可输入：
  *
  *   arrow                       打印用法
  *   arrow get                   读回当前角度和 pivot
  *   arrow set <deg>             设置角度（也可写成 arrow <deg>）
  *   arrow pivot <x> <y>         设置旋转中心
  *   arrow sweep <step> <ms>     周期性自动旋转，每 ms 毫秒 +step 度
  *   arrow stop                  停止 sweep
  *
  * @return 0 = 注册成功；其它 = 失败
  */
 int page_5_cli_init(void);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* __PAGE_5_API_H__ */