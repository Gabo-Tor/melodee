// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash, webcrypto} from 'node:crypto';
import {productOf, logicalImage} from './fm1pkg.js';
import {JSDOM} from 'jsdom';
import {MELODEE_LOADER, STOCK_LOADER, pack7} from './fm1ota.js';
import {SteadyUpdater, GAP_MS} from './install/transfer.js';
import {fakeAccess} from './install/fakefm1.js';
import {hold, busyElsewhere} from './install/keepalive.js';
import {createInstallSession} from './install/session.js';
import {progressModel, remaining} from './install/progress.js';

const nativeSet = globalThis.setTimeout, nativeClear = globalThis.clearTimeout;
const tick = () => new Promise(resolve => setImmediate(resolve));
const bytes = new Uint8Array(0x8000).map((_,i) => (i * 31 + 7) & 255);
const PRODUCT = 'FM-1_91045';
const clock = {set:(f,ms) => nativeSet(f,ms >= 1000 ? ms / 20 : ms),clear:nativeClear};
{
  const {access,device} = fakeAccess({images:{firmware:bytes},identity:'FM-1_015',after:PRODUCT,loader:MELODEE_LOADER,timers:clock});
  const steps = [];
  // Background page timers are delayed; timestamped MIDI sends use the device's own clock.
  globalThis.setTimeout = (f,ms = 0,...args) => nativeSet(f,ms >= 1000 ? ms / 20 : Math.max(ms,250),...args);
  try {
    assert.equal(await new SteadyUpdater(access).install(bytes,PRODUCT,(...s) => steps.push(s),MELODEE_LOADER),PRODUCT);
  } finally { globalThis.setTimeout = nativeSet; }
  const sends = device.sends.filter(s => s.delay > 0);
  assert(sends.length > 100 && sends.every(s => s.delay <= GAP_MS + 1));
  assert(steps.some(([k,n,b]) => k === 'write' && n > 0 && b > 0));
  const wrong = fakeAccess({images:{firmware:bytes},state:'loader',after:PRODUCT,loader:STOCK_LOADER,timers:clock});
  assert.equal(await new SteadyUpdater(wrong.access).resume(bytes,null,PRODUCT,MELODEE_LOADER),false);
  assert.equal(wrong.device.sends.filter(s => s.delay > 0).length,0);
  const open = fakeAccess({identity:'FM1_OFFICIAL_OPEN',after:PRODUCT,timers:clock});
  await assert.rejects(new SteadyUpdater(open.access).install(bytes,PRODUCT,null,MELODEE_LOADER),e=>e.code==='open');
  assert(!open.device.sends.some(s=>s.bytes[1]===0x22),'Open firmware is never sent the upgrade command');
  console.log('Steady transfer: timestamped replies survive throttled page timers; bytes reported; wrong loader receives no image');
}
{
  // CHECK HEAD diagnostics must retain the last address even after switching transfer loops.
  const request = (addr,len) => {
    const body = [0,0x59,0x30,9,0,0,0,addr&255,(addr>>>8)&255,(addr>>>16)&255,addr>>>24,len&255,len>>>8,0];
    body.push(~body.slice(6).reduce((a,b)=>a+b,0)&255);
    return [0xf0,...pack7(body),0xf7];
  };
  let count = 0;
  const link = {lost:true,output:{send(){}},async read(){return count++ ? null : request(0x4000,512);}};
  const result = await new SteadyUpdater({}).serve(link,bytes,0xe0000000,0,()=>{});
  assert.equal(result.lastRead,0x4000);assert(result.lost);
  count = 0;
  await assert.rejects(new SteadyUpdater({}).serve({...link,async read(){return request(bytes.length-1,512);}},bytes,0xe0000000,0),e=>e.code==='badreq');
  const lostFinish = await new SteadyUpdater({}).serve({output:{send(){throw Error('lost');}},async read(){return request(0xe0000000,8);}},bytes,0xe0000000,0);
  assert(lostFinish.lost && !lostFinish.finished);
  const model = progressModel(bytes);
  assert(model.verify(model.check) < model.writing(model.write));assert(model.writing(model.write)<100);
  assert.equal(remaining(500,1000,60000),60);
}

const html = readFileSync(new URL('index_pkg.html',import.meta.url),'utf8');
const dom = new JSDOM(html.replace(/<script type="module">[\s\S]*?<\/script>/,''),{url:'https://example.test/webapp/installer/',pretendToBeVisual:true});
const w = dom.window, d = w.document;
w.HTMLDialogElement.prototype.showModal = function(){this.setAttribute('open','');};
w.HTMLDialogElement.prototype.close = function(){this.removeAttribute('open');};
let visibility = 'visible';Object.defineProperty(d,'visibilityState',{get:()=>visibility});
let lock = null,wakeReleases=0;
const locks = {
  async request(name,options,fn) {
    if (lock) return fn(null);
    lock = {name};try { return await fn(lock); } finally { lock = null; }
  },
  async query(){return {held:lock?[lock]:[]};}
};
class TestWorker {
  constructor(){this.timers=new Map();}
  postMessage([on,id,ms]) {
    if(on)this.timers.set(id,nativeSet(()=>{this.timers.delete(id);this.onmessage({data:id});},ms));
    else{nativeClear(this.timers.get(id));this.timers.delete(id);}
  }
}
const originals = new Map(['window','document','localStorage','navigator','Worker'].map(key=>[key,Object.getOwnPropertyDescriptor(globalThis,key)]));
for(const [key,value] of Object.entries({window:w,document:d,localStorage:w.localStorage,Worker:TestWorker,navigator:{locks,wakeLock:{async request(){return {addEventListener(){},async release(){wakeReleases++;}};}}}}))
  Object.defineProperty(globalThis,key,{value,configurable:true,writable:true});
const submit = () => d.querySelector('#preflight form').dispatchEvent(new w.SubmitEvent('submit',{cancelable:true,submitter:d.getElementById('pfgo')}));
const accept = async (session,options) => {
  const promise = session.begin(options);await tick();
  if(options.backup){d.getElementById('pfok').checked=true;d.getElementById('pfok').dispatchEvent(new w.Event('change'));}
  submit();assert.equal(await promise,true);
};
try {
  const release = await hold({leaveText:'writing'});assert(release);
  assert(await busyElsewhere());assert.equal(await hold(),null);
  const leave = new w.Event('beforeunload',{cancelable:true});w.dispatchEvent(leave);assert(leave.defaultPrevented);
  release();release();await tick();assert.equal(await busyElsewhere(),false);assert.equal(wakeReleases,1);
  const after = new w.Event('beforeunload',{cancelable:true});w.dispatchEvent(after);assert(!after.defaultPrevented);

  const request = locks.request;
  locks.request = async()=>{throw Error('Lock API failed');};
  assert.equal(await hold(),null,'lock errors must not bypass tab exclusion');locks.request=request;
  const wakeRequest = navigator.wakeLock.request;let lateWake;
  navigator.wakeLock.request=()=>new Promise(resolve=>lateWake=resolve);
  const lateRelease=await hold();lateRelease();const releasedBefore=wakeReleases;
  lateWake({async release(){wakeReleases++;}});await tick();
  assert.equal(wakeReleases,releasedBefore+1,'a wake lock granted after cleanup must be released');
  navigator.wakeLock.request=wakeRequest;
  const session = createInstallSession({t:key=>key});
  const canceled = session.begin({key:'stable:1',label:'Melodee 1',image:bytes,backup:true});await tick();
  assert(d.getElementById('preflight').open);assert(d.getElementById('pfgo').disabled);
  submit();await tick();assert(d.getElementById('preflight').open,'unchecked backup acknowledgement cannot submit');
  d.getElementById('preflight').dispatchEvent(new w.Event('cancel',{cancelable:true}));
  assert.equal(await canceled,false);await tick();assert(!await busyElsewhere());assert.equal(w.localStorage.length,0);
  await accept(session,{key:'nightly:abc',label:'Nightly abc',image:bytes,backup:true});
  assert(d.getElementById('run').open);
  const escape = new w.Event('cancel',{cancelable:true});d.getElementById('run').dispatchEvent(escape);assert(escape.defaultPrevented);
  session.step('loader');session.step('write',10,10000);
  const before = +d.getElementById('bar').getAttribute('aria-valuenow');
  session.step('write',1,512);assert.equal(+d.getElementById('bar').getAttribute('aria-valuenow'),before,'progress never moves backwards');
  visibility='hidden';d.dispatchEvent(new w.Event('visibilitychange'));
  assert.match(d.title,/comeBack/);
  visibility='visible';d.dispatchEvent(new w.Event('visibilitychange'));
  session.finish(false,{code:'lost',message:'Cable disconnected'});await tick();
  assert.equal(d.getElementById('run').dataset.state,'paused');assert(!d.getElementById('run-resume').hidden);
  assert.equal(JSON.parse(w.localStorage.getItem('melodee-install-pending')).key,'nightly:abc');
  assert(!await busyElsewhere());d.getElementById('run-close').click();assert(!d.getElementById('run').open);
  await assert.rejects(session.begin({key:'nightly:def',label:'Nightly def',image:bytes,resume:true}),e=>e.code==='pending');
  assert(!d.getElementById('preflight').open);
  await accept(session,{key:'nightly:abc',label:'Nightly abc',image:bytes,resume:true});
  session.step('reboot');session.finish(false,{code:'noreturn',message:'Did not return'});await tick();
  assert.equal(d.getElementById('run').dataset.state,'restart');assert(w.localStorage.length);
  d.getElementById('run-close').click();
  await accept(session,{key:'nightly:abc',label:'Nightly abc',image:bytes,resume:true});
  session.finish(true);await tick();
  assert.equal(d.getElementById('run').dataset.state,'done');assert.equal(w.localStorage.length,0);assert(d.getElementById('resumebanner').hidden);
  assert(!await busyElsewhere());
  console.log('Installer session: preflight/cancel, backup acknowledgement, tab exclusion, wake/unload guards, monotonic progress, persistent same-build resume and restart/success states passed');
} finally {
  for(const [key,descriptor] of originals){if(descriptor)Object.defineProperty(globalThis,key,descriptor);else delete globalThis[key];}
  dom.window.close();
}

// Execute the exact library bundle produced for Pages, not a mocked session.
const libraries = execFileSync('python3',['-c',"import sys;sys.path.insert(0,'web');from make_site import installer_libraries;print(installer_libraries())"],{encoding:'utf8'});
const firmware = new Uint8Array(readFileSync(new URL('../build/melodee.fwsc',import.meta.url)));
const digest = createHash('sha256').update(firmware).digest('hex');
for (const valid of [false,true]) {
  const page = new JSDOM(html.replace(/<script type="module">[\s\S]*?<\/script>/,''),{url:'https://example.test/webapp/installer/',runScripts:'outside-only'});
  const win=page.window;let midiCalls=0;
  const simulated=fakeAccess({images:{firmware:logicalImage(firmware)},identity:'FM-1_015',after:productOf(firmware),loader:MELODEE_LOADER,stride:128,timers:clock});
  const pageSet=win.setTimeout.bind(win);win.setTimeout=(f,ms=0,...a)=>pageSet(f,ms>=1000?ms/20:ms,...a);
  win.HTMLDialogElement.prototype.showModal=function(){this.setAttribute('open','');};
  win.HTMLDialogElement.prototype.close=function(){this.removeAttribute('open');};
  win.TextEncoder=TextEncoder;Object.defineProperty(win.crypto,'subtle',{value:webcrypto.subtle});
  win.fetch=async()=>({ok:true,arrayBuffer:async()=>firmware.slice().buffer});
  win.navigator.requestMIDIAccess=async()=>{midiCalls++;return simulated.access;};
  const metadata={version:'1.4.5',product:productOf(firmware),pkg:{dual:'test.fwsc'},sha256:{dual:valid?digest:'0'.repeat(64)}};
  const script=html.match(/<script type="module">([\s\S]*?)<\/script>/)[1].replace('/*LIB*/',()=>libraries).replace('/*META*/{}',JSON.stringify(metadata));
  win.eval(script);await new Promise(r=>nativeSet(r,30));
  assert.equal(win.document.getElementById('go').disabled,!valid);
  if(valid){
    win.document.getElementById('go').click();await tick();
    assert(win.document.getElementById('preflight').open);assert.equal(midiCalls,0,'preflight precedes any MIDI permission or access');
    win.document.getElementById('preflight').dispatchEvent(new win.Event('cancel',{cancelable:true}));await tick();
    assert(!win.document.getElementById('go').disabled);assert.equal(midiCalls,0);
    win.document.getElementById('go').click();await tick();
    win.document.getElementById('pfok').checked=true;win.document.getElementById('pfok').dispatchEvent(new win.Event('change'));
    win.document.querySelector('#preflight form').dispatchEvent(new win.SubmitEvent('submit',{cancelable:true,submitter:win.document.getElementById('pfgo')}));
    const deadline=Date.now()+8000;
    while(win.document.getElementById('run').dataset.state==='running' && Date.now()<deadline)await new Promise(r=>nativeSet(r,10));
    assert.equal(win.document.getElementById('run').dataset.state,'done',win.document.getElementById('log').textContent);
    assert.equal(win.document.getElementById('bar').getAttribute('aria-valuenow'),'100');
    assert(simulated.device.log.includes('written: '+productOf(firmware)));
  }else assert.match(win.document.getElementById('status').textContent,/checksum/);
  page.window.close();
}
console.log('Published bundle: starts with actual firmware; checksum mismatch blocks Install; cancellation sends no MIDI; complete controller install verifies the returned identity');
