Armino AI 解决方案简介
------------------------------------

:link_to_translation:`en:[English]`

概述
------------------------------------

Armino AI 解决方案是博通集成电路（上海）股份有限公司基于 Armino SMP 架构开发的智能 AI 设备解决方案。该方案提供了完整的端到云、云到大模型的 AI 交互能力，支持多种大语言模型接入，为开发者提供快速构建智能 AI 设备的完整开发框架。当前支持火山、声网等大模型应用。


工程编译
-------------------------------------

1. 环境准备
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1  **下载 Armino SMP SDK**:

您可从 gitlab 上下载 Armino SMP 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

1.2 **下载 AI 解决方案代码**:

您可从 gitlab 上下载 Armino AI解决方案 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1


2. 工程介绍
--------------------------------
Armino AI 解决方案主要包含声网RTC版本工程、火山RTC版本工程、AI camera版本工程。

声网RTC版本工程 (beken_genie)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

基于BK7258芯片和声网RTC SDK的AI设备解决方案，提供端到云、云到大模型的完整AI交互能力。
- 支持声网RTC实时音视频通信，集成音频处理引擎（AEC、NS、KWS）
- 支持OPUS、PCM音频编码格式，支持提示音播放
- 支持多种大语言模型接入（OpenAI、豆包、DeepSeek等）
- 支持双SPI LCD屏幕显示，提供视觉加语音的交互体验
- 包含丰富外设参考设计：陀螺仪、NFC、按键、震动马达、NAND Flash、LED灯效、充电管理、DVP camera等

火山RTC版本工程 (volc_rtc)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

基于BK7258芯片和火山引擎RTC SDK的实时音视频通信解决方案，支持与云端AI Agent进行实时对话。
- 支持火山引擎RTC实时音视频通信，集成音频处理引擎（AEC、NS）
- 支持G722、OPUS、PCM音频编码格式
- 支持火山引擎AI Agent服务集成，支持语音对话和图像识别
- 支持双SPI LCD屏幕显示，提供视觉加语音的交互体验
- 包含丰富外设参考设计：陀螺仪、NFC、按键、震动马达、NAND Flash、LED灯效、充电管理、DVP camera等

AI Camera版本工程 (ai_camera)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

AI相机解决方案，目前正在开发中。


3. 编译项目
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**方式一：直接编译**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

**方式二：或者可以通过export来指定SDK路径**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

**方式三：使用 Docker（Linux/Mac）**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

**方式四：使用 Docker（Windows PowerShell）**

.. code:: powershell

    cd C:\armino\bk_solution_ai\projects\beken_genie
        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        .\dbuild.ps1 make clean
        .\dbuild.ps1 make bk7258

4. 烧录固件到设备
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

在编译完成后，在AI 解决方案代码的/build/bk7258/beken_genie/package目录下将生成all-app.bin，使用烧录工具烧录到开发板即可。

4.1 **资源文件烧录**

    - 1、Armino 支持在 Windows/Linux 平台进行固件烧录, 烧录方法参考烧录工具中指导文档。以Windows 平台为例， Armino 目前支持 UART 烧录。

      具体 `烧录流程 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_ 请参考 `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_


5. 工程演示以及操作步骤请见如下链接
----------------------------------

5.1  APP下载地址：`下载 <https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html>`_

    注册登录：使用邮箱注册登录

5.2  操作步骤：主要包含关于APP配网方式和流程，如何正常启动Agent，详细流程请参考 `AI解决方案 <https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/intro/index.html>`_

