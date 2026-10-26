// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// The Bluetooth recovery page (ble-calibration.html), laid out and run as Melodee's installer (install/installer.js,
// after Saadet): find the FM-1 and read its radio, have the radio switched off, then calibrate in a screen of its
// own. Calibrating installs the helper (FM-1_998), measures, downloads the result, reinstalls Melodee and saves the
// result there (blecalibration.js recoverDevice). A run that stops while the helper or the update loader holds the
// FM-1 is finished from this page: the device itself and a mark in localStorage say so. ?demo runs the page against
// a simulated FM-1 (ble-calibration-demo.js): nothing is written or downloaded.

import { extractCalibration, readCalibration, calibrationFile, calibrationPreflight, restoreCalibration,
  generateCalibration, recoverDevice, helperTrace, helperState, helperResult, HELPER_PRODUCT } from './blecalibration.js';
import { calibrationPackages } from './ble-calibration-packages.js';
import { MELODEE_LOADER } from './fm1ota.js';
import { SteadyUpdater } from './install/transfer.js';
import { hold } from './install/keepalive.js';
import { progressModel, remaining, releaseVersion } from './install/progress.js';
import { demoSetup } from './ble-calibration-demo.js';

const $ = id => document.getElementById(id);
if (!location.pathname.endsWith('/ble-calibration.html')) throw Error('Unexpected recovery page location');
const params = new URLSearchParams(location.search);
const DEMO = params.has('demo') ? (params.get('demo') || 'nocal') : null;
const MARK = DEMO ? 'melodee-radio-recovery-demo' : 'melodee-radio-recovery';   // a run that has not reported Done
const ON_SITE = location.pathname.includes('/webapp/editor/');
const EDITOR = ON_SITE ? './' : './editor.html';
const MELODEE = /^FM-1_9\d+$/;
const IS_OTA = id => /^ota-/i.test(id.model);
const runsMelodee = id => MELODEE.test(id.text) && id.text !== HELPER_PRODUCT;
const fail = (code, message, detail = '') => Object.assign(Error(message), { code, detail });
// seconds each phase takes on an FM-1, for the bar and the time left; a timed install replaces the guess
const EST = { helper: 190, measure: 45, return: 190, save: 4 };

const CONN = {
  nomidi: "This browser can't reach USB MIDI devices. Open this page in Chrome or Edge on a computer.",
  denied: 'MIDI access was not allowed. Allow MIDI devices in the site settings (the icon in the address bar), then press Find FM-1 again.',
  notfound: 'No FM-1 answered. Check that it is on, USB HOST is OFF and the cable carries data (not a charging-only one). Close other apps and tabs that use MIDI, then look again.',
  multiple: 'More than one FM-1 is connected. Unplug the others, then look again.',
  busy: 'An installation or recovery is running in another tab. Finish it there first.',
  noreply: "The FM-1 didn't answer the radio check. Stop playback, then look again.",
};
const ERR = {
  notfound: 'No FM-1 answered over USB.', lost: 'The USB connection was lost.', stopped: 'The FM-1 stopped asking for data.',
  noloader: 'The FM-1 in update mode was not found.', noreturn: "The FM-1 didn't come back after restarting.",
  mismatch: e => `Written, but the FM-1 reports another firmware: ${e.detail}.`, badreq: 'The FM-1 asked for data outside the package.',
  wrongloader: e => `The FM-1's update mode (${e.detail}) doesn't take Melodee's firmware.`,
  open: "This FM-1 runs M-VAVE's Open firmware.", model: 'The connected device is not an FM-1.',
  multiple: CONN.multiple, nomidi: CONN.nomidi, denied: CONN.denied,
};
const GO = { calibrate: 'Calibrate Bluetooth', continue: 'Continue calibration', finish: 'Return to Melodee' };

const st = {
  pkgs: null, version: '',                 // the helper and Melodee images this page carries, and Melodee's version
  access: null, dev: null,                 // MIDI access; the FM-1 found: { id, kind, status | helper }
  finding: false, running: false, connErr: null, run: null,
  copy: null, file: '',                    // the calibration measured in this visit, and its download's name
  demo: null,
};
const ver = () => st.version;
const kept = () => { try { return JSON.parse(localStorage.getItem(MARK)); } catch { return null; } };
const mark = value => { try { if (value) localStorage.setItem(MARK, JSON.stringify(value)); else localStorage.removeItem(MARK); } catch { /* not kept */ } };
const keptCopy = () => { try { return readCalibration(kept()?.calibration); } catch { return null; } };
const clock = at => new Date(at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

/* ---- the page's firmware pair: checked before anything asks for MIDI ---- */
async function loadPackages() {
  if (DEMO) {
    st.demo = demoSetup(DEMO, globalThis.MELODEE_CALIBRATION_DEMO);   /* tests: a quicker simulated FM-1 */
    st.pkgs = st.demo.packages;
    $('demo').hidden = false;
    $('demoleave').href = location.pathname;
  } else {
    try { st.pkgs = await calibrationPackages(); }
    catch (e) { pageFail(e.message + ' Saved calibration files can still be restored (below).'); }
  }
  const product = st.pkgs?.firmware.product;
  st.version = (product && releaseVersion(product)) || globalThis.MELODEE_CALIBRATION_BUNDLE?.version || '';
  $('fact-version').hidden = !st.version;
  $('fact-version').textContent = 'Returns to Melodee ' + st.version;
}
function pageFail(text) {
  $('pagefail').hidden = false;
  $('pagefailtext').textContent = text;
}

/* ---- the FM-1 over USB MIDI ---- */
async function midi() {
  if (DEMO) return st.demo.access;
  if (!navigator.requestMIDIAccess) throw fail('nomidi', 'no Web MIDI');
  try { return await navigator.requestMIDIAccess({ sysex: true }); }
  catch (e) { throw fail('denied', e.message); }
}
async function access() { return st.access ||= await midi(); }
// two FM-1s answer alike: a write must go to the one that was found
function onlyOne(a) {
  const outputs = [...a.outputs.values()];
  const n = [...a.inputs.values()].filter(p => p.state !== 'disconnected' && /fm-1|melodee|felucca|ota/i.test(p.name || '') &&
    outputs.some(o => o.name === p.name && o.state !== 'disconnected')).length;
  if (n > 1) throw fail('multiple', 'More than one FM-1 MIDI connection is visible.');
}
async function request(link, [cmd, args], { timeout = 5000 } = {}) {
  link.drain();
  if (!link.send([0xf0, 0x7d, 0x46, 0x4c, cmd, ...args, 0xf7])) throw fail('lost', 'USB connection lost.');
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    const b = await link.read(end - Date.now());
    if (b?.[0] === 0xf0 && b.at(-1) === 0xf7 && b[1] === 0x7d && b[2] === 0x46 && b[3] === 0x4c && b[4] === cmd) return b.slice(5, -1);
  }
  throw Error('No reply from the FM-1. Check the selected firmware and USB connection.');
}
// wait: ms to let an FM-1 that is restarting come back (inside a run)
async function withDevice(fn, wait = 0) {
  const a = await access(), up = new SteadyUpdater(a);
  const d = wait ? await up.waitFor(() => true, wait) : await up.find();
  if (!d) throw fail('notfound', 'FM-1 not found.');
  try { onlyOne(a); return await fn((r, o) => request(d.link, r, o), d.id); }
  finally { d.link.close(); }
}

// what the FM-1 runs, and for Melodee its radio: { kind, status | helper }
async function inspect(req, id) {
  if (IS_OTA(id)) return { kind: 'loader' };
  if (id.open) return { kind: 'open' };
  if (id.model !== 'FM-1') return { kind: 'model' };
  if (id.text === HELPER_PRODUCT) return { kind: 'helper', helper: await helperState(req).catch(() => null) };
  if (MELODEE.test(id.text)) return { kind: 'melodee', status: await calibrationPreflight(req, id) };
  return { kind: /^FM-1_0\d\d$/.test(id.text) && id.text !== 'FM-1_099' ? 'stock' : 'other' };
}

async function connect() {
  if (st.finding || st.running) return;
  st.finding = true;
  st.connErr = null;
  render();
  let release;
  try {
    release = await hold();
    if (!release) throw fail('busy', 'busy');
    st.dev = await withDevice(async (req, id) => ({ id, ...await inspect(req, id) }));
  } catch (e) {
    st.dev = null;
    st.connErr = CONN[e.code] ? e.code : 'noreply';
  } finally {
    release?.();
    st.finding = false;
    render();
  }
}
$('find').addEventListener('click', connect);
$('recheck').addEventListener('click', connect);

/* ---- the page's state, drawn ---- */
// [what it runs, chip, chip tone]
function describe(d) {
  const v = releaseVersion(d.id.text);
  const melodee = d.id.text === 'FM-1_900' ? 'Melodee development firmware (FM-1_900)' : 'Melodee ' + (v || d.id.text);
  if (d.kind === 'loader') return ['In update mode: a recovery is waiting to finish', 'Update mode', 'warn'];
  if (d.kind === 'helper') {
    const what = ['Calibration helper, ready to measure', 'Calibration helper, measuring', 'Calibration helper, result ready',
      'Calibration helper, its last run stopped'][d.helper] || 'Calibration helper';
    return [what, 'Helper', 'warn'];
  }
  if (d.kind === 'melodee') {
    const s = d.status;
    if (s.legacy) return [melodee + ', before radio recovery', 'Radio unknown', 'warn'];
    if (s.radioOn) return [melodee, 'Radio on', 'bad'];
    if (s.calibrated) return s.saved ? [melodee, 'Calibrated', 'ok'] : [melodee, 'Not saved yet', 'warn'];
    return [melodee, 'NO CAL', 'warn'];
  }
  if (d.kind === 'stock') return ['M-VAVE firmware V' + parseInt(d.id.text.slice(5), 10), "Can't calibrate", 'bad'];
  if (d.kind === 'open') return ["M-VAVE's Open firmware", "Can't calibrate", 'bad'];
  if (d.kind === 'model') return [`Not an FM-1 (${d.id.text})`, "Can't calibrate", 'bad'];
  return [`Another firmware (${d.id.text})`, "Can't calibrate", 'bad'];
}

// what pressing the step's button does: { kind: calibrate | continue | finish, ... } or null
function currentPlan() {
  const d = st.dev;
  if (!d || !st.pkgs) return null;
  if (d.kind === 'loader') return { kind: 'finish', copy: !!(st.copy || keptCopy()) };
  if (d.kind === 'helper') return d.helper === 0 ? { kind: 'continue' } : { kind: 'finish', copy: d.helper === 2 || !!st.copy, result: d.helper === 2 };
  if (d.kind === 'melodee' && !d.status.radioOn && !d.status.calibrated) return { kind: 'calibrate', legacy: !!d.status.legacy };
  return null;
}
const calibrated = d => d?.kind === 'melodee' && d.status.calibrated && d.status.saved;

function planText(d, plan) {
  const v = ver(), measures = `The FM-1 restarts into a calibration helper, which measures its radio, then returns to Melodee ${v} with the result.`;
  if (!st.pkgs) return "This page doesn't carry the firmware it needs to calibrate. A saved calibration can still be restored (below).";
  if (!d) return measures + ' Projects and presets stay.';
  if (plan?.kind === 'calibrate')
    return measures + (plan.legacy ? ` This Melodee is older than radio recovery, so it comes back as ${v}.` : ' Projects and presets stay.') +
      ' Back up your sounds first: Settings, then Backup, in the editor.';
  if (plan?.kind === 'continue') return `The calibration helper is waiting. Continuing measures the radio, then returns to Melodee ${v} with the result.`;
  if (plan?.kind === 'finish') {
    if (plan.result) return `The helper has measured the radio. Finishing returns to Melodee ${v} and saves the result there.`;
    if (d.kind === 'helper') return `The helper's last run stopped. Finishing returns to Melodee ${v}; the radio stays as it was. To report it, save recovery diagnostics (below) first.`;
    const at = st.copy ? '' : kept()?.at;
    return `The FM-1 waits in update mode. Finishing writes Melodee ${v} again` +
      (plan.copy ? ` and saves the calibration measured${at ? ' at ' + clock(at) : ''}.` : '. Then restore the downloaded calibration file (below).');
  }
  if (d.kind === 'melodee') {
    if (d.status.radioOn) return 'Switch the radio off first (step 2).';
    if (calibrated(d)) return "This FM-1's radio is calibrated: there's nothing to fix. On the FM-1, open Settings → Bluetooth and turn Radio ON.";
    if (d.status.calibrated) return "The radio calibration is valid, but Melodee hasn't saved it yet. Stop playback, then look again.";
  }
  if (d.kind === 'open') return "This FM-1 runs M-VAVE's Open firmware. Install Melodee first.";
  if (d.kind === 'model') return `This is not an FM-1 (${d.id.text}).`;
  return "This FM-1 doesn't run Melodee. Install Melodee first: NO CAL is Melodee's message.";
}

function render() {
  const d = st.dev, plan = currentPlan(), busy = st.finding || st.running;
  /* step 1 */
  const fb = $('find');
  fb.replaceChildren();
  if (st.finding) { const s = document.createElement('span'); s.className = 'spin'; fb.append(s, 'Looking for the FM-1…'); }
  else fb.append(d || st.connErr ? 'Look again' : 'Find FM-1');
  fb.disabled = busy;
  fb.classList.toggle('primary', !d);
  $('connmsg').textContent = st.connErr ? CONN[st.connErr] : '';
  $('device').hidden = !d;
  if (d) {
    const [what, chip, tone] = describe(d);
    $('devwhat').textContent = what;
    $('devchip').textContent = chip;
    $('devchip').className = 'chip ' + tone;
  }
  $('st-connect').dataset.state = d ? 'done' : 'active';
  /* step 2: only for a Melodee that reports its radio */
  const radio = d?.kind === 'melodee' && !d.status.legacy;
  $('st-radio').hidden = !!d && !radio;
  $('st-radio').dataset.state = !radio ? 'todo' : d.status.radioOn ? 'active' : 'done';
  $('recheck').hidden = !(radio && d.status.radioOn);
  $('recheck').disabled = busy;
  /* step 3 */
  const go = $('go');
  go.textContent = plan ? (GO[plan.kind] + (plan.kind === 'finish' ? ' ' + ver() : '')).trim() : calibrated(d) ? 'Nothing to fix' : GO.calibrate;
  go.disabled = !plan || busy;
  $('gohint').hidden = !!d || !st.pkgs;
  $('plantext').textContent = planText(d, plan);
  $('st-run').dataset.state = plan ? 'active' : calibrated(d) ? 'done' : 'todo';
  /* below: a saved file, a reinstall, the helper's record */
  $('import').disabled = !$('backup').files?.length || busy;
  $('finish').disabled = !st.pkgs || busy;
  $('finish').textContent = ('Reinstall Melodee ' + ver()).trim();
  $('diagnose').disabled = busy;
  /* an unfinished run: kept from an earlier visit, or the FM-1 itself */
  const k = kept();
  if (k && !st.running && d?.kind === 'melodee' && (!k.calibration || calibrated(d))) mark(null);   /* it finished after all */
  const left = kept(), waiting = ['loader', 'helper'].includes(d?.kind);
  $('resumebanner').hidden = st.running || !(left || waiting);
  $('resumetext').textContent = d?.kind === 'melodee' && left?.calibration
    ? `A calibration measured at ${clock(left.at)} wasn't saved. Restore it from the downloaded file under “Use a saved calibration”.`
    : 'Your FM-1 may still run the calibration helper or wait in update mode. Connect it and press Find FM-1 to return to Melodee.';
}

/* ---- before writing: the rules, and the backup ---- */
function preflight(plan) {
  const d = $('preflight');
  const [min, how] = {
    calibrate: [7, 'The FM-1 restarts twice: into the calibration helper, then back into Melodee. Its screen shows UPDATING or CALIBRATING, or stays dark. All are normal.'],
    continue: [4, 'The helper measures (its screen shows CALIBRATING), then the FM-1 restarts into Melodee. UPDATING or a dark screen is normal.'],
    finish: [3, 'The FM-1 restarts into Melodee. Its screen shows UPDATING, or stays dark. Both are normal.'],
  }[plan.kind];
  $('pftime').textContent = `It takes about ${min} minutes`;
  $('pftimetext').textContent = how;
  $('pffile').hidden = plan.kind === 'finish' && !plan.result;
  $('pfgo').textContent = { calibrate: 'Start calibration', continue: 'Continue', finish: 'Return to Melodee' }[plan.kind];
  const askBackup = plan.kind === 'calibrate';
  $('pfcheck').hidden = !askBackup;
  $('pfok').checked = false;
  $('pfgo').disabled = askBackup;
  d.showModal();
  (askBackup ? $('pfok') : $('pfgo')).focus();
  /* answered by the button itself (submit), as the installer: a hidden page may hold back the close event */
  return new Promise(resolve => {
    const form = d.querySelector('form');
    const done = go => {
      form.removeEventListener('submit', submit);
      d.removeEventListener('cancel', cancel);
      d.close();
      resolve(go);
    };
    const submit = e => { e.preventDefault(); if (e.submitter?.value !== 'go' || !askBackup || $('pfok').checked) done(e.submitter?.value === 'go'); };
    const cancel = e => { e.preventDefault(); done(false); };
    form.addEventListener('submit', submit);
    d.addEventListener('cancel', cancel);
  });
}
$('pfok').addEventListener('change', () => { $('pfgo').disabled = !$('pfok').checked; });

/* ---- the run's screen ---- */
const PHASES = { calibrate: ['helper', 'measure', 'return', 'save'], continue: ['measure', 'return', 'save'], save: ['save'] };
const ICON = { done: '#i-check', nothing: '#i-check', returned: '#i-check', paused: '#i-pause', safe: '#i-info',
  restart: '#i-alert', partial: '#i-alert', failed: '#i-alert' };
const FAVICON = c => `data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='4' fill='${c}'/></svg>`)}`;
const baseTitle = document.title;

function log(line) {
  const el = $('log'), atEnd = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
  el.append(line + '\n');
  if (atEnd) el.scrollTop = el.scrollHeight;
}

function openRun(plan) {
  const list = PHASES[plan.kind] || (plan.copy ? ['return', 'save'] : ['return']);
  st.run = { plan, list, pct: 0, phase: null, lastAt: Date.now(), hiddenPct: 0, writeAt: 0, installSec: 0, file: '', diag: '' };
  const d = $('run');
  d.dataset.state = 'running';
  $('end').hidden = true;
  $('log').textContent = '';
  $('backnote').hidden = $('stall').hidden = true;
  $('eta').textContent = '';
  $('runtitle').textContent = { calibrate: 'Calibrating Bluetooth', continue: 'Calibrating Bluetooth', save: 'Saving the radio calibration' }[plan.kind] ||
    'Returning to Melodee ' + ver();
  document.querySelectorAll('#phases li').forEach(li => { li.hidden = !list.includes(li.dataset.ph); li.className = ''; });
  setPct(0);
  setPhase(list[0]);
  setStep('Starting');
  $('favicon').href = FAVICON('#d08a00');
  if (!d.open) d.showModal();
}
$('run').addEventListener('cancel', e => { if (st.running) e.preventDefault(); });

const est = p => (p === 'helper' || p === 'return') && st.run.installSec ? st.run.installSec : EST[p];
// a phase's share of the bar, by the seconds each phase takes: f (0..1) of it -> percent
function within(phase, f) {
  const list = st.run.list, total = list.reduce((s, p) => s + est(p), 0);
  let from = 0;
  for (const p of list) { if (p === phase) break; from += est(p); }
  return (from + est(phase) * Math.min(1, Math.max(0, f))) / total * 100;
}
function setPct(p) {
  const r = st.run;
  r.pct = Math.max(r.pct, Math.min(100, Math.floor(p)));
  $('pct').textContent = r.pct;
  $('barfill').style.width = r.pct + '%';
  $('bar').setAttribute('aria-valuenow', r.pct);
  document.title = document.visibilityState === 'hidden' ? `⚠ Come back: recovering, ${r.pct}%` : `${r.pct}% · Bluetooth recovery`;
}
function setPhase(phase) {
  const r = st.run;
  if (phase === r.phase) return;
  r.phase = phase;
  const at = r.list.indexOf(phase);
  document.querySelectorAll('#phases li').forEach(li => {
    const i = r.list.indexOf(li.dataset.ph);
    li.className = i < 0 ? '' : i < at ? 'past' : i === at ? 'now' : '';
  });
}
function setStep(text) { $('runstep').textContent = text; }
// time left: this phase's seconds, then the guesses for the phases after it
function eta(seconds) {
  const r = st.run, s = seconds + r.list.slice(r.list.indexOf(r.phase) + 1).reduce((n, p) => n + est(p), 0);
  $('eta').textContent = s >= 60 ? `About ${Math.ceil(s / 60)} min left` : 'Less than a minute left';
}

// fm1ota.js's steps for one install -> the screen
function stepper(phase, pm, what, onStaged) {
  const r = st.run;
  return (k, a, b) => {
    log(`${k} ${a ?? ''}${b != null ? ' ' + b : ''}`);
    r.lastAt = Date.now();
    $('stall').hidden = true;
    if (k === 'start') setStep('Starting');
    else if (k === 'verify') { setStep('Checking the package'); setPct(within(phase, pm.verify(b) / 100)); }
    else if (k === 'loader') { onStaged(); setStep('Switching the FM-1 to update mode'); }
    else if (k === 'write') {
      if (!r.writeAt) r.writeAt = Date.now() - 1;
      setStep('Writing ' + what);
      setPct(within(phase, pm.writing(b) / 100));
      const s = remaining(b, pm.write, Date.now() - r.writeAt);
      if (s == null) $('eta').textContent = ''; else eta(s + 15);
    } else if (k === 'reboot') {
      $('eta').textContent = '';
      setStep(phase === 'helper' ? 'Starting the calibration helper' : 'Restarting into Melodee');
      setPct(within(phase, 0.99));
    } else if (k === 'done') setPct(within(phase, 1));
  };
}

// no progress for a while during a transfer: say what the page is waiting for (UI only)
setInterval(() => {
  const r = st.run;
  if (!st.running || !r || !['helper', 'return'].includes(r.phase)) return;
  $('stall').hidden = Date.now() - r.lastAt < 12000;
}, 1000);

const away = ms => { const s = Math.round(ms / 1000); return s < 60 ? s + ' s' : Math.round(s / 60) + ' min'; };
const onHidden = () => {
  const r = st.run;
  if (!r) return;
  r.hiddenPct = r.pct;
  document.title = `⚠ Come back: recovering, ${r.pct}%`;
};
const onVisible = ms => {
  const r = st.run;
  if (!r || !st.running) return;
  document.title = `${r.pct}% · Bluetooth recovery`;
  if (ms < 1500) return;
  const going = r.pct > r.hiddenPct || r.phase === 'measure';
  $('backtext').textContent = going ? `You left this tab for ${away(ms)}. The recovery kept going, but please stay here until it's done.`
    : `You left this tab for ${away(ms)}, and the browser slowed the page. It continues now: please stay on this tab.`;
  $('backnote').hidden = false;
};

function reasonText(e) {
  const c = e.cause || e, m = ERR[c.code];
  return typeof m === 'function' ? m(c) : m || c.message;
}

function endRun({ state, error, saved }, plan) {
  const r = st.run, v = ver(), d = $('run');
  d.dataset.state = { nothing: 'done', returned: 'done' }[state] || state;
  $('end').hidden = false;
  $('endicon').setAttribute('href', ICON[state]);
  const tips = [], acts = [];
  const button = (text, primary, fn) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'btn' + (primary ? ' primary' : '');
    b.textContent = text;
    b.addEventListener('click', fn);
    acts.push(b);
  };
  const reason = error ? reasonText(error) : '';
  const radioOn = 'On the FM-1, open Settings → Bluetooth and turn Radio ON.';
  let title, text;
  if (state === 'done') {
    title = plan.kind === 'save' ? 'Calibration restored' : 'Bluetooth is calibrated';
    text = plan.kind === 'save' ? 'Melodee confirmed the radio calibration and saved it.'
      : `Melodee ${v} is running again, with this FM-1's own radio calibration.`;
    tips.push(radioOn);
    const file = r.file || (plan.kind !== 'save' && st.file);
    if (file) tips.push(`Keep ${file} (downloaded): it restores this calibration if it's ever lost.`);
    const a = Object.assign(document.createElement('a'), { className: 'btn primary', href: EDITOR, textContent: 'Open the editor' });
    a.style.textDecoration = 'none';
    acts.push(a);
    button('Close', false, closeRun);
    setPct(100);
  } else if (state === 'nothing') {
    title = 'Nothing to fix';
    text = saved ? "This FM-1's radio is already calibrated. Nothing was changed." : "The radio calibration is valid, but Melodee hasn't saved it yet. Nothing was changed.";
    tips.push(saved ? radioOn : 'Stop playback so Melodee can save it, then look again.');
    button('Close', true, closeRun);
  } else if (state === 'returned') {
    title = `Melodee ${v} is back`;
    text = "The FM-1 runs Melodee again. Its radio calibration wasn't changed.";
    tips.push('To fix NO CAL, press Find FM-1 and calibrate, or restore a saved calibration file.');
    button('Close', true, closeRun);
  } else if (state === 'failed') {
    title = "Calibration didn't finish";
    text = `Melodee ${v} is running again. The radio calibration wasn't changed.`;
    tips.push(reason, r.diag ? `The helper's record was saved as ${r.diag}. Please send it with a bug report.` : "The helper's record couldn't be read.");
    button('Close', true, closeRun);
  } else if (state === 'paused') {
    title = 'Recovery paused';
    text = 'The FM-1 is safe. Until Melodee is back, it starts the calibration helper or waits in update mode, even after switching it off.';
    tips.push(reason, error?.calibrationError && 'Calibration had already stopped: ' + error.calibrationError.message,
      r.file && `The measured calibration was downloaded as ${r.file}, and this page keeps it too.`,
      'Check the cable, or plug it in again.', 'Close other music apps and editor tabs.',
      st.copy ? 'Then press Resume. It writes Melodee again from the start, then saves the calibration.' : 'Then press Resume. It writes Melodee again from the start.');
    button('Resume', true, () => { closeRun(); start({ kind: 'finish', copy: !!st.copy }); });
    button('Close', false, closeRun);
  } else if (state === 'restart') {
    title = "The FM-1 didn't come back";
    text = (error?.cause || error)?.code === 'mismatch' ? reason : "Melodee was written, but the FM-1 didn't report back. Switch it off and on again.";
    tips.push("If it shows UPDATING or stays dark, it's still in update mode: press Find FM-1 and finish there.");
    if (r.file) tips.push(`If the page doesn't offer the calibration then, restore ${r.file} under “Use a saved calibration”.`);
    button('Close', true, closeRun);
  } else if (state === 'partial') {
    title = "Melodee is back, the calibration isn't saved";
    text = `Melodee ${v} is running, but it didn't confirm the new calibration.`;
    tips.push(reason, 'Stop playback and release all notes, then press Try saving again.');
    if (r.file) tips.push(`It's also in ${r.file}: restore it later under “Use a saved calibration”.`);
    button('Try saving again', true, () => { closeRun(); start({ kind: 'save', copy: st.copy, retry: true }); });
    button('Close', false, closeRun);
  } else {
    title = 'Nothing was changed';
    text = plan.kind === 'save' ? "The radio calibration wasn't saved." : plan.kind === 'finish' ? 'Nothing was written to the FM-1.'
      : 'The FM-1 still runs its previous firmware.';
    tips.push(reason, 'Check the cable, or plug it in again.', 'Close other music apps and editor tabs.');
    button('Try again', true, () => { closeRun(); if (plan.kind === 'save') start(plan); else connect(); });
    button('Close', false, closeRun);
  }
  $('endtitle').textContent = title;
  $('endtext').textContent = text;
  $('endtips').replaceChildren(...tips.filter(Boolean).map(x => Object.assign(document.createElement('li'), { textContent: x })));
  $('endactions').replaceChildren(...acts);
  const good = ['done', 'nothing', 'returned'].includes(state);
  document.title = good ? 'Done · Bluetooth recovery' : baseTitle;
  $('favicon').href = FAVICON(good ? '#1f8a46' : state === 'safe' ? '#777' : '#d08a00');
  $('status').textContent = title;
  (acts[0] || $('endtitle')).focus();
}

function closeRun() {
  $('run').close();
  document.title = baseTitle;
  render();
}

/* ---- the runs ---- */
// a file the player keeps; the demo's calibration is never offered, so it can't reach a real FM-1
function download(file, name) {
  const filename = `${name}-${new Date().toISOString().slice(0, 10)}.json`;
  if (DEMO) { log('demo: not downloaded ' + filename); return filename; }
  const url = URL.createObjectURL(new Blob([JSON.stringify(file, null, 2) + '\n'], { type: 'application/json' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
  return filename;
}
function keep(copy) {
  const file = calibrationFile(copy, 'sdk-first-boot');
  st.copy = copy;
  st.file = st.run.file = download(file, 'Melodee-radio-calibration');
  mark({ at: Date.now(), calibration: file });
}

// one firmware over the FM-1's update route; onStaged: the FM-1 has left the running firmware
async function install(pkg, phase, what, onStaged = () => {}) {
  const r = st.run, a = await access(), up = new SteadyUpdater(a, log);
  const d = await up.waitFor(() => true, 8000);
  if (!d) throw fail('notfound', 'FM-1 not found.');
  try { onlyOne(a); } catch (e) { d.link.close(); throw e; }
  const updating = IS_OTA(d.id), t0 = Date.now();
  d.link.close();
  setPhase(phase);
  r.writeAt = 0;
  r.lastAt = Date.now();
  const step = stepper(phase, progressModel(pkg.image, updating), what, onStaged);
  if (updating) {
    onStaged();
    if (d.id.text !== MELODEE_LOADER) throw fail('wrongloader', `This package requires ${MELODEE_LOADER}; found ${d.id.text}`, d.id.text);
    setStep('Finding the FM-1 in update mode');
    if (!await up.resume(pkg.image, step, pkg.product, MELODEE_LOADER)) throw fail('stopped', 'The update did not finish.');
  } else {
    await up.install(pkg.image, pkg.product, step, MELODEE_LOADER);
    r.installSec = (Date.now() - t0) / 1000;
  }
}

async function measure() {
  const r = st.run, t0 = Date.now();
  setPhase('measure');
  setStep('Measuring the radio');
  const tick = () => {
    const s = (Date.now() - t0) / 1000;
    r.lastAt = Date.now();
    setPct(within('measure', Math.min(0.95, s / EST.measure)));
    $('eta').textContent = `${Math.round(s)} s · usually under a minute, at most 80 s`;
  };
  tick();
  const ticker = setInterval(tick, 500);
  try {
    return await withDevice((req, id) => {
      if (id.text !== HELPER_PRODUCT) throw fail('nohelper', 'The calibration helper is not running.');
      return generateCalibration(req);
    }, 8000);
  } catch (e) {
    // the helper's own record of the run, saved beside the error
    try {
      const trace = await withDevice(req => helperTrace(req), 4000);
      r.diag = download(trace, 'Melodee-radio-diagnostics');
      e.message += ` Stage ${trace.stage} (${trace.stage_name}), ${trace.elapsed_ms} ms.`;
    } catch { /* the helper may have reset: Save recovery diagnostics reads it later */ }
    throw e;
  } finally { clearInterval(ticker); $('eta').textContent = ''; }
}

async function save(copy) {
  setPhase('save');
  setStep('Saving the calibration in Melodee');
  await withDevice((req, id) => {
    if (!runsMelodee(id)) throw fail('notmelodee', `The FM-1 runs ${id.text}, not Melodee.`);
    return restoreCalibration(req, copy);
  }, 8000);
  setPct(100);
  mark(null);
}

// helper, measure, return, save; continuing starts in the helper. Always back to Melodee once it has left
async function calibrate(plan) {
  const { helper, firmware } = st.pkgs;
  let staged = plan.kind === 'continue', returned = false, measured = false;
  if (plan.kind === 'calibrate') {
    setStep('Checking the radio');
    const before = await withDevice((req, id) => calibrationPreflight(req, id), 4000);
    if (before.calibrated) return { state: 'nothing', saved: before.saved };
    if (before.radioOn) throw Error('The radio is on. On the FM-1, open Settings → Bluetooth and turn Radio OFF.');
  }
  mark({ at: Date.now() });
  try {
    await recoverDevice({
      installHelper: () => plan.kind === 'calibrate' && install(helper, 'helper', 'the calibration helper', () => { staged = true; }),
      generate: measure,
      save: file => { measured = true; keep(readCalibration(file)); },
      installMelodee: async () => {
        // nothing was written: Melodee never left
        if (!staged && await withDevice(async (req, id) => runsMelodee(id), 4000).catch(() => false)) return;
        await install(firmware, 'return', 'Melodee ' + ver());
        returned = true;
      },
      restore: save,
    });
  } catch (e) {
    if (!staged) return { state: 'safe', error: e };
    if (!returned) return { state: ['noreturn', 'mismatch'].includes(e.cause?.code) ? 'restart' : 'paused', error: e };
    return { state: measured ? 'partial' : 'failed', error: e };
  }
  return { state: 'done' };
}

// back to Melodee from the helper or update mode, then the calibration: the helper's own result, or this visit's
async function finish() {
  let copy = st.copy || keptCopy(), staged = false;
  setStep('Looking for the FM-1');
  const fromHelper = await withDevice(async (req, id) => {
    staged = !runsMelodee(id);                        /* the helper or update mode; a running Melodee is only reinstalled */
    return id.text === HELPER_PRODUCT && await helperState(req) === 2 ? helperResult(req) : null;
  }, 8000).catch(() => null);
  if (fromHelper) { copy = fromHelper; keep(copy); }
  try { await install(st.pkgs.firmware, 'return', 'Melodee ' + ver(), () => { staged = true; }); }
  catch (e) { return { state: !staged ? 'safe' : ['noreturn', 'mismatch'].includes(e.code) ? 'restart' : 'paused', error: e }; }
  if (!copy) { mark(null); return { state: 'returned' }; }
  try { await save(copy); } catch (e) { return { state: 'partial', error: e }; }
  return { state: 'done' };
}

async function start(plan = currentPlan()) {
  if (st.running || st.finding || !plan || (plan.kind !== 'save' && !st.pkgs)) return;
  st.running = true;                                  /* from the first click: a second one starts nothing */
  render();
  let release;
  try {
    release = await hold({ leaveText: 'Bluetooth recovery is running. Leaving now can stop it.', onHidden, onVisible });
    if (release && plan.kind !== 'save' && !await preflight(plan)) { release(); release = false; }
  } catch (e) { st.running = false; render(); throw e; }
  if (!release) {
    st.running = false;
    if (release === null) st.connErr = 'busy';
    render();
    return;
  }
  openRun(plan);
  let result;
  try {
    result = await (plan.kind === 'finish' ? finish() : plan.kind === 'save' ? save(plan.copy).then(() => ({ state: 'done' })) : calibrate(plan));
  } catch (e) { result = { state: plan.retry ? 'partial' : 'safe', error: e }; }
  if (result.error) log(String(result.error.stack || result.error));
  if (plan.kind !== 'save' && ['safe', 'failed', 'nothing'].includes(result.state)) mark(null);
  st.running = false;
  release();
  st.dev = null;                                      /* what it runs now is shown on the next Find */
  endRun(result, plan);
  render();
}
$('go').addEventListener('click', () => start());
$('finish').addEventListener('click', () => start({ kind: 'finish', copy: !!(st.copy || keptCopy()) }));

/* ---- a saved calibration: this FM-1's file, or its full-flash backup ---- */
$('backup').addEventListener('change', () => { $('importstatus').textContent = ''; render(); });
$('import').addEventListener('click', async () => {
  const file = $('backup').files[0];
  let copy;
  try {
    if (!file || file.size > 0x100000) throw Error('Choose this FM-1’s calibration file or its 1 MiB full-flash backup.');
    copy = file.size === 0x100000 ? extractCalibration(new Uint8Array(await file.arrayBuffer())) : readCalibration(JSON.parse(await file.text()));
  } catch (e) {
    $('importstatus').textContent = e instanceof SyntaxError ? 'Not a Melodee radio calibration file.' : e.message;
    return;
  }
  $('importstatus').textContent = '';
  start({ kind: 'save', copy });
});

/* ---- the helper's record of its last run ---- */
$('diagnose').addEventListener('click', async () => {
  if (st.running || st.finding) return;
  const say = text => { $('diagstatus').textContent = text; };
  const release = await hold();
  if (!release) { say(CONN.busy); return; }
  st.running = true;
  render();
  say('Reading…');
  try {
    const trace = await withDevice((req, id) => {
      if (id.text !== HELPER_PRODUCT) throw fail('nohelper', `The FM-1 runs ${id.text}, not the calibration helper, so there is no record to read.`);
      return helperTrace(req);
    });
    say(`Saved ${download(trace, 'Melodee-radio-diagnostics')}: stage ${trace.stage} (${trace.stage_name}), ${trace.elapsed_ms} ms, caller ${trace.caller_hex}.`);
  } catch (e) { say(CONN[e.code] || e.message); }
  finally { release(); st.running = false; render(); }
});

/* ---- start ---- */
for (const id of ['brand', 'nav-editor']) $(id).href = EDITOR;
$('nav-install').hidden = $('nav-manual').hidden = !ON_SITE;
(async () => {
  await loadPackages();
  render();
  /* MIDI already allowed (a returning visit, or a reload during a run): look without asking */
  let granted = !!DEMO && params.has('auto');
  try {
    if (!DEMO && navigator.permissions && navigator.requestMIDIAccess)
      granted = (await navigator.permissions.query({ name: 'midi', sysex: true })).state === 'granted';
  } catch { /* unknown: wait for the button */ }
  if (granted) connect();
})();
