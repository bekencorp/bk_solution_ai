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

#ifdef __cplusplus
}
#endif

#endif
