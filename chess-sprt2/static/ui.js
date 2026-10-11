/* Shared helpers, components, settings form builder, and the game viewer. */
const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const main = $('#main');

const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmt = (n) => (n == null ? '–' : Number(n).toLocaleString());

async function api(method, url, body) {
  const opt = { method, headers: {} };
  if (body instanceof FormData) opt.body = body;
  else if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
  const r = await fetch(url, opt);
  let data = null;
  try { data = await r.json(); } catch (e) { /* no body */ }
  if (!r.ok) {
    const err = new Error((data && data.error) || r.statusText);
    err.status = r.status; err.data = data || {};
    throw err;
  }
  return data;
}

function toast(msg, kind = '') {
  const el = document.createElement('div');
  el.className = 'toast ' + kind;
  el.textContent = msg;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), kind === 'bad' ? 7000 : 3500);
}

/* ---------------------------------------------------------------- polling */
const state = { timer: null, refs: null };
function stopPolling() { if (state.timer) clearInterval(state.timer); state.timer = null; }
function poll(fn, ms = 1500) {
  stopPolling();
  state.timer = setInterval(() => { if (!document.hidden) fn().catch(() => {}); }, ms);
}

async function loadRefs() {
  const [engines, experiments, settings, system] = await Promise.all([
    api('GET', '/api/engines'), api('GET', '/api/experiments'), api('GET', '/api/settings'), api('GET', '/api/system'),
  ]);
  state.refs = { engines, experiments, settings, system };
  return state.refs;
}

/* ---------------------------------------------------------------- formatting */
function eloText(t) {
  if (t.elo == null) return '–';
  const e = t.elo;
  if (Math.abs(e) >= 1000) return (e > 0 ? '+' : '−') + '1000+';
  let s = (e > 0 ? '+' : e < 0 ? '−' : '') + Math.abs(e).toFixed(1);
  if (t.elo_margin != null) s += ` ± ${t.elo_margin.toFixed(1)}`;
  return s;
}
function ago(ts) {
  if (!ts) return '–';
  const s = Math.max(0, Date.now() / 1000 - ts);
  if (s < 60) return 'just now';
  if (s < 3600) return Math.floor(s / 60) + ' min ago';
  if (s < 86400) return Math.floor(s / 3600) + ' h ago';
  return Math.floor(s / 86400) + ' d ago';
}
function clock(ts) { return new Date(ts * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }); }
function dur(sec) {
  if (sec == null) return '–';
  sec = Math.round(sec);
  if (sec < 90) return sec + ' s';
  if (sec < 5400) return Math.round(sec / 60) + ' min';
  return (sec / 3600).toFixed(1) + ' h';
}
const VERDICT = { better: 'Definitely better', worse: 'Definitely worse', equal: 'Equal', inconclusive: 'Inconclusive' };
const STATUS = { running: 'Running', queued: 'Queued', paused: 'Paused', pausing: 'Pausing', finished: 'Finished', aborted: 'Aborted' };
const TERM = {
  checkmate: 'Checkmate', stalemate: 'Stalemate', threefold_repetition: 'Threefold repetition', fifty_move_rule: '50-move rule',
  insufficient_material: 'Insufficient material', max_plies: 'Move limit', illegal_move: 'Illegal move', crash: 'Crash',
  timeout: 'Hang (timeout)', adjudication_win: 'Adjudicated win', adjudication_draw: 'Adjudicated draw',
};
const verdictBadge = (v) => `<span class="badge ${v}">${VERDICT[v] || v}</span>`;
const statusBadge = (s) => `<span class="badge ${s}">${STATUS[s] || s}</span>`;

function resultBar(w, d, l, big = false) {
  const n = w + d + l;
  const pct = (x) => (n ? Math.round((x / n) * 100) : 0);
  const seg = (cls, x) => (x ? `<i class="${cls}" style="flex:${x}">${big ? `${x} · ${pct(x)}%` : ''}</i>` : '');
  return `<div class="rbar ${big ? 'big' : ''}" title="${w} wins, ${d} draws, ${l} losses (candidate)">${seg('w', w)}${seg('d', d)}${seg('l', l)}</div>`;
}

function gauge(label, hint, llr, lower, upper) {
  const pct = Math.max(0, Math.min(100, ((llr - lower) / (upper - lower)) * 100));
  return `<div class="gauge"><div class="gauge-top"><span>${label}</span><span class="mono">${llr.toFixed(2)}</span></div>
    <div class="gtrack"><i style="left:${pct}%"></i></div>
    <div class="gscale mono"><span>${lower.toFixed(2)}</span><span>${upper.toFixed(2)}</span></div><p class="hint">${hint}</p></div>`;
}

function pentaChart(p) {
  const max = Math.max(1, ...p);
  const labels = ['0', '½', '1', '1½', '2'];
  return `<div class="penta">${p.map((c, i) => `<div><em>${c}</em><b style="height:${(c / max) * 70}px"></b><span>${labels[i]}</span></div>`).join('')}</div>
    <p class="hint">Points the candidate scored over each opening's pair of games.</p>`;
}

/* ---------------------------------------------------------------- settings forms */
const TEST_GROUPS = [
  { title: 'Games', fields: [
    { k: 'movetime_ms', label: 'Move time (ms)', type: 'int', help: 'Fixed time per move. Locked once a test has games.' },
    { k: 'max_games', label: 'Max games', type: 'int', help: 'Test ends as inconclusive if no decision by then.' },
    { k: 'max_plies', label: 'Draw after (plies)', type: 'int', help: 'Safety limit for endless games.' },
  ] },
  { title: 'Decision rules', fields: [
    { k: 'band_elo', label: 'Equal band (± Elo)', type: 'float', help: 'Differences inside this band count as equal.' },
    { k: 'alpha', label: 'Alpha', type: 'float', help: 'Chance of wrongly accepting a difference (0.05 = 95%).' },
    { k: 'beta', label: 'Beta', type: 'float', help: 'Chance of missing a real difference.' },
    { k: 'min_games', label: 'Min games before a verdict', type: 'int', help: 'Guards against lucky starts.' },
    { k: 'auto_stop', label: 'Stop when decided', type: 'bool', help: 'End the test as soon as it is better, worse or equal.' },
  ] },
  { title: 'Safety', fields: [
    { k: 'error_abort_limit', label: 'Abort after N engine errors', type: 'int', help: 'Illegal moves, crashes and hangs. 0 = never abort.' },
    { k: 'hard_timeout_factor', label: 'Hang timeout (× move time)', type: 'float', help: 'No bestmove after this long = the engine loses.' },
    { k: 'hard_timeout_min_ms', label: 'Hang timeout minimum (ms)', type: 'int' },
    { k: 'startup_grace_ms', label: 'First-move grace (ms)', type: 'int', help: 'Extra time for an engine\'s first move (startup).' },
    { k: 'score_perspective', label: 'Engine scores are from', type: 'choice', options: [['white', 'White\'s view (+ = white winning)'], ['stm', 'Side to move']] },
  ] },
  { title: 'Adjudication (ends games early using engine scores)', fields: [
    { k: 'adj_win_enabled', label: 'Adjudicate wins', type: 'bool', help: 'Trusts engine scores, so it is off by default.' },
    { k: 'adj_win_cp', label: 'Win score (cp)', type: 'int' },
    { k: 'adj_win_moves', label: 'Win: consecutive moves per side', type: 'int' },
    { k: 'adj_draw_enabled', label: 'Adjudicate draws', type: 'bool' },
    { k: 'adj_draw_cp', label: 'Draw score (± cp)', type: 'int' },
    { k: 'adj_draw_moves', label: 'Draw: consecutive moves per side', type: 'int' },
    { k: 'adj_draw_min_move', label: 'Draw: earliest move number', type: 'int' },
  ] },
];

function fieldHtml(f, v, locked) {
  const id = `f_${f.k}`;
  const help = f.help ? `<span class="help">${esc(f.help)}</span>` : '';
  const dis = locked && locked.includes(f.k) ? 'disabled' : '';
  if (f.type === 'bool') {
    return `<label class="check"><input type="checkbox" data-k="${f.k}" data-t="bool" ${v ? 'checked' : ''} ${dis}><span>${esc(f.label)}${help ? `<span class="help">${esc(f.help)}</span>` : ''}</span></label>`;
  }
  if (f.type === 'text') {
    return `<label class="field">${esc(f.label)}<input type="${f.input || 'text'}" data-k="${f.k}" data-t="text" value="${esc(v)}" placeholder="${esc(f.placeholder || '')}" ${dis}>${help}</label>`;
  }
  if (f.type === 'choice') {
    return `<label class="field">${esc(f.label)}<select data-k="${f.k}" data-t="choice" ${dis}>${f.options.map(([val, txt]) => `<option value="${val}" ${val === v ? 'selected' : ''}>${esc(txt)}</option>`).join('')}</select>${help}</label>`;
  }
  const step = f.type === 'float' ? 'any' : '1';
  return `<label class="field">${esc(f.label)}<input type="number" step="${step}" data-k="${f.k}" data-t="${f.type}" value="${esc(v)}" ${dis}>${help}</label>`;
}

function settingsFormHtml(values, locked) {
  return TEST_GROUPS.map((g, i) => `<div class="${i ? 'fieldgroup' : ''}"><h3>${esc(g.title)}</h3><div class="fields">${g.fields.map((f) => fieldHtml(f, values[f.k], locked)).join('')}</div></div>`).join('');
}

function readFields(root) {
  const out = {};
  $$('[data-k]', root).forEach((el) => {
    if (el.disabled) return;
    const t = el.dataset.t;
    if (t === 'bool') out[el.dataset.k] = el.checked;
    else if (t === 'int') out[el.dataset.k] = parseInt(el.value, 10);
    else if (t === 'float') out[el.dataset.k] = parseFloat(el.value);
    else out[el.dataset.k] = el.value;
  });
  return out;
}

/* ---------------------------------------------------------------- modal */
let modalKey = null;
function openModal(html, cls = '') {
  closeModal();
  const ov = document.createElement('div');
  ov.className = 'overlay';
  ov.innerHTML = `<div class="modal ${cls}" role="dialog" aria-modal="true">${html}</div>`;
  ov.addEventListener('mousedown', (e) => { if (e.target === ov) closeModal(); });
  document.body.appendChild(ov);
  return $('.modal', ov);
}
function closeModal() {
  $$('.overlay').forEach((o) => o.remove());
  if (modalKey) { document.removeEventListener('keydown', modalKey); modalKey = null; }
}

/* ---------------------------------------------------------------- game viewer */
const GLYPH = { k: '♚', q: '♛', r: '♜', b: '♝', n: '♞', p: '♟' };

function fenGrid(fen) {
  const rows = fen.split(' ')[0].split('/');
  return rows.map((row) => {
    const out = [];
    for (const ch of row) {
      if (/\d/.test(ch)) for (let i = 0; i < +ch; i++) out.push(null);
      else out.push(ch);
    }
    return out;
  });
}
function boardHtml(fen, uci) {
  const grid = fenGrid(fen);
  const hl = new Set();
  if (uci) {
    for (const sq of [uci.slice(0, 2), uci.slice(2, 4)]) hl.add(sq);
  }
  let html = '';
  for (let r = 0; r < 8; r++) {
    for (let c = 0; c < 8; c++) {
      const name = 'abcdefgh'[c] + (8 - r);
      const p = grid[r][c];
      const light = (r + c) % 2 === 0;
      const piece = p ? `<span class="${p === p.toUpperCase() ? 'pw' : 'pb'}">${GLYPH[p.toLowerCase()]}\uFE0E</span>` : '';
      const rk = c === 0 ? `<i class="rk">${8 - r}</i>` : '';
      const fl = r === 7 ? `<i class="fl">${'abcdefgh'[c]}</i>` : '';
      html += `<div class="sq ${light ? 'l' : 'd'} ${hl.has(name) ? 'hl' : ''}">${rk}${fl}${piece}</div>`;
    }
  }
  return html;
}

async function openGame(gid, startPly) {
  const g = await api('GET', `/api/games/${gid}`);
  const startFen = g.start_fen || 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
  const n = g.plies.length;
  let ply = startPly != null ? startPly : n;
  let timer = null;
  const firstWhite = g.start_turn === 'w';

  const resultLabel = { '1-0': 'White wins', '0-1': 'Black wins', '1/2-1/2': 'Draw' }[g.result];
  const err = g.error;
  const m = openModal(`
    <div class="modal-head"><div>
      <h2>${esc(g.white.name)} vs ${esc(g.black.name)}</h2>
      <div class="muted">${esc(g.opening_name || '')} · ${resultLabel} · ${esc(TERM[g.termination] || g.termination)} · test <a href="#/test/${g.test_id}">#${g.test_id}</a></div>
    </div><div class="head-actions"><a class="btn sm" href="/api/games/${g.id}/pgn">Download PGN</a><button class="btn sm" data-x="close">Close</button></div></div>
    ${err ? `<div class="notice bad"><strong>${esc(TERM[err.kind] || err.kind)}</strong> by ${esc(err.role)} (${esc(err.engine_id)}) playing ${esc(err.color)} at ply ${err.ply}: ${esc(err.message)}. The board shows the position before the move.</div>` : ''}
    <div class="viewer">
      <div>
        <div class="player"><span>${esc(g.black.name)}<span class="role">${g.black.role}</span></span></div>
        <div class="board" id="vboard"></div>
        <div class="player"><span>${esc(g.white.name)}<span class="role">${g.white.role}</span></span></div>
        <div class="vctl">
          <button class="btn" data-x="first" title="Start (Home)">⏮</button>
          <button class="btn" data-x="prev" title="Back (←)">◀</button>
          <button class="btn" data-x="play" title="Play / pause (Space)">▶</button>
          <button class="btn" data-x="next" title="Forward (→)">▶▶</button>
          <button class="btn" data-x="last" title="End">⏭</button>
          <span class="muted mono" id="vpos" style="margin-left:auto"></span>
        </div>
      </div>
      <div>
        <div class="moves" id="vmoves"></div>
        <svg class="evalgraph" id="vgraph" viewBox="0 0 300 90" preserveAspectRatio="none"></svg>
        <p class="hint">Engine score per move, white's view (clamped to ±10 pawns). Click to jump.</p>
        <div id="vinfo" class="muted mono"></div>
      </div>
    </div>
    ${err ? errorDetailsHtml(err) : ''}`);

  const movesEl = $('#vmoves', m);
  // moves list
  let ml = '';
  g.plies.forEach((p, i) => {
    const whiteMove = (i % 2 === 0) === firstWhite;
    const num = Math.floor((i + (firstWhite ? 0 : 1)) / 2) + 1;
    if (whiteMove) ml += `<span class="mn">${num}.</span>`;
    else if (i === 0) ml += `<span class="mn">${num}…</span>`;
    ml += `<button data-ply="${i + 1}" class="${i < g.opening_plies ? 'op' : ''}">${esc(p.san)}</button>`;
    if (i + 1 === g.opening_plies && g.opening_plies > 0) ml += '<br><span class="muted">— engines take over —</span><br>';
  });
  if (!n) ml = '<span class="muted">No moves were played.</span>';
  movesEl.innerHTML = ml;

  // eval graph
  const graph = $('#vgraph', m);
  const pts = [];
  g.plies.forEach((p, i) => {
    if (p.eval && p.eval[0] != null) {
      const v = Math.max(-1000, Math.min(1000, p.eval[0]));
      pts.push([(i + 1) / Math.max(1, n) * 300, 45 - (v / 1000) * 42]);
    }
  });
  const line = pts.length > 1 ? `<polyline fill="none" stroke="#2547d0" stroke-width="1.6" vector-effect="non-scaling-stroke" points="${pts.map((p) => p.join(',')).join(' ')}"/>` : '';
  const draw = () => {
    const x = (ply / Math.max(1, n)) * 300;
    graph.innerHTML = `<line x1="0" x2="300" y1="45" y2="45" stroke="#c4ccd6" stroke-width="1" vector-effect="non-scaling-stroke"/>${line}<line x1="${x}" x2="${x}" y1="0" y2="90" stroke="#15202d" stroke-width="1.5" vector-effect="non-scaling-stroke"/>`;
  };

  const render = () => {
    const fen = ply === 0 ? startFen : g.plies[ply - 1].fen;
    $('#vboard', m).innerHTML = boardHtml(fen, ply ? g.plies[ply - 1].uci : null);
    $('#vpos', m).textContent = `ply ${ply} / ${n}`;
    $$('#vmoves button', m).forEach((b) => b.classList.toggle('cur', +b.dataset.ply === ply));
    const cur = $('#vmoves button.cur', m);
    if (cur) cur.scrollIntoView({ block: 'nearest' });
    const p = ply ? g.plies[ply - 1] : null;
    $('#vinfo', m).textContent = p && p.eval ? `score ${p.eval[0] == null ? '–' : (p.eval[0] / 100).toFixed(2)}  depth ${p.eval[1] ?? '–'}  ${p.eval[2]} ms` : (p ? 'opening move' : '');
    draw();
  };
  const go = (v) => { ply = Math.max(0, Math.min(n, v)); render(); };
  const stop = () => { if (timer) { clearInterval(timer); timer = null; $('[data-x=play]', m).textContent = '▶'; } };
  const toggle = () => {
    if (timer) return stop();
    if (ply >= n) ply = 0;
    $('[data-x=play]', m).textContent = '⏸';
    timer = setInterval(() => { if (ply >= n) return stop(); go(ply + 1); }, 450);
  };
  m.addEventListener('click', (e) => {
    const t = e.target.closest('[data-x],[data-ply]');
    if (!t) return;
    if (t.dataset.ply) { stop(); return go(+t.dataset.ply); }
    ({ close: () => { stop(); closeModal(); }, first: () => { stop(); go(0); }, prev: () => { stop(); go(ply - 1); },
       next: () => { stop(); go(ply + 1); }, last: () => { stop(); go(n); }, play: toggle })[t.dataset.x]();
  });
  graph.addEventListener('click', (e) => {
    const r = graph.getBoundingClientRect();
    stop(); go(Math.round(((e.clientX - r.left) / r.width) * n));
  });
  modalKey = (e) => {
    if (e.key === 'Escape') { stop(); closeModal(); }
    else if (e.key === 'ArrowLeft') { stop(); go(ply - 1); e.preventDefault(); }
    else if (e.key === 'ArrowRight') { stop(); go(ply + 1); e.preventDefault(); }
    else if (e.key === 'Home') { stop(); go(0); }
    else if (e.key === 'End') { stop(); go(n); }
    else if (e.key === ' ') { toggle(); e.preventDefault(); }
  };
  document.addEventListener('keydown', modalKey);
  render();
}

function errorDetailsHtml(e) {
  return `<details style="margin-top:14px"><summary>Full error report</summary>
    <dl class="kv">
      <dt>Engine</dt><dd>${esc(e.engine_id)} (${esc(e.role)}, ${esc(e.color)})</dd>
      <dt>What happened</dt><dd>${esc(e.message)}</dd>
      <dt>Raw bestmove line</dt><dd class="mono">${esc(e.bestmove_line ?? '(none)')}</dd>
      <dt>Exit code</dt><dd class="mono">${esc(e.exit_code ?? 'still running (killed by tester)')}</dd>
      <dt>Time taken</dt><dd class="mono">${esc(e.elapsed_ms ?? '–')} ms</dd>
      <dt>Ply</dt><dd>${e.ply}</dd>
      <dt>FEN before move</dt><dd class="mono">${esc(e.fen_before)}</dd>
      <dt>Legal moves</dt><dd class="mono">${esc((e.legal_moves || []).join(' '))}</dd>
    </dl>
    <h3>Command sent</h3><pre class="log">${esc(e.command_sent || '')}</pre>
    <h3>Last info lines</h3><pre class="log">${esc((e.last_info_lines || []).join('\n') || '(none)')}</pre>
    <h3>Engine output tail (stdout + stderr)</h3><pre class="log">${esc((e.output_tail || []).join('\n') || '(none)')}</pre></details>`;
}
