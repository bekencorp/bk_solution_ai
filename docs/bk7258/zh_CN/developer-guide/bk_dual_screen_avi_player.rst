Dual Screen AVI Player 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Dual Screen AVI Player 是一个双屏 AVI 视频播放模块，支持在双 SPI LCD 屏幕上同步播放 AVI 格式视频。该模块封装了 AVI 播放器、双屏显示控制器和文件系统访问等功能，为上层应用提供简单的双屏视频播放接口。支持 RGB565 格式输出，自动处理字节序交换。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **双屏同步播放**: 支持两个 SPI LCD 屏幕同步播放视频
- **AVI 格式支持**: 支持 AVI 视频格式播放
- **RGB565 输出**: 输出格式为 RGB565，自动字节序交换
- **文件系统支持**: 支持从 VFS 文件系统读取视频文件
- **自动资源管理**: 自动管理显示控制器和帧缓冲区
- **线程安全**: 使用信号量和线程机制保证线程安全

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Dual Screen AVI Player 模块架构如下：

::

    应用层 (Application)
            ↓
    Dual Screen AVI Player API (bk_dual_screen_avi_player.h)
            ↓
    AVI Player (avi_player)
            ↓
    双屏显示控制器 (bk_display_dual_spi)
            ↓
    帧缓冲区 (frame_buffer_t)
            ↓
    SPI LCD 屏幕 (GC9D01 x2)

工作流程
---------------------------------

启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_dual_screen_avi_player_start()`` 详细流程：

::

    1. 检查运行状态
        - 如果已在运行，直接返回
        ↓
    2. 初始化 VFS
        - bk_avi_player_vfs_init()
        ↓
    3. 打开 AVI 播放器
        - bk_avi_player_open()
        - 配置输出格式为 RGB565
        - 启用字节序交换
        ↓
    4. 获取播放器句柄
        - bk_avi_player_get_handle()
        ↓
    5. 创建信号量
        - rtos_init_semaphore_ex()
        ↓
    6. 创建双屏显示控制器
        - bk_display_dual_spi_new()
        - 配置两个 LCD 屏幕参数
        ↓
    7. 打开显示控制器
        - bk_display_open()
        ↓
    8. 分配帧缓冲区
        - os_malloc(sizeof(frame_buffer_t))
        ↓
    9. 创建播放线程
        - rtos_create_thread()
        ↓
    10. 启动背光
        - lcd_backlight_open()
        ↓
    播放线程运行中

播放线程流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

播放线程主要流程：

::

    1. 等待信号量
        - rtos_get_semaphore()
        ↓
    2. 读取视频帧
        - bk_avi_player_get_frame()
        ↓
    3. 设置帧缓冲区
        - 填充 frame_buffer_t 结构
        ↓
    4. 显示到双屏
        - bk_display_write_frame()
        - 同时显示到两个屏幕
        ↓
    5. 释放帧
        - bk_avi_player_release_frame()
        ↓
    循环步骤 1-5

停止流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_dual_screen_avi_player_stop()`` 详细流程：

::

    1. 检查运行状态
        - 如果未运行，直接返回
        ↓
    2. 设置停止标志
        - g_dual_screen_avi_player_is_running = false
        ↓
    3. 释放信号量
        - rtos_set_semaphore()
        - 唤醒播放线程
        ↓
    4. 等待线程结束
        - rtos_delete_thread()
        ↓
    5. 关闭背光
        - lcd_backlight_close()
        ↓
    6. 释放帧缓冲区
        - os_free(lcd_frame_buffer)
        ↓
    7. 关闭显示控制器
        - bk_display_close()
        - bk_display_delete()
        ↓
    8. 关闭 AVI 播放器
        - bk_avi_player_close()
        ↓
    9. 反初始化 VFS
        - bk_avi_player_vfs_deinit()
        ↓
    10. 销毁信号量
        - rtos_deinit_semaphore()
        ↓
    停止完成

重要接口
---------------------------------

播放控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 启动双屏 AVI 播放
     * 
     * @param file_path AVI 文件路径
     * @return bk_err_t 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    bk_err_t bk_dual_screen_avi_player_start(char *file_path);

    /**
     * @brief 停止双屏 AVI 播放
     * 
     * @return bk_err_t 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    bk_err_t bk_dual_screen_avi_player_stop(void);

配置结构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

双屏 SPI 控制器配置：

.. code:: c

    typedef struct {
        bk_display_spi_ctlr_config_t lcd0_config;  // LCD0 配置
        bk_display_spi_ctlr_config_t lcd1_config;  // LCD1 配置
    } bk_display_dual_spi_ctlr_config_t;

    // 默认配置
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

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用双屏 AVI 播放器
    CONFIG_DUAL_SCREEN_AVI_PLAYER=y

    // 依赖配置
    CONFIG_AVI_PLAYER=y          // AVI 播放器
    CONFIG_LCD_SPI_GC9D01=y      // GC9D01 LCD 驱动
    CONFIG_LCD_SPI_DEVICE_NUM=2  // 双屏配置


使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "bk_dual_screen_avi_player.h"

    void example_usage(void)
    {
        // 启动播放
        bk_err_t ret = bk_dual_screen_avi_player_start("/sdcard/video.avi");
        if (ret != BK_OK) {
            // 处理错误
            return;
        }
        
        // 播放中...
        
        // 停止播放
        bk_dual_screen_avi_player_stop();
    }

注意事项
---------------------------------

1. **文件格式**: 仅支持 AVI 格式，且必须经过 AVI 转换工具转换，分辨率应为 320x160

2. **文件系统**: 视频文件必须存储在可访问的文件系统中（如 SD NAND），路径为绝对路径

3. **资源管理**: 播放前确保文件系统已挂载，播放后及时停止释放资源


