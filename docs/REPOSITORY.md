# Repository preparation

Source: the original `gruppo6` competition snapshot, preserved separately.
Repository: [EmanuelChirico/Pondulo](https://github.com/EmanuelChirico/Pondulo).

| Original location below firmware/ | New location |
|---|---|
| menu-funzionante/cursorSensor | sensor-node |
| menu-funzionante/RT-STM32G474RE-NUCLEO64-GFX01M2-DEMO7 | display-node |
| bilanciaFinale | scale-node |
| RT-STM32G474RE-NUCLEO64-ESP8266menuonline | price-bridge |
| ESP project's Listino_Salumeria | esp8266/Listino_Salumeria |

The SVN working-copy database, pristine store, local IDE settings, timestamped
backups and prebuilt firmware were omitted. They remain in the original folder.
Old hardware and price-bench documentation is preserved in `docs/legacy/`;
original PDFs are under `docs/reference/`.

No SVN history was converted into Git history. The organized snapshot is imported on top of the repository's existing initial commit; its GitHub remote and history are retained. No calibration or hardware settings were
changed. ChibiOS and uGFX paths now point to ignored `external/` directories and
can be overridden from Make's command line.

Before describing a public release as fully reproducible, recover the missing
SDK revisions, check embedded builds and perform the hardware acceptance run.
No new project-wide licence was assigned to team or third-party code.
