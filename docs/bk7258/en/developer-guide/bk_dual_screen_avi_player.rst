Dual Screen AVI Player Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Dual Screen AVI Player is a dual-screen AVI video playback module that supports synchronized playback of AVI format videos on dual SPI LCD screens. This module encapsulates AVI player, dual-screen display controller, and file system access, providing a simple dual-screen video playback interface for upper-layer applications. Supports RGB565 format output with automatic byte order swapping.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Dual-screen Synchronized Playback**: Supports synchronized video playback on two SPI LCD screens
- **AVI Format Support**: Supports AVI video format playback
- **RGB565 Output**: Output format is RGB565 with automatic byte order swapping
- **File System Support**: Supports reading video files from VFS file system
- **Automatic Resource Management**: Automatically manages display controller and frame buffers
- **Thread Safety**: Uses semaphore and thread mechanisms to ensure thread safety

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Dual Screen AVI Player module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Dual Screen AVI Player API (bk_dual_screen_avi_player.h)
            ↓
    AVI Player (avi_player)
            ↓
    Dual-screen Display Controller (bk_display_dual_spi)
            ↓
    Frame Buffer (frame_buffer_t)
            ↓
    SPI LCD Screens (GC9D01 x2)

Workflow
----------------

Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_dual_screen_avi_player_start()``:

::

    1. Check Running Status
        - If already running, return directly
        ↓
    2. Initialize VFS
        - bk_avi_player_vfs_init()
        ↓
    3. Open AVI Player
        - bk_avi_player_open()
        - Configure output format as RGB565
        - Enable byte order swapping
        ↓
    4. Get Player Handle
        - bk_avi_player_get_handle()
        ↓
    5. Create Semaphore
        - rtos_init_semaphore_ex()
        ↓
    6. Create Dual-screen Display Controller
        - bk_display_dual_spi_new()
        - Configure two LCD screen parameters
        ↓
    7. Open Display Controller
        - bk_display_open()
        ↓
    8. Allocate Frame Buffer
        - os_malloc(sizeof(frame_buffer_t))
        ↓
    9. Create Playback Thread
        - rtos_create_thread()
        ↓
    10. Start Backlight
        - lcd_backlight_open()
        ↓
    Playback Thread Running

Playback Thread Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Main flow of playback thread:

::

    1. Wait for Semaphore
        - rtos_get_semaphore()
        ↓
    2. Read Video Frame
        - bk_avi_player_get_frame()
        ↓
    3. Set Frame Buffer
        - Fill frame_buffer_t structure
        ↓
    4. Display to Dual Screens
        - bk_display_write_frame()
        - Display to both screens simultaneously
        ↓
    5. Release Frame
        - bk_avi_player_release_frame()
        ↓
    Loop steps 1-5

Stop Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_dual_screen_avi_player_stop()``:

::

    1. Check Running Status
        - If not running, return directly
        ↓
    2. Set Stop Flag
        - g_dual_screen_avi_player_is_running = false
        ↓
    3. Release Semaphore
        - rtos_set_semaphore()
        - Wake up playback thread
        ↓
    4. Wait for Thread End
        - rtos_delete_thread()
        ↓
    5. Close Backlight
        - lcd_backlight_close()
        ↓
    6. Release Frame Buffer
        - os_free(lcd_frame_buffer)
        ↓
    7. Close Display Controller
        - bk_display_close()
        - bk_display_delete()
        ↓
    8. Close AVI Player
        - bk_avi_player_close()
        ↓
    9. Deinitialize VFS
        - bk_avi_player_vfs_deinit()
        ↓
    10. Destroy Semaphore
        - rtos_deinit_semaphore()
        ↓
    Stop Complete

Important Interfaces
--------------------

Playback Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Start dual-screen AVI playback
     * 
     * @param file_path AVI file path
     * @return bk_err_t 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    bk_err_t bk_dual_screen_avi_player_start(char *file_path);

    /**
     * @brief Stop dual-screen AVI playback
     * 
     * @return bk_err_t 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    bk_err_t bk_dual_screen_avi_player_stop(void);

Configuration Structure
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Dual-screen SPI controller configuration:

.. code-block:: C

    typedef struct {
        bk_display_spi_ctlr_config_t lcd0_config;  // LCD0 configuration
        bk_display_spi_ctlr_config_t lcd1_config;  // LCD1 configuration
    } bk_display_dual_spi_ctlr_config_t;

    // Default configuration
    bk_display_dual_spi_ctlr_config_t dual_spi_ctlr_config = {
        .lcd0_config.lcd_device = &lcd_device_gc9d01,
        .lcd0_config.spi_id = 0,
        .lcd0_config.dc_pin = GPIO_7,
        .lcd0_config.reset_pin = GPIO_6,
        .lcd1_config.lcd_device = &lcd_device_gc9d01,
        .lcd1_config.spi_id = 1,
        .lcd1_config.dc_pin = GPIO_5,
        .lcd1_config.reset_pin = GPIO_45,
    };

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable dual-screen AVI player
    CONFIG_DUAL_SCREEN_AVI_PLAYER=y

    // Dependencies
    CONFIG_AVI_PLAYER=y          // AVI player
    CONFIG_LCD_SPI_GC9D01=y      // GC9D01 LCD driver
    CONFIG_LCD_SPI_DEVICE_NUM=2  // Dual-screen configuration

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "bk_dual_screen_avi_player.h"

    void example_usage(void)
    {
        // Start playback
        bk_err_t ret = bk_dual_screen_avi_player_start("/sdcard/video.avi");
        if (ret != BK_OK) {
            // Handle error
            return;
        }
        
        // Playing...
        
        // Stop playback
        bk_dual_screen_avi_player_stop();
    }

Notes
----------------

1. **File Format**: Only supports AVI format, must be converted using AVI conversion tool, resolution should be 320x160

2. **File System**: Video files must be stored in an accessible file system (such as SD NAND), path should be absolute

3. **Resource Management**: Ensure file system is mounted before playback, stop in time after playback to release resources

