#ifndef BAF_FILE_H
#define BAF_FILE_H

#include <bk_baf.h>   /* bk_baf_source_t, bk_baf_media_t, bk_baf_decoder_ops */

#ifdef __cplusplus
extern "C" {
#endif

/* Load a .baf animation file (see baf_tool/tools/to_baf.py for the layout) from
 * the mounted FATFS volume into a single PSRAM buffer and wrap it in a
 * bk_baf_source_t that bk_baf_open() accepts -- no SDK change, this reuses the
 * public bk_baf_media_t / bk_baf_decoder_ops surface exactly like the compiled-in
 * asset. Returns NULL on error (missing file / bad magic / truncated).
 *
 * The returned source stays valid until baf_file_unload(); keep it alive for the
 * whole playback (the decoder reads the H.264 blobs in place). */
const bk_baf_source_t *baf_file_load(const char *path);

/* Free a source returned by baf_file_load(). Call only after bk_baf_close(). */
void baf_file_unload(const bk_baf_source_t *source);

#ifdef __cplusplus
}
#endif

#endif /* BAF_FILE_H */
