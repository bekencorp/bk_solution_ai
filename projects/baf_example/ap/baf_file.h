#ifndef BAF_FILE_H
#define BAF_FILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read a whole .baf container file (see baf_tool/tools/to_baf.py for the layout)
 * from the mounted FATFS volume into a single PSRAM buffer and return the RAW
 * container bytes -- NOT parsed. Hand the buffer straight to
 *   bk_baf_open(&(bk_baf_config_t){ .data = buf, .data_len = *out_size });
 * and bk_baf parses/validates it internally (same path as a compiled-in array).
 *
 * Returns NULL on error (missing file / bad size / read fail). Free the buffer
 * with psram_free() AFTER bk_baf_close() -- the decoder aliases these bytes while
 * playing. *out_size receives the byte length on success. */
uint8_t *baf_file_read(const char *path, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* BAF_FILE_H */
