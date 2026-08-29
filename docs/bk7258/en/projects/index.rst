Reference Projects
=====================================

:link_to_translation:`zh_CN:[中文]`

This document contains reference project descriptions for the Armino AI Solution.

Choosing a project
---------------------------------

- **``beken_genie``**: **Agora RTC** and cloud LLM flows; most quick-start examples use this project.
- **``volc_rtc``**: **VolcEngine RTC / AI Agent**; use ``projects/volc_rtc`` in build commands; output under ``build/bk7258/volc_rtc/package``.
- **``volc_rtc_ab``**: Same functionality as ``volc_rtc``, but adds **OTA A/B (dual-partition) upgrade** support; the difference is the partition layout and firmware upgrade flow—see that chapter.

Pick the RTC / model provider based on your accounts, region, and compliance. Review each vendor's terms and regional availability before integration.

.. toctree::
    :maxdepth: 1

    Beken Genie AI Project <beken_genie/index>
    Beken Genie VolcEngine RTC Project <volc_rtc/index>
    Beken Genie VolcEngine RTC OTA A/B Project <volc_rtc_ab/index>

