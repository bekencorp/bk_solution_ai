快速开始
=================================

:link_to_translation:`en:[English]`

.. rubric:: 阅读前说明

- **BK AI 与 SMP**：本仓库为解决方案与业务代码；固件编译依赖 **BK AVDK SMP**，通过 ``SDK_DIR`` 指向 SMP 工程。驱动、RTOS、Wi‑Fi/蓝牙等能力以 SMP 为准。详见 :doc:`../intro/index`。
- **工程选型**：声网 ``beken_genie``、火山 ``volc_rtc``、``ai_camera`` 等如何选择，见 :doc:`../projects/index` 中的「工程选型」。

.. note::

   GitLab 若仅对企业用户开放，请向对接人申请权限，或直接使用下文 **GitHub** 克隆方式获取代码。

1. 环境准备
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1  **下载 Armino SMP SDK**:

**GitLab**：您可从 GitLab 上下载 Armino SMP 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

**GitHub**：也可从 `bekencorp/bk_avdk_smp（GitHub） <https://github.com/bekencorp/bk_avdk_smp>`_ 克隆 Armino SMP 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v3.1.1

1.2 **下载 AI 解决方案代码**:

**GitLab**：您可从 GitLab 上下载 Armino AI解决方案 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1

**GitHub**：也可从 `bekencorp/bk_solution_ai（GitHub） <https://github.com/bekencorp/bk_solution_ai>`_ 克隆 AI 解决方案代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v3.1.1

.. note::

   在 **Windows** 下克隆代码前建议执行 ``git config --global core.autocrlf false``，避免换行符导致后续编译失败。


1.3 环境部署及编译
------------------------

我们提供了本地编译环境和Docker编译环境两种部署方式，用户可根据硬件和使用习惯选择一种方式部署，我们更推荐使用本地编译环境。

本地部署方案目前仅支持Windows和Linux系统下的编译。

Docker部署是基于Docker容器的环境部署与编译方案，支持在Linux、macOS及Windows系统上高效完成编译工作。
借助Docker容器化技术，您无需手动安装编译所需的各类库文件及工具链，从而显著简化了部署与编译流程。
该方案适用于熟悉Docker环境并了解其基本使用方法的用户，可帮助您快速实现环境的部署与编译。
对于不熟悉Docker技术或因网络条件限制无法使用Docker环境的用户，请使用本地编译部署方案。

.. toctree::
    :maxdepth: 1

    本地部署 <env-manual>
    Docker部署 <env-docker>


2. 编译项目
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

以下命令以 ``beken_genie`` 为例。若编译 **volc_rtc**，请将路径中的 ``beken_genie`` 替换为 ``volc_rtc``；产物位于 ``build/bk7258/<工程名>/package``。

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

3. 烧录固件到设备
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

在编译完成后，在 AI 解决方案代码的 ``build/bk7258/beken_genie/package`` 目录下将生成 ``all-app.bin``（其他工程请将 ``beken_genie`` 替换为对应目录名），使用烧录工具烧录到开发板即可。

3.1 **资源文件烧录**

    - 1、Armino 支持在 Windows/Linux 平台进行固件烧录, 烧录方法参考烧录工具中指导文档。以Windows 平台为例， Armino 目前支持 UART 烧录。

      具体 `烧录流程 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_ 请参考 `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_


4. APP注册和下载
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP下载：https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html

    注册登录：使用邮箱注册登录


5. 常见问题
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **``SDK_DIR`` 应指向哪里？** 指向已克隆的 **BK AVDK SMP** 仓库根目录（含 ``Makefile``、``tools`` 等），不是 BK AI 仓库根目录。
- **编译报错找不到工具链或脚本？** 先完成 :doc:`env-manual` 或 :doc:`env-docker` 中的 SMP 环境部署；Docker 方式需本机已安装并启动 Docker。
- **国内网络拉 Docker 镜像较慢？** 可使用 :doc:`env-docker` 中的 BEKEN 下载站等方式获取镜像。


