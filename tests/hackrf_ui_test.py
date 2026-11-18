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
print('HackRF UI capability contracts PASS')
