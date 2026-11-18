from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=(root/'main/main.c').read_text()
p=(root/'main/panel.c').read_text()
v=(root/'main/view.c').read_text()
assert 'HackRF One RX-only receiver' in m
assert 'HackRF: counter test unavailable' in m
assert 'c->gain_label(c->gain_idx) : "AGC"' not in p
assert 'manual LNA / VGA; amp OFF' in p
assert '3.3V ACTIVE LNA ONLY' in p
assert 'NO DIRECT ADC' in p
assert 'Spectrum  HackRF RX' in v
u=(root/'main/ui.c').read_text()
assert 'if (act >= VIEW_LNA_DN && act <= VIEW_VGA_UP)' in u
assert 'sdr_set_stage_gain(lna,db)' in u
assert 'view_set_stage_gains(sdr_stage_gain(true)' in u
assert 'hackrf_gain_save' in u and 'hackrf_gain_load' in m
assert '#ifndef CONFIG_SDR_BACKEND_HACKRF\n    nvs_set_i32(s_nvs, "gaint"' in u
print('HackRF UI capability contracts PASS')
