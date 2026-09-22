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

## 2026-09-22 K151-R device verification

The firmware built from this repository was uploaded with verification to an M5Stack CoreS3 StackChan K151-R on `COM3`.

The on-boot SD test completed successfully:

```text
FRONTEND=READY dic=/sd/openjtalk/dic psram_free=2041500
RESULT=PASS source=SD rate=16000 frame_period=80 frames=619 samples=49520
elapsed_ms=4511 peak_hts_bytes=424780 psram_free=2035844
```

On-device UTF-8 analysis and speech were then tested with the sentence `新しいフォルダーから、オープンジェートークが動いています。` at a pitch shift of three semitones:

```text
LABELS source=DEVICE count=48 elapsed_ms=1370 psram_used=129028
RESULT=PASS source=DEVICE rate=16000 frame_period=80 frames=827 samples=66160
elapsed_ms=6125 peak_hts_bytes=576620 psram_free=1912472
```

Both the SD-label path and the M5Stack-side label-generation path passed.
