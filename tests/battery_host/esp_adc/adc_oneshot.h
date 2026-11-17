#pragma once
#include <stddef.h>
typedef void *adc_oneshot_unit_handle_t;
typedef void *adc_cali_handle_t;
typedef struct {int unit_id;} adc_oneshot_unit_init_cfg_t;
typedef struct {int atten,bitwidth;} adc_oneshot_chan_cfg_t;
typedef struct {int unit_id,chan,atten,bitwidth;} adc_cali_curve_fitting_config_t;
#define ADC_UNIT_1 1
#define ADC_CHANNEL_4 4
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_DEFAULT 0
#define ESP_OK 0
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*,void**);
int adc_oneshot_config_channel(void*,int,const adc_oneshot_chan_cfg_t*);
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*,void**);
int adc_oneshot_get_calibrated_result(void*,void*,int,int*);
void adc_cali_delete_scheme_curve_fitting(void*);
void adc_oneshot_del_unit(void*);
