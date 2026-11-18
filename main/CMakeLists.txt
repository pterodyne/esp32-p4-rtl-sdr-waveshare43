set(SDR_SOURCE "sdr.c")
if(CONFIG_SDR_BACKEND_HACKRF)
    set(SDR_SOURCE "sdr_hackrf.c" "hackrf_rx.c" "hackrf_stream.c" "hackrf_resample.c" "hackrf_dot96_p4.S")
    # Keep capture-rate hot loop optimized independently of the app profile.
    set_source_files_properties(hackrf_resample.c PROPERTIES COMPILE_OPTIONS "-O3")
endif()
idf_component_register(SRCS "battery_voltage.c" "main.c" ${SDR_SOURCE} "hf_diag.c" "remote_diag.c" "rf_policy.c" "spectrum.c" "view.c" "panel.c" "adsb.c" "adsb_view.c" "amap.c" "sdcard.c" "sdlog.c" "wav_audio.c" "sd_audio.c" "scan_view.c" "sensor_view.c" "sensor_pipeline.c" "weather_wh2.c" "tpms_steelmate.c" "radio_scan.c" "dsp.c" "demod.c" "rds.c" "audio.c" "ui.c" "theme_store.c" "audio_settings_store.c" "tune_step.c"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../components/rtlsdr/shim"
                       REQUIRES esp_adc rpi_display rtlsdr usb driver esp_timer nvs_flash fatfs sdmmc esp_driver_sdmmc esp_wifi esp_event bt esp_driver_i2c esp_driver_i2s)
