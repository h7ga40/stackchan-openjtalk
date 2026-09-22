# Third-party source notices

## OpenJTalk working source

The embedded sources under `vendor/` were imported from:

- Repository: `https://github.com/h7ga40/OpenJTalk.git`
- Commit: `f37fa1930804aed4dff8bcb93c2cfb2ea8b92ccc`
- Imported components: `hts_engine_raw_API-1.10`, `open_jtalk-1.11`, and the frontend API sources from `OpenJTalk/src`

The untracked `OpenJTalk/user_dict.csv` in the source working tree was not imported.

Open JTalk and HTS Engine are redistributed under their modified BSD licenses. MeCab is available under GPL, LGPL, or BSD terms; this repository selects its BSD option. Source files retain their original copyright and license headers. Standalone copies of the selected terms are preserved under `LICENSES/`.

- Open JTalk: `https://open-jtalk.sourceforge.net/`
- HTS Engine API: `https://hts-engine.sourceforge.net/`
- MeCab licensing: `https://github.com/taku910/mecab/blob/master/mecab/COPYING`

## Open JTalk dictionary

The runtime dictionary is kept outside Git under `assets/cache/open_jtalk_dic_utf_8-1.11` and is not distributed by this repository. Users obtain the official UTF-8 binary package from the Open JTalk 1.11 website. Its `COPYING` file must be retained alongside the cached dictionary. The dictionary is copied only to the SD card by `tools/prepare_sd.ps1`.

## Mei voice

`assets/mei_normal_16.raw` is the converted runtime voice used by the firmware. The accompanying upstream notices are preserved as `assets/MEI_VOICE_README.txt` and `assets/MEI_VOICE_COPYRIGHT.txt`. The voice is distributed under Creative Commons Attribution 3.0; retain the attribution and license notice when redistributing the converted voice.

## StackChan-BSP

`libraries/StackChan-BSP` is based on M5Stack StackChan-BSP 1.1.0, commit `f7ed40e6f5d9a1d08440cb926f3a0865b81882f8`, with the local M5Unified 0.2.21 IO-expander compatibility changes used by the verified K151-R build.

The upstream MIT license is retained as `libraries/StackChan-BSP/LICENSE`. MIT notices for the bundled FTServo Arduino driver and UI toolkit are retained in their respective source directories.
