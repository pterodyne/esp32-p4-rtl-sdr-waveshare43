"""Metadata recording names require long filenames on the actual FAT volume."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def check(path):
    text = path.read_text().splitlines()
    assert 'CONFIG_FATFS_LFN_HEAP=y' in text, f'{path}: heap long filenames required'
    assert 'CONFIG_FATFS_LFN_NONE=y' not in text, f'{path}: 8.3-only names break recording'
    assert 'CONFIG_FATFS_MAX_LFN=255' in text, f'{path}: expected long-name capacity'

check(root / 'sdkconfig.defaults')
for rev in ('rev1', 'rev3'):
    config = root / f'build-waveshare43-{rev}/sdkconfig'
    if config.exists():
        check(config)
print('FAT metadata filename configuration: defaults and available revision builds passed')
