快速入门
=================================

:link_to_translation:`en:[English]`

本页仅说明 BK7259 机器人方案的代码下载、编译与烧录入口。开发板上电、APP 下载、BLE 配网、按键与 UI 操作请参考 :doc:`../projects/beken_robot/index`。

BK7259 机器人方案代码依赖 **BK AVDK SMP** 编译。下面示例默认代码放在 ``~/armino`` 目录：

- SDK：``~/armino/bk_avdk_smp``
- 方案仓库：``~/armino/bk_solution_ai``
- 当前发布工程：``~/armino/bk_solution_ai/projects/beken_robot``

.. note::

   GitLab 仓库目前仅对企业用户开放，请向对接人申请权限；外部用户请使用 GitHub 镜像。

1. 代码下载
---------------------------------

1.1 下载 BK AVDK SMP SDK
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**GitLab** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v4.0.1

**GitHub** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v4.0.1

SDK 默认使用 ``release/v4.0.1`` 分支，版本对应 ``bk_avdk_smp_release_4.0.1.x`` 系列。

1.2 下载 BK7259 机器人方案代码
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

机器人方案对应代码仓库为 **AI Solution** （``bk_solution_ai``）。仓库根目录下直接包含 ``projects/``、``components/``、``docs/``，没有外层 ``solution/`` 目录。

**GitLab** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v4.0.1

**GitHub** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v4.0.1

.. note::

   Windows 下克隆代码前建议执行 ``git config --global core.autocrlf false``，避免换行符导致后续编译失败。

1.3 编译环境部署
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

BK AVDK SMP 提供本地编译环境和 Docker 编译环境。环境部署只需选择一种方式完成：

.. toctree::
    :maxdepth: 1

    本地部署 <env-manual>
    Docker部署 <env-docker>


2. 编译工程
---------------------------------

以下命令以 ``beken_robot`` 工程为例。

2.1 直接指定 SDK 路径
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7259 SDK_DIR=~/armino/bk_avdk_smp

2.2 通过环境变量指定 SDK 路径
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7259

2.3 使用 Docker（Linux / macOS）
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    export SDK_DIR=~/armino/bk_avdk_smp
    ./dbuild.sh make clean
    ./dbuild.sh make bk7259

2.4 使用 Docker（Windows PowerShell）
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: powershell

    cd C:\armino\bk_solution_ai\projects\beken_robot
    $env:SDK_DIR = "C:\armino\bk_avdk_smp"
    .\dbuild.ps1 make clean
    .\dbuild.ps1 make bk7259

3. 烧录固件
---------------------------------

编译完成后，烧录 bin 文件路径为（相对仓库根 ``bk_solution_ai/``）：

.. code::

    projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin

Armino 支持在 Windows / Linux 平台烧录固件。以 Windows 平台为例，可使用 BKFIL（``bk_loader``）通过 UART 烧录。

详细烧录流程请参考 `SMP 快速入门（BK7259） <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html>`_ 与 `SMP 主页 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html>`_。

固件烧录完成后的开发板上电、APP 下载、BLE 配网、按键操作与 AI 功能验证，请继续阅读 :doc:`../projects/beken_robot/index`。
