"""Source-path guards for UI code that requires the ESP-IDF runtime.

Behavior/state-machine tests live in the C host tests; these guards ensure the
firmware actually routes through them, including long foreground/recovery loops.
Use --baseline to demonstrate missing integration in the unmodified HEAD.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
src = (subprocess.check_output(["git", "show", "HEAD:main/ui.c"], cwd=root).decode()
       if "--baseline" in sys.argv else (root / "main/ui.c").read_text())
panel = src.split("static void panel_event(", 1)[1].split("static void poll_touch(", 1)[0]
assert "!(ch & CTL_BAND)" in panel, "preset close must not bypass tuning"
assert panel.index("apply_audio();") < panel.rindex("if (ch & CTL_CLOSE) panel_show(false)"), "close after applying preset"
touch = src.split("static void poll_touch(", 1)[1].split("/* -------------------------------------------------------------------------- ADS-B", 1)[0]
assert touch.index("screen_idle_update(") < touch.index("touch_cal_map("), "wake tap must be consumed before routing"
assert "screen_action & SCREEN_CONSUME" in touch
assert "(!s_panel && view_button_at(x, y) >= 0)" in touch, "bottom tuning routes before footer hit suppression"
sensors = src.split("static void sensor_mode(", 1)[1].split("static void scan_mode(", 1)[0]
change = sensors.split("if (s_sensor_band_action)", 1)[1].split("int64_t t0", 1)[0]
for operation in ("sdr_set_freq(sensor_view_band_hz(band))", "pos = sdr_total_bytes()",
                  "sensor_pipeline_reset(", "memset(&info, 0", "memset(seen_us, 0", "dropped = 0;"):
    assert operation in change, operation
assert change.index("sdr_set_freq(") < change.index("pos = sdr_total_bytes()")
assert "sdr_set_freq(previous_freq)" in sensors
assert "audio_pause(false)" in sensors
recovery = src.split("while (!sdr_open(5000, NULL, 0))", 1)[1].split("restarts = 0", 1)[0]
assert "poll_touch(cal)" in recovery
assert 'nvs_int("screen_sec", 0)' in src
assert "screen_settings_try_save();" in touch
print("UI contracts: apply-before-close, bottom tuning, band reset/restore, wake consumption, NVS and recovery polling")
