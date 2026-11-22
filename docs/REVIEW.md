# Source review and remaining work

This preparation reorganizes and documents the competition snapshot. It does not
redesign the firmware or claim a new hardware-validated release.

## Findings that affect future development

- **ADC access from two threads:** the UI tick and `LinkDiagThread` both call
  `weight_adc_get_grams()`. That function shares an ADC driver, sample buffer and
  EMA state without a module mutex. The diagnostic path should read a cached
  result, or all conversions and filter state should have a single owner. This
  requires an embedded build and concurrency/hardware validation before release.
- **Historical comments:** display `main.c` and UI headers still describe dormant
  flows and absent sensor gestures even though the implementation has evolved.
  Use `ARCHITECTURE.md` for the current module map; preserve old comments as
  development context until a firmware cleanup can be hardware-tested.
- **Missing references:** original notes refer to `REPORT.md`, `specs/spec.md`,
  `PLAN.md` and `FILESYSTEM.md` which were absent from the supplied folder.
  No replacement chronology or specification has been invented.
- **SD and old panels:** `storage/` and `ui/` are not in the display build.
  Their presence does not imply a complete storage feature or active second UI.
- **Price integration:** no price receiver runs in the display build. Before
  integrating it, reconcile 32-bit incoming prices with the UI's 16-bit prices,
  preserve existing cart rows, define synchronization and update display state
  from its owning thread.
- **Prototype transport limits:** the analog weight link has no disconnect
  detection; the price stream has no ACK/CRC. These are documented design limits,
  not features added during repository preparation.
- **Dependencies:** exact SDK revisions and Arduino package versions are missing.
  Recover the original environment before claiming reproducible embedded builds.

## Scope of changes

Node directories were renamed and external paths made configurable. Application
logic, calibration values and hardware configuration were retained. Test-only
corrections reflect the current UI. Generated images, old backups and IDE metadata
were omitted from the Git repository; the original folder remains intact.
