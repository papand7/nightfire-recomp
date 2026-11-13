// 007 Remastered's launcher window (Electron, Windows and Linux): NightFire and Agent Under Fire, a tab each. For each,
// the player's disc image found beside it, the game built on this PC by its <short>LauncherService (sdk/launcher/service.cpp: the same builder as the ImGui launcher,
// driven by JSON lines), the game's settings.toml and graphics.toml edited without starting it, the HD texture pack
// made (hd_pack, Windows).
const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const { spawn, execFile } = require('child_process');
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const TOML = require('smol-toml');

const WINDOWS = process.platform === 'win32';
const EXE = WINDOWS ? '.exe' : '';
// the folder the player put the launcher (and their .iso) in: a portable exe runs from a temporary copy, an AppImage
// from its mounted image (APPIMAGE is the file itself)
const LAUNCHER_DIR = process.env.PORTABLE_EXECUTABLE_DIR ||
    (process.env.APPIMAGE ? path.dirname(process.env.APPIMAGE) : null) ||
    (app.isPackaged ? path.dirname(app.getPath('exe')) : (process.env.NF_LAUNCHER_DIR || path.resolve(__dirname, '../../out/launcher-dev')));
// the games this launcher installs, each by its own service (sdk/launcher/service.cpp built with that game's
// launcher/game.json) in its own work folder beside the launcher. Agent Under Fire's service is built from that
// game's repository (build_service.bat, AUF_REPO).
const SVC_DIR = name => app.isPackaged ? path.join(process.resourcesPath, name + EXE)
                                       : path.resolve(__dirname, '../../out', name === 'NightFireLauncherService' ? 'launcher-svc' : 'launcher-svc-auf', name + EXE);
const GAMES = {
    nightfire: { root: 'NightFire', service: SVC_DIR('NightFireLauncherService'), exe: 'Nightfire',
                 exes: ['Nightfire', 'NightfireDriving'], collect: true, android: true },
    auf: { root: 'AgentUnderFire', service: SVC_DIR('AgentUnderFireLauncherService'), exe: 'AgentUnderFire',
           exes: ['AgentUnderFire', 'AgentUnderFireBond', 'AgentUnderFireBondFre', 'AgentUnderFireBondGer', 'AgentUnderFireDriving'],
           collect: false, android: false },
};
for (const [id, g] of Object.entries(GAMES)) Object.assign(g, { id, dir: path.join(LAUNCHER_DIR, g.root), svc: null, state: null });
let active = 'nightfire';  // the game tab the window shows: the settings and texture calls are about it
const G = () => GAMES[active];

let win = null;
let android = null;
let androidOutput = null;
let pack = null;  // the running hd_pack
let collect = null;  // the game running to collect the textures from its files

function send(channel, data) {
    if (win && !win.isDestroyed()) win.webContents.send(channel, data);
}

function startService(g) {
    const emit = ev => send('svc', { ...ev, game: g.id });
    if (!fs.existsSync(g.service)) {
        g.missing = true;
        emit({ ev: 'error', message: 'This launcher was made without this game\'s service.' });
        return;
    }
    fs.mkdirSync(g.dir, { recursive: true });
    // the game's source the launcher carries (git bundles of the game and its runtime): installs need no download of it
    const bundles = app.isPackaged ? path.join(process.resourcesPath, 'source') : path.resolve(__dirname, '../../out/source-bundles');
    const svc = g.svc = spawn(g.service, ['--games', LAUNCHER_DIR, '--root', g.dir],
                { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, NF_SOURCE_BUNDLES: bundles } });
    readline.createInterface({ input: svc.stdout }).on('line', line => {
        let ev;
        try { ev = JSON.parse(line); } catch { return; }
        if (ev.ev === 'state') g.state = ev;
        emit(ev);
    });
    readline.createInterface({ input: svc.stderr }).on('line', line => emit({ ev: 'log', line }));
    svc.on('error', err => emit({ ev: 'error', message: `The launcher service did not start: ${err.message}` }));
    svc.on('exit', code => {
        g.svc = null;
        if (!app.isQuitting) emit({ ev: 'error', message: `The launcher service stopped (exit ${code}).` });
    });
}

// cmd.game: the game it is for (else the shown one)
function command(cmd) {
    const g = GAMES[cmd.game] || G();
    if (android && !['state', 'quit'].includes(cmd.cmd)) return;
    const { game, ...rest } = cmd;
    if (g.svc && g.svc.stdin.writable) g.svc.stdin.write(JSON.stringify(rest) + '\n');
}

// a new or removed .iso beside the launcher is looked at again, by each game's service that has none yet
function watchGames() {
    let timer = null;
    try {
        fs.watch(LAUNCHER_DIR, (_, name) => {
            if (!name || !/\.iso$/i.test(name)) return;
            clearTimeout(timer);
            timer = setTimeout(() => {
                for (const g of Object.values(GAMES)) if (!(g.state && g.state.game && g.state.game.ok)) command({ cmd: 'scan', game: g.id });
            }, 1500);
        });
    } catch { /* a folder that cannot be watched: the Scan again button */ }
}

function createWindow() {
    win = new BrowserWindow({
        width: 1180, height: 760, minWidth: 960, minHeight: 640,
        frame: false, backgroundColor: '#07080b', show: false,
        title: '007 Remastered',
        icon: path.resolve(__dirname, WINDOWS ? '../icon.ico' : '../icon.png'),
        webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true },
    });
    win.removeMenu();
    win.loadFile(path.join(__dirname, 'ui', 'index.html'));
    win.once('ready-to-show', () => win.show());
    // a test's pictures (not in a packaged launcher): NF_SHOT=<folder>, each page in turn
    if (process.env.NF_SHOT && !app.isPackaged) {
        win.webContents.on('console-message', e => console.log(`page: ${e.message} (${e.sourceId}:${e.lineNumber})`));
        win.webContents.once('did-finish-load', async () => {
            const wait = ms => new Promise(r => setTimeout(r, ms));
            await wait(2500);
            // a page as [game@]page[:settings tab], e.g. auf@settings:effects
            for (const page of (process.env.NF_SHOT_PAGES || 'home,settings,textures').split(',')) {
                const [g, rest] = page.includes('@') ? page.split('@') : [null, page];
                const [name, tab] = rest.split(':');
                if (g) { await win.webContents.executeJavaScript(`switchGame(${JSON.stringify(g)})`); await wait(600); }
                await win.webContents.executeJavaScript(`show(${JSON.stringify(name)});` + (tab ? `tab=${JSON.stringify(tab)};renderSettings();` : ''));
                await wait(900);
                const img = await win.webContents.capturePage();
                fs.writeFileSync(path.join(process.env.NF_SHOT, `launcher_${page.replace(/[:@]/g, '_')}.png`), img.toPNG());
            }
            app.quit();
        });
    }
    win.on('maximize', () => send('win', { maximized: true }));
    win.on('unmaximize', () => send('win', { maximized: false }));
    win.webContents.setWindowOpenHandler(({ url }) => { shell.openExternal(url); return { action: 'deny' }; });
}

// ---- the game's settings: settings.toml and graphics.toml in play\, beside the exe
function playDir(g = G()) { return (g.state && g.state.play_dir) || path.join(g.dir, 'play'); }

function readToml(file) {
    try { return TOML.parse(fs.readFileSync(file, 'utf8')); } catch { return null; }
}
function writeToml(file, table, header) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + '.launcher.tmp';
    fs.writeFileSync(tmp, (header ? header + '\n' : '') + TOML.stringify(table) + '\n');
    fs.renameSync(tmp, file);
}

// the game in this launcher's play folder running (another copy elsewhere writes its own files)
function gameRunning() {
    const GAME_EXES = G().exes;
    if (!WINDOWS) {
        // each process's executable, /proc/<pid>/exe (the player's own processes are readable)
        const dir = path.resolve(playDir());
        let found = false;
        try {
            for (const pid of fs.readdirSync('/proc')) {
                if (!/^\d+$/.test(pid)) continue;
                let exe;
                try { exe = fs.readlinkSync(`/proc/${pid}/exe`); } catch { continue; }
                if (GAME_EXES.includes(path.basename(exe)) && path.dirname(exe) === dir) { found = true; break; }
            }
        } catch { /* no /proc: not known, taken as not running */ }
        return Promise.resolve(found);
    }
    const script = "Get-CimInstance Win32_Process -Filter \"" + GAME_EXES.map(n => `Name='${n}.exe'`).join(' or ') +
        "\" | ForEach-Object { $_.ExecutablePath }";
    return new Promise(resolve => {
        execFile('powershell', ['-NoProfile', '-NonInteractive', '-Command', script], { windowsHide: true }, (err, out) => {
            if (err) return resolve(false);
            const dir = path.resolve(playDir()).toLowerCase();
            resolve(out.split(/\r?\n/).some(p => p.trim() && path.dirname(path.resolve(p.trim())).toLowerCase() === dir));
        });
    });
}

ipcMain.handle('settings:load', async () => {
    const dir = playDir();
    const s = readToml(path.join(dir, 'settings.toml'));
    const g = readToml(path.join(dir, 'graphics.toml'));
    return { settings: s || {}, graphics: g || {}, haveSettings: !!s, haveGraphics: !!g, dir, running: await gameRunning() };
});

// patch: { settings: {key: value}, graphics: {key: value} }, merged into what the files hold (the tables kept)
ipcMain.handle('settings:save', async (_, patch) => {
    if (await gameRunning()) return { ok: false, message: 'The game is running: close it first, or it writes its own settings over these.' };
    const dir = playDir();
    try {
        if (patch.settings && Object.keys(patch.settings).length) {
            const f = path.join(dir, 'settings.toml');
            writeToml(f, Object.assign(readToml(f) || {}, patch.settings));
        }
        if (patch.graphics && Object.keys(patch.graphics).length) {
            const f = path.join(dir, 'graphics.toml');
            writeToml(f, Object.assign(readToml(f) || {}, patch.graphics), '# graphics styles (the game\'s Graphics Style settings; written by the launcher)');
        }
        return { ok: true };
    } catch (e) {
        return { ok: false, message: e.message };
    }
});

// ---- the HD texture pack
function countPng(dir, filter) {
    try { return fs.readdirSync(dir).filter(n => /\.png$/i.test(n) && (!filter || filter(n))).length; } catch { return 0; }
}
// the pack maker: the game's own, else NightFire's (Agent Under Fire's builds none; hd_pack only reads mods/textures/dump
// and writes the pack beside it, the same in both games)
function hdPack(dir) {
    for (const d of [dir, playDir(GAMES.nightfire)]) {
        const exe = path.join(d, 'tools', 'hd_pack' + EXE);
        if (fs.existsSync(exe)) return exe;
    }
    return null;
}
ipcMain.handle('textures:info', async () => {
    const dir = playDir(), tex = path.join(dir, 'mods', 'textures');
    return {
        dumped: countPng(path.join(tex, 'dump')),
        pack: countPng(tex, n => !/_(n|r)\.png$/i.test(n)),
        maps: countPng(tex, n => /_n\.png$/i.test(n)),
        tool: !!hdPack(dir),
        canCollect: G().collect,
        folder: tex,
        running: !!pack,
        collecting: !!collect,
        game: fs.existsSync(path.join(dir, G().exe + EXE)),
    };
});
ipcMain.handle('textures:run', async (_, o) => {
    if (pack) return { ok: false, message: 'Already making the pack.' };
    const dir = playDir(), exe = hdPack(dir);
    if (!exe) return { ok: false, message: "hd_pack is not there yet: build the game first (for Agent Under Fire, NightFire's comes with NightFire)." };
    const args = ['--dir', dir, '--scale', String(o.scale === 2 ? 2 : 4), '--model', o.model === 'anime' ? 'anime' : 'x4plus'];
    if (o.pbr && G().id === 'nightfire') args.push('--pbr');  // the PBR maps are NightFire's materials'
    if (o.force) args.push('--force');
    pack = spawn(exe, args, { cwd: dir, windowsHide: true });
    const game = active;
    const out = line => send('pack', { line, game });
    readline.createInterface({ input: pack.stdout }).on('line', out);
    readline.createInterface({ input: pack.stderr }).on('line', out);
    pack.on('exit', code => { pack = null; send('pack', { done: true, code, game }); });
    pack.on('error', e => { pack = null; send('pack', { done: true, code: -1, line: e.message, game }); });
    return { ok: true };
});
ipcMain.handle('textures:cancel', async () => { if (pack) pack.kill(); if (collect) collect.kill(); });
// the game started with NF_COLLECT_TEXTURES=1 reads its level files and saves every texture in them, then quits (about
// half a minute; nothing to play)
ipcMain.handle('textures:collect', async () => {
    if (!G().collect) return { ok: false, message: 'This game has no texture collection: turn on saving the textures and play.' };
    if (collect || pack) return { ok: false, message: 'Another texture job is running.' };
    if (await gameRunning()) return { ok: false, message: 'Close the game first.' };
    const dir = playDir(), exe = path.join(dir, G().exe + EXE);
    if (!fs.existsSync(exe)) return { ok: false, message: 'Install the game first.' };
    collect = spawn(exe, [], { cwd: dir, env: { ...process.env, NF_COLLECT_TEXTURES: '1' }, windowsHide: false });
    collect.on('exit', code => { collect = null; send('collect', { done: true, code }); });
    collect.on('error', e => { collect = null; send('collect', { done: true, code: -1, line: e.message }); });
    return { ok: true };
});

// ---- Android package: the same Linux build on Linux and in Windows' WSL.
ipcMain.handle('android:run', async (_, options = {}) => {
    const nf = GAMES.nightfire, state = nf.state;
    if (active !== 'nightfire') return { ok: false, message: "The Android package is NightFire's only." };
    if (android || (state && state.busy)) return { ok: false, message: 'Another job is running.' };
    const repo = path.join(nf.dir, 'source');
    if (!state || !state.built || !fs.existsSync(path.join(repo, 'sdk/scripts/android/package.sh')))
        return { ok: false, message: 'Install the game on the Mission page first. If the source was removed, rebuild it.' };
    const output = await dialog.showSaveDialog(win, { title: 'Save Android game package',
        defaultPath: path.join(LAUNCHER_DIR, 'NightFire-android-package.zip'),
        filters: [{ name: 'Android game package', extensions: ['zip'] }] });
    if (output.canceled) return { ok: false, cancelled: true };
    // Check again after the native dialog: a service job may have started while it was open.
    if (android || (nf.state && nf.state.busy)) return { ok: false, message: 'Another job is running.' };  // (its state now, not the one before)
    const args = [repo, path.join(playDir(nf), 'assets'), output.filePath,
        options.textures === true ? path.join(playDir(nf), 'mods', 'textures') : '',
        options.abi === 'x86_64' ? 'x86_64' : 'arm64-v8a'];
    const child = spawn(WINDOWS ? 'wsl.exe' : 'bash', WINDOWS
        ? ['--exec', 'bash', '-s', '--', ...args] : ['-s', '--', ...args],
        { windowsHide: true, detached: !WINDOWS, stdio: ['pipe', 'pipe', 'pipe'] });
    android = child;
    const emit = ev => send('android', ev);
    readline.createInterface({ input: child.stdout }).on('line', line => emit({ line }));
    readline.createInterface({ input: child.stderr }).on('line', line => emit({ line }));
    let failed = false;
    child.on('error', e => { failed = true; emit({ line: e.message }); });
    child.stdin.on('error', () => {});
    child.on('close', code => {
        android = null;
        if (!failed && code === 0) androidOutput = output.filePath;
        emit({ done: true, ok: !failed && code === 0, path: output.filePath,
            line: code === 0 ? 'Package ready. Copy it to your Android device and import it in the NightFire app.'
                : 'Packaging failed. Check the log and the build requirements above.' });
    });
    child.stdin.end(fs.readFileSync(path.join(__dirname, 'android-package.sh'), 'utf8').replace(/\r\n/g, '\n'));
    return { ok: true };
});
ipcMain.handle('android:reveal', () => { if (androidOutput) shell.showItemInFolder(androidOutput); });
// ---- the rest
ipcMain.on('svc', (_, cmd) => command(cmd));
ipcMain.handle('svc:state', (_, id) => (GAMES[id] || G()).state);
ipcMain.handle('game:set', (_, id) => { if (GAMES[id]) active = id; return { missing: !!G().missing }; });
ipcMain.handle('paths', () => ({ launcher: LAUNCHER_DIR, root: G().dir, platform: process.platform }));

// a newer launcher: the latest release on GitHub, else the mirror's latest.json; null when this one is the newest or
// neither answers
const RELEASES = 'https://github.com/papand7/nightfire-recomp/releases/latest';
function newer(a, b) {  // a > b, versions like 1.5.1 (a leading v and a -suffix ignored)
    const p = v => String(v).replace(/^v/, '').split('-')[0].split('.').map(n => parseInt(n, 10) || 0);
    const x = p(a), y = p(b);
    for (let i = 0; i < Math.max(x.length, y.length); i++) if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0);
    return false;
}
async function latestRelease() {
    const tries = [
        ['https://api.github.com/repos/papand7/nightfire-recomp/releases/latest',
         j => ({ version: j.tag_name, url: j.html_url, assets: (j.assets || []).map(a => ({ name: a.name, url: a.browser_download_url, size: a.size })) })],
        ['https://mirror.dankelsoft.dk/latest.json', j => ({ version: j.version, url: j.url || RELEASES })],
    ];
    for (const [url, pick] of tries) {
        try {
            const r = await fetch(url, { headers: { 'User-Agent': 'NightFireLauncher' }, signal: AbortSignal.timeout(8000) });
            if (r.ok) return pick(await r.json());
        } catch (e) { /* the next source */ }
    }
    return null;
}
let latestFound = null;  // the newer release launcher:update found, for launcher:install
// the file this launcher runs from, which an update replaces: the portable exe (not its unpacked copy) or the AppImage
const SELF = process.env.PORTABLE_EXECUTABLE_FILE || process.env.APPIMAGE || null;
// the release's launcher for this system: the portable exe, the AppImage (named 007Remastered.* from 1.5.15, before
// that NightFireLauncher.*)
function launcherAsset(assets) {
    const want = WINDOWS ? /\.exe$/i : process.platform === 'linux' ? /x86_64\.AppImage$/i : null;
    if (!want) return null;
    const all = (assets || []).filter(a => want.test(a.name));
    // 007Remastered.* first: a release may also carry the ImGui launcher (CI's NightFireLauncher.exe), not this one
    return all.find(a => /^007Remastered/i.test(a.name)) || all.find(a => /^NightFireLauncher/i.test(a.name)) || null;
}
ipcMain.handle('launcher:update', async () => {
    const latest = await latestRelease();
    if (!latest || !latest.version || !newer(latest.version, app.getVersion())) return null;
    latestFound = latest;
    return { version: String(latest.version).replace(/^v/, ''), url: latest.url || RELEASES, current: app.getVersion(),
             installable: !!(app.isPackaged && SELF && launcherAsset(latest.assets)) };
});
// the newer launcher downloaded beside this one (GitHub, else the mirror's copy of the same file) and put in its place:
// Windows cannot replace a running exe, so a small script waits for this one to close, moves the new one in and starts
// it; Linux replaces the AppImage at once and starts the new one
ipcMain.handle('launcher:install', async () => {
    const asset = latestFound && launcherAsset(latestFound.assets);
    if (!app.isPackaged || !SELF || !asset) return { ok: false, message: 'Download the new launcher from the release page.' };
    const dir = path.dirname(SELF), tmp = path.join(dir, asset.name + '.download');
    const target = path.join(dir, asset.name);
    let done = 0;
    const get = async url => {
        const r = await fetch(url, { headers: { 'User-Agent': '007Remastered' }, signal: AbortSignal.timeout(15 * 60000) });
        if (!r.ok || !r.body) throw new Error(`HTTP ${r.status}`);
        const total = Number(r.headers.get('content-length')) || asset.size || 0;
        const out = fs.createWriteStream(tmp);
        done = 0;
        for await (const chunk of r.body) {
            done += chunk.length;
            if (!out.write(chunk)) await new Promise(res => out.once('drain', res));
            send('update', { fraction: total ? done / total : -1, done, total });
        }
        await new Promise((res, rej) => out.end(err => err ? rej(err) : res()));
        if (asset.size && fs.statSync(tmp).size !== asset.size) throw new Error('the download is incomplete');
    };
    try {
        try { await get(asset.url); }
        catch (e) { await get(asset.url.replace('https://github.com/', 'https://mirror.dankelsoft.dk/github.com/')); }
    } catch (e) {
        try { fs.unlinkSync(tmp); } catch {}
        return { ok: false, message: `The download failed (${e.message}).` };
    }
    if (WINDOWS) {
        const script = path.join(app.getPath('temp'), '007remastered-update.cmd');
        const q = s => `"${s}"`;
        fs.writeFileSync(script, [
            '@echo off',
            `:wait`,
            `tasklist /fi "PID eq ${process.pid}" | find "${process.pid}" >nul && (timeout /t 1 /nobreak >nul & goto wait)`,
            // the old exe goes when the new one has another name (NightFireLauncher.exe -> 007Remastered.exe)
            path.resolve(SELF).toLowerCase() !== path.resolve(target).toLowerCase() ? `del /f /q ${q(SELF)}` : 'rem same name',
            `move /y ${q(tmp)} ${q(target)} >nul`,
            `start "" ${q(target)}`,
            '(goto) 2>nul & del "%~f0"',
        ].join('\r\n'));
        spawn('cmd.exe', ['/c', script], { detached: true, stdio: 'ignore', windowsHide: true }).unref();
    } else {
        fs.chmodSync(tmp, 0o755);
        fs.renameSync(tmp, target);
        if (path.resolve(SELF) !== path.resolve(target)) { try { fs.unlinkSync(SELF); } catch {} }
        spawn(target, [], { detached: true, stdio: 'ignore' }).unref();
    }
    setTimeout(() => app.quit(), 300);
    return { ok: true };
});
ipcMain.on('win', (_, what) => {
    if (!win) return;
    if (what === 'min') win.minimize();
    else if (what === 'max') win.isMaximized() ? win.unmaximize() : win.maximize();
    else if (what === 'close') win.close();
});
ipcMain.handle('pick', async (_, kind) => {
    const r = await dialog.showOpenDialog(win, kind === 'iso'
        ? { title: "Choose the game's disc image", properties: ['openFile'], filters: [{ name: 'Xbox disc image', extensions: ['iso'] }, { name: 'default.xbe', extensions: ['xbe'] }] }
        : { title: kind === 'music' ? 'Choose a folder of your music' : "Choose the folder with the game's files", properties: ['openDirectory'] });
    return r.canceled ? null : r.filePaths[0];
});
ipcMain.on('open', (_, target) => {
    if (/^https?:\/\//.test(target)) shell.openExternal(target);
    else shell.openPath(target);
});

app.whenReady().then(() => {
    createWindow();
    for (const g of Object.values(GAMES)) startService(g);
    watchGames();
});
app.on('before-quit', () => {
    app.isQuitting = true;
    if (android) {
        if (WINDOWS) spawn('taskkill', ['/pid', String(android.pid), '/T', '/F'], { windowsHide: true });
        else { try { process.kill(-android.pid, 'SIGTERM'); } catch {} }
    }
    for (const g of Object.values(GAMES)) if (g.svc) { command({ cmd: 'quit', game: g.id }); g.svc.stdin.end(); }
});
app.on('window-all-closed', () => app.quit());
