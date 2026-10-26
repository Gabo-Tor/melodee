// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import assert from 'node:assert/strict';
import {readFileSync,existsSync,mkdtempSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFileSync} from 'node:child_process';
import {crc16,extractCalibration,checkCalibration,calibrationFile,readCalibration,restoreCalibration,recoverDevice,
  generateCalibration,helperTrace,helperState,helperResult,calibrationPreflight} from './blecalibration.js';
import {bkPack,bkUnpack} from './fm1backup.js';
import {JSDOM} from 'jsdom';
import {webcrypto} from 'node:crypto';
import vm from 'node:vm';
import {calibrationPackages} from './ble-calibration-packages.js';
import {demoCalibration} from './ble-calibration-demo.js';
function image(base=0xe8000){
  const a=new Uint8Array(0x100000).fill(255);a.set([0x55,0xaa,0xaa,0x55],base);let p=base+4;
  for(const [id,n] of [[106,2],[107,7],[108,20],[187,68]]){
    const d=Uint8Array.from({length:n},(_,i)=>(i*53+id)&255);
    if(id===187){const c=crc16(d.subarray(0,64));d[64]=c&255;d[65]=c>>8;}
    a.set([crc16(d)&255,id&255,id>>8|(n&15)<<4,n>>4],p);a.set(d,p+4);p+=4+n;
  }return a;
}
const im=image(),copy=extractCalibration(im),crc=copy[98]|copy[99]<<8;
assert.deepEqual(readCalibration(calibrationFile(copy)),copy);
for(const n of [0,100,100000,0x100001])assert.throws(()=>extractCalibration(new Uint8Array(n)));
const bad=copy.slice();bad[32]^=1;assert.throws(()=>checkCalibration(bad));
const inner=copy.slice();inner[94]^=1;const c=crc16(inner.subarray(0,98));inner[98]=c&255;inner[99]=c>>8;assert.throws(()=>checkCalibration(inner));
const conflict=image(0xe7000);im.set(conflict.subarray(0xe7000,0xe8000),0xe7000);
im[0xe7008]^=1;im[0xe7004]=crc16(im.subarray(0xe7008,0xe700a))&255;
assert.throws(()=>extractCalibration(im),/conflicting/);
let calls=0;
const reply=(cal,saved,rc=0)=>[rc,1,cal,0,0,saved,crc&127,(crc>>7)&127,crc>>14];
await restoreCalibration(async ([cmd,args])=>{
  assert.equal(cmd,81);calls++;
  if(calls===1)return reply(0,0);
  if(calls===2){assert.deepEqual(bkUnpack(args.slice(1),100),copy);return reply(1,0,4);}
  return reply(1,1);
},copy,{sleep:async()=>{}});
assert.equal(calls,3);
assert((await calibrationPreflight(async()=>{throw Error('No reply from the FM-1. Check the selected firmware');},{text:'FM-1_91050'})).legacy);
await assert.rejects(()=>calibrationPreflight(async()=>{throw Error('USB connection lost.');},{text:'FM-1_91050'}),/USB/);
await assert.rejects(()=>calibrationPreflight(async()=>reply(0,0),{text:'FM-1_015'}),/running Melodee/);
await assert.rejects(()=>restoreCalibration(async()=>[0,1,0,1,0,0,0,0,0],copy),/Radio OFF/);
let nr=0;await assert.rejects(()=>restoreCalibration(async()=>++nr===1?reply(1,1):reply(1,1,2),copy),/different valid/);
assert.deepEqual(await generateCalibration(async([,args],options)=>{if(args[0]===1)assert.equal(options.timeout,80000);return args[0]===1?[0,1,2]:[0,1,2,...bkPack(copy)];}),copy);
const raw=new Uint8Array(52),v=new DataView(raw.buffer);
[0x52464342,1,11,60000,80000,1000,0x1c0bbcc,80000,1,32,4,60000,1].forEach((x,i)=>v.setUint32(i*4,x,true));
const trace=await helperTrace(async()=>[0,1,3,...bkPack(raw)]);assert.equal(trace.stage,11);assert.equal(trace.caller_hex,'0x1c0bbcc');
await assert.rejects(()=>generateCalibration(async([,args])=>args[0]===1?[0,1,3]:[0,1,3,...bkPack(raw)]),/60000 ms/);
for(const failure of [null,'helper','generate','save','return','restore']){
  const calls=[];
  const fn=name=>async value=>{calls.push(name);if(failure===name)throw Error(name);return name==='generate'?copy:value;};
  const action=()=>recoverDevice({installHelper:fn('helper'),generate:fn('generate'),save:fn('save'),installMelodee:fn('return'),restore:fn('restore')});
  if(failure)await assert.rejects(action);else assert.deepEqual(await action(),copy);
  assert(calls.includes('return'),'always attempts to return to Melodee');
  if(failure==='return')assert(!calls.includes('restore'));
  if(['helper','generate','save'].includes(failure))assert(!calls.includes('restore'));
  if(!failure)assert.deepEqual(calls,['helper','generate','save','return','restore']);
}
assert.equal(await helperState(async([cmd,args])=>{assert.deepEqual([cmd,args],[82,[0]]);return [0,1,2];}),2);
await assert.rejects(()=>helperState(async()=>[0,1,7]),/Unknown/);
assert.deepEqual(await helperResult(async()=>[0,1,2,...bkPack(copy)]),copy);
await assert.rejects(()=>helperResult(async()=>[2,1,0]),/no calibration result/);
checkCalibration(demoCalibration());
await assert.rejects(()=>calibrationPackages(null),/not included/);
await assert.rejects(()=>calibrationPackages({schema:1,helper:{}}),/incomplete/);

// The page: the classic bundle's code, as shipped, in a DOM against the demo's simulated FM-1 (?demo=<scenario>).
const page=readFileSync(new URL('./ble-calibration.html',import.meta.url),'utf8');
for(const id of ['find','recheck','go','preflight','pfok','run','backup','import','finish','diagnose','status'])assert(page.includes('id="'+id+'"'),id);
assert(!page.includes('id="helper"')&&!page.includes('id="firmware"'),'no firmware pickers');
assert(page.includes('href="./install/installer.css"'),"the installer's styles");
const tmp=join(mkdtempSync(join(tmpdir(),'ble-calibration-')),'bundle.js');
execFileSync('python3',['-c',"import sys;sys.path.insert(0,'web');from make_ble_calibration import write_bundle;write_bundle(sys.argv[1])",tmp],
  {cwd:new URL('..',import.meta.url)});
const code=readFileSync(tmp,'utf8');new vm.Script(code);assert(!/^import\s/m.test(code));
const nativeSet=setTimeout,wait=ms=>new Promise(r=>nativeSet(r,ms));
async function until(cond,ms=20000){const end=Date.now()+ms;while(!cond()&&Date.now()<end)await wait(10);return cond();}
function open(query,options={}){
  const dom=new JSDOM(page,{url:'https://example.test/webapp/editor/ble-calibration.html'+query,runScripts:'outside-only'});
  const win=dom.window,doc=win.document,$=id=>doc.getElementById(id),downloads=[];
  const pageSet=win.setTimeout.bind(win);win.setTimeout=(f,ms=0,...a)=>pageSet(f,ms>=100?ms/20:ms,...a);
  win.HTMLDialogElement.prototype.showModal=function(){this.setAttribute('open','');};
  win.HTMLDialogElement.prototype.close=function(){this.removeAttribute('open');};
  Object.defineProperty(win,'crypto',{value:webcrypto});win.TextEncoder=TextEncoder;
  win.URL.createObjectURL=()=>'blob:test';win.URL.revokeObjectURL=()=>{};
  win.HTMLAnchorElement.prototype.click=function(){downloads.push(this.download);};
  let midi=0;Object.defineProperty(win.navigator,'requestMIDIAccess',{value:async()=>{midi++;throw Error('test MIDI permission boundary');}});
  win.MELODEE_CALIBRATION_DEMO={stride:64,measureMs:2000};
  const source=options.source||code;
  win.eval('bundle' in options?source.replace(/^globalThis\.MELODEE_CALIBRATION_BUNDLE=.*;$/m,()=>'globalThis.MELODEE_CALIBRATION_BUNDLE='+JSON.stringify(options.bundle)+';'):source);
  const ended=async()=>{await until(()=>$('endtitle').textContent,30000);return $('run').dataset.state;};
  const submit=()=>doc.querySelector('#preflight form').dispatchEvent(new win.SubmitEvent('submit',{cancelable:true,submitter:$('pfgo')}));
  return {win,doc,$,downloads,midis:()=>midi,ended,submit,chip:()=>$('devchip').textContent,
    find:async()=>{$('connmsg').textContent='';$('device').hidden=true;$('find').click();
      await until(()=>!$('find').disabled&&(!$('device').hidden||$('connmsg').textContent));},
    run:async(backup=true)=>{$('endtitle').textContent='';$('go').click();await until(()=>$('preflight').open);
      assert.equal($('pfcheck').hidden,!backup);
      if(backup){assert($('pfgo').disabled,'the backup is asked first');$('pfok').checked=true;$('pfok').dispatchEvent(new win.Event('change'));}
      submit();return ended();},
    close:()=>doc.querySelector('#endactions button:last-child').click()};
}
const demoMark='melodee-radio-recovery-demo';

// NO CAL: preflight can be cancelled; the whole run; the FM-1 then reports its saved calibration
let p=open('?demo=nocal');await p.find();
assert.equal(p.chip(),'NO CAL');assert.equal(p.$('st-radio').dataset.state,'done');assert(!p.$('go').disabled);
p.$('go').click();await until(()=>p.$('preflight').open);
p.$('preflight').dispatchEvent(new p.win.Event('cancel',{cancelable:true}));await until(()=>!p.$('go').disabled);
assert(!p.$('run').open,'cancelling starts nothing');
assert.equal(await p.run(),'done',p.$('log').textContent);
assert.match(p.$('endtitle').textContent,/Bluetooth is calibrated/);assert.equal(p.$('bar').getAttribute('aria-valuenow'),'100');
assert.deepEqual(p.downloads,[],'the demo never offers its calibration as a file');
assert.equal(p.win.localStorage.getItem(demoMark),null);
p.close();await p.find();assert.equal(p.chip(),'Calibrated');assert.equal(p.$('go').textContent,'Nothing to fix');
assert(p.$('go').disabled);assert.equal(p.$('st-run').dataset.state,'done');p.win.close();

// Nothing to do, radio on, and a Melodee from before command 81 (returns as the included release)
p=open('?demo=ok&auto');await until(()=>!p.$('device').hidden);assert.equal(p.chip(),'Calibrated');assert(p.$('go').disabled);p.win.close();
p=open('?demo=radio');await p.find();assert.equal(p.chip(),'Radio on');assert.equal(p.$('st-radio').dataset.state,'active');
assert(!p.$('recheck').hidden);assert(p.$('go').disabled);p.win.close();
p=open('?demo=legacy');await p.find();assert.equal(p.chip(),'Radio unknown');assert(p.$('st-radio').hidden);
assert.equal(await p.run(),'done',p.$('log').textContent);p.close();await p.find();assert.equal(p.chip(),'Calibrated');p.win.close();

// A failed measurement still returns to Melodee, and says what the helper recorded
p=open('?demo=fail');await p.find();assert.equal(await p.run(),'failed',p.$('log').textContent);
assert.match(p.$('endtips').textContent,/Stage 11 \(time limit\)/);assert.match(p.$('endtips').textContent,/helper's record was saved/);
assert.equal(p.win.localStorage.getItem(demoMark),null);p.close();await p.find();assert.equal(p.chip(),'NO CAL');p.win.close();

// The cable goes during the return write: paused with the result kept; Resume finishes and saves it
p=open('?demo=interrupt');await p.find();assert.equal(await p.run(),'paused',p.$('log').textContent);
assert(JSON.parse(p.win.localStorage.getItem(demoMark)).calibration,'the measured calibration is kept');
assert.equal(p.localStorage,undefined);assert.equal(p.win.localStorage.getItem('melodee-radio-recovery'),null,'the demo never marks a real run');
p.$('endtitle').textContent='';p.doc.querySelector('#endactions button').click();
await until(()=>p.$('preflight').open);assert(p.$('pfcheck').hidden);p.submit();
assert.equal(await p.ended(),'done',p.$('log').textContent);assert.equal(p.win.localStorage.getItem(demoMark),null);
p.close();await p.find();assert.equal(p.chip(),'Calibrated');p.win.close();

// The helper: holding a result (finish saves it), or ready (continue measures)
p=open('?demo=result');await p.find();assert.equal(p.chip(),'Helper');assert.match(p.$('go').textContent,/^Return to Melodee 1\.5\.0$/);
assert(!p.$('resumebanner').hidden);assert.equal(await p.run(false),'done',p.$('log').textContent);
p.close();await p.find();assert.equal(p.chip(),'Calibrated');p.win.close();
p=open('?demo=helper');await p.find();assert.equal(p.$('go').textContent,'Continue calibration');
assert.equal(await p.run(false),'done',p.$('log').textContent);p.win.close();

// Update mode with nothing kept: Melodee comes back and the radio stays as it was
p=open('?demo=update');await p.find();assert.equal(p.chip(),'Update mode');
assert.equal(await p.run(false),'done',p.$('log').textContent);assert.match(p.$('endtitle').textContent,/Melodee 1\.5\.0 is back/);
p.close();await p.find();assert.equal(p.chip(),'NO CAL');p.win.close();

// Melodee never confirms the saved copy: partial, with Try saving again
p=open('?demo=refuse');await p.find();assert.equal(await p.run(),'partial',p.$('log').textContent);
assert.match(p.$('endactions').textContent,/Try saving again/);assert(p.win.localStorage.getItem(demoMark));p.win.close();

// A saved file: a wrong one is refused before any run; this FM-1's file is restored
p=open('?demo=nocal');
const choose=file=>{Object.defineProperty(p.$('backup'),'files',{value:[file],configurable:true});p.$('backup').dispatchEvent(new p.win.Event('change'));};
choose({size:20,text:async()=>'{"nope":1}'});assert(!p.$('import').disabled);p.$('import').click();
await until(()=>p.$('importstatus').textContent);assert.match(p.$('importstatus').textContent,/Not a Melodee radio calibration/);assert(!p.$('run').open);
choose({size:1000,text:async()=>JSON.stringify(calibrationFile(demoCalibration()))});p.$('import').click();
assert.equal(await p.ended(),'done',p.$('log').textContent);assert.equal(p.$('endtitle').textContent,'Calibration restored');
p.close();await p.find();assert.equal(p.chip(),'Calibrated');p.win.close();

// Diagnostics: only the helper keeps a record
p=open('?demo=nocal');p.$('diagnose').click();await until(()=>/not the calibration helper/.test(p.$('diagstatus').textContent));p.win.close();
p=open('?demo=fail');p.$('diagnose').click();await until(()=>/^Saved .*stage \d/.test(p.$('diagstatus').textContent));p.win.close();

// Without its firmware pair the page says so at once, still restores files, and asks for MIDI only on Find
for(const [bundle,why] of [[null,/not included/],[{schema:1,helper:{}},/incomplete/]]){
  p=open('',{bundle});await until(()=>!p.$('pagefail').hidden);
  assert.match(p.$('pagefailtext').textContent,why);assert(p.$('go').disabled&&p.$('finish').disabled&&!p.$('find').disabled);
  assert.equal(p.midis(),0);await p.find();assert.equal(p.midis(),1);assert.match(p.$('connmsg').textContent,/MIDI access was not allowed/);
  p.win.close();
}
// The built bundle with its firmware pair: it starts clean, and a damaged image is caught before MIDI access
const bundlePath=new URL('./ble-calibration-bundled.js',import.meta.url);
if(existsSync(bundlePath)){
  const shipped=readFileSync(bundlePath,'utf8');new vm.Script(shipped);assert(!/^import\s/m.test(shipped));
  p=open('',{source:shipped});await until(()=>!p.$('fact-version').hidden,10000);assert(p.$('pagefail').hidden);p.win.close();
  const pair=JSON.parse(shipped.match(/^globalThis\.MELODEE_CALIBRATION_BUNDLE=(.*);$/m)[1]);pair.firmware.sha256='0'.repeat(64);
  p=open('',{source:shipped,bundle:pair});await until(()=>!p.$('pagefail').hidden,10000);
  assert.match(p.$('pagefailtext').textContent,/checksum/);assert(p.$('go').disabled);assert.equal(p.midis(),0);p.win.close();
}
console.log('BLE web recovery: VM/CRC validation, helper status, recovery order, and the page against a simulated FM-1 '+
  '(calibrate, nothing to do, radio on, legacy, failed measurement, interrupted return + resume, helper result/continue, update mode, '+
  'unconfirmed save, file restore, diagnostics, missing or damaged firmware pair) passed');
