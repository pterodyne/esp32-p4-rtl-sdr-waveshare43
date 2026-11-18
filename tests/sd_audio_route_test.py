from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'main/audio.c').read_text()
assert 'sd_audio_exchange(pcm, k)' in s, 'demodulated PCM must pass through asynchronous SD audio exchange'
u=(p/'main/ui.c').read_text()
assert 'VIEW_PRESETS' in u and 'panel_open_presets' in u
assert 'VIEW_PLAY' in u and 'panel_open_recordings' in u
assert 'sd_audio_snapshot' in u
assert 'sd_audio_output_exclusive()' in s
record = u.split('if (act == VIEW_RECORD)', 1)[1].split('if (act == VIEW_PRESETS', 1)[0]
assert 'record_requested) sd_audio_request(SD_AUDIO_STOP' in record
assert 'wav_record_name(name,sizeof name,sdr_freq(),s_ctl.bw,(int64_t)time(NULL),0)' in record
assert 'sd_audio_request(SD_AUDIO_RECORD,name)' in record
assert 'sd_audio_record_error("REC unavailable: audio codec init failed")' in record
assert 'view_set_record_error(s_ctl.sd_audio.record_error)' in record
assert 'view_set_record_error(snapshot.record_error)' in u
assert 'if(!s_ctl.sd_audio.record_error) sd_audio_request(SD_AUDIO_REFRESH, NULL)' in u, 'INFO must retain the recording error until explicit REFRESH'
v=(p/'main/view.c').read_text()
assert 's_record_error ? "INFO" : "PLAY"' in v and 's_record_error ? "ERR" : "REC"' in v
assert 's_ctl.audio_delete_name' in u and 'SD_AUDIO_DELETE' in u
assert 'panel_show' not in record and 'panel_open' not in record, 'REC must stay on receiver'
assert 'sd_audio_playback_page(true);' in u
assert 'if (was_recordings && !panel_recordings_visible()) sd_audio_playback_page(false);' in u
show = u.split('static void panel_show(', 1)[1].split('static void set_look(', 1)[0]
assert 'sd_audio_playback_page(false);' in show
routing = (p/'main/sd_audio.c').read_text().split('void sd_audio_playback_page(', 1)[1].split('static size_t transfer(', 1)[0]
assert 'command_epoch++' in routing and 'stop_requested=true' in routing
assert 'audio_set_mute' not in routing and 'set_out_mute' not in routing and 'set_volume' not in routing
assert 'static sd_audio_snapshot_t snapshot;' in u, 'large filename-list polling buffer must not consume UI task stack'
assert 'view_header_repeats(act)' in u
print('Main REC stays on receiver; page ownership covers header/menu/BACK, epoch cancellation, codec mute/volume untouched')
print('SD audio and shortcut routes present')

card=(p/"main/sdcard.c").read_text()
assert "taskENTER_CRITICAL(&s_init_mux)" in card, "SD mount mutex lazy initialization must be safe across UI and SD worker"

assert "l2[80]" in (p/"main/panel.c").read_text(), "SD status errors must not be truncated at 31 characters"
