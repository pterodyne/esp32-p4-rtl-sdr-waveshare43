"""Assemble real P4 kernel and interpret its disassembly, NOT silicon timing.
Checks operand restrictions, exact load ranges, lane sums, output stores and
global CFG policy against an independent scalar dot product. No P4 emulation claim.
"""
from pathlib import Path
import os, random, re, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
cc=shutil.which('riscv32-esp-elf-gcc')
if not cc:
    choices=sorted(Path.home().glob('.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc'))
    assert choices, 'Export ESP-IDF: P4 toolchain required'
    cc=str(choices[-1])
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
    obj=Path(tmp)/'dot.o'
    subprocess.run([cc,'-march=rv32imafc_zicsr_zifencei_xesppie','-mabi=ilp32f','-c',str(root/'main/hackrf_dot96_p4.S'),'-o',str(obj)],check=True)
    text=subprocess.check_output([cc.replace('gcc','objdump'),'-d',str(obj)],text=True)
code=[]
active=False
for line in text.splitlines():
    if '<hackrf_dot96_p4>:' in line: active=True;continue
    m=re.match(r'\s*[0-9a-f]+:\s+[0-9a-f]+\s+([^\s]+)\s*(.*)',line)
    if active and m: code.append((m[1],m[2].replace(' ','')))
assert sum(op=='esp.vmulas.s16.xacc' for op,_ in code)==24
assert sum(op=='esp.vld.128.ip' for op,_ in code)==48
rng=random.Random(9183)
for trial in range(256):
    c=[rng.randrange(-16000,16001) for _ in range(96)]
    i=[rng.randrange(-128,128) for _ in range(96)]
    q=[rng.randrange(-128,128) for _ in range(96)]
    off=2*(trial%8)
    mem={}
    for addr,values in [(4096,c),(8192+off,i),(12288+off,q)]:
        mem.update({addr+2*k:v for k,v in enumerate(values)})
    regs={'a0':4096,'a1':8192+off,'a2':12288+off,'a3':16384}
    vectors={};out={};cfg=4;acc=0
    for op,args in code:
        a=args.split(',')
        if op=='esp.movx.r.cfg': regs[a[0]]=cfg
        elif op=='esp.movx.w.cfg': cfg=regs[a[0]]
        elif op=='ori': regs[a[0]]=regs[a[1]]|int(a[2])
        elif op=='mv': regs[a[0]]=regs[a[1]]
        elif op=='li': regs[a[0]]=int(a[1])
        elif op=='esp.zero.xacc': acc=0
        elif op=='esp.vld.128.ip':
            assert cfg&2
            vectors[a[0]]=[mem[regs[a[1]]+2*k] for k in range(8)]
            regs[a[1]]+=int(a[2])
        elif op=='esp.vmulas.s16.xacc':
            acc+=sum(x*y for x,y in zip(vectors[a[0]],vectors[a[1]]))
        elif op=='esp.srs.s.xacc': regs[a[0]]=acc>>regs[a[1]]
        elif op=='sw':
            m=re.fullmatch(r'(\d+)\((\w+)\)',a[1]); assert m
            out[regs[m[2]]+int(m[1])]=regs[a[0]]
        elif op=='ret': break
        else: raise AssertionError((op,args))
    assert out=={16384:sum(x*y for x,y in zip(c,i)),16388:sum(x*y for x,y in zip(c,q))}
    # Follow esp-dsp's global CFG convention: unaligned-access stays enabled.
    assert cfg==6
print(f'P4 kernel assembled/disassembly model: 256 vectors, 8 alignments, exact bounds/CFG/sums PASS ({len(code)} instructions; NOT cycles or hardware execution)')
