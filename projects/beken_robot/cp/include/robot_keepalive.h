#pragma once

#include <common/bk_include.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t robot_cp_keepalive_init(void);
bk_err_t robot_cp_keepalive_stop(void);
bk_err_t robot_cp_keepalive_mark_ap_wakeup(void);

#ifdef __cplusplus
}
#endif
