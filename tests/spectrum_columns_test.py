"""Execute the production FFT-column mapping at narrow and full spans."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[1]
s=(r/'main/view.c').read_text()
a=s.index('    int vis =',s.index('void view_render('));b=s.index('    update_floor();',a)
code='''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#define LCD_W 800
static uint32_t s_span,s_rate=2400000;
static bool s_have;
static float s_col[LCD_W],s_smooth[LCD_W],s_peak[LCD_W];
static void columns(const float*db,int n){
'''+s[a:b]+'''
}
int main(void){
float db[4096];for(int i=0;i<4096;i++)db[i]=(float)i;
unsigned spans[]={2400000,1200000,600000,300000,200000,100000};
for(int z=0;z<6;z++){
s_span=spans[z];columns(db,4096);
int vis=(int)((int64_t)4096*s_span/s_rate),first=2048-vis/2;
assert(s_col[0]>=first && s_col[799]<first+vis);
for(int x=1;x<800;x++) assert(s_col[x]>=s_col[x-1]);
if(z>=4){int distinct=1;for(int x=1;x<800;x++)distinct+=s_col[x]!=s_col[x-1];assert(distinct==vis);}
}
// Full-span aggregation preserves a single-bin peak, never averaging it away.
for(int i=0;i<4096;i++)db[i]=-100;db[2001]=0;
s_span=2400000;columns(db,4096);assert(s_col[((2001+1)*800+4095)/4096-1]==0);
return 0;
}
'''
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as d:
 p=Path(d);(p/'t.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('Actual column mapping: 200/100 kHz coarse bins repeated without fabricated resolution; wide-span max aggregation PASS')
