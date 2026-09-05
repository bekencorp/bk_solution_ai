#include "baf_page.h"

#include "lvgl.h"
#include <bk_baf.h>                  /* bk_baf_decoder_result_t (event param) */
#include <components/log.h>          /* BK_LOGI / BK_LOGW / BK_LOGE */

#define TAG "baf_page"

/* BAF v1 container as a compiled-in C array (byte-identical to hello.baf), from
 * assets/hello_baf.c. Played directly via lv_baf_set_src() (bk_baf parses it). */
extern const unsigned char hello_baf[];

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

    /* Prefer the BAF v1 container file from the TF card via lv_fs (exercises the
     * lv_fs -> baf_parse -> lv_baf path). Same single entry as gif's lv_gif_set_src:
     * a path string here, a container pointer below. */
    lv_baf_set_src(animation, "S:/baf/hello.baf");
    if(lv_baf_is_loaded(animation)) {
        BK_LOGI(TAG, "BAF playing from file S:/baf/hello.baf\n");
    }
    /* Fallback: the compiled-in BAF v1 C array (no SD dependency). Same lv_baf_set_src,
     * auto-detected as an in-memory container by its "BAFANIM1" magic. */
    if(!lv_baf_is_loaded(animation)) {
        lv_baf_set_src(animation, hello_baf);
        if(lv_baf_is_loaded(animation)) {
            BK_LOGI(TAG, "BAF playing from compiled-in array\n");
        }
        else {
            BK_LOGW(TAG, "BAF array load failed\n");
        }
    }
    if(!lv_baf_is_loaded(animation)) {
        BK_LOGE(TAG, "BAF load failed\n");
        lv_obj_delete(animation);
        return;
    }
    lv_obj_align(animation, LV_ALIGN_TOP_LEFT, 0, 0);
}
