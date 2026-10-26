#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build the RAM-resident, data-write-free radio calibration helper."""
import hashlib,json,os,re,shutil,sys
from pathlib import Path
import build as b
import fm1pkg_make

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/rf-calibration'
SDK=Path(os.environ.get('AC79_SDK',str(Path.home()/'fw-AC79_AIoT_SDK')))
LIB=SDK/'cpu/wl82/liba/wl_rf_common.a'
RF_SHA256='ec50bb31af02ee3e2db9d87726c1b18b317f2acd72f233c9ce7b79a89ea324b2'

def main():
    os.environ.setdefault('JIELI_TOOLCHAIN',str(Path.home()/'.jieli/toolchain'))
    OUT.mkdir(parents=True,exist_ok=True)
    if hashlib.sha256(LIB.read_bytes()).hexdigest()!=RF_SHA256:
        raise SystemExit('SDK RF library changed; review its ABI and calibration path before updating the pin')
    # External library remains external. Record the exact input and its SDK licence.
    rf=OUT/'wl_rf_common.a';rf.write_bytes(LIB.read_bytes())
    fm1pkg_make.SDK=SDK
    for source in ['helper.c','bridge.c','start.S']:
        b.tc('cc',*b.CFLAGS,'-Ifirmware/hal','-Ifirmware/src','-Ibuild/gen','-c',
             ROOT/'firmware/calibration'/source,'-o',OUT/(source+'.o'))
    tool=b.toolchain()
    if b.use_docker(): plugin='/opt/jieli/pi32v2/bin/LLVMgold.so';rt='/opt/jieli/pi32v2/lib/r3-large/libcompiler_rt.a'
    else: plugin=str(tool/'pi32v2/bin/LLVMgold.so');rt=str(tool/'pi32v2/lib/r3-large/libcompiler_rt.a')
    elf=OUT/'helper.elf'
    b.tc('pi32v2/bin/ld','--plugin',plugin,'--plugin-opt=mcpu=r3',
         '--plugin-opt=-function-sections','--gc-sections','-T',ROOT/'firmware/calibration/helper.ld',
         *(OUT/(s+'.o') for s in ['start.S','helper.c','bridge.c']),
         '--start-group',rf,rt,'--end-group','-o',elf)
    syms=b.tc('common/bin/objdump','-t',elf)
    # No vendor VM/flash, scheduler, IRQ registration or app partition API may enter.
    forbidden=['vm_read','vm_write','syscfg_write','syscfg_vm_init','norflash_write','norflash_erase','os_task_create','request_irq']
    for name in forbidden:
        if re.search(r'\s'+name+r'$',syms,re.M):raise SystemExit('Forbidden SDK dependency: '+name)
    for line in syms.splitlines():
        m=re.match(r'^([0-9a-f]+)\s+.*?\bF\s+(\S+)\s+[0-9a-f]+\s+(\S+)$',line)
        if m and m[3]!='_start' and (m[2]!='.ram' or not 0x1c08000<=int(m[1],16)<0x1c72000):
            raise SystemExit('Helper code outside SRAM: '+line)
    if not re.search(r'^01c7c040\s+.*\.noinit\s+00000038\s+trace_store$',syms,re.M):
        raise SystemExit('Retained breadcrumbs are not at the reserved watchdog-surviving address')
    for section in ['boot','ram']:
        b.tc('common/bin/objcopy','-O','binary','-j','.'+section,elf,OUT/(section+'.bin'))
    boot=(OUT/'boot.bin').read_bytes();ram=(OUT/'ram.bin').read_bytes()
    image=b.build_stub()+boot+ram
    (OUT/'helper.bin').write_bytes(image)
    ota=(ROOT/'build/loader/ota.bin').read_bytes()
    package=fm1pkg_make.ufw(fm1pkg_make.flash_image(image,fm1pkg_make.KEY),ota,'FM-1_998')
    path=OUT/'melodee-ble-calibration.fwsc';path.write_bytes(package)
    report=dict(schema=1,product='FM-1_998',package_sha256=hashlib.sha256(package).hexdigest(),
        sdk_rf_sha256=hashlib.sha256(LIB.read_bytes()).hexdigest(),sdk=str(SDK),
        ram_image_bytes=len(ram),app_bytes=len(image),hardware_validated=False)
    (OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'helper.dis').write_text(b.tc('common/bin/objdump','-d',elf))
    (OUT/'LICENSES').mkdir(exist_ok=True)
    for source,target in [('LICENSE','LICENSE'),('LICENSING.md','LICENSING.md'),
                          ('docs/BLE_CALIBRATION.md','README.md'),
                          ('LICENSES/Apache-2.0.txt','LICENSES/Apache-2.0.txt'),
                          ('LICENSES/compiler-rt.txt','LICENSES/compiler-rt.txt')]:
        shutil.copyfile(ROOT/source,OUT/target)
    sys.path.insert(0,str(ROOT/'web'))
    from make_ble_calibration import write_bundle
    write_bundle(ROOT/'web/ble-calibration-bundled.js',ROOT/'build/melodee.fwsc',path)
    print(json.dumps(report,indent=2));print(path)

if __name__=='__main__':main()
