参考工程
=====================================

:link_to_translation:`en:[English]`

本文档包含 Armino AI 解决方案的参考工程说明。

工程选型
---------------------------------

- **``beken_genie``**：采用 **声网 Agora RTC** 与云端大模型能力；快速入门与多数文档以本工程为例。
- **``volc_rtc``**：采用 **火山引擎 RTC / AI Agent**；编译时将命令中的路径替换为 ``projects/volc_rtc``，产物在 ``build/bk7258/volc_rtc/package``。
- **``volc_rtc_ab``**：在 ``volc_rtc`` 基础上增加 **OTA A/B 分区（双分区）升级** 能力的工程；功能与 ``volc_rtc`` 一致，区别在于分区布局与固件升级流程，详见该工程文档。

请结合已有账号、云服务区域与合规要求选择 RTC／模型供应商；集成第三方服务前请阅读各厂商条款与可用区域说明。

.. toctree::
    :maxdepth: 1

    博通集成精灵AI(Beken-Genie)工程 <beken_genie/index>
    博通集成精灵火山RTC版本工程 <volc_rtc/index>
    博通集成精灵火山RTC OTA A/B版本工程 <volc_rtc_ab/index>

