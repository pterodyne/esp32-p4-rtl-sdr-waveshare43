#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "hackrf_gain_store.h"
static int32_t stored, pending;
static int fail_set,fail_commit,fail_get;
esp_err_t nvs_set_i32(nvs_handle_t h,const char*k,int32_t v){(void)h;assert(!strcmp(k,"hrgain1"));if(fail_set)return ESP_FAIL;pending=v;return ESP_OK;}
esp_err_t nvs_get_i32(nvs_handle_t h,const char*k,int32_t*v){(void)h;assert(!strcmp(k,"hrgain1"));if(fail_get)return ESP_FAIL;*v=stored;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;if(fail_commit)return ESP_FAIL;stored=pending;return ESP_OK;}
int main(void){
 int l=40,v=0;
 assert(hackrf_gain_save(7,24,30)==ESP_OK);
 assert(hackrf_gain_load(7,&l,&v) && l==24 && v==30);
 fail_set=1;assert(hackrf_gain_save(7,40,62)!=ESP_OK);fail_set=0;
 fail_commit=1;assert(hackrf_gain_save(7,40,62)!=ESP_OK);fail_commit=0;
 assert(hackrf_gain_load(7,&l,&v) && l==24 && v==30);
 assert(hackrf_gain_save(7,7,30)!=ESP_OK);
 stored=0x01000103;assert(!hackrf_gain_load(7,&l,&v));
 fail_get=1;assert(!hackrf_gain_load(7,&l,&v));
 puts("HackRF grouped independent gain persistence/ranges/failures PASS");
}
