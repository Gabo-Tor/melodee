#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ble_calibration_test import image_at
from ble_calibration import extract
from ble_recalibrate import recover,read_helper,helper_trace
from prophet_device import pack

class Recalibrate(unittest.TestCase):
    def test_live_protocol(self):
        copy=extract(image_at()[0])[1]
        class Device:
            def request(self,cmd,args,timeout=5):
                self.cmd=cmd
                if args==[1]:assert timeout==80
                return [0,1,2] if args==[1] else [0,1,2]+pack(copy)
        device=Device();self.assertEqual(read_helper(device),copy);self.assertEqual(device.cmd,82)
    def test_retained_trace_after_timeout(self):
        import struct
        raw=struct.pack('<13I',0x52464342,1,11,60000,80000,1000,0x1c0bbcc,80000,1,32,4,60000,1)
        class Device:
            def request(self,cmd,args,timeout=5):
                if args==[1]:
                    assert timeout==80
                    raise TimeoutError('reset')
                return [0,1,3]+pack(raw)
        trace=helper_trace(Device());self.assertEqual(trace['caller_hex'],'0x1c0bbcc')
        with self.assertRaisesRegex(RuntimeError,'60000'):
            read_helper(Device())
    def test_failure_returns(self):
        copy=extract(image_at()[0])[1]
        for fail in [None,'helper','generate','save','return','import']:
            calls=[]
            def fn(name):
                def call(*args):
                    calls.append(name)
                    if name==fail:raise RuntimeError(name)
                    return copy if name=='generate' else None
                return call
            def action():return recover(install_helper=fn('helper'),generate=fn('generate'),save=fn('save'),install_melodee=fn('return'),import_copy=fn('import'))
            if fail:
                with self.assertRaises(RuntimeError):action()
            else:action();self.assertEqual(calls,['helper','generate','save','return','import'])
            self.assertIn('return',calls)
            if fail in ['helper','generate','save','return']:self.assertNotIn('import',calls)

if __name__=='__main__':unittest.main()
