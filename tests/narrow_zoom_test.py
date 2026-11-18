from pathlib import Path
import subprocess, tempfile, os
r=Path(__file__).resolve().parents[1]
p=(r/'main/panel.c').read_text()
# Execute actual persisted lookup tables and FFT clamp, not a model.
a=p.index('static const uint32_t s_zoom_hz'); b=p.index('\n',p.index('static const char *s_zoom_sub',a))
f=p.index('uint32_t panel_zoom_hz('); e=p.index('\n',f)
code='#include <assert.h>\n#include "panel.h"\n#include "spectrum.h"\n'+p[a:b]+'\n'+p[f:e]+'''
int main(void){
assert(PANEL_ZOOMS==6);
unsigned expected[]={2400000,1200000,600000,300000,200000,100000};
for(int z=0;z<6;z++){
assert(panel_zoom_hz(z)==expected[z]);
spectrum_set_size(1024<<z); assert(spectrum_size()==(z<2 ? 1024<<z : 4096));
assert(s_zoom_lbl[z][0] && s_zoom_sub[z][0]);
}
assert(panel_zoom_hz(-1)==2400000);
return 0;
}
'''
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as d:
 t=Path(d);(t/'t.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-I'+str(r/'main'),'-I'+str(r/'tools/preview/stub'),str(t/'t.c'),str(r/'main/spectrum.c'),'-lm','-o',str(t/'t')],check=True)
 subprocess.run([str(t/'t')],check=True)
print('Persisted zoom IDs 0-3 unchanged; 200/100 kHz appended; actual FFT clamp 4096 PASS')
