"""Guard audio NVS retries while SDR hardware is absent."""
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "main/ui.c").read_text()
outer = src.split("void ui_run(const touch_cal_t *cal)", 1)[1]
loop = outer.split("for (;;) {", 1)[1]
assert loop.lstrip().startswith("audio_settings_try_save(false);"), (
    "retry must run before the non-streaming recovery branch and its early continues"
)
recovery = loop.split("if (!sdr_streaming()) {", 1)[1].split("int64_t t0 =", 1)[0]
assert "while (!sdr_open(5000, NULL, 0)) {" in recovery
reopen = recovery.split("while (!sdr_open(5000, NULL, 0)) {", 1)[1]
assert reopen.lstrip().startswith("audio_settings_try_save(false);"), (
    "retry must continue during indefinite dongle reopen failures"
)
print("Audio persistence retries during stream-loss and indefinite dongle reopen")
