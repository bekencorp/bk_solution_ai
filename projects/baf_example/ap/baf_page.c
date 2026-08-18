#include "baf_page.h"

#include "lvgl.h"
#include "lv_vendor.h"
#include <os/os.h>

#define TAG "baf_page"

extern const bk_baf_source_t sample_bk_baf_source;

/* The animation widget. NULL until baf_page_create() succeeds. */
static lv_obj_t * s_baf_animation = NULL;

/* Solid light-grey screen background. The lv_baf GPU-overlay compositor reads
 * this screen colour to restore the region under the animation each frame (and
 * it also shows through the animation's transparent areas). */
#define BAF_BG_COLOR 0xD0D0D0

static void animation_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_CANCEL) {
        bk_baf_decoder_result_t result = (bk_baf_decoder_result_t)(lv_intptr_t)lv_event_get_param(event);
        if(result == BK_BAF_DECODER_RESULT_RGB_ERROR) {
            BK_LOGE(TAG, "BAF RGB H.264 decode failed\n");
        }
        else if(result == BK_BAF_DECODER_RESULT_ALPHA_ERROR) {
            BK_LOGE(TAG, "BAF Alpha H.264 decode failed\n");
        }
        else {
            BK_LOGE(TAG, "BAF H.264 decode failed\n");
        }
    }
}

void baf_page_create(void)
{
    lv_obj_t * screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BAF_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * animation = lv_baf_create(screen);
    lv_obj_add_event_cb(animation, animation_event_cb, LV_EVENT_ALL, NULL);
    lv_baf_set_src(animation, &sample_bk_baf_source);
    if(!lv_baf_is_loaded(animation)) {
        BK_LOGE(TAG, "BAF load failed\n");
        lv_obj_delete(animation);
        return;
    }
    lv_obj_align(animation, LV_ALIGN_TOP_LEFT, 0, 0);
    s_baf_animation = animation;
}
