from pathlib import Path
s=Path('main/ui.c').read_text()
assert 'battery_voltage_start();' in s
assert 'if (!s_panel && battery_voltage_get() != s_battery_drawn)' in s
b=Path('main/battery_voltage.c').read_text()
assert 'adc_oneshot_get_calibrated_result' in b
assert 'pdMS_TO_TICKS(4700)' in b and 'i<16' in b
assert 'gpio_set_level' not in b
print('Battery startup, calibrated worker, slow sampling, changed-value UI refresh')
