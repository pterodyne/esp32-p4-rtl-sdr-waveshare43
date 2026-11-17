"""Owner-task integration contracts; host parser tests exercise actual C."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
ui = (root / 'main/ui.c').read_text()
assert 'remote_diag_poll();' in ui, 'missing owner-task UART poll'
assert 's_remote_active ? s_remote_saved_hz : sdr_freq()' in ui, 'diagnostic tune can leak to NVS'
assert 'read(STDIN_FILENO, &ch, 1)' in ui, 'missing bounded console reader'
assert 'sdr_set_bias_tee(false)' in ui, 'diagnostic safety gate missing'
assert 'remote_diag.c' in (root / 'main/CMakeLists.txt').read_text()
print('remote owner/NVS/safety contracts OK')
