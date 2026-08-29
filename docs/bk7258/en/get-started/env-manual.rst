.. _env_manual:

Local Deployment
===================

:link_to_translation:`zh_CN:[中文]`

Setup Build Environment on Linux
--------------------------------------------------------------------

.. note::

      The Armino compilation environment on Linux requires Ubuntu 20.04 LTS version and above, CentOS 7 version or above, ArchLinux, and Debian 11 version or above. This chapter will take Ubuntu 20.04 LTS version as an example to introduce the construction of the entire compilation environment.


The compilation environment can be installed using an environment installation script or manual installation. It is recommended to use the installation script. If the installation script is not available in the SDK directory, you can download it from the BEKEN server.


- Script installation:

    If there is an installation script in the SDK, enter the SDK directory and execute the installation script. Follow the prompts to complete the build environment installation::

        cd ~/armino/bk_avdk_smp
        sudo bash tools/env_tools/setup/armino_env_setup.sh

    If the installation script does not exist in the SDK, it can be downloaded and executed from the `link <https://dl.bekencorp.com/d/tools/arminosdk/autoinstall_script/armino_env_setup.sh?sign=Xy2UU1MypajrqyIwyGPC_uW_6e4yqfKhJBuk14gtjx8=:0>`_ .

- Manual installation:

    Please refer to `SMP documentation: Manual installation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/manual_install.html>`_ for manual installation.


.. note::

    The default toolchain path for the AP is defined in ``ap/middleware/soc/bk7258_ap/soc_config.mk``, and the default toolchain path for the CP is defined in ``cp/middleware/soc/bk7258/soc_config.mk``. You can also change the toolchain path in the project file ``pj_config.mk``::

        COMPILER_TOOLCHAIN_PATH := /opt/gcc-arm-none-eabi-10.3-2021.10/bin


Setup Build Environment on Windows
--------------------------------------------------------------------

Armino requires a Windows 7 or higher 64 bit operating system in the Windows environment, and the integrated compilation environment for Windows is Armino Bash.
Click the `link <https://dl.bekencorp.com/d/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.0.1.exe?sign=kV09ojlMImuMMFs2ctN2xyByJKWm1FQSizzA8Q67OXc=:0>`__ to download the installation package.
The installation path cannot contain Chinese characters or spaces. After installation, generate a shortcut icon for Armino Bash on the desktop. Double click the shortcut icon to enter the Armino Bash terminal.
The Armino Bash terminal is based on git bash and supports commonly used Linux commands and shell script execution. The path style is consistent with git bash,
such as ` C:\\Programs` is represented as `/c/Programs` in Armino bash.

.. note::

    If you need to use a Windows style path, you can include it in single quotes, for example:

        cd 'C:\\Programs'

.. note::

    The installation path and SDK code path of Windows tools cannot contain Chinese characters and spaces. The Windows system currently does not support document compilation. Pulling code from Armino Bash in Win7 system will result in errors. You can use the installation version of Git to pull the code after installing Git.


Build The Project
------------------------------------

Run following commands to build BK7258 app project::

    cd ~/armino/bk_avdk_smp
    make bk7258


This default project enables Wi-Fi, BLE, initializes common drivers, and starts the Armino default CLI.

For more compilation-related commands, please refer to `SMP documentation: Build overview <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/developer-guide/build_config/bk_build_overview.html>`_
