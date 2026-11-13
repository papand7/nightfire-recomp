// The launcher's page: the shown game's service state drawn (main.js forwards each game's <short>LauncherService JSON
// lines, tagged with the game), the settings editor (schema.js, schema_auf.js), the texture pack and the log
const $ = id => document.getElementById(id);
const GAME_INFO = {
    nightfire: { title: 'NIGHTFIRE', schema: () => SCHEMA },
    auf: { title: 'AGENT UNDER FIRE', schema: () => SCHEMA_AUF },
};
let game = 'nightfire'; // the game tab shown
const SS = {};          // each game's service's last state
const LOGS = {};        // each game's log
const checkedUpdate = {};
let S = null;           // the shown game's state
const schema = () => GAME_INFO[game].schema();
const nightfire = () => game === 'nightfire';
let androidBusy = false;
let androidOutput = null;

function toast(text) {
    const t = $('toast');
    t.textContent = text;
    t.classList.add('show');
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => t.classList.remove('show'), 3200);
}
function mb(bytes) {
    if (!bytes) return '0 MB';
    return bytes >= 1e9 ? (bytes / 1073741824).toFixed(1) + ' GB' : Math.round(bytes / 1048576) + ' MB';
}
function setMsg(el, text, ok) {
    el.textContent = text || '';
    el.className = 'msg' + (text ? (ok ? ' ok' : ' bad') : '');
}

// ---- the window
document.querySelectorAll('[data-win]').forEach(b => b.onclick = () => nf.window(b.dataset.win));
document.querySelectorAll('#nav > button').forEach(b => b.onclick = () => show(b.dataset.page));
function show(page) {
    document.querySelectorAll('#nav > button').forEach(b => b.classList.toggle('active', b.dataset.page === page));
    document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + page));
    if (page === 'settings') loadSettings();
    if (page === 'textures') loadTextures();
}

// ---- the service
const logEl = $('log');
function log(line, id = game) {
    LOGS[id] = ((LOGS[id] || '') + line + '\n').slice(-400000);
    if (id !== game) return;
    logEl.textContent += line + '\n';
    if (logEl.textContent.length > 400000) logEl.textContent = logEl.textContent.slice(-300000);
    logEl.scrollTop = logEl.scrollHeight;
}
nf.onService(ev => {
    const id = ev.game || 'nightfire';
    if (ev.ev === 'state') SS[id] = ev;
    if (ev.ev === 'state' && !checkedUpdate[id]) { checkedUpdate[id] = true; nf.command({ cmd: 'check_update', game: id }); }
    if (id !== game) {  // the other tab's: kept for when it is shown
        if (ev.ev === 'log') log(ev.line, id);
        else if (ev.ev === 'error') log('error: ' + ev.message, id);
        return;
    }
    if (ev.ev === 'state') {
        S = ev;
        render();
    } else if (ev.ev === 'status') {
        $('progress').hidden = false;
        $('step').textContent = ev.step || 'Working…';
        $('detail').textContent = ev.detail || '';
        const fill = $('bar-fill');
        fill.classList.toggle('indet', !(ev.fraction >= 0));
        if (ev.fraction >= 0) fill.style.width = (ev.fraction * 100).toFixed(1) + '%';
    } else if (ev.ev === 'log') {
        log(ev.line);
    } else if (ev.ev === 'done') {
        if (ev.job === 'play') {
            if (ev.ok) toast('Starting the game…');
            else setMsg($('build-msg'), ev.message, false);
        } else if (ev.job === 'music') {
            setMsg($('music-msg'), ev.message, ev.ok);
        } else if (ev.job === 'build' || ev.job === 'remove_build' || ev.job === 'start_over' || ev.job === 'install_requirements') {
            setMsg($('build-msg'), ev.message, ev.ok);
            if (ev.ok && ev.job === 'build') toast('The game is built. Press Play.');
        }
    } else if (ev.ev === 'error') {
        setMsg($('build-msg'), ev.message, false);
        log('error: ' + ev.message);
    }
});

function render() {
    if (!S) return;
    $('btn-android').disabled = androidBusy || S.busy || !S.built;
    $('version').textContent = 'Launcher ' + S.version;
    $('nav-music').hidden = !S.music;
    const g = S.game || {};
    const line = $('game-line');
    line.className = 'status-line ' + (g.ok ? 'ok' : 'bad');
    line.innerHTML = '<span class="dot"></span>';
    line.append(g.ok ? `${g.release} disc image found` : 'No game yet');
    $('game-path').textContent = g.ok ? g.path : (g.error || '');

    const busy = S.busy || androidBusy, building = busy && S.job === 'build';
    $('build-title').textContent = !S.built ? 'Install' : S.update ? 'Update ready' : 'Installed';
    const info = $('build-info');
    if (!S.built) {
        info.innerHTML = '';
        info.append(S.os !== 'windows'
            ? `The game is built on this PC from your disc with your system's compilers (the launcher says which packages
            to install if any are missing) and takes 10 to 60 minutes. Needs about 3 GB free; this drive has ${mb(S.free_bytes)}.`
            : `The game is built on this PC from your disc. The first time this downloads about 1.4 GB of build tools
            (nothing is installed in Windows) and takes 30 to 90 minutes. Needs about 3 GB free; this drive has ${mb(S.free_bytes)}.`);
        info.className = 'muted';
    } else {
        info.textContent = S.update ? 'A new version of the game is ready to build. Your saves and settings are kept.'
                                    : `Built and ready${S.built_version ? ' (' + S.built_version + ')' : ''}.`;
        info.className = S.update ? '' : 'msg ok';
    }
    if (S.path_too_long) setMsg($('build-msg'), S.path_message, false);
    $('license-row').hidden = S.built || !S.license_url;  // Linux: nothing of Microsoft's is fetched
    $('license').checked = S.agreed || $('license').checked;
    $('license').disabled = busy;
    const canBuild = g.ok && (S.built || $('license').checked) && !S.path_too_long && !busy;
    const bb = $('btn-build');
    // the big INSTALL button installs; this one only updates (or rebuilds) an installed game
    bb.textContent = S.update ? 'Update' : 'Rebuild';
    bb.disabled = !canBuild;
    bb.hidden = building || !S.built;
    $('btn-stop').hidden = !building;
    $('progress').hidden = !busy || S.job === 'check_update';
    const clean = $('btn-clean');
    clean.hidden = !S.built || !S.build_bytes || busy;
    clean.textContent = `Remove build files (${mb(S.build_bytes)})`;
    $('btn-startover').hidden = busy;
    $('btn-reqs').hidden = busy || !S.install_command;  // Linux: the build packages it found missing
    $('btn-pick').disabled = $('btn-scan').disabled = busy;

    const play = $('btn-play');
    $('play-label').textContent = S.built ? 'PLAY' : 'INSTALL';
    play.disabled = busy || !g.ok || (!S.built && !canBuild);
    $('play-hint').textContent = !g.ok ? 'Put your .iso beside the launcher first.'
        : building ? 'Building…' : !S.built ? ($('license').checked ? 'Builds the game, then it is ready to play.' : 'Accept the license terms to install.')
        : 'In the game: F1 opens the PC settings.';
    $('btn-folder').disabled = $('btn-logs').disabled = !S.built;

    const rb = $('release-banner');
    rb.hidden = !S.release;
    if (S.release) {
        rb.innerHTML = '';
        rb.append(`Version ${S.release.tag} is out (this launcher is ${S.version}). `);
        const a = document.createElement('a');
        a.href = '#'; a.textContent = 'Download it';
        a.onclick = e => { e.preventDefault(); nf.open(S.release.page); };
        rb.append(a);
    }
    $('music-dir').textContent = S.music_dir ? 'Your music: ' + S.music_dir : '';
    $('btn-music-again').hidden = !S.music_dir;
    $('btn-music').disabled = $('btn-music-again').disabled = busy;
}

function build() {
    setMsg($('build-msg'), '');
    if (!S.built && !S.agreed) nf.command({ cmd: 'agree' });
    nf.command({ cmd: 'build' });
}
$('license').onchange = render;
$('license-link').onclick = e => { e.preventDefault(); nf.open(S ? S.license_url : 'https://go.microsoft.com/fwlink/?LinkId=2179911'); };
$('btn-build').onclick = build;
$('btn-stop').onclick = () => nf.command({ cmd: 'cancel' });
$('btn-clean').onclick = () => nf.command({ cmd: 'remove_build' });
$('btn-reqs').onclick = () => nf.command({ cmd: 'install_requirements' });
$('btn-startover').onclick = () => {
    if (confirm('Start over? This removes the installed game, the downloaded source and its build (not the build tools, your disc image, your saves, settings or HD textures). Then press Install.'))
        nf.command({ cmd: 'start_over' });
};
$('btn-scan').onclick = () => nf.command({ cmd: 'scan' });
$('btn-pick').onclick = async () => { const p = await nf.pick('iso'); if (p) nf.command({ cmd: 'set_game', path: p }); };
$('btn-play').onclick = () => { if (S.built) { setMsg($('build-msg'), ''); nf.command({ cmd: 'play' }); } else build(); };
$('btn-folder').onclick = () => nf.open(S.play_dir);
// (the service's own separator: Linux and macOS paths have no backslashes)
const sub = (dir, name) => dir + (String(dir).includes('\\') ? '\\' : '/') + name;
$('btn-logs').onclick = () => nf.open(sub(S.play_dir, 'logs'));
$('btn-launcher-log').onclick = () => S && nf.open(sub(S.root, 'launcher.log'));
$('btn-music').onclick = async () => { const p = await nf.pick('music'); if (p) { setMsg($('music-msg'), 'Converting…', true); nf.command({ cmd: 'music', path: p }); } };
$('btn-music-again').onclick = () => { setMsg($('music-msg'), 'Converting…', true); nf.command({ cmd: 'music', path: S.music_dir }); };

// ---- settings
let files = null;       // what settings:load read
let edits = { s: {}, g: {} };
let tab = SCHEMA[0].id;

function value(f, key) {
    if (key in edits[f]) return edits[f][key];
    const table = f === 's' ? files.settings : files.graphics;
    return table[key];
}
function itemValue(it) {
    if (it.key === 'resolution') {
        const w = value('s', 'width') || 0, h = value('s', 'height') || 0;
        return `${w}x${h}`;
    }
    const v = value(it.f, it.key);
    // a choice since removed reads as the one the game takes it for (it.alias: old value -> today's)
    if (it.alias && v in it.alias) return it.alias[v];
    return v === undefined ? it.def : v;
}
function vulkan() { return !nightfire() || (value('s', 'renderer') || 'd3d12') === 'vulkan'; }  // Agent Under Fire: Vulkan only
function graphicsNow() {
    const out = {};
    for (const p of schema()) for (const gr of p.groups) for (const it of gr.items) if (it.f === 'g') out[it.key] = itemValue(it);
    return out;
}
function find(f, key) {
    for (const p of schema()) for (const gr of p.groups) for (const it of gr.items) if (it.f === f && it.key === key) return it;
    return null;
}
const v = (f, key) => itemValue(find(f, key));

// the quality preset and the five values it sets (ApplyQuality / MatchQuality in style.cpp)
function applyQuality(q) {
    const p = QUALITY[q];
    if (!p) return;
    const smaa = p.aa === 'smaa' && vulkan(), fxaa = p.aa === 'fxaa' || (p.aa === 'smaa' && !vulkan());
    Object.assign(edits.s, { anisotropic_filtering: p.aniso, fxaa, smaa });
    Object.assign(edits.g, { quality_ao: p.ao, quality_bloom: p.bloom });
}
function matchQuality() {
    for (const [q, p] of Object.entries(QUALITY)) {
        const smaa = p.aa === 'smaa' && vulkan(), fxaa = p.aa === 'fxaa' || (p.aa === 'smaa' && !vulkan());
        if (v('s', 'anisotropic_filtering') === p.aniso && !!v('s', 'fxaa') === fxaa && !!v('s', 'smaa') === smaa &&
            v('g', 'quality_ao') === p.ao && !!v('g', 'quality_bloom') === p.bloom) return q;
    }
    return 'custom';
}
const QUALITY_KEYS = ['s:anisotropic_filtering', 's:fxaa', 's:smaa', 'g:quality_ao', 'g:quality_bloom'];
// the shader style's own values (the Shader Style page but its style choice)
const STYLE_KEYS = new Set(SCHEMA.find(p => p.id === 'style').groups.flatMap(gr => gr.items.map(it => it.key)).filter(k => k !== 'mode'));

function set(it, val) {
    if (it.key === 'resolution') {
        const [w, h] = val.split('x').map(Number);
        edits.s.width = w; edits.s.height = h;
    } else {
        edits[it.f][it.key] = val;
    }
    if (nightfire()) {  // NightFire's quality preset and shader styles (graphics.toml)
        if (it.f === 'g' && it.key === 'quality') applyQuality(val);
        else if (QUALITY_KEYS.includes(it.f + ':' + it.key)) edits.g.quality = matchQuality();
        if (it.key === 'mode') edits.g.preset = val;  // a built-in style chosen by its name
        else if (STYLE_KEYS.has(it.key)) edits.g.preset = '';  // a style's value changed: "(Edited)"
    }
    $('savebar').hidden = false;
    renderSettings();
}

async function loadSettings() {
    files = await nf.loadSettings();
    edits = { s: {}, g: {} };
    $('savebar').hidden = true;
    const w = $('settings-warn');
    w.hidden = true;
    if (files.running) { w.hidden = false; w.textContent = 'The game is running: close it before saving, or it writes its own settings over these.'; }
    else if (!files.haveSettings) { w.hidden = false; w.textContent = 'The game has not been started yet: these are its defaults. Saving writes them for its first start.'; }
    renderSettings();
}

function control(it) {
    const val = itemValue(it);
    if (it.options) {
        const s = document.createElement('select');
        for (const o of it.options) {
            const op = document.createElement('option');
            op.value = JSON.stringify(o.value); op.textContent = o.label;
            if (o.value === val) op.selected = true;
            s.append(op);
        }
        if (!it.options.some(o => o.value === val)) {
            const op = document.createElement('option');
            op.value = JSON.stringify(val); op.textContent = String(val); op.selected = true;
            s.append(op);
        }
        s.onchange = () => set(it, JSON.parse(s.value));
        return s;
    }
    if (it.type === 'range') {
        const wrap = document.createElement('div');
        wrap.className = 'range';
        const r = document.createElement('input');
        r.type = 'range'; r.min = it.min; r.max = it.max; r.step = it.step;
        r.value = Math.min(it.max, Math.max(it.min, Number(val)));
        const out = document.createElement('output');
        const dec = String(it.step).includes('.') ? String(it.step).split('.')[1].length : 0;
        const paint = () => {
            out.textContent = Number(r.value).toFixed(dec) + (it.unit || '');
            r.style.setProperty('--p', ((r.value - it.min) / (it.max - it.min) * 100) + '%');
        };
        paint();
        r.oninput = paint;
        r.onchange = () => set(it, dec ? Number(Number(r.value).toFixed(dec)) : Math.round(Number(r.value)));
        wrap.append(r, out);
        return wrap;
    }
    if (it.type === 'text') {
        const t = document.createElement('input');
        t.type = 'text'; t.value = val ?? ''; t.spellcheck = false;
        t.onchange = () => set(it, t.value.trim());
        return t;
    }
    if (it.type === 'int') {
        const n = document.createElement('input');
        n.type = 'number'; n.min = it.min; n.max = it.max; n.value = val;
        n.onchange = () => set(it, Math.round(Math.min(it.max, Math.max(it.min, Number(n.value) || 0))));
        return n;
    }
    const c = document.createElement('input');
    c.type = 'checkbox'; c.className = 'switch'; c.checked = !!val;
    c.onchange = () => set(it, c.checked);
    return c;
}

function renderSettings() {
    if (!files) return;
    const tabs = $('settings-tabs');
    tabs.innerHTML = '';
    for (const p of schema()) {
        const b = document.createElement('button');
        b.textContent = p.title;
        b.className = p.id === tab ? 'on' : '';
        b.onclick = () => { tab = p.id; renderSettings(); };
        tabs.append(b);
    }
    const page = schema().find(p => p.id === tab) || schema()[0];
    const body = $('settings-body');
    body.innerHTML = '';
    const g = graphicsNow();
    const vk = vulkan();
    for (const gr of page.groups) {
        if (gr.when && !gr.when(g)) continue;
        const card = document.createElement('div');
        card.className = 'card';
        const head = document.createElement('div');
        head.className = 'card-head'; head.textContent = gr.title;
        const cb = document.createElement('div');
        cb.className = 'card-body';
        cb.style.gap = '0';
        for (const it of gr.items) {
            const row = document.createElement('div');
            const needsVk = (page.vulkan || it.vulkan) && !vk;
            row.className = 'set' + (needsVk ? ' off' : '');
            const lbl = document.createElement('div');
            lbl.className = 'lbl';
            lbl.textContent = it.label;
            const changed = it.key === 'resolution' ? ('width' in edits.s) : (it.key in edits[it.f]);
            if (changed) lbl.insertAdjacentHTML('beforeend', '<span class="tag changed">CHANGED</span>');
            if (needsVk) lbl.insertAdjacentHTML('beforeend', '<span class="tag">VULKAN</span>');
            if (it.restart) lbl.insertAdjacentHTML('beforeend', '<span class="tag">NEXT START</span>');
            row.append(lbl, control(it));
            if (it.help) {
                const h = document.createElement('div');
                h.className = 'help'; h.textContent = it.help;
                row.append(h);
            }
            cb.append(row);
        }
        card.append(head, cb);
        body.append(card);
    }
    if (page.vulkan && !vk) {
        const n = document.createElement('div');
        n.className = 'note';
        n.textContent = 'These are drawn by the Vulkan renderer only: Graphics > Renderer > Vulkan.';
        body.prepend(n);
    }
    if (page.note) {
        const n = document.createElement('div');
        n.className = 'note'; n.textContent = page.note;
        body.append(n);
    }
}
$('btn-revert').onclick = () => loadSettings();
$('btn-save').onclick = async () => {
    const r = await nf.saveSettings({ settings: edits.s, graphics: edits.g });
    if (r.ok) { toast('Settings saved'); loadSettings(); }
    else toast(r.message);
};

// ---- the HD texture pack
let packOpts = { scale: 4, model: 'x4plus' };
function seg(id, key) {
    document.querySelectorAll(`#${id} button`).forEach(b => b.onclick = () => {
        document.querySelectorAll(`#${id} button`).forEach(x => x.classList.toggle('on', x === b));
        packOpts[key] = key === 'scale' ? Number(b.dataset.v) : b.dataset.v;
    });
}
seg('seg-scale', 'scale');
seg('seg-model', 'model');

async function loadTextures() {
    const [info, f] = await Promise.all([nf.textureInfo(), nf.loadSettings()]);
    files = files || f;
    $('n-dumped').textContent = info.dumped;
    $('n-pack').textContent = info.pack;
    $('n-maps').textContent = info.maps;
    const gr = nightfire() ? f.graphics : f.settings;  // Agent Under Fire keeps these in settings.toml
    $('collect-row').hidden = $('collect-text').hidden = !info.canCollect;
    $('pbr-row').hidden = $('relief-row').hidden = !nightfire();
    $('dump').checked = !!gr.dump_textures;
    $('use-pack').checked = gr.texture_pack !== false;
    $('use-relief').checked = !!gr.materials && (gr.surface_depth || 0) > 0;
    $('btn-collect').disabled = !info.game || info.collecting || info.running;
    $('btn-collect').textContent = info.collecting ? 'Collecting...' : 'Collect all textures now';
    const ready = info.tool && !info.running && !info.collecting;
    $('btn-pack').disabled = !ready || !info.dumped;
    $('btn-pack').hidden = info.running;
    $('btn-pack-stop').hidden = !info.running;
    $('pack-progress').hidden = !info.running;
    $('btn-pack-folder').onclick = () => nf.open(info.folder);
    if (!info.tool) setMsg($('pack-msg'), 'Install the game first: the pack maker comes with it.', false);
    else if (!info.dumped && !info.running) setMsg($('pack-msg'), info.canCollect ? 'Nothing collected yet: press Collect all textures now, or turn on saving the textures and play.' : 'Nothing collected yet: turn on saving the textures and play.', false);
    else if (!info.running && $('pack-msg').classList.contains('bad')) setMsg($('pack-msg'), '');
}
async function saveGraphics(patch, text) {
    const r = await nf.saveSettings(nightfire() ? { graphics: patch } : { settings: patch });
    toast(r.ok ? text : r.message);
    if (!r.ok) loadTextures();
    else files = null;
}
$('dump').onchange = () => saveGraphics({ dump_textures: $('dump').checked }, $('dump').checked ? 'The game saves its textures while you play.' : 'Texture saving off.');
$('use-pack').onchange = () => saveGraphics({ texture_pack: $('use-pack').checked }, 'Saved');
$('use-relief').onchange = () => {
    const on = $('use-relief').checked;
    saveGraphics(on ? { materials: true, surface_depth: 0.6, ground_detail: true } : { surface_depth: 0 },
                 on ? 'PBR + Relief on (Vulkan, at the next start).' : 'Relief off.');
};
$('btn-pack').onclick = async () => {
    $('pack-log').textContent = '';
    setMsg($('pack-msg'), '');
    const r = await nf.makeTextures({ scale: packOpts.scale, model: packOpts.model, pbr: $('pbr').checked, force: $('force').checked });
    if (!r.ok) setMsg($('pack-msg'), r.message, false);
    loadTextures();
};
$('btn-pack-stop').onclick = () => nf.stopTextures();
$('btn-collect').onclick = async () => {
    setMsg($('collect-msg'), '');
    const r = await nf.collectTextures();
    if (!r.ok) setMsg($('collect-msg'), r.message, false);
    else setMsg($('collect-msg'), 'Collecting: the game closes when it is done.', true);
    loadTextures();
};
nf.onCollect(ev => {
    setMsg($('collect-msg'), ev.code === 0 ? 'Done: the textures are collected. Now make the pack below.' : `The game stopped before it finished (exit ${ev.code}).`, ev.code === 0);
    loadTextures();
});
nf.onPack(ev => {
    if (ev.game && ev.game !== game) return;
    const el = $('pack-log');
    if (ev.line) { el.textContent += ev.line + '\n'; el.scrollTop = el.scrollHeight; }
    if (ev.done) {
        setMsg($('pack-msg'), ev.code === 0 ? 'The pack is made. The game loads it at its next start.' : `The pack maker stopped (exit ${ev.code}).`, ev.code === 0);
        loadTextures();
    }
});

nf.state('nightfire').then(s => { if (s) { S = SS.nightfire = s; render(); } });

// ---- the game tabs: NightFire | Agent Under Fire, each with its own service, folder, settings and log
async function switchGame(id) {
    if (id === game || !GAME_INFO[id]) return;
    game = id;
    const r = await nf.setGame(id);
    document.querySelectorAll('#gametabs button').forEach(b => b.classList.toggle('on', b.dataset.game === id));
    $('hero-title').textContent = GAME_INFO[id].title;
    $('nav-android').hidden = !nightfire();
    S = SS[id] || await nf.state(id) || null;
    if (S) SS[id] = S;
    logEl.textContent = LOGS[id] || '';
    logEl.scrollTop = logEl.scrollHeight;
    setMsg($('build-msg'), r && r.missing ? 'This launcher was made without this game: get the full 007 Remastered launcher.' : '', false);
    $('progress').hidden = true;
    $('pack-log').textContent = '';
    setMsg($('pack-msg'), ''); setMsg($('collect-msg'), ''); setMsg($('music-msg'), '');
    files = null; edits = { s: {}, g: {} }; tab = schema()[0].id;
    $('savebar').hidden = true;
    const page = document.querySelector('#nav > button.active');
    show(page && (page.dataset.page !== 'android' || nightfire()) ? page.dataset.page : 'home');
    render();
}
document.querySelectorAll('#gametabs button').forEach(b => b.onclick = () => switchGame(b.dataset.game));

// ---- Android export
$('btn-android').onclick = async () => {
    $('btn-android').disabled = true;
    try {
        const r = await nf.makeAndroid({ abi: $('android-abi').value, textures: $('android-textures').checked });
        if (r.ok) {
            androidBusy = true;
            $('android-log').textContent = '';
            $('android-progress').hidden = false;
            $('btn-android-folder').hidden = true;
            setMsg($('android-msg'), 'Building Android package...', true);
        } else if (!r.cancelled) setMsg($('android-msg'), r.message, false);
    } catch (e) { setMsg($('android-msg'), e.message, false); }
    render();
};
nf.onAndroid(ev => {
    if (ev.line) {
        const el = $('android-log');
        el.textContent = (el.textContent + ev.line + '\n').slice(-300000);
        el.scrollTop = el.scrollHeight;
    }
    if (ev.done) {
        androidBusy = false;
        $('android-progress').hidden = true;
        setMsg($('android-msg'), ev.line, ev.ok);
        androidOutput = ev.ok ? ev.path : null;
        $('btn-android-folder').hidden = !androidOutput;
        render();
    }
});
$('btn-android-folder').onclick = () => nf.revealAndroid();
// a newer launcher: a banner at the top, asked at start, every 6 hours and by Check for updates. Update now downloads
// it and puts it in this one's place (the portable exe, the AppImage); elsewhere Download opens the release page.
async function checkLauncherUpdate(asked) {
    let u = null;
    try { u = await nf.launcherUpdate(); } catch (e) { if (asked) toast('Could not check for updates.'); return; }
    if (!u) { if (asked) toast('This launcher is the newest.'); return; }
    if (!asked && localStorage.getItem('update-later') === u.version) return;
    $('update-text').textContent = `007 Remastered ${u.version} is out (you have ${u.current}).`;
    $('update-get').textContent = u.installable ? 'Update now' : 'Download';
    $('update-get').disabled = false;
    $('update-get').onclick = async () => {
        if (!u.installable) return nf.open(u.url);
        $('update-get').disabled = true;
        $('update-text').textContent = 'Downloading the new launcher…';
        const r = await nf.installUpdate();
        if (r.ok) $('update-text').textContent = 'Restarting with the new launcher…';
        else { $('update-text').textContent = r.message; $('update-get').disabled = false; $('update-get').textContent = 'Download'; u.installable = false; }
    };
    $('update-later').onclick = () => { localStorage.setItem('update-later', u.version); $('update-banner').hidden = true; };
    $('update-banner').hidden = false;
    toast(`Version ${u.version} is available`);
}
checkLauncherUpdate();
setInterval(checkLauncherUpdate, 6 * 3600 * 1000);
$('btn-check-update').onclick = async () => {
    const b = $('btn-check-update');
    b.disabled = true; b.textContent = 'Checking…';
    await checkLauncherUpdate(true);
    b.disabled = false; b.textContent = 'Check for updates';
};
nf.onUpdate(ev => {
    $('update-text').textContent = 'Downloading the new launcher… ' +
        (ev.fraction >= 0 ? Math.round(ev.fraction * 100) + '%' : mb(ev.done));
});
