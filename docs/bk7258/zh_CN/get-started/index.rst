快速开始
=================================

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


2. 编译项目
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

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

在编译完成后，在AI 解决方案代码的/build/bk7258/beken_genie/package目录下将生成all-app.bin，使用烧录工具烧录到开发板即可。

3.1 **资源文件烧录**

    - 1、Armino 支持在 Windows/Linux 平台进行固件烧录, 烧录方法参考烧录工具中指导文档。以Windows 平台为例， Armino 目前支持 UART 烧录。

      具体 `烧录流程 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_ 请参考 `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_


4. APP注册和下载
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP下载：https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html

    注册登录：使用邮箱注册登录


