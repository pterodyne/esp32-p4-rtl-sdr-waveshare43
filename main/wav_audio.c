#include "wav_audio.h"
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
static void put16(unsigned char *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static void put32(unsigned char *p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }
static uint16_t get16(const unsigned char *p) { return p[0]|(uint16_t)p[1]<<8; }
static uint32_t get32(const unsigned char *p) { return get16(p)|(uint32_t)get16(p+2)<<16; }
static bool header(FILE *f, uint32_t bytes) {
    unsigned char h[44] = {0};
    memcpy(h,"RIFF",4); put32(h+4,36+bytes); memcpy(h+8,"WAVEfmt ",8);
    put32(h+16,16); put16(h+20,1); put16(h+22,1); put32(h+24,WAV_AUDIO_RATE);
    put32(h+28,WAV_AUDIO_RATE*2); put16(h+32,2); put16(h+34,16);
    memcpy(h+36,"data",4); put32(h+40,bytes);
    return fseek(f,0,SEEK_SET)==0 && fwrite(h,1,sizeof h,f)==sizeof h;
}
bool wav_begin(wav_writer_t *w, FILE *f) {
    *w=(wav_writer_t){.file=f}; w->failed=!header(f,0); return !w->failed;
}
bool wav_append(wav_writer_t *w, const int16_t *pcm, size_t count) {
    if(w->failed || count>(WAV_AUDIO_MAX_BYTES-w->bytes)/2) { w->failed=true; return false; }
    /* ESP32-P4 and supported host tests are little endian. */
    size_t n=fwrite(pcm,1,count*2,w->file); w->bytes+=(uint32_t)n;
    if(n!=count*2) w->failed=true;
    return !w->failed;
}
bool wav_finish(wav_writer_t *w) {
    bool ok=!w->failed;
    /* On a short write, trim the declared payload to complete samples. */
    if(fflush(w->file)!=0) ok=false;
    clearerr(w->file);
    if((w->bytes & 1u) && ftruncate(fileno(w->file),(off_t)44+(w->bytes & ~1u))!=0) ok=false;
    if(!header(w->file,w->bytes & ~1u) || fflush(w->file)!=0) ok=false;
    return ok;
}
bool wav_open(wav_reader_t *r, FILE *f) {
    *r=(wav_reader_t){.file=f};
    unsigned char h[44];
    if(fseek(f,0,SEEK_SET) || fread(h,1,44,f)!=44) return false;
    /* Deliberately strict: canonical listening-audio WAVs, not arbitrary codecs/rates. */
    if(memcmp(h,"RIFF",4)||memcmp(h+8,"WAVEfmt ",8)||get32(h+16)!=16 ||
       get16(h+20)!=1||get16(h+22)!=1||get32(h+24)!=WAV_AUDIO_RATE||
       get32(h+28)!=WAV_AUDIO_RATE*2||get16(h+32)!=2||get16(h+34)!=16||memcmp(h+36,"data",4)) return false;
    uint32_t n=get32(h+40);
    if((n&1)||n>WAV_AUDIO_MAX_BYTES||get32(h+4)!=36+n) return false;
    if(fseek(f,0,SEEK_END)!=0 || ftell(f)<0 || (unsigned long)ftell(f)!=44UL+n || fseek(f,44,SEEK_SET)!=0) return false;
    r->remaining=n; return true;
}
size_t wav_read(wav_reader_t *r,int16_t *pcm,size_t count) {
    if(count>r->remaining/2) count=r->remaining/2;
    size_t n=fread(pcm,2,count,r->file); r->remaining-=(uint32_t)n*2;
    if(n!=count) r->failed=true;
    return n;
}
bool wav_safe_name(const char *s) {
    if(!s) return false;
    size_t n=strlen(s); if(n<5||n>63) return false;
    if(strcmp(s+n-4,".WAV") && strcmp(s+n-4,".wav")) return false;
    for(size_t i=0;i<n-4;i++) if(!isalnum((unsigned char)s[i]) && s[i]!='_' && s[i]!='-' && s[i]!='.') return false;
    return true;
}

static void frequency(char *out,size_t size,uint32_t hz,uint32_t unit,int digits,char suffix) {
    char fraction[8];
    snprintf(fraction,sizeof fraction,"%0*u",digits,(unsigned)(hz%unit));
    size_t n=strlen(fraction);while(n && fraction[n-1]=='0') fraction[--n]=0;
    snprintf(out,size,"%u%s%s%c",(unsigned)(hz/unit),n?".":"",fraction,suffix);
}
bool wav_record_name(char *out,size_t size,uint32_t hz,int bw,int64_t utc,unsigned collision) {
    static const char *modes[]={"FM","NFM","NFM","AM","USB","LSB"};
    if(bw<0 || bw>=6 || collision>999999) return false;
    char mhz[24],khz[24],stamp[24]="unsynced",suffix[16]="";
    frequency(mhz,sizeof mhz,hz,1000000,6,'M');
    frequency(khz,sizeof khz,hz,1000,3,'k');
    /* Consume real system clock only; never set time or configure a network. */
    bool valid=utc>=1704067200LL && utc<4102444800LL;
    time_t t=(time_t)utc;struct tm tm;
    if(valid && (int64_t)t==utc && gmtime_r(&t,&tm)) {
        if(!strftime(stamp,sizeof stamp,"%y%m%d-%H%M%S",&tm)) return false;
    } else valid=false;
    if(collision || !valid) snprintf(suffix,sizeof suffix,"_%u",collision?collision:1);
    int n=snprintf(out,size,"%s_%s_%s%s.wav",stamp,strlen(khz)<strlen(mhz)?khz:mhz,modes[bw],suffix);
    return n>0 && (size_t)n<size;
}
static bool digits(const char **p,int n) {
    for(int i=0;i<n;i++) {if(!isdigit((unsigned char)**p)) return false;(*p)++;}
    return true;
}
bool wav_recording_name(const char *s) {
    if(!wav_safe_name(s)) return false;
    size_t n=strlen(s);
    if(n==12 && !strncmp(s,"RX",2) && !strcmp(s+8,".WAV")) {
        const char *p=s+2;return digits(&p,6);
    }
    const char *p=s;
    bool unsynced=!strncmp(p,"unsynced_",9);
    if(unsynced) p+=9;
    else {if(n<23 || !digits(&p,6) || *p++!='-' || !digits(&p,6) || *p++!='_') return false;}
    if(!isdigit((unsigned char)*p)) return false;
    while(isdigit((unsigned char)*p)) p++;
    if(*p=='.') {p++;if(!isdigit((unsigned char)*p))return false;while(isdigit((unsigned char)*p))p++;}
    if((*p!='M' && *p!='k') || p[1]!='_') return false;
    p+=2;
    const char *modes[]={"FM","NFM","AM","USB","LSB"};
    bool found=false;
    for(unsigned i=0;i<5;i++) {size_t len=strlen(modes[i]);if(!strncmp(p,modes[i],len) && (p[len]=='_' || p[len]=='.')) {p+=len;found=true;break;}}
    if(!found) return false;
    if(*p=='_') {p++;if(!isdigit((unsigned char)*p))return false;while(isdigit((unsigned char)*p))p++;}
    else if(unsynced) return false;
    return !strcmp(p,".wav");
}
