/* Pages and routing. */
let refresh = async () => {};   // re-renders whatever page is open

/* ================================================================ shared: tests table */
function rowAction(t) {
  if (t.status === 'running') return `<button class="btn sm" data-act="pause" data-id="${t.id}">Pause</button>`;
  if (t.status === 'pausing') return `<button class="btn sm" disabled>Pausing…</button>`;
  if (t.status === 'queued') return `<button class="btn sm" data-act="pause" data-id="${t.id}">Unqueue</button>`;
  return `<button class="btn sm primary" data-act="start" data-id="${t.id}">${t.games ? 'Resume' : 'Start'}</button>`;
}

function testsTable(tests, opts = {}) {
  const { group = 'none' } = opts;
  if (!tests.length) {
    return `<div class="empty"><strong>No tests here</strong>Pick a base and a candidate engine with “New test” to start one.</div>`;
  }
  const row = (t) => {
    const live = t.live && t.live.games_per_min ? `<div class="rcount">${t.live.games_per_min.toFixed(0)} games/min</div>` : '';
    const meta = [t.label && esc(t.label), t.experiment_name && `<a href="#/experiment/${t.experiment_id}">${esc(t.experiment_name)}</a>`, `${t.settings.movetime_ms} ms`]
      .filter(Boolean).join(' · ');
    return `<tr class="click" data-href="#/test/${t.id}">
      <td class="mono muted">#${t.id}</td>
      <td class="title-cell"><strong>${esc(t.cand_name)}</strong> <span class="vs">vs</span> ${esc(t.base_name)}${t.self_test ? ' <span class="muted">(self-test)</span>' : ''}
        <div class="meta">${meta}</div></td>
      <td class="num">${eloText(t)}</td>
      <td style="min-width:150px">${resultBar(t.wins, t.draws, t.losses)}<div class="rcount">${t.wins} / ${t.draws} / ${t.losses}</div></td>
      <td class="num">${fmt(t.games)} / ${fmt(t.settings.max_games)}</td>
      <td>${verdictBadge(t.verdict)}</td>
      <td>${statusBadge(t.status)}${live}</td>
      <td class="actions">${rowAction(t)}</td></tr>`;
  };
  let body = '';
  if (group === 'none') body = tests.map(row).join('');
  else {
    const groups = new Map();
    for (const t of tests) {
      const key = group === 'base' ? t.base_id : (t.experiment_name || '');
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key).push(t);
    }
    const ordered = [...groups.entries()].sort((a, b) => b[1].length - a[1].length);
    for (const [key, list] of ordered) {
      const title = group === 'base'
        ? `Base: ${esc(list[0].base_name)} <span class="muted">· ${list.length} test${list.length > 1 ? 's' : ''}</span>`
        : (key ? `Experiment: ${esc(key)}` : 'No experiment') + ` <span class="muted">· ${list.length}</span>`;
      body += `<tr class="group"><td colspan="8">${title}</td></tr>` + list.map(row).join('');
    }
  }
  return `<table class="tbl"><thead><tr><th>#</th><th>Test</th><th class="num">Elo</th><th>Wins / draws / losses</th><th class="num">Games</th><th>Verdict</th><th>Status</th><th></th></tr></thead><tbody>${body}</tbody></table>`;
}

/* ================================================================ tests page */
const filters = { base: '', cand: '', experiment: '', status: '', verdict: '', q: '' };
let groupMode = localStorage.getItem('groupMode') || 'none';

const engineLabel = (e) => (e.name !== e.id ? `${e.name}  (${e.id})` : e.id);
const engineOptions = (engines, sel, any) =>
  (any ? `<option value="">${any}</option>` : '') + engines.map((e) => `<option value="${esc(e.id)}" ${e.id === sel ? 'selected' : ''}>${esc(engineLabel(e))}</option>`).join('');

async function pageTests(params) {
  const refs = await loadRefs();
  for (const k of Object.keys(filters)) filters[k] = params.get(k) || '';
  if (params.get('exp')) filters.experiment = params.get('exp');
  const defaults = refs.settings.test_defaults;
  const engines = refs.engines;

  main.innerHTML = `
    <header class="page-head"><div><h1>Tests</h1><div class="sub">Each test plays a candidate against a base over many openings, both colors.</div></div>
      <div class="head-actions"><button class="btn primary" data-act="toggle-new">New test</button></div></header>

    <section class="panel hidden" id="newtest">
      <h2>New test</h2>
      ${engines.length < 1 ? '<div class="notice">No engines found yet. Add one on the Engines page, or drop a folder with an .exe into engines/.</div>' : ''}
      <div class="fields wide">
        <label class="field">Base engine<select id="nt_base">${engineOptions(engines, filters.base || localStorage.getItem('lastBase') || '')}</select></label>
        <label class="field" id="nt_cand_wrap">Candidate engine<select id="nt_cand">${engineOptions(engines, filters.cand || (engines.length ? engines[engines.length - 1].id : ''))}</select></label>
        <label class="field">Experiment<select id="nt_exp"><option value="">None</option>${refs.experiments.map((x) => `<option value="${x.id}" ${String(x.id) === params.get('exp') ? 'selected' : ''}>${esc(x.name)}</option>`).join('')}<option value="__new">New experiment…</option></select></label>
        <label class="field hidden" id="nt_newexp_wrap">New experiment name<input type="text" id="nt_newexp" placeholder="e.g. doubled pawn penalty"></label>
        <label class="field">Label<input type="text" id="nt_label" placeholder="e.g. penalty 10 cp"></label>
        <label class="field">Notes<textarea id="nt_notes"></textarea></label>
      </div>
      <label class="check" style="margin-top:12px"><input type="checkbox" id="nt_self"><span>Self-test<span class="help">Plays the base engine against itself. A healthy setup lands near 0 Elo and ends “equal” or “inconclusive”, never “better” or “worse”.</span></span></label>
      <details open style="margin-top:14px"><summary>Settings for this test</summary><div id="nt_settings" style="margin-top:8px">${settingsFormHtml(defaults)}</div></details>
      <div class="form-actions">
        <button class="btn primary" data-act="create-test">Create test</button>
        <label class="check"><input type="checkbox" id="nt_start" checked><span>Start immediately</span></label>
        <button class="btn" data-act="toggle-new">Cancel</button>
      </div>
    </section>

    <div class="filters">
      <select id="f_base">${engineOptions(engines, filters.base, 'Any base')}</select>
      <select id="f_cand">${engineOptions(engines, filters.cand, 'Any candidate')}</select>
      <select id="f_experiment"><option value="">Any experiment</option><option value="none" ${filters.experiment === 'none' ? 'selected' : ''}>No experiment</option>${refs.experiments.map((x) => `<option value="${x.id}" ${String(x.id) === filters.experiment ? 'selected' : ''}>${esc(x.name)}</option>`).join('')}</select>
      <select id="f_status"><option value="">Any status</option>${Object.entries(STATUS).map(([k, v]) => `<option value="${k}" ${filters.status === k ? 'selected' : ''}>${v}</option>`).join('')}</select>
      <select id="f_verdict"><option value="">Any verdict</option>${Object.entries(VERDICT).map(([k, v]) => `<option value="${k}" ${filters.verdict === k ? 'selected' : ''}>${v}</option>`).join('')}</select>
      <input type="search" id="f_q" class="grow" placeholder="Search labels and notes" value="${esc(filters.q)}">
      <select id="f_group"><option value="none">No grouping</option><option value="base" ${groupMode === 'base' ? 'selected' : ''}>Group by base</option><option value="experiment" ${groupMode === 'experiment' ? 'selected' : ''}>Group by experiment</option></select>
    </div>
    <div class="panel flush" id="list"></div>`;

  if (params.get('new') || !engines.length) $('#newtest').classList.remove('hidden');

  $('#nt_exp').onchange = () => $('#nt_newexp_wrap').classList.toggle('hidden', $('#nt_exp').value !== '__new');
  $('#nt_self').onchange = () => $('#nt_cand_wrap').classList.toggle('hidden', $('#nt_self').checked);

  let t = null;
  const onFilter = () => {
    for (const k of Object.keys(filters)) filters[k] = $(`#f_${k}`).value.trim();
    groupMode = $('#f_group').value; localStorage.setItem('groupMode', groupMode);
    loadList();
  };
  $$('.filters select').forEach((el) => (el.onchange = onFilter));
  $('#f_q').oninput = () => { clearTimeout(t); t = setTimeout(onFilter, 250); };

  const loadList = async () => {
    const qs = new URLSearchParams();
    for (const [k, v] of Object.entries(filters)) if (v) qs.set(k, v);
    history.replaceState(null, '', '#/tests' + (qs.toString() ? '?' + qs : ''));
    const tests = await api('GET', '/api/tests?' + qs);
    $('#list').innerHTML = testsTable(tests, { group: groupMode });
  };
  refresh = loadList;
  await loadList();
  poll(loadList, 2000);
}

async function createTest(allowDuplicate = false) {
  const self = $('#nt_self').checked;
  const base = $('#nt_base').value;
  const expSel = $('#nt_exp').value;
  const body = {
    base_id: base, cand_id: self ? base : $('#nt_cand').value,
    experiment_id: expSel && expSel !== '__new' ? +expSel : null,
    new_experiment: expSel === '__new' ? $('#nt_newexp').value : '',
    label: $('#nt_label').value || (self ? 'Self-test' : ''), notes: $('#nt_notes').value,
    settings: readFields($('#nt_settings')), start: $('#nt_start').checked, allow_duplicate: allowDuplicate || self,
  };
  try {
    const r = await api('POST', '/api/tests', body);
    localStorage.setItem('lastBase', base);
    location.hash = `#/test/${r.id}`;
  } catch (e) {
    if (e.data.code === 'duplicate') {
      if (confirm(`${e.message}\n\nOpen test #${e.data.existing_id} instead?`)) location.hash = `#/test/${e.data.existing_id}`;
      else if (confirm('Create a separate new test anyway?')) createTest(true);
    } else toast(e.message, 'bad');
  }
}

async function startTest(id, force = false) {
  try {
    await api('POST', `/api/tests/${id}/start`, { force });
    toast('Test queued');
  } catch (e) {
    if (e.data.code === 'binary_changed' && !force) {
      if (confirm(`${e.message}\n\nContinue with the new executable anyway?`)) return startTest(id, true);
    } else toast(e.message, 'bad');
  }
  refresh();
}

/* ================================================================ test detail */
const view = { tab: 'games', page: 0, gf: { outcome: '', color: '', termination: '', opening: '' }, lastGames: -1, lastStatus: '' };
const PAGE = 50;

function controlsHtml(t) {
  const active = ['running', 'queued', 'pausing'].includes(t.status);
  let b = '';
  if (t.status === 'running') b += `<button class="btn" data-act="pause" data-id="${t.id}">Pause</button><button class="btn" data-act="pause-now" data-id="${t.id}" title="Stop in-progress games and discard them">Pause now</button>`;
  else if (t.status === 'pausing') b += `<button class="btn" disabled>Pausing…</button><button class="btn" data-act="pause-now" data-id="${t.id}">Pause now</button>`;
  else if (t.status === 'queued') b += `<button class="btn" data-act="pause" data-id="${t.id}">Remove from queue</button>`;
  else b += `<button class="btn primary" data-act="start" data-id="${t.id}">${t.games ? 'Resume' : 'Start'}</button>`;
  b += `<a class="btn" href="/api/tests/${t.id}/pgn">Download PGN</a>`;
  if (!active) b += `<button class="btn danger" data-act="delete" data-id="${t.id}">Delete</button>`;
  return b;
}

function verdictNote(t) {
  const band = t.settings.band_elo;
  if (t.verdict === 'better') return 'The test accepted that the candidate is stronger than the base.';
  if (t.verdict === 'worse') return 'The test accepted that the candidate is weaker than the base.';
  if (t.verdict === 'equal') return `The test accepted that any difference is smaller than about ±${band} Elo.`;
  if (t.games < t.settings.min_games) return `Waiting for at least ${t.settings.min_games} games before deciding.`;
  if (['finished', 'aborted'].includes(t.status)) return 'Not enough evidence either way. Add games or openings and resume to narrow it down.';
  return 'Not enough evidence yet.';
}

function renderLive(t) {
  const s = t.settings;
  const active = ['running', 'queued', 'pausing'].includes(t.status);
  const lv = t.live;
  const liveBits = [];
  if (lv) {
    liveBits.push(`${lv.inflight} game${lv.inflight === 1 ? '' : 's'} in play`);
    if (lv.games_per_min) liveBits.push(`${lv.games_per_min.toFixed(0)} games/min`);
    if (lv.eta_seconds != null) liveBits.push(`about ${dur(lv.eta_seconds)} left`);
  }
  $('#thead').innerHTML = `
    <header class="page-head"><div>
      <div class="muted"><a href="#/tests">Tests</a> / #${t.id}</div>
      <h1>${esc(t.cand_name)} <span class="muted" style="font-weight:400">vs</span> ${esc(t.base_name)}${t.self_test ? ' <span class="muted" style="font-weight:400">(self-test)</span>' : ''}</h1>
      <div class="sub">${[t.label && esc(t.label), t.experiment_name && `<a href="#/experiment/${t.experiment_id}">${esc(t.experiment_name)}</a>`, `base <a href="#/tests?base=${encodeURIComponent(t.base_id)}">all tests with this base</a>`].filter(Boolean).join(' · ')}</div>
    </div><div class="head-actions">${controlsHtml(t)}</div></header>`;

  const warns = [];
  if (t.engines_missing.length) warns.push(`<div class="notice bad">The ${t.engines_missing.join(' and ')} engine is missing from engines/, so this test can't run.</div>`);
  if (t.binary_changed.length) warns.push(`<div class="notice">The ${t.binary_changed.join(' and ')} executable has changed since this test started. Resuming will ask you to confirm, because new games won't be comparable with the old ones.</div>`);
  if (['aborted'].includes(t.status)) warns.push(`<div class="notice bad"><strong>Aborted:</strong> ${esc(t.status_reason)}. See the Errors tab.</div>`);
  $('#warn').innerHTML = warns.join('');

  const eloNum = t.elo == null ? '–' : (Math.abs(t.elo) >= 1000 ? (t.elo > 0 ? '+1000+' : '−1000+') : (t.elo > 0 ? '+' : t.elo < 0 ? '−' : '') + Math.abs(t.elo).toFixed(1));
  const margin = t.elo_margin != null ? ` <small>± ${t.elo_margin.toFixed(1)} Elo</small>` : (t.elo != null ? ' <small>Elo</small>' : '');
  $('#live-main').innerHTML = `
    <div class="result-head">
      <div><div class="big-elo">${eloNum}${margin}</div>
        <div class="muted">Candidate relative to base, 95% interval${t.los != null ? ` · ${(t.los * 100).toFixed(t.los > 0.99 || t.los < 0.01 ? 2 : 0)}% chance it is better` : ''}</div></div>
      <div style="text-align:right">${verdictBadge(t.verdict)}<div class="hint" style="max-width:300px">${esc(verdictNote(t))}</div></div>
    </div>
    ${resultBar(t.wins, t.draws, t.losses, true)}
    <div class="legend"><span style="--c:var(--win)">Candidate wins</span><span style="--c:var(--draw)">Draws</span><span style="--c:var(--loss)">Candidate losses</span></div>
    <div class="statline">
      <div class="stat"><div class="k">Games</div><div class="v">${fmt(t.games)}</div></div>
      <div class="stat"><div class="k">Complete pairs</div><div class="v">${fmt(t.pairs)}</div></div>
      <div class="stat"><div class="k">Draw rate</div><div class="v">${t.draw_ratio == null ? '–' : Math.round(t.draw_ratio * 100) + '%'}</div></div>
      <div class="stat"><div class="k">Engine errors</div><div class="v">${t.errors_cand + t.errors_base}<span class="muted" style="font-size:12px;font-weight:400"> ${t.errors_cand + t.errors_base ? `(cand ${t.errors_cand}, base ${t.errors_base})` : ''}</span></div></div>
    </div>
    <div style="margin-top:14px;display:flex;gap:10px;align-items:center;flex-wrap:wrap">${statusBadge(t.status)}<span class="muted">${esc(t.status_reason || '')}${liveBits.length ? (t.status_reason ? ' · ' : '') + liveBits.join(' · ') : ''}</span></div>`;

  const o = t.openings;
  const pctGames = Math.min(100, (t.games / s.max_games) * 100);
  $('#live-side').innerHTML = `
    <h2>Evidence</h2>
    ${gauge('Better than the base?', `Right end: better. Left end: not better by ${s.band_elo} Elo.`, t.llr_up, t.lower, t.upper)}
    ${gauge('Worse than the base?', `Right end: worse. Left end: not worse by ${s.band_elo} Elo.`, t.llr_down, t.lower, t.upper)}
    <p class="hint">Equal means both markers reach the left end. The test stops when a verdict is reached${s.auto_stop ? '' : ' (auto-stop is off)'}.</p>
    <div class="fieldgroup"><h3>Game pairs by candidate points</h3>${pentaChart(t.penta)}</div>
    <div class="fieldgroup"><h3>Progress</h3>
      <div class="mono">${fmt(t.games)} of ${fmt(s.max_games)} games</div><div class="progress"><i style="width:${pctGames}%"></i></div>
      <p class="hint">${o.started} of ${o.total} openings started · ${o.unplayed} not played yet. New files in openings/ are added when you resume.</p></div>`;

  $('#events').innerHTML = t.events.length
    ? t.events.map((e) => `<li><time>${clock(e.ts)}</time><span>${esc(e.message)}</span></li>`).join('')
    : '<li class="muted">Nothing yet.</li>';
}

async function pageTest(id) {
  const refs = await loadRefs();
  let t = await api('GET', `/api/tests/${id}`);
  view.page = 0; view.lastGames = -1; view.tab = 'games';

  main.innerHTML = `
    <div id="thead"></div><div id="warn"></div>
    <div class="grid side">
      <div>
        <section class="panel" id="live-main"></section>
        <section class="panel flush">
          <div class="tabs" id="tabs"></div>
          <div id="tabbody"></div>
        </section>
      </div>
      <div>
        <section class="panel" id="live-side"></section>
        <section class="panel"><h2>Details</h2>
          <div class="fields" style="grid-template-columns:1fr">
            <label class="field">Label<input type="text" id="d_label" value="${esc(t.label)}"></label>
            <label class="field">Experiment<select id="d_exp"><option value="">None</option>${refs.experiments.map((x) => `<option value="${x.id}" ${x.id === t.experiment_id ? 'selected' : ''}>${esc(x.name)}</option>`).join('')}</select></label>
            <label class="field">Notes<textarea id="d_notes">${esc(t.notes)}</textarea></label>
          </div>
          <div class="form-actions"><button class="btn" data-act="save-details" data-id="${t.id}">Save details</button></div>
        </section>
        <section class="panel"><h2>Log</h2><ul class="events" id="events"></ul></section>
      </div>
    </div>
    <section class="panel"><details ${t.games ? '' : 'open'}><summary>Test settings</summary>
      <p class="hint">Changes apply to games that start after saving. Move time is locked once a test has games, so every game in a test is comparable.</p>
      <div id="settings-form">${settingsFormHtml(t.settings, t.games > 0 ? ['movetime_ms'] : [])}</div>
      <div class="form-actions"><button class="btn primary" data-act="save-settings" data-id="${t.id}">Save settings</button></div>
    </details></section>`;

  const renderTabs = () => {
    const errs = t.errors_base + t.errors_cand;
    $('#tabs').innerHTML = [['games', 'Games'], ['errors', `Errors${errs ? ` (${errs})` : ''}`]]
      .map(([k, l]) => `<button data-act="tab" data-tab="${k}" class="${view.tab === k ? 'on' : ''}">${l}</button>`).join('');
  };

  const loadTab = async () => {
    renderTabs();
    if (view.tab === 'games') {
      const f = view.gf;
      const qs = new URLSearchParams({ limit: PAGE, offset: view.page * PAGE });
      for (const [k, v] of Object.entries(f)) if (v) qs.set(k, v);
      const r = await api('GET', `/api/tests/${id}/games?${qs}`);
      const pages = Math.max(1, Math.ceil(r.total / PAGE));
      const focus = document.activeElement && document.activeElement.id;
      $('#tabbody').innerHTML = `
        <div class="filters" style="padding:0 12px">
          <select id="g_outcome"><option value="">Any result</option><option value="W" ${f.outcome === 'W' ? 'selected' : ''}>Candidate won</option><option value="D" ${f.outcome === 'D' ? 'selected' : ''}>Draw</option><option value="L" ${f.outcome === 'L' ? 'selected' : ''}>Candidate lost</option></select>
          <select id="g_color"><option value="">Any color</option><option value="white" ${f.color === 'white' ? 'selected' : ''}>Candidate white</option><option value="black" ${f.color === 'black' ? 'selected' : ''}>Candidate black</option></select>
          <select id="g_termination"><option value="">Any ending</option>${r.terminations.map((x) => `<option value="${x}" ${f.termination === x ? 'selected' : ''}>${esc(TERM[x] || x)}</option>`).join('')}</select>
          <input type="search" id="g_opening" class="grow" placeholder="Search openings" value="${esc(f.opening)}">
        </div>
        ${r.games.length ? `<table class="tbl"><thead><tr><th>Game</th><th>Opening</th><th>Candidate plays</th><th>Result</th><th>Ending</th><th class="num">Plies</th></tr></thead><tbody>
          ${r.games.map((g) => `<tr class="click" data-act="open-game" data-id="${g.id}">
            <td class="mono muted">#${g.id}</td><td>${esc(g.opening_name)}</td><td>${g.cand_white ? 'White' : 'Black'}</td>
            <td><strong style="color:var(--${g.outcome === 'W' ? 'win' : g.outcome === 'L' ? 'loss' : 'muted'})">${{ W: 'Win', D: 'Draw', L: 'Loss' }[g.outcome]}</strong></td>
            <td>${g.error_kind ? `<span class="badge aborted">${esc(TERM[g.termination] || g.termination)}</span>` : esc(TERM[g.termination] || g.termination)}</td>
            <td class="num">${g.plies}</td></tr>`).join('')}</tbody></table>
          <div class="pager"><span>${fmt(r.total)} games</span><button class="btn sm" data-act="gpage" data-d="-1" ${view.page === 0 ? 'disabled' : ''}>Newer</button><span>page ${view.page + 1} of ${pages}</span><button class="btn sm" data-act="gpage" data-d="1" ${view.page + 1 >= pages ? 'disabled' : ''}>Older</button></div>`
          : '<div class="empty"><strong>No games yet</strong>Finished games show up here as they complete.</div>'}`;
      for (const k of ['outcome', 'color', 'termination']) $(`#g_${k}`).onchange = (e) => { view.gf[k] = e.target.value; view.page = 0; loadTab(); };
      let to; $('#g_opening').oninput = (e) => { clearTimeout(to); to = setTimeout(() => { view.gf.opening = e.target.value; view.page = 0; loadTab(); }, 300); };
      if (focus === 'g_opening') { const el = $('#g_opening'); el.focus(); el.setSelectionRange(el.value.length, el.value.length); }
    } else {
      const r = await api('GET', `/api/tests/${id}/games?errors=1&limit=100`);
      $('#tabbody').innerHTML = r.games.length
        ? `<div style="padding:4px 14px 14px">${r.games.map((g) => `<div class="errcard"><div class="top"><span class="badge aborted">${esc(TERM[g.error.kind] || g.error.kind)}</span>
            <strong>${esc(g.error.role)}</strong> <span class="mono">${esc(g.error.engine_id)}</span> as ${esc(g.error.color)}, ply ${g.error.ply}
            <button class="btn sm" data-act="open-game" data-id="${g.id}" style="margin-left:auto">Replay game #${g.id}</button></div>
            <div>${esc(g.error.message)}</div>${errorDetailsHtml(g.error)}</div>`).join('')}</div>`
        : '<div class="empty"><strong>No engine errors</strong>Illegal moves, crashes and hangs are logged here with a full report.</div>';
    }
  };
  view.reloadTab = loadTab;

  const tick = async () => {
    t = await api('GET', `/api/tests/${id}`);
    renderLive(t);
    if (t.games !== view.lastGames || t.status !== view.lastStatus) {
      const first = view.lastGames === -1;
      view.lastGames = t.games; view.lastStatus = t.status;
      if (first || view.page === 0) await loadTab();
    }
  };
  refresh = tick;
  await tick();
  poll(tick, 1500);
}

/* ================================================================ engines page */
async function pageEngines() {
  const refs = await loadRefs();
  const engines = refs.engines;
  main.innerHTML = `
    <header class="page-head"><div><h1>Engines</h1><div class="sub">Everything under <span class="mono">${esc(refs.system.engines_dir)}</span>. A folder counts as an engine when it contains an executable.</div></div>
      <div class="head-actions"><button class="btn primary" data-act="toggle-add">Add engine</button></div></header>

    <section class="panel hidden" id="addengine"><h2>Add engine</h2>
      <form id="addform"><div class="fields wide">
        <label class="field">Executable<input type="file" name="exe" required></label>
        <label class="field">Folder name<input type="text" name="folder" required placeholder="v012-doubled-pawn-10"><span class="help">Letters, digits, dot, dash and underscore.</span></label>
        <label class="field">Group (optional subfolder)<input type="text" name="group" placeholder="doubled-pawn"><span class="help">Keeps variants of one feature together.</span></label>
        <label class="field">Display name<input type="text" name="name" placeholder="v12 doubled pawn -10"></label>
        <label class="field">Version<input type="text" name="version" placeholder="12"></label>
        <label class="field">Parent engine<select name="parent"><option value="">None</option>${engineOptions(engines, '')}</select></label>
        <label class="field">Tags<input type="text" name="tags" placeholder="eval, pawns"><span class="help">Comma separated.</span></label>
        <label class="field">Notes<textarea name="notes"></textarea></label>
      </div>
      <div class="form-actions"><button class="btn primary" type="submit">Add engine</button><button class="btn" type="button" data-act="toggle-add">Cancel</button></div></form>
    </section>

    <div class="panel flush">${engines.length ? `<table class="tbl"><thead><tr><th>Engine</th><th>Version</th><th>Parent</th><th>Tags</th><th>Tests</th><th>File</th><th></th></tr></thead><tbody>
      ${engines.map((e) => `<tr>
        <td class="title-cell"><strong>${esc(e.name)}</strong><div class="meta mono">${esc(e.id)}</div>${e.notes ? `<div class="meta">${esc(e.notes)}</div>` : ''}</td>
        <td>${esc(e.version)}</td><td class="mono">${esc(e.parent)}</td><td>${e.tags.map(esc).join(', ')}</td>
        <td><a href="#/tests?base=${encodeURIComponent(e.id)}">${e.tests_as_base} as base</a><br><a href="#/tests?cand=${encodeURIComponent(e.id)}">${e.tests_as_candidate} as candidate</a></td>
        <td class="mono muted">${(e.size / 1048576).toFixed(1)} MB<br>${e.sha}</td>
        <td class="actions"><button class="btn sm" data-act="edit-engine" data-id="${esc(e.id)}">Edit</button></td></tr>`).join('')}</tbody></table>`
      : `<div class="empty"><strong>No engines yet</strong>Add one above, or copy a folder containing engine.exe into engines/.</div>`}</div>`;
  $('#addform').onsubmit = async (ev) => {
    ev.preventDefault();
    try { await api('POST', '/api/engines', new FormData(ev.target)); toast('Engine added', 'ok'); pageEngines(); }
    catch (e) { toast(e.message, 'bad'); }
  };
  refresh = pageEngines;
}

function editEngine(id) {
  const e = state.refs.engines.find((x) => x.id === id);
  if (!e) return;
  const m = openModal(`<div class="modal-head"><h2>Edit ${esc(e.id)}</h2><button class="btn sm" data-x="close">Close</button></div>
    <div class="fields wide" id="ee">
      <label class="field">Display name<input type="text" data-k="name" data-t="text" value="${esc(e.name)}"></label>
      <label class="field">Version<input type="text" data-k="version" data-t="text" value="${esc(e.version)}"></label>
      <label class="field">Parent engine<select data-k="parent" data-t="choice"><option value="">None</option>${engineOptions(state.refs.engines.filter((x) => x.id !== e.id), e.parent)}</select></label>
      <label class="field">Tags<input type="text" data-k="tags" data-t="text" value="${esc(e.tags.join(', '))}"></label>
      <label class="field">Notes<textarea data-k="notes" data-t="text">${esc(e.notes)}</textarea></label></div>
    <div class="form-actions"><button class="btn primary" data-x="save">Save</button></div>`, 'narrow');
  m.addEventListener('click', async (ev) => {
    const x = ev.target.dataset.x;
    if (x === 'close') closeModal();
    if (x === 'save') {
      try { await api('PATCH', '/api/engines/' + encodeURI(id), readFields($('#ee', m))); closeModal(); pageEngines(); }
      catch (err) { toast(err.message, 'bad'); }
    }
  });
}

/* ================================================================ experiments */
async function pageExperiments() {
  const refs = await loadRefs();
  main.innerHTML = `
    <header class="page-head"><div><h1>Experiments</h1><div class="sub">Group tests that try variants of one idea, such as a penalty at 5, 10 and 15 cp.</div></div></header>
    <section class="panel"><h2>New experiment</h2><div class="fields wide">
      <label class="field">Name<input type="text" id="x_name" placeholder="doubled pawn penalty"></label>
      <label class="field">Description<input type="text" id="x_desc" placeholder="What are you trying?"></label></div>
      <div class="form-actions"><button class="btn primary" data-act="create-exp">Create experiment</button></div></section>
    <div class="panel flush">${refs.experiments.length ? `<table class="tbl"><thead><tr><th>Experiment</th><th>Description</th><th class="num">Tests</th></tr></thead><tbody>
      ${refs.experiments.map((x) => `<tr class="click" data-href="#/experiment/${x.id}"><td><strong>${esc(x.name)}</strong></td><td class="muted">${esc(x.description)}</td><td class="num">${x.tests}</td></tr>`).join('')}</tbody></table>`
      : '<div class="empty"><strong>No experiments yet</strong>Create one, then pick it when you start a test.</div>'}</div>`;
  refresh = pageExperiments;
}

async function pageExperiment(id) {
  await loadRefs();
  const load = async () => {
    const x = await api('GET', `/api/experiments/${id}`);
    const tests = [...x.tests].sort((a, b) => (b.elo ?? -1e9) - (a.elo ?? -1e9));
    if (!$('#xhead')) {
      main.innerHTML = `<div id="xhead"></div><div class="panel flush" id="xtests"></div>`;
    }
    $('#xhead').innerHTML = `<header class="page-head"><div><div class="muted"><a href="#/experiments">Experiments</a> / ${x.id}</div><h1>${esc(x.name)}</h1><div class="sub">${esc(x.description)}</div></div>
      <div class="head-actions"><button class="btn" data-act="rename-exp" data-id="${x.id}">Rename</button><a class="btn primary" href="#/tests?new=1&exp=${x.id}">New test in this experiment</a></div></header>`;
    $('#xtests').innerHTML = testsTable(tests) + (tests.length > 1 ? '<div class="pager">Sorted by Elo, best first.</div>' : '');
  };
  refresh = load;
  await load();
  poll(load, 2500);
}

/* ================================================================ settings */
const GLOBAL_FIELDS = [
  { k: 'workers', label: 'Parallel games', type: 'int', help: 'Each game runs on its own CPU. Keep below your core count so Windows and the web page have room.' },
  { k: 'pin_cpus', label: 'Pin each game to one CPU', type: 'bool', help: 'Both engines in a game share a CPU, so the game is fair even on mixed fast and slow cores.' },
  { k: 'cpu_list', label: 'CPU list (optional)', type: 'text', placeholder: 'auto', help: 'Logical processor ids to use, like 0,2,4-7. Empty = one per physical core.' },
];
const NOTIFY_FIELDS = [
  { k: 'discord_webhook_url', label: 'Discord webhook URL', type: 'text', input: 'url', placeholder: 'https://discord.com/api/webhooks/…' },
  { k: 'notify_finish', label: 'Notify when a test finishes', type: 'bool' },
  { k: 'notify_abort', label: 'Notify when a test aborts', type: 'bool', help: 'Too many illegal moves, crashes or hangs.' },
  { k: 'public_url', label: 'Address used in notification links', type: 'text', help: 'Use your PC\'s LAN address if you want the link to work from your phone.' },
];

async function pageSettings() {
  const refs = await loadRefs();
  const g = refs.settings.global, td = refs.settings.test_defaults, sys = refs.system, op = sys.openings;
  main.innerHTML = `
    <header class="page-head"><div><h1>Settings</h1><div class="sub">Defaults for new tests, and options for the whole app.</div></div>
      <div class="head-actions"><button class="btn primary" data-act="save-global">Save settings</button></div></header>
    <section class="panel"><h2>Hardware</h2>
      <p class="muted" style="margin-top:0">${sys.logical_cpus} logical processors, ${sys.physical_cpus} physical cores. Games will use CPUs: <span class="mono">${sys.cpu_pool.join(', ') || 'unpinned'}</span></p>
      <div class="fields" id="g_hw">${GLOBAL_FIELDS.map((f) => fieldHtml(f, g[f.k])).join('')}</div></section>
    <section class="panel"><h2>Defaults for new tests</h2><div id="g_defaults">${settingsFormHtml(td)}</div></section>
    <section class="panel"><h2>Notifications</h2><div class="fields wide" id="g_notify">${NOTIFY_FIELDS.map((f) => fieldHtml(f, g[f.k])).join('')}</div>
      <div class="form-actions"><button class="btn" data-act="test-webhook">Send test message</button></div></section>
    <section class="panel"><h2>Openings</h2>
      <p class="muted" style="margin-top:0">${fmt(op.total)} valid openings from <span class="mono">${esc(op.dir)}</span>. Drop more .json files there, then resume a test to add them to its queue.</p>
      ${op.files.length ? `<table class="tbl"><thead><tr><th>File</th><th class="num">Entries</th><th class="num">Valid</th><th class="num">Duplicates</th></tr></thead><tbody>${op.files.map((f) => `<tr><td class="mono">${esc(f.file)}</td><td class="num">${f.total}</td><td class="num">${f.valid}</td><td class="num">${f.duplicates}</td></tr>`).join('')}</tbody></table>` : '<div class="notice">No opening files found.</div>'}
      ${op.invalid_count ? `<h3 style="margin-top:14px">${op.invalid_count} invalid entries (skipped)</h3><pre class="log">${esc(op.invalid.slice(0, 40).map((i) => `${i.file}${i.index != null ? ' #' + (i.index + 1) : ''} ${i.name || ''}: ${i.reason}`).join('\n'))}</pre>` : ''}
      <div class="form-actions"><button class="btn" data-act="rescan-openings">Rescan folder</button></div></section>`;
  refresh = pageSettings;
}

/* ================================================================ actions + routing */
const ACTIONS = {
  'toggle-new': () => $('#newtest').classList.toggle('hidden'),
  'create-test': () => createTest(),
  start: (b) => startTest(+b.dataset.id),
  pause: async (b) => { try { await api('POST', `/api/tests/${b.dataset.id}/pause`, {}); } catch (e) { toast(e.message, 'bad'); } refresh(); },
  'pause-now': async (b) => { try { await api('POST', `/api/tests/${b.dataset.id}/pause`, { now: true }); } catch (e) { toast(e.message, 'bad'); } refresh(); },
  delete: async (b) => {
    if (!confirm('Delete this test and all of its games? This cannot be undone.')) return;
    try { await api('DELETE', `/api/tests/${b.dataset.id}`); location.hash = '#/tests'; } catch (e) { toast(e.message, 'bad'); }
  },
  'save-details': async (b) => {
    try {
      await api('PATCH', `/api/tests/${b.dataset.id}`, { label: $('#d_label').value, notes: $('#d_notes').value, experiment_id: $('#d_exp').value || null });
      toast('Details saved', 'ok'); refresh();
    } catch (e) { toast(e.message, 'bad'); }
  },
  'save-settings': async (b) => {
    try { await api('PATCH', `/api/tests/${b.dataset.id}`, { settings: readFields($('#settings-form')) }); toast('Settings saved. They apply to games that start from now on.', 'ok'); refresh(); }
    catch (e) { toast(e.message, 'bad'); }
  },
  tab: (b) => { view.tab = b.dataset.tab; view.page = 0; view.reloadTab(); },
  gpage: (b) => { view.page = Math.max(0, view.page + +b.dataset.d); view.reloadTab(); },
  'open-game': (b) => openGame(+b.dataset.id).catch((e) => toast(e.message, 'bad')),
  'toggle-add': () => $('#addengine').classList.toggle('hidden'),
  'edit-engine': (b) => editEngine(b.dataset.id),
  'create-exp': async () => {
    try { const r = await api('POST', '/api/experiments', { name: $('#x_name').value, description: $('#x_desc').value }); location.hash = `#/experiment/${r.id}`; }
    catch (e) { toast(e.message, 'bad'); }
  },
  'rename-exp': async (b) => {
    const x = await api('GET', `/api/experiments/${b.dataset.id}`);
    const name = prompt('Experiment name', x.name); if (name == null) return;
    const description = prompt('Description', x.description); if (description == null) return;
    try { await api('PATCH', `/api/experiments/${x.id}`, { name, description }); refresh(); } catch (e) { toast(e.message, 'bad'); }
  },
  'save-global': async () => {
    try {
      await api('PUT', '/api/settings', { ...readFields($('#g_hw')), ...readFields($('#g_defaults')), ...readFields($('#g_notify')) });
      toast('Settings saved', 'ok'); pageSettings();
    } catch (e) { toast(e.message, 'bad'); }
  },
  'test-webhook': async () => {
    try { await api('POST', '/api/settings/test-webhook', { url: $('[data-k=discord_webhook_url]').value }); toast('Message sent. Check Discord.', 'ok'); }
    catch (e) { toast('Webhook failed: ' + e.message, 'bad'); }
  },
  'rescan-openings': async () => { await api('POST', '/api/openings/rescan'); pageSettings(); },
};

document.addEventListener('click', (e) => {
  const b = e.target.closest('[data-act]');
  if (b && ACTIONS[b.dataset.act]) { e.preventDefault(); e.stopPropagation(); ACTIONS[b.dataset.act](b); return; }
  const tr = e.target.closest('tr[data-href]');
  if (tr && !e.target.closest('a,button,input,select')) location.hash = tr.dataset.href;
});

async function route() {
  stopPolling();
  closeModal();
  const [path, qs] = location.hash.replace(/^#/, '').split('?');
  const params = new URLSearchParams(qs || '');
  const parts = path.split('/').filter(Boolean);
  const page = parts[0] || 'tests';
  $$('.rail a[data-nav]').forEach((a) => a.classList.toggle('active', a.dataset.nav === (page === 'test' ? 'tests' : page === 'experiment' ? 'experiments' : page)));
  try {
    if (page === 'tests') await pageTests(params);
    else if (page === 'test') await pageTest(+parts[1]);
    else if (page === 'engines') await pageEngines();
    else if (page === 'experiments') await pageExperiments();
    else if (page === 'experiment') await pageExperiment(+parts[1]);
    else if (page === 'settings') await pageSettings();
    else main.innerHTML = '<div class="empty"><strong>Page not found</strong><a href="#/tests">Back to tests</a></div>';
  } catch (e) {
    main.innerHTML = `<div class="notice bad"><strong>Couldn't load this page.</strong> ${esc(e.message)}</div>`;
  }
}

async function refreshNow() {
  try {
    const list = await api('GET', '/api/tests?status=running');
    const queued = await api('GET', '/api/tests?status=queued');
    const el = $('#now');
    if (list.length) {
      const t = list[0];
      el.innerHTML = `Running <a href="#/test/${t.id}">#${t.id}</a><br>${fmt(t.games)} games${queued.length ? `<br>${queued.length} queued` : ''}`;
    } else el.textContent = queued.length ? `${queued.length} queued` : 'Idle';
  } catch (e) { /* server restarting */ }
}

window.addEventListener('hashchange', route);
route();
refreshNow();
setInterval(() => { if (!document.hidden) refreshNow(); }, 3000);
