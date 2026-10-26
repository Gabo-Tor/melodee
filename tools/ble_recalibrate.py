#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Recalibrate one FM-1, save its result, reinstall Melodee and import it.

Uses a temporary RAM-resident helper. SDK board defaults replace missing
production trim records; live RF calibration is measured on the connected unit.
Does not restore a flash image, erase a VM, or restore musical archives.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from ble_calibration import copy_valid, restore, status
from fm1_install import load_package

HELPER_PRODUCT='FM-1_998'

def helper_trace(device):
    from prophet_device import unpack
    b=device.request(82,[3]);raw=unpack(b[3:])
    if b[:2]!=[0,1] or len(raw)!=52:raise RuntimeError('No helper diagnostics')
    import struct
    fields=('magic','version','stage','elapsed_ms','delay_calls','last_delay_us','caller','feeds','resets','reset_p3','reset_src','cap_ms','attempt')
    trace=dict(zip(fields,struct.unpack('<13I',raw)))
    if trace['magic']!=0x52464342 or trace['version']!=1:raise RuntimeError('Unknown helper diagnostics')
    trace['caller_hex']=hex(trace['caller'])
    stages=['idle','prepare','MAC reset','SIFS','CFR','PHY init','RF calibration','RF stop','validate','complete','failed','time limit','finalize RF cache']
    trace['stage_name']=stages[trace['stage']] if trace['stage']<len(stages) else 'unknown'
    return trace

def read_helper(device):
    try:reply=device.request(82,[1],timeout=80)
    except TimeoutError as e:
        try:details=helper_trace(device)
        except Exception:details={'message':'Reconnect to the helper and query command 82 action 3'}
        raise RuntimeError('Calibration timed out; retained diagnostics: '+json.dumps(details)) from e
    if reply!=[0,1,2]:
        try:details=helper_trace(device)
        except Exception:details={}
        raise RuntimeError('Device calibration failed; retained diagnostics: '+json.dumps(details))
    b=device.request(82,[2])
    if b[:3]!=[0,1,2]:raise RuntimeError('Helper has no calibration result')
    from prophet_device import unpack
    copy=unpack(b[3:])
    if not copy_valid(copy):raise RuntimeError('Device calibration failed checksum validation')
    return copy

def recover(*,install_helper,generate,save,install_melodee,import_copy):
    copy,error=None,None
    try:
        install_helper()
        copy=generate()
        if not copy_valid(copy):raise ValueError('Invalid calibration result')
        save(copy) # save before any return flash; an interrupted return is recoverable
    except Exception as e:error=e
    try:install_melodee()
    except Exception as e:
        first=f' Calibration had already failed: {error!r}' if error else ''
        raise RuntimeError('Melodee could not be reinstalled. Finish with fm1_install.py; keep the calibration file.'+first) from e
    if error:raise error
    return import_copy(copy)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--helper',type=Path)
    p.add_argument('--firmware',type=Path,help='Melodee package with calibration import support')
    p.add_argument('--port',required=True,help='unique FM-1 MIDI port name or substring')
    p.add_argument('--output',type=Path,help='save this device’s checked calibration JSON')
    p.add_argument('--diagnose',action='store_true',help='read retained helper breadcrumbs without installing or calibrating')
    p.add_argument('--measure',action='store_true',help='developer: measure even if already calibrated; keep existing calibration')
    a=p.parse_args()
    if a.diagnose:
        from prophet_device import Device
        import mido
        names=[n for n in mido.get_input_names() if a.port.lower() in n.lower() and n in mido.get_output_names()]
        if len(names)!=1:p.error('Connect exactly one matching FM-1 MIDI device')
        d=Device(names[0])
        try:result=helper_trace(d)
        finally:d.close()
        text=json.dumps(result,indent=2)+'\n'
        if a.output:
            with a.output.open('x') as f:f.write(text)
        print(text);return
    if not a.helper or not a.firmware or not a.output:p.error('Recovery needs --helper, --firmware and --output')
    helper,_,_=load_package(a.helper,False);firmware,_,_=load_package(a.firmware,False)
    if helper!=HELPER_PRODUCT or not firmware.startswith('FM-1_9') or firmware==HELPER_PRODUCT:
        p.error('Choose the calibration helper and a normal Melodee firmware package')
    if a.output.exists():p.error('Output already exists; choose a new path to keep the previous calibration')
    # Fail before installing anything if the result cannot be saved.
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x') as f:f.write('{}\n')
    from prophet_device import Device
    import mido
    def device():
        names=[n for n in mido.get_input_names() if a.port.lower() in n.lower() and n in mido.get_output_names()]
        if len(names)!=1:raise RuntimeError('Connect exactly one matching FM-1 MIDI device')
        return Device(names[0])
    d=device()
    try:before=status(d)
    finally:d.close()
    if before['calibrated'] and not a.measure:
        a.output.unlink();print('This FM-1 already has valid radio calibration. Nothing changed.');return
    if before['radio_on']:raise RuntimeError('Switch Bluetooth Radio OFF first')
    def install(path):
        print('Installing '+str(path),flush=True)
        subprocess.run([sys.executable,str(Path(__file__).with_name('fm1_install.py')),str(path),
                        '--port',a.port,'--yes'],check=True)
    def generate():
        d=device();error=None
        try:
            copy=read_helper(d)
            try:
                details=helper_trace(d)
                a.output.with_suffix('.diagnostics.json').write_text(json.dumps(details,indent=2)+'\n')
                print('Calibration complete: '+str(details['elapsed_ms'])+' ms; '+str(details['feeds'])+' watchdog feeds',flush=True)
            except Exception:pass
            return copy
        except Exception as e:error=e
        finally:d.close()
        # A watchdog reset can invalidate the old MIDI handle. Reopen before
        # returning to Melodee so its startup cannot replace retained evidence.
        details=None
        try:
            d=device()
            try:details=helper_trace(d)
            finally:d.close()
        except Exception:pass
        if details is not None:
            path=a.output.with_suffix('.diagnostics.json')
            path.write_text(json.dumps(details,indent=2)+'\n')
            raise RuntimeError(str(error)+'; diagnostics saved: '+str(path)) from error
        raise error
    def save(copy):
        a.output.write_text(json.dumps(dict(format='melodee-radio-calibration',version=1,
            source='sdk-first-boot',calibration=list(copy)),indent=2)+'\n')
        print('Checked calibration saved: '+str(a.output),flush=True)
    def apply(copy):
        d=device()
        try:
            if a.measure and before['calibrated']:
                after=status(d)
                if not after['calibrated'] or after['crc']!=before['crc']:raise RuntimeError('Existing calibration changed')
                return dict(measured=True,existing_calibration_kept=True,status=after)
            return restore(d,copy)[1]
        finally:d.close()
    result=recover(install_helper=lambda:install(a.helper),generate=generate,save=save,
        install_melodee=lambda:install(a.firmware),import_copy=apply)
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
