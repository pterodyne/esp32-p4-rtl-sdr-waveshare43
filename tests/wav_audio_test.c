#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "wav_audio.h"
int main(void) {
    FILE *f = tmpfile(); assert(f);
    wav_writer_t w;
    assert(wav_begin(&w, f));
    int16_t pcm[480] = {1,-2,32767,-32768};
    assert(wav_append(&w, pcm, 480));
    assert(wav_finish(&w));
    assert(fseek(f,0,SEEK_END)==0); assert(ftell(f) == 1004);
    rewind(f);
    wav_reader_t r; assert(wav_open(&r,f));
    assert(r.remaining == 960);
    int16_t out[480]; assert(wav_read(&r,out,480)==480);
    assert(!memcmp(pcm,out,sizeof pcm)); assert(wav_read(&r,out,480)==0);
    rewind(f); assert(wav_begin(&w,f)); assert(wav_finish(&w));
    /* Existing trailing payload cannot masquerade as a valid zero-length WAV. */
    assert(!wav_open(&r,f));
    fclose(f);
    f=tmpfile(); assert(f); assert(wav_begin(&w,f)); assert(wav_finish(&w));
    assert(wav_open(&r,f) && r.remaining==0);
    assert(fseek(f,24,SEEK_SET)==0); unsigned char wrong_rate[4]={0x44,0xac,0,0};
    assert(fwrite(wrong_rate,1,4,f)==4); assert(!wav_open(&r,f));
    fclose(f);
    f=tmpfile(); assert(f); assert(wav_begin(&w,f));
    /* 32-bit target fseek/ftell must represent header + complete payload. */
    assert((uint64_t)WAV_AUDIO_MAX_BYTES+44<=INT32_MAX);
    assert((WAV_AUDIO_MAX_BYTES&1u)==0);
    assert((uint64_t)WAV_AUDIO_MAX_BYTES+46>INT32_MAX);
    w.bytes=WAV_AUDIO_MAX_BYTES-2; assert(wav_append(&w,pcm,1));
    assert(w.bytes==WAV_AUDIO_MAX_BYTES);
    assert(!wav_append(&w,pcm,1));
    fclose(f);
    f=tmpfile();assert(f);assert(wav_begin(&w,f));
    unsigned char partial[3]={1,2,3};assert(fwrite(partial,1,3,f)==3);
    w.bytes=3;w.failed=true;assert(!wav_finish(&w));
    assert(wav_open(&r,f) && r.remaining==2); /* salvage complete samples after odd short write */
    fclose(f);
    f=fopen("/dev/full","wb"); assert(f); setbuf(f,NULL);
    assert(!wav_begin(&w,f)); assert(!wav_finish(&w)); fclose(f);
    assert(wav_safe_name("RX000001.WAV"));
    assert(!wav_safe_name("../bad.WAV")); assert(!wav_safe_name("x.wav/evil"));
    char name[64];
    assert(wav_record_name(name,sizeof name,99100000,0,1794866730,0));
    assert(!strcmp(name,"261116-220530_99.1M_FM.wav"));
    assert(wav_record_name(name,sizeof name,7100100,4,0,1));
    assert(!strcmp(name,"unsynced_7.1001M_USB_1.wav"));
    assert(wav_record_name(name,sizeof name,100101,5,0,2));
    assert(!strcmp(name,"unsynced_100.101k_LSB_2.wav"));
    assert(wav_record_name(name,sizeof name,100000000,2,-1,3));
    assert(!strcmp(name,"unsynced_100M_NFM_3.wav"));
    assert(wav_recording_name(name));
    assert(wav_record_name(name,sizeof name,1000000,3,1794866730,0));
    assert(!strcmp(name,"261116-220530_1M_AM.wav"));
    assert(wav_recording_name(name));
    for(int mode=0;mode<6;mode++) {
        static const char *labels[]={"FM","NFM","NFM","AM","USB","LSB"};
        assert(wav_record_name(name,sizeof name,4294967295u,mode,4102444800LL,999999));
        assert(strstr(name,"unsynced_4294.967295M_"));
        assert(strstr(name,labels[mode]));assert(wav_recording_name(name));
    }
    assert(!wav_record_name(name,8,99100000,0,1794866730,0));
    assert(!wav_record_name(name,sizeof name,99100000,6,0,0));
    assert(wav_recording_name("RX000001.WAV"));
    assert(!wav_recording_name("BAD.WAV"));
    assert(!wav_recording_name("../RX000001.WAV"));
    assert(!wav_recording_name("261116-220530_99.1M_FM.wav/evil"));
    puts("WAV roundtrip, exact sizes, metadata and safe names passed");
}
