// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Checked radio recovery; calibration files contain no music or pairing keys.
import { bkPack, bkUnpack } from './fm1backup.js';
export const CAL_COMMAND = 81, HELPER_COMMAND = 82, HELPER_PRODUCT = 'FM-1_998';
export function crc16(bytes) {
  let c=0;
  for(const b of bytes){c^=b<<8;for(let k=0;k<8;k++)c=((c<<1)^(c&0x8000?0x1021:0))&65535;}
  return c;
}
const u16=(b,i)=>b[i]|b[i+1]<<8;
export function checkCalibration(b) {
  if(!(b instanceof Uint8Array)||b.length!==100||![0xaa,0xab,0xac,0xad].includes(b[0])||
    crc16(b.subarray(0,98))!==u16(b,98)||crc16(b.subarray(30,94))!==u16(b,94))
    throw Error('The radio calibration is incomplete or its checksum is invalid.');
  return b;
}
export function extractCalibration(image) {
  if(image.length!==0x100000)throw Error('Choose this FM-1’s original 1 MiB full-flash backup. Musical backups do not contain radio calibration.');
  const found=[];
  for(const [base,size,mark] of [[0xe8000,4096,0xac],[0xe7000,4096,0xad],[0x93000,8192,0xaa],[0x95000,8192,0xab]]){
    const a=image.subarray(base,base+size),records=new Map(),lengths=new Map([[106,2],[107,7],[108,20],[187,68]]);
    if(a[0]!==0x55||a[1]!==0xaa||a[2]!==0xaa||a[3]!==0x55)continue;
    let p=4;
    while(p+4<=a.length){
      const id=a[p+1]|(a[p+2]&15)<<8,n=a[p+2]>>4|a[p+3]<<4;
      if(!n||p+4+n>a.length)break;
      const data=a.slice(p+4,p+4+n);if((crc16(data)&255)!==a[p])break;
      if(lengths.has(id)){if(n===lengths.get(id))records.set(id,data);else records.delete(id);}
      p+=4+n;
    }
    if(records.size!==4)continue;
    const b=new Uint8Array(100);b[0]=mark;let k=1;
    for(const id of lengths.keys()){b.set(records.get(id),k);k+=lengths.get(id);}
    const c=crc16(b.subarray(0,98));b[98]=c&255;b[99]=c>>8;
    try{found.push(checkCalibration(b));}catch{/* another candidate may be valid */}
  }
  if(!found.length)throw Error('This backup has no complete, valid radio calibration. Use device recalibration instead.');
  if(found.some(b=>b.subarray(1,98).some((x,i)=>x!==found[0][i+1])))throw Error('The backup contains conflicting calibration sets.');
  return found[0];
}
export function calibrationFile(copy,source='device') {
  checkCalibration(copy);
  return {format:'melodee-radio-calibration',version:1,source,calibration:Array.from(copy)};
}
export function readCalibration(file) {
  if(!file||file.format!=='melodee-radio-calibration'||file.version!==1||!Array.isArray(file.calibration)||
    file.calibration.length!==100||file.calibration.some(x=>!Number.isInteger(x)||x<0||x>255))
    throw Error('Not a Melodee radio calibration file.');
  return checkCalibration(Uint8Array.from(file.calibration));
}
export function parseStatus(b) {
  if(b.length!==9||b.some(x=>!Number.isInteger(x)||x<0||x>127)||b[1]!==1||b.slice(2,6).some(x=>x>1)||b[8]>3)
    throw Error('Update Melodee to a version with radio calibration recovery.');
  return {rc:b[0],calibrated:!!b[2],radioOn:!!b[3],ready:!!b[4],saved:!!b[5],crc:b[6]|b[7]<<7|b[8]<<14};
}
const pause=ms=>new Promise(r=>setTimeout(r,ms));
export async function calibrationStatus(request) {return parseStatus(await request([CAL_COMMAND,[0]],{timeout:1500,retries:0}));}
export async function calibrationPreflight(request,identity) {
  if(!/^FM-1_9\d+$/.test(identity?.text||'')||identity.text===HELPER_PRODUCT)
    throw Error('Connect a running Melodee device, or use Reinstall Melodee to finish recovery.');
  try{return await calibrationStatus(request);}
  catch(e){
    // Previous Melodee releases predate command 81. They can still enter the
    // usual updater; the bundled new firmware provides the final import.
    if(/^No reply from the FM-1\.|^Update Melodee to a version with radio calibration recovery\./.test(e.message))
      return {calibrated:false,radioOn:false,legacy:true};
    throw e;
  }
}
export async function restoreCalibration(request,copy,{sleep=pause}={}) {
  checkCalibration(copy);
  const before=await calibrationStatus(request);
  if(before.radioOn)throw Error('Turn Bluetooth Radio OFF on the FM-1 first.');
  let after=parseStatus(await request([CAL_COMMAND,[1,...bkPack(copy)]],{timeout:5000,retries:0}));
  if(after.rc===2)throw Error('This FM-1 already has different valid calibration. It was kept.');
  if(![0,4].includes(after.rc))throw Error(after.rc===3?'Calibration could not be saved.':'Calibration was refused.');
  if(!after.calibrated)throw Error('Stop playback, release notes and close USB audio streams, then retry.');
  for(let k=0;!after.saved&&k<50;k++){await sleep(200);after=await calibrationStatus(request);}
  if(!after.saved||after.crc!==u16(copy,98))throw Error('The FM-1 did not confirm the expected calibration was saved.');
  return after;
}
export async function helperTrace(request) {
  const reply=await request([HELPER_COMMAND,[3]],{timeout:3000,retries:0});
  if(reply[0]||reply[1]!==1)throw Error('No helper diagnostics available.');
  const bytes=bkUnpack(reply.slice(3),52),view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const fields=['magic','version','stage','elapsed_ms','delay_calls','last_delay_us','caller','feeds','resets','reset_p3','reset_src','cap_ms','attempt'];
  const trace=Object.fromEntries(fields.map((key,i)=>[key,view.getUint32(i*4,true)]));
  if(trace.magic!==0x52464342||trace.version!==1)throw Error('Unknown helper diagnostics.');
  trace.caller_hex='0x'+trace.caller.toString(16);
  trace.stage_name=['idle','prepare','MAC reset','SIFS','CFR','PHY init','RF calibration','RF stop','validate','complete','failed','time limit','finalize RF cache'][trace.stage]||'unknown';return trace;
}
export async function generateCalibration(request) {
  let start;
  try{start=await request([HELPER_COMMAND,[1]],{timeout:80000,retries:0});}
  catch(e){throw Error('Calibration did not finish. Reconnect to the helper and read recovery diagnostics. '+e.message);}
  if(start.length!==3||start[0]||start[1]!==1||start[2]!==2){
    let details='';try{const trace=await helperTrace(request);details=` Stage ${trace.stage}, ${trace.elapsed_ms} ms, caller ${trace.caller_hex}.`;}catch{/* helper may have reset */}
    throw Error('Device recalibration failed.'+details);
  }
  return helperResult(request);
}
// 0 ready, 1 calibrating, 2 result ready, 3 failed or reset during a run
export async function helperState(request) {
  const reply=await request([HELPER_COMMAND,[0]],{timeout:3000,retries:0});
  if(reply.length!==3||reply[0]||reply[1]!==1||reply[2]>3)throw Error('Unknown calibration helper status.');
  return reply[2];
}
export async function helperResult(request) {
  const reply=await request([HELPER_COMMAND,[2]],{timeout:3000,retries:0});
  if(reply[0]||reply[1]!==1||reply[2]!==2)throw Error('The helper has no calibration result.');
  return checkCalibration(bkUnpack(reply.slice(3),100));
}
// Always return to Melodee after entering the helper, including calibration
// failures. A return/install failure leaves the downloaded result recoverable.
export async function recoverDevice({installHelper,generate,save,installMelodee,restore,onState=()=>{}}) {
  let copy,error;
  try{onState('Installing calibration helper');await installHelper();onState('Calibrating this FM-1');copy=checkCalibration(await generate());await save(calibrationFile(copy,'sdk-first-boot'));}
  catch(e){error=e;}
  onState('Reinstalling Melodee');
  try{await installMelodee();}catch(e){throw Object.assign(Error('Melodee could not be reinstalled. Use the installer to finish; keep the calibration file.'+(error?' Calibration had already failed: '+error.message:'')),{cause:e,copy,calibrationError:error});}
  if(error)throw error;
  onState('Saving radio calibration');await restore(copy);
  onState('Calibration recovered');return copy;
}
