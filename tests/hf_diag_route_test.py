"""Guard the on-device capture path; host cannot exercise ESP USB/HCD or the panel."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
lib = (root / 'components/rtlsdr/blog/src/librtlsdr.c').read_text()
sdr = (root / 'main/sdr.c').read_text()
ui = (root / 'main/ui.c').read_text()
panel = (root / 'main/panel.c').read_text()
assert 'rtlsdr_get_r82xx_tune_diag(' in lib
assert 'dev->r82xx_p.diag_pll = -1;' in lib.split('int rtlsdr_set_center_freq(', 1)[1].split('uint32_t rtlsdr_get_center_freq(', 1)[0]
assert 'hf_diag_identity(&s_hf_diag' in sdr
assert 'rtlsdr_get_usb_strings(s_dev, manufacturer, product' in sdr
assert 'hf_diag_tune(&s_hf_diag' in sdr
assert 'int rc = rtlsdr_set_center_freq(s_dev, hz);' in sdr
assert 'sdr_hf_diag_snapshot()' in ui
assert 'hf_diag_state_t sdr_hf_diag_snapshot(void)' in sdr
assert 's_page == P_DIAG' in panel and 'hf_diag_line(' in panel
print('Diagnostic firmware route: USB identity -> tuner PLL/GPIO5 -> SDR return -> on-demand panel')
