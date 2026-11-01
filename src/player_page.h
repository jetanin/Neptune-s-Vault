#pragma once

const char PLAYER_PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Neptune's Vault | Players</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#071522;color:#e7f3fa}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px}
main{width:min(100%,760px)}header{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:32px}
.brand{font-weight:800;letter-spacing:.08em}.connection{font-size:.8rem;color:#a8c6d6;text-align:right}
.panel{background:#102738;border:1px solid #284858;border-radius:24px;padding:clamp(24px,5vw,44px)}
.eyebrow{color:#71d4e5;font-size:.8rem;letter-spacing:.15em}h1{font-size:clamp(2rem,6vw,3.2rem);margin:14px 0}
.message{color:#b9d0dd;line-height:1.7;min-height:3.4em}.metrics{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:28px}
.metric{background:#091d2a;border-radius:16px;padding:20px}.label{color:#a8c6d6;font-size:.9rem}
.value{font-size:clamp(2rem,7vw,3.5rem);font-weight:750;font-variant-numeric:tabular-nums;margin-top:6px}
.attempt-summary{margin-top:24px;padding:24px;border:2px solid #e9a34d;border-radius:20px;background:#2b221b;text-align:center}
.attempt-label{font-size:clamp(1.1rem,4vw,1.5rem);font-weight:800;color:#ffdaaa}
.attempt-count{font-size:clamp(3.5rem,15vw,6rem);font-weight:850;line-height:1.15;font-variant-numeric:tabular-nums;color:#fff0dc;margin:8px 0}
.remaining{font-size:clamp(1rem,3.5vw,1.25rem);font-weight:650;color:#ffdaaa;margin:12px 0 0}
.attempt-summary[data-warning="true"]{border-color:#ff8d8d;background:#351d26}.attempt-summary[data-warning="true"] .attempt-count{color:#ffaaaa}
.attempts{margin-top:16px;display:flex;gap:10px;justify-content:center;flex-wrap:wrap}.attempt{width:56px;height:12px;background:#61dbbc;border-radius:8px}.used{background:#f58989}
.help{margin-top:28px;padding-top:20px;border-top:1px solid #284858;color:#a8c6d6;line-height:1.8;font-size:.9rem}
.switch-panel{margin-top:24px}.switch-panel h2{font-size:1.1rem;margin:0 0 14px}
.switches{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:8px;list-style:none;padding:0;margin:0}
.switch{border:1px solid #416273;border-radius:14px;padding:16px 4px;text-align:center;background:#091d2a;color:#b9d0dd}
.switch-name{font-size:.85rem;font-weight:700}.switch-state{display:block;margin-top:8px;font-weight:850;font-size:clamp(1rem,4vw,1.5rem)}
.switch[data-on="true"]{background:#124638;border-color:#61dbbc;color:#9cf5d9}
.switch-note{color:#a8c6d6;font-size:.85rem;line-height:1.6;margin:12px 0 0}
main[data-state="SOLVED"] h1{color:#61dbbc}main[data-state="FAILED"] h1{color:#f58989}
.offline{color:#ffc979}footer{margin-top:18px;text-align:center;color:#87a7b8;font-size:.8rem}
dialog{margin:auto;width:min(440px,calc(100% - 32px));padding:32px;border:1px solid #416273;border-radius:24px;background:#102738;color:#e7f3fa;text-align:center;box-shadow:0 24px 80px #0008}
dialog::backdrop{background:#020b16cc}dialog h2{font-size:2rem;margin:12px 0}dialog p{color:#b9d0dd;line-height:1.7}
dialog[data-result="SOLVED"] h2{color:#61dbbc}dialog[data-result="FAILED"] h2{color:#f58989}
dialog button{margin-top:12px;padding:12px 28px;border:0;border-radius:12px;background:#71d4e5;color:#071522;font:inherit;font-weight:700;cursor:pointer}
dialog button:focus-visible{outline:3px solid white;outline-offset:4px}
@media(max-width:420px){body{padding:16px}.metrics{gap:10px}.metric{padding:14px}header{align-items:flex-start}.brand{font-size:.85rem}}
</style></head><body>
<main id="game" data-state="SETUP">
<header><span class="brand">NEPTUNE'S VAULT</span><span id="connection" class="connection" role="status">Connecting…</span></header>
<section class="panel">
<div class="eyebrow">SAFE CHALLENGE · PLAYER VIEW</div>
<div aria-live="polite"><h1 id="title">Loading game status</h1><p id="message" class="message">Connect to the event WiFi to view the game.</p></div>
<div class="metrics">
<div class="metric"><div class="label">Current group</div><div id="group" class="value">—</div></div>
<div class="metric"><div class="label">Elapsed time</div><div id="elapsed" class="value">00:00</div></div>
</div>
<section id="attemptSummary" class="attempt-summary" aria-labelledby="attemptLabel" aria-live="polite" aria-atomic="true">
<div id="attemptLabel" class="attempt-label">Incorrect attempts</div>
<div id="incorrectCount" class="attempt-count">—</div>
<div id="attempts" class="attempts" aria-hidden="true"></div>
<p id="remaining" class="remaining">Waiting for staff to start the round</p>
</section>
<section class="switch-panel" aria-labelledby="switchTitle">
<h2 id="switchTitle">Current switches</h2>
<ul class="switches" aria-label="Physical switch states">
<li id="switch4" class="switch"><span class="switch-name">SW5</span><span class="switch-state">—</span></li>
<li id="switch3" class="switch"><span class="switch-name">SW4</span><span class="switch-state">—</span></li>
<li id="switch2" class="switch"><span class="switch-name">SW3</span><span class="switch-state">—</span></li>
<li id="switch1" class="switch"><span class="switch-name">SW2</span><span class="switch-state">—</span></li>
<li id="switch0" class="switch"><span class="switch-name">SW1</span><span class="switch-state">—</span></li>
</ul>
<p id="switchNote" class="switch-note" role="status">Waiting for switch status…</p>
</section>
<div class="help">How to play: Set all 5 switches to your calculated code, then press the confirm button on the box.<br>This page updates automatically.</div>
</section>
<footer>Crack the code · Unlock the vault · Claim the treasure</footer>
</main>
<dialog id="resultDialog" aria-labelledby="resultTitle" aria-describedby="resultMessage">
<div class="eyebrow">ROUND RESULT</div>
<h2 id="resultTitle"></h2><p id="resultMessage"></p>
<form method="dialog"><button autofocus>OK</button></form>
</dialog>
<script>
const views = {
  SETUP: ['Get ready', 'Wait for staff to select your group and start the round.'],
  PLAYING: ['Challenge in progress', 'Calculate the code, set the switches, then press confirm on the box.'],
  SOLVED: ['Vault unlocked!', 'Correct code! You can now open the vault.'],
  FAILED: ['No attempts left', 'Wait for staff to reset the game before trying again.']
};
const el = id => document.getElementById(id);
let lastResultKey = null;
function renderSwitches(switches) {
  const available = Array.isArray(switches) && switches.length === 5;
  const active = [];
  for (let i = 4; i >= 0; i--) {
    const card = el('switch' + i);
    const on = available && switches[i] === 1;
    card.dataset.on = String(on);
    card.querySelector('.switch-state').textContent = available ? (on ? 'ON' : 'OFF') : '—';
    if (on) active.push('SW' + (i + 1));
  }
  el('switchNote').textContent = !available ? 'Switch status unavailable' :
    active.length ? 'ON: ' + active.join(', ') : 'All switches OFF';
}
function updateResult(d, view) {
  const dialog = el('resultDialog');
  if (d.state !== 'SOLVED' && d.state !== 'FAILED') {
    lastResultKey = null;
    if (dialog.open) dialog.close();
    return;
  }
  const key = d.group + ':' + d.state;
  if (key === lastResultKey) return;
  lastResultKey = key;
  dialog.dataset.result = d.state;
  el('resultTitle').textContent = d.state === 'SOLVED' ? 'Challenge completed!' : 'Challenge failed';
  el('resultMessage').textContent = 'Group ' + d.group + ' · ' + view[1];
  if (!dialog.open) dialog.showModal();
}
function render(d) {
  renderSwitches(d.switches);
  const view = views[d.state] || views.SETUP;
  el('game').dataset.state = d.state;
  el('title').textContent = view[0];
  el('message').textContent = view[1];
  el('group').textContent = d.group > 0 ? String(d.group) : '—';
  const seconds = Math.max(0, Math.floor(d.elapsed));
  el('elapsed').textContent = String(Math.floor(seconds / 60)).padStart(2, '0') + ':' + String(seconds % 60).padStart(2, '0');
  const remaining = Math.max(0, d.max_wrong - d.wrong);
  el('incorrectCount').textContent = d.state === 'SETUP' ? '—' : d.wrong + ' / ' + d.max_wrong;
  el('attemptSummary').dataset.warning = String(d.state !== 'SETUP' && remaining <= 1);
  el('remaining').textContent = d.state === 'SETUP' ? 'Waiting for staff to start the round' : 'Attempts remaining: ' + remaining;
  el('attempts').replaceChildren();
  if (d.state !== 'SETUP') {
    for (let i = 0; i < d.max_wrong; i++) {
      const marker = document.createElement('span');
      marker.className = 'attempt' + (i < d.wrong ? ' used' : '');
      el('attempts').appendChild(marker);
    }
  }
  updateResult(d, view);
}
async function refresh() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  try {
    const response = await fetch('/player-status', {cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error('status');
    render(await response.json());
    el('connection').textContent = 'Connected';
    el('connection').className = 'connection';
  } catch (_) {
    renderSwitches(null);
    el('connection').textContent = 'Disconnected · Retrying';
    el('connection').className = 'connection offline';
    el('title').textContent = 'Waiting for connection';
    el('message').textContent = 'Status may be out of date. Please check your connection to the event WiFi.';
  } finally {
    clearTimeout(timeout);
    setTimeout(refresh, 500);
  }
}
refresh();
</script></body></html>
)HTML";
