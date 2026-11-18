#pragma once
#include <stdbool.h>
#include "nvs.h"
/* Versioned atomic pair: never reuse or migrate RTL's tuner/AGC gaint key. */
static inline bool hackrf_gain_valid(int l,int v) {
    return l>=0 && l<=40 && !(l%8) && v>=0 && v<=62 && !(v%2);
}
static inline esp_err_t hackrf_gain_save(nvs_handle_t h,int l,int v) {
    if(!hackrf_gain_valid(l,v)) return ESP_FAIL;
    esp_err_t e=nvs_set_i32(h,"hrgain1",0x01000000|(l<<8)|v);
    return e==ESP_OK ? nvs_commit(h) : e;
}
static inline bool hackrf_gain_load(nvs_handle_t h,int *l,int *v) {
    int32_t n;
    if(nvs_get_i32(h,"hrgain1",&n)!=ESP_OK || (n&0xffff0000)!=0x01000000) return false;
    int a=(n>>8)&255,b=n&255;
    if(!hackrf_gain_valid(a,b)) return false;
    *l=a;*v=b;return true;
}
