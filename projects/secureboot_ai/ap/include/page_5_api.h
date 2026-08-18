/**
 * @file page_5_api.h
 * @brief Page 5 (sound source localization) external control surface.
 *
 * Page 5 used to render an arrow image (page_5_image_1) pointing to
 * the estimated sound source direction. Business code (audio DOA
 * algorithm, beamforming estimator, ...) calls into this header to
 * push the angle.
 *
 * Thread safety: all entry points take lv_vendor_disp_lock
 * internally, so they are safe to call from any task.
 */

 #ifndef __PAGE_5_API_H__
 #define __PAGE_5_API_H__
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /**
  * @brief Set the page_5 arrow direction.
  *
  * Angle convention follows LVGL rotation:
  *   - 0 deg   = unrotated (Designer default)
  *   - increases clockwise
  *   - 90 deg  = clockwise 90
  *   - 180 deg = upside down
  *   - 270 deg = counter-clockwise 90
  *
  * @param degrees  Target angle in degrees. Any signed integer is
  *                 accepted; the function normalizes via mod 360
  *                 (negatives included).
  *
  * Behavior:
  *   - The angle is cached in a static. Even when page_5 has not been
  *     created yet, the next init_page_page_5() applies the most
  *     recent value.
  *   - If page_5 is already alive (foreground or not) the image
  *     rotation is updated immediately and takes effect on the next
  *     refresh frame.
  */
 void page_5_set_arrow_angle(int degrees);
 
 /**
  * @brief Return the cached arrow angle (normalized to [0, 360)).
  */
 int page_5_get_arrow_angle(void);
 
 /**
  * @brief Adjust the rotation pivot of the arrow image, in image-local
  *        pixels.
  *
  * The Designer default pivot is (128, 64), which is NOT the geometric
  * center of the 128x128 arrow image -- the eye perceives the rotation
  * as slightly off-axis. If the business wants "rotate around the
  * image center", call page_5_set_arrow_pivot(64, 64).
  *
  * Safe to call before page_5 is created -- the value is cached and
  * applied at the next init.
  */
 void page_5_set_arrow_pivot(int x, int y);
 
 /**
  * @brief Register the page_5 debug CLI (command name: `arrow`).
  *
  * Call once during startup in ap_main. After registration the serial
  * console accepts:
  *
  *   arrow                       print usage
  *   arrow get                   read back the current angle and pivot
  *   arrow set <deg>             set angle (also accepts `arrow <deg>`)
  *   arrow pivot <x> <y>         set the rotation pivot
  *   arrow sweep <step> <ms>     auto-rotate every <ms> by <step> deg
  *   arrow stop                  stop sweep
  *
  * @return 0 on success; non-zero on registration failure.
  */
 int page_5_cli_init(void);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* __PAGE_5_API_H__ */
