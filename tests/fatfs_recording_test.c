/* Exercise the installed IDF FatFs, not the host filesystem. Only block I/O,
 * allocation and single-thread mutex hooks are substituted. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ff.h"
#include "diskio.h"
#include "wav_audio.h"
#define SECTORS 32768
static unsigned char disk[SECTORS][512];
PARTITION VolToPart[FF_VOLUMES] = {{0,0}};
void *ff_memalloc(unsigned n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
int ff_mutex_create(int v) {(void)v;return 1;}
void ff_mutex_delete(int v) {(void)v;}
int ff_mutex_take(int v) {(void)v;return 1;}
void ff_mutex_give(int v) {(void)v;}
DWORD get_fattime(void) {return (46u<<25)|(11u<<21)|(18u<<16);}
DSTATUS disk_initialize(BYTE p) {(void)p;return 0;}
DSTATUS disk_status(BYTE p) {(void)p;return 0;}
DRESULT disk_read(BYTE p,BYTE *b,LBA_t s,UINT n) {(void)p;if(s+n>SECTORS)return RES_PARERR;memcpy(b,disk[s],n*512);return RES_OK;}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t s,UINT n) {(void)p;if(s+n>SECTORS)return RES_PARERR;memcpy(disk[s],b,n*512);return RES_OK;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *b) {(void)p;switch(cmd){case CTRL_SYNC:case CTRL_TRIM:return RES_OK;case GET_SECTOR_COUNT:*(LBA_t*)b=SECTORS;return RES_OK;case GET_SECTOR_SIZE:*(WORD*)b=512;return RES_OK;case GET_BLOCK_SIZE:*(DWORD*)b=1;return RES_OK;default:return RES_PARERR;}}
int main(void) {
    FATFS fs;BYTE work[4096];MKFS_PARM fmt={.fmt=FM_FAT|FM_SFD};
    assert(f_mkfs("0:",&fmt,work,sizeof work)==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);assert(f_mkdir("/AUDIO")==FR_OK);
    const int64_t clocks[]={0,1794866730,1704067200,4102444799LL};
    const uint32_t freqs[]={0,100101,7100100,99100000,4294967295u};
    unsigned count=0;
    for(unsigned t=0;t<4;t++)for(unsigned f=0;f<5;f++)for(int mode=0;mode<6;mode++) {
        char name[64],path[80];assert(wav_record_name(name,sizeof name,freqs[f],mode,clocks[t],++count));
        assert(wav_safe_name(name)&&wav_recording_name(name));snprintf(path,sizeof path,"/AUDIO/%s",name);
        FIL file;FRESULT rc=f_open(&file,path,FA_READ|FA_WRITE|FA_CREATE_NEW);
        if(rc!=FR_OK){fprintf(stderr,"FatFs create %s: %d (LFN=%d max=%d)\n",name,rc,FF_USE_LFN,FF_MAX_LFN);return 1;}
        UINT n;short pcm[]={12,-34,56,-78};assert(f_write(&file,pcm,sizeof pcm,&n)==FR_OK&&n==sizeof pcm);assert(f_close(&file)==FR_OK);
        assert(f_open(&file,path,FA_WRITE|FA_CREATE_NEW)==FR_EXIST);
        FILINFO info;assert(f_stat(path,&info)==FR_OK&&info.fsize==sizeof pcm);
        assert(f_unlink(path)==FR_OK);
    }
    FIL file;assert(f_open(&file,"/AUDIO/bad*record.wav",FA_WRITE|FA_CREATE_NEW)==FR_INVALID_NAME);
    assert(f_open(&file,"/AUDIO/RX000001.WAV",FA_WRITE|FA_CREATE_NEW)==FR_OK);assert(f_close(&file)==FR_OK);
    FF_DIR dir;FILINFO info;assert(f_opendir(&dir,"/AUDIO")==FR_OK);assert(f_readdir(&dir,&info)==FR_OK);assert(wav_recording_name(info.fname));assert(f_closedir(&dir)==FR_OK);
    printf("IDF FatFs: %u metadata names create/write/exclusive/stat/delete, embedded dots, offline/UTC, mode/frequency bounds, invalid wildcard, legacy list passed (LFN=%d max=%d)\n",count,FF_USE_LFN,FF_MAX_LFN);
}
