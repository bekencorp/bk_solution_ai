/**
 * @file muse_commands.h
 * @brief Minimum link.register/link.invoke JSON contract.
 */
#ifndef __BK_MUSE_COMMANDS_H__
#define __BK_MUSE_COMMANDS_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int muse_commands_build_register(const char *node_id, const char *register_id,
                                 char *out, size_t out_size);
int muse_commands_handle_invoke(const char *json, char *out, size_t out_size);
/* 会话每次（重）连成功时调用一次：开启 draw_text 的补发宽限窗
 *（muse_commands.c 内注释）。 */
void muse_commands_note_connected(void);

#ifdef __cplusplus
}
#endif

#endif
