// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import { Package, logicalImage, sha256hex } from './fm1pkg.js';
import { HELPER_PRODUCT } from './blecalibration.js';

// Both images travel with the page. Validate the whole pair before requesting
// MIDI access; a missing or damaged return image must never strand the user.
export async function calibrationPackages(bundle=globalThis.MELODEE_CALIBRATION_BUNDLE) {
  if(!bundle||bundle.schema!==1)throw Error('Calibration is not included in this page. Open the current Melodee Bluetooth recovery page.');
  const result={};
  for(const kind of ['helper','firmware']){
    const entry=bundle[kind];
    if(!entry||!Number.isInteger(entry.bytes)||entry.bytes<960||entry.bytes>2*1024*1024||
      !/^[0-9a-f]{64}$/.test(entry.sha256||'')||typeof entry.data!=='string'||
      entry.data.length!==4*Math.ceil(entry.bytes/3)||!/^[A-Za-z0-9+/]*={0,2}$/.test(entry.data))
      throw Error('The calibration page is incomplete. Reopen it before continuing.');
    const bytes=Uint8Array.from(atob(entry.data),c=>c.charCodeAt(0));
    if(bytes.length!==entry.bytes||await sha256hex(bytes)!==entry.sha256)
      throw Error('The bundled firmware failed its checksum. Reopen the calibration page.');
    const pkg=new Package(bytes);
    if(pkg.product!==entry.product||(kind==='helper'?pkg.product!==HELPER_PRODUCT:
        !/^FM-1_9\d+$/.test(pkg.product)||pkg.product===HELPER_PRODUCT)||
        !pkg.entries.some(e=>e.name==='ota.bin'))
      throw Error('The calibration page contains an incompatible firmware pair.');
    result[kind]={image:logicalImage(bytes),product:pkg.product};
  }
  return result;
}
