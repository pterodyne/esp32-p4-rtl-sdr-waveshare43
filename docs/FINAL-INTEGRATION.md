# Final receiver integration

The owner reports the final receiver firmware works well and authorizes integration into `waveshare-43`. This is broad user-reported hardware evidence, not a measurement of ADC accuracy or proof of reception on every band. The owner's V4 has no verified HF reception outside the P4 either; no root cause is proven. HackRF/RSP1 backends and TX are not included.

## Review correction

Independent review identified that metadata WAV basenames exceed FAT 8.3 limits while generated firmware configurations selected `CONFIG_FATFS_LFN_NONE=y`. Linux filesystem tests could not detect this target configuration defect. Checked-in defaults now select heap-backed long filenames and a 255-character limit. Both silicon configurations were regenerated from defaults and rebuilt. `tests/fatfs_config_test.py` fails on the old configuration and verifies defaults plus available generated configurations; it is included in the SD test suite. Physical SD recording with this rebuilt image still needs confirmation; no board was flashed during integration.

## Verification

- Controls, RF and SD-audio host suites passed after the correction.
- Remote diagnostic parser and route tests passed during integration.
- Rev1 and rev3 ESP-IDF builds passed. Every merged-image constituent matched its declared flash offset byte-for-byte.
- No GitHub CI checks are configured on this PR; local tests are not described as a CI run.
- Only the owner's origin was fetched/pushed. Other dirty worktrees were preserved.

Final corrected full images (flash at `0x0`; can reset settings):

| Silicon | Bytes | SHA-256 |
|---|---:|---|
| rev1 | 1098976 | 54f1f98eafc4368ce0cc957605dc856800001325352831b92f48363219f238df |
| rev3 | 1101744 | 34871051bb0db0937d2394e115d40bfaa49ce4d0bc9933afad004dc79886aa59 |

Earlier implementation reports retain historical hashes and evidence; these corrected images supersede those binaries for this integration.
