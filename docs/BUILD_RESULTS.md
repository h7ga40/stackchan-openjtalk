# Build results

## 2026-09-22 self-contained source verification

The K151-R firmware was clean-built from the sources under this repository without supplying an external OpenJTalk source path.

```text
Sketch uses 1415362 bytes (44%) of program storage space.
Global variables use 35788 bytes (10%) of dynamic memory,
leaving 291892 bytes for local variables.
```

The verification reused the already installed M5Stack ESP32 3.2.5 Arduino toolchain cache by setting `STACKCHAN_ARDUINO_STATE`. OpenJTalk, MeCab, HTS Engine, the sketch, and the project-owned Arduino libraries all came from this repository.

The SD preparation script was also verified with the repository-local voice, labels, and ignored dictionary cache. SHA-256 values were identical before and after preparation:

```text
A66439AE9AC1C61EB41568D0EDC85DBDD9D7F9CF26F2F2C239E1B7BE9F7D3F6D  mei_normal_16.raw
4B33FAA4934B04945A5A9EF585FE51CDE8794895D68C9CA1989E226490A5AC91  openjtalk_real_labels.txt
CA57D9029691A70A5DFB99AFC2844180256161D7130DA65B1A867510E129B9A6  sys.dic
```

This migration verification did not upload new firmware to the physical K151-R. The original verified firmware remains available in `stackchan-standard` until a build from this repository has been uploaded and exercised on the device.
