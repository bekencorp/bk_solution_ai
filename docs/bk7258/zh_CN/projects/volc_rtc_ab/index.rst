Beken Genie火山RTC OTA A/B版本
=====================================

:link_to_translation:`en:[English]`

**1. 简介**
---------------------------------

    * 本工程在 :doc:`../volc_rtc/index` 工程的基础上，增加了 **OTA A/B 双分区升级** 能力。
    * 实时音视频通信、音频处理（AEC/NS）、AI Agent 集成、房间管理、双屏显示、外设参考设计等功能与 ``volc_rtc`` 完全一致。
    * 本文档 **仅重点描述 OTA A/B 分区相关的差异**，其余功能请直接参考火山 RTC 工程文档： :doc:`../volc_rtc/index`。

.. note::

    简单来说： **``volc_rtc_ab`` = ``volc_rtc`` 的全部功能 + OTA A/B 分区升级**。

**2. OTA A/B 升级简介**
---------------------------------

**2.1 什么是 OTA A/B**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

OTA A/B（双分区）升级的基本思路：Flash 中保留两份固件分区，设备从当前分区运行，OTA 时把新固件写入 **另一份（备用）分区**，随后切换启动分区完成升级。相比单固件分区方案，升级过程中始终保留一份固件，代价是占用更多 Flash 空间。

.. note::

    启动分区切换、校验、回退等具体行为由 Bootloader 与 SMP SDK 的 OTA 机制决定，请以 `SMP OTA 文档 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_ 为准；本工程与 ``volc_rtc`` 的差异仅体现在下述分区表与打包配置上。

**2.2 分区布局差异**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

下表数值与读写属性均来自两个工程的分区表文件 ``partitions/bk7258/auto_partitions.csv``：

.. list-table::
    :header-rows: 1
    :widths: 20 20 20 40

    * - 分区
      - volc_rtc（单分区）
      - volc_rtc_ab（A/B）
      - 说明
    * - primary_bootloader
      - 68k
      - 68k
      - Bootloader
    * - primary_cp_app
      - 1428k (Write=FALSE)
      - 1360k (Write=TRUE)
      - CP 核固件分区；A/B 工程中属性由只读改为可写
    * - primary_ap_app
      - 2992k (Write=FALSE)
      - 2652k (Write=TRUE)
      - AP 核固件分区；A/B 工程中属性由只读改为可写
    * - ota
      - 3128K
      - 无
      - 单分区方案的 OTA 分区
    * - s_app
      - 无
      - 4012k
      - A/B 工程新增分区；分区表注释标注为 “B partition”（备用分区）
    * - ota_fina_executive
      - 无
      - 4K
      - A/B 工程新增分区；用途以 SMP OTA 文档为准
    * - usr_config
      - 60K
      - 56K
      - 用户配置分区

要点（均可在分区表中核对）：``s_app`` 为 A/B 工程新增分区，其注释明确标注为 “B partition”；``primary_cp_app`` / ``primary_ap_app`` 的写属性由 ``FALSE`` 改为 ``TRUE``。

**2.3 A/B 位置无关升级（position independent）**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

工程新增 ``partitions/bk7258/ab_position_independent.csv``::

    pos_independent,TRUE

``pos_independent`` 为 ``TRUE`` 时启用 A/B **位置无关（position-independent）升级** 功能；不需要该功能时可设为 ``False``（见文件内注释）。该功能的具体实现与限制请以 SMP OTA 文档为准。

**2.4 OTA 升级包（RBL）打包配置**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

工程新增 ``partitions/bk7258/ota_rbl.config``，用于生成 A/B 升级包 ``app_packab.rbl``::

    {
        "key":"0123456789ABCDEF0123456789ABCDEH",
        "iv":"0123456789ABCDEF",
        "gzip":"0",
        "aes":"0",
        "outf":"app_packab.rbl"
    }

各字段含义（以 SMP 的 rbl 打包工具文档为准）：

    * ``key`` / ``iv``：升级包加解密使用的密钥与初始向量；
    * ``aes``：升级包是否 AES 加密（``0`` / ``1``）；
    * ``gzip``：升级包是否压缩（``0`` / ``1``）；
    * ``outf``：输出升级包文件名（此处为 ``app_packab.rbl``）。

.. warning::

    建议正式量产前将示例中的 ``key`` / ``iv`` 替换为项目自有的安全密钥。

**3. 工程使用介绍**
---------------------------------

**3.1 代码下载及编译、烧录**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * 编译流程 `请参考快速开始章节 <../get-started/index.html>`_ ，将命令中的工程路径替换为 ``projects/volc_rtc_ab`` 即可。
    * 完整固件（首次整包烧录，路径命名与 ``volc_rtc`` 一致）： ``projects/volc_rtc_ab/build/bk7258/volc_rtc_ab/package/all-app.bin``
    * A/B OTA 升级包：文件名由 ``ota_rbl.config`` 的 ``outf`` 指定，即 ``app_packab.rbl``。

**3.2 升级流程说明**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. 首次通过烧录工具整包烧录 ``all-app.bin``；
    2. 后续版本通过 OTA 通道下发 A/B 升级包 ``app_packab.rbl`` 完成升级。

    OTA 的下发、写入、校验与分区切换等具体流程和接口，请参考 `SMP OTA 文档 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_ 。

**3.3 其他功能（配网、AI 对话、按键、灯效等）**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

配网、AI 对话、按键、灯效、提示音、主要配置以及火山相关功能开发等，与 ``volc_rtc`` 一致，请参考 :doc:`../volc_rtc/index`。
