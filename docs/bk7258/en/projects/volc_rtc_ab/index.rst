Beken Genie VolcEngine RTC OTA A/B Version
==========================================

:link_to_translation:`zh_CN:[中文]`

**1. Introduction**
--------------------

    * This project adds **OTA A/B dual-partition upgrade** capability on top of the :doc:`../volc_rtc/index` project.
    * Real-time audio/video communication, audio processing (AEC/NS), AI Agent integration, room management, dual-screen display, peripheral reference designs, and other features are identical to ``volc_rtc``.
    * This document **focuses only on the OTA A/B partition differences**; for all other features, please refer directly to the VolcEngine RTC project documentation: :doc:`../volc_rtc/index`.

.. note::

    In short: **``volc_rtc_ab`` = all features of ``volc_rtc`` + OTA A/B partition upgrade**.

**2. OTA A/B Upgrade Introduction**
------------------------------------

**2.1 What is OTA A/B**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The basic idea of OTA A/B (dual-partition) upgrade: Flash keeps two firmware partitions; the device runs from the current partition, and during OTA the new firmware is written into the **other (standby) partition**, after which the boot partition is switched to complete the upgrade. Compared with a single firmware partition scheme, one firmware copy is always kept during the upgrade, at the cost of more Flash space.

.. note::

    The exact behavior of boot-partition switching, verification, and rollback is determined by the bootloader and the SMP SDK OTA mechanism—please refer to the `SMP OTA documentation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_. The difference between this project and ``volc_rtc`` is limited to the partition table and packaging configuration described below.

**2.2 Partition Layout Differences**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The values and read/write attributes below are taken directly from the two projects' partition table files ``partitions/bk7258/auto_partitions.csv``:

.. list-table::
    :header-rows: 1
    :widths: 20 20 20 40

    * - Partition
      - volc_rtc (single)
      - volc_rtc_ab (A/B)
      - Description
    * - primary_bootloader
      - 68k
      - 68k
      - Bootloader
    * - primary_cp_app
      - 1428k (Write=FALSE)
      - 1360k (Write=TRUE)
      - CP-core firmware partition; changed from read-only to writable
    * - primary_ap_app
      - 2992k (Write=FALSE)
      - 2652k (Write=TRUE)
      - AP-core firmware partition; changed from read-only to writable
    * - ota
      - 3128K
      - none
      - OTA partition of the single-partition scheme
    * - s_app
      - none
      - 4012k
      - Added by the A/B project; annotated as "B partition" (standby)
    * - ota_fina_executive
      - none
      - 4K
      - Added by the A/B project; see the SMP OTA documentation for details
    * - usr_config
      - 60K
      - 56K
      - User configuration partition

Key points (all verifiable in the partition table): ``s_app`` is a partition added by the A/B project, explicitly annotated as "B partition"; the write attribute of ``primary_cp_app`` / ``primary_ap_app`` is changed from ``FALSE`` to ``TRUE``.

**2.3 A/B Position-Independent Upgrade**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The project adds ``partitions/bk7258/ab_position_independent.csv``::

    pos_independent,TRUE

When ``pos_independent`` is ``TRUE``, the A/B **position-independent upgrade** feature is enabled; set it to ``False`` if the feature is not needed (see the comment inside the file). Please refer to the SMP OTA documentation for the concrete implementation and limitations of this feature.

**2.4 OTA Upgrade Package (RBL) Configuration**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The project adds ``partitions/bk7258/ota_rbl.config``, used to generate the A/B upgrade package ``app_packab.rbl``::

    {
        "key":"0123456789ABCDEF0123456789ABCDEH",
        "iv":"0123456789ABCDEF",
        "gzip":"0",
        "aes":"0",
        "outf":"app_packab.rbl"
    }

Field meanings (refer to the SMP rbl packaging tool documentation):

    * ``key`` / ``iv``: the key and initialization vector used for upgrade-package encryption/decryption;
    * ``aes``: whether the upgrade package is AES-encrypted (``0`` / ``1``);
    * ``gzip``: whether the upgrade package is compressed (``0`` / ``1``);
    * ``outf``: output upgrade package file name (here ``app_packab.rbl``).

.. warning::

    It is recommended to replace the example ``key`` / ``iv`` with your project's own secure keys before mass production.

**3. Project Usage Introduction**
-----------------------------------

**3.1 Code Download, Compilation, and Flashing**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * For the compilation flow, `please refer to the Quick Start chapter <../get-started/index.html>`_. Replace the project path in the commands with ``projects/volc_rtc_ab``.
    * Full firmware (initial full flash; path naming follows ``volc_rtc``): ``projects/volc_rtc_ab/build/bk7258/volc_rtc_ab/package/all-app.bin``
    * A/B OTA upgrade package: the file name is defined by ``outf`` in ``ota_rbl.config``, i.e. ``app_packab.rbl``.

**3.2 Upgrade Flow**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. Flash ``all-app.bin`` with the flashing tool for the first time;
    2. For subsequent versions, push the A/B upgrade package ``app_packab.rbl`` via the OTA channel to complete the upgrade.

    For the concrete OTA flow and interfaces (download, write, verification, and partition switching), please refer to the `SMP OTA documentation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_.

**3.3 Other Features (Provisioning, AI Dialog, Keys, LED Effects, etc.)**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Network provisioning, AI conversation, keys, LED effects, prompt tones, main configuration, and VolcEngine-related function development are identical to ``volc_rtc``. Please refer to :doc:`../volc_rtc/index`.
