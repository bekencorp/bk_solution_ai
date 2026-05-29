.. _env_manual:

本地部署
===================

:link_to_translation:`en:[English]`


Linux编译环境
------------------------------------

.. note::

    Armino 在Linux下编译环境要求Ubuntu 20.04 LTS 版本及以上，Centos 7版本及以上，Archlinux或者Debian 11版本及以上，本章节将以 Ubuntu 20.04 LTS 版本为例，介绍整个编译环境的搭建。

编译环境可以使用环境安装脚本进行安装，也可以手动安装，这里推荐使用使用安装脚本方式。如果在sdk目录中没有安装脚本，可以从beken服务器上下载安装脚本。


- 脚本安装：

    如果sdk中存在安装脚本，那么进入sdk目录后执行安装脚本，按照安装脚本引导即可完成构建环境安装::

        cd ~/armino/bk_avdk_smp
        sudo bash tools/env_tools/setup/armino_env_setup.sh

    如果sdk中不存在安装脚本，可以从 `链接 <https://dl.bekencorp.com/d/tools/arminosdk/autoinstall_script/armino_env_setup.sh?sign=Xy2UU1MypajrqyIwyGPC_uW_6e4yqfKhJBuk14gtjx8=:0>`_  处下载并执行。

- 手动安装：

    手动安装请参考 `SMP 文档：手动安装 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/manual_install.html>`_


.. note::

    AP的工具链默认路径在 ap/middleware/soc/bk7259_ap/soc_config.mk 文件中定义，CP的工具链默认路径在 cp/middleware/soc/bk7259/soc_config.mk 文件中定义，您也可以在项目文件pj_config.mk中更改工具链路径::

        COMPILER_TOOLCHAIN_PATH := /opt/gcc-arm-none-eabi-10.3-2021.10/bin


Windows编译环境
------------------------------------

Armino 在Windows下环境要求Windows7及以上64位操作系统，Windows下集成编译环境为Armino Bash，点击 `链接 <https://dl.bekencorp.com/d/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.0.1.exe?sign=kV09ojlMImuMMFs2ctN2xyByJKWm1FQSizzA8Q67OXc=:0>`__ 下载安装包。
安装路径不能带有中文和空格，安装完成后在桌面生成Armino Bash的快捷图标，双击快捷图标即可进入Armino Bash终端。
Armino Bash终端中基于git bash制作，支持常用的linux的命令和执行shell脚本，路径风格和git bash一致，如 `C:\\Programs` 在armino bash中表示为 `/c/Programs` 。

.. note::
    如果需要使用windows风格的路径，可以使用单引号包含，比如：

        cd 'C:\\Programs'

.. note::

    Windows工具安装路径和SDK代码路径不能带有中文和空格，Windows系统暂不支持文档编译，Win7 系统中在Armino Bash中拉取代码会报错，可以通过在安装git后，使用安装版git拉取代码。



编译工程
------------------------------------

在Linux终端中输入下述命令编译 Armino 默认工程，PROJECT为可选参数，默认为app，默认工程为启动 WiFi，BLE，初始化常见驱动，并启动 Armino 默认 Cli 程序::

    cd ~/armino/bk_avdk_smp
    make bk7259

更多编译相关命令请参考 `SMP 文档：编译概述 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/developer-guide/build_config/bk_build_overview.html>`_
