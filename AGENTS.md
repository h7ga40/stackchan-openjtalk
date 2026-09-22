# Repository instructions

These instructions apply to the entire repository.

## Project constraints

- Keep this repository self-contained. Do not add dependencies on an external Open JTalk working tree or on user-specific absolute paths.
- Continue using the bundled memory-reduced HTS Engine, Open JTalk/MeCab frontend, and patched StackChan-BSP unless the task explicitly requires replacing them.
- Do not commit `.arduino/`, `assets/cache/`, `build/`, or `out/`.
- Preserve third-party copyright, license, and attribution files. New redistributed assets or source code must be recorded in `THIRD_PARTY_NOTICES.md`.

## PowerShell and text encoding

- Run the project scripts from the repository root with Windows PowerShell 5.1 or PowerShell 7.
- Keep repository documentation and source files in UTF-8. Do not convert Japanese text to Shift_JIS or another locale-dependent encoding.
- Preserve UTF-8 for the device serial protocol. In particular, do not replace the explicit UTF-8 encoding in `tools/speak_openjtalk_device.ps1` with a system-default encoding.
- Windows PowerShell 5.1 does not reliably interpret a BOM-less UTF-8 `.ps1` file containing non-ASCII literals. If Japanese or other non-ASCII text is added directly to a PowerShell script, save that `.ps1` file as UTF-8 with BOM. ASCII-only `.ps1` files may remain BOM-less.
- When a script writes or reads a text file and the BOM matters, specify the encoding explicitly instead of relying on the PowerShell version's default.

## Standard commands

```powershell
.\tools\setup.ps1
.\tools\build.ps1 -Clean
```

- Use `tools/Invoke-ArduinoCli.ps1` so the isolated Arduino directories and M5Stack board-manager URL are applied consistently.
- Discover the serial port with `.\tools\Invoke-ArduinoCli.ps1 board list`; do not assume `COM3` on another machine.
- Treat SD-card drive letters such as `E:\` as examples, not fixed configuration.

## Verification

- Run `git diff --check` for documentation-only changes.
- After changing firmware, bundled libraries, or build scripts, run a clean build unless the required toolchain is unavailable.
- Hardware checks require a connected K151-R/CoreS3 and a prepared microSD card. Do not report a hardware result as verified unless the test was actually run; otherwise state what remains unverified.
