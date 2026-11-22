# External dependencies

Place the original competition-compatible libraries here:

```text
external/chibios2111/   ChibiOS 21.11 tree (must include os/ and tools/)
external/ugfx/          uGFX tree (must include gfx.mk and ILI9341 driver)
```

These trees were not included in the supplied project. Obtain the original
ChibiStudio/campus distribution where possible; exact revision hashes and local
patches are unknown. A similarly named upstream version is not evidence of binary
compatibility. The libraries are ignored by Git. Their own licences apply.

All node Makefiles accept `CHIBIOS=/absolute/path`; the display also accepts
`GFXLIB=/absolute/path`. Relative paths are resolved from the node's directory.
