#include "battery_voltage.h"
#include <stdatomic.h>
#include "sdkconfig.h"
static atomic_int voltage = ATOMIC_VAR_INIT(-1);
int battery_voltage_get(void) { return atomic_load(&voltage); }
#ifdef CONFIG_SDR_WAVESHARE_43
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static void measure(void *arg) {
    adc_oneshot_unit_handle_t adc = NULL;
    adc_cali_handle_t cal = NULL;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id=ADC_UNIT_1};
    adc_oneshot_chan_cfg_t channel = {.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    adc_cali_curve_fitting_config_t calibration = {.unit_id=ADC_UNIT_1,.chan=ADC_CHANNEL_4,
        .atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if (adc_oneshot_new_unit(&unit,&adc)!=ESP_OK) goto done;
    if (adc_oneshot_config_channel(adc,ADC_CHANNEL_4,&channel)!=ESP_OK) goto done;
    if (adc_cali_create_scheme_curve_fitting(&calibration,&cal)!=ESP_OK) goto done;
    for (;;) {
        int total=0; bool valid=true;
        for (int i=0;i<16;i++) {
            int mv=0;
            if (adc_oneshot_get_calibrated_result(adc,cal,ADC_CHANNEL_4,&mv)!=ESP_OK ||
                battery_from_adc_mv(mv,true)<0) valid=false;
            else total+=mv;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        atomic_store(&voltage,battery_from_adc_mv((total+8)/16,valid));
        vTaskDelay(pdMS_TO_TICKS(4700));
    }
done:
    if(cal) adc_cali_delete_scheme_curve_fitting(cal);
    if(adc) adc_oneshot_del_unit(adc);
    vTaskDelete(NULL);
}
void battery_voltage_start(void) {
    static bool started;
    if(!started) started=xTaskCreate(measure,"battery",3072,NULL,1,NULL)==pdPASS;
}
#else
void battery_voltage_start(void) {}
#endif
