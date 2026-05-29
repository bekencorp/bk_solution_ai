BK7259 机器人方案 开发者指南
=====================================

:link_to_translation:`en:[English]`

本文档详细介绍 BK7259 机器人方案中核心模块的开发指南，包括 Audio Engine、Video Engine、Network Transfer、按键、事件、工厂配置、LED、马达、倒计时等组件的简介、工作流程、重要接口和主要宏定义。

.. note::

   - LVGL UI 页面骨架与导航流程见 :doc:`../projects/beken_robot/index` 中的 *LVGL 页面整体导航流程* 与 *按键开发说明* 章节。

目录
---------------------------------

.. toctree::
    :maxdepth: 1
    :caption: 模块文档:

    audio_engine
    video_engine
    network_engine
    bk_app_event
    bk_countdown
    bk_factory_config
    bk_key_app
    bk_led_blink
    bk_motor
