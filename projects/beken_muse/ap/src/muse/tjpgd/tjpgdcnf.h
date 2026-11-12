/*----------------------------------------------------------------------------/
/ TJpgDec configuration for the Muse display.draw_url path (vendored copy,
/ LVGL-free). Values follow the LVGL tjpgdcnf.h shape, with RGB565 output
/ and scaling enabled (the upstream esp32 image_fetch contract).
/----------------------------------------------------------------------------*/
#pragma once

#define JD_SZBUF        512
/* Specifies size of stream input buffer */

#define JD_FORMAT       1
/* Output pixel format: 0:RGB888 (24-bit), 1:RGB565 (16-bit), 2:Grayscale */

#define JD_USE_SCALE    1
/* Enable output scaling 1/2, 1/4, 1/8 */

#define JD_TBLCLIP      1
/* Use table conversion for saturation arithmetic (faster, +1KB code) */

#define JD_FASTDECODE   1
/* Optimization level: 0:basic, 1:+32-bit barrel shifter, 2:+huffman LUTs */
