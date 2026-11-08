// The NightFire launcher's window (Electron, Windows and Linux): the player's disc image found beside it, the game
// built on this PC by NightFireLauncherService (sdk/launcher/service.cpp: the same builder as the ImGui launcher,
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
const ROOT = path.join(LAUNCHER_DIR, 'NightFire');
const SERVICE = app.isPackaged ? path.join(process.resourcesPath, 'NightFireLauncherService' + EXE)
                               : path.resolve(__dirname, '../../out/launcher-svc/NightFireLauncherService' + EXE);
// the game's executables (launcher/game.json's exes)
const GAME_EXES = ['Nightfire', 'NightfireDriving'];

let win = null;
let svc = null;
let state = null;
let pack = null;  // the running hd_pack

function send(channel, data) {
    if (win && !win.isDestroyed()) win.webContents.send(channel, data);
}

function startService() {
    fs.mkdirSync(ROOT, { recursive: true });
    svc = spawn(SERVICE, ['--games', LAUNCHER_DIR, '--root', ROOT], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    readline.createInterface({ input: svc.stdout }).on('line', line => {
        let ev;
        try { ev = JSON.parse(line); } catch { return; }
        if (ev.ev === 'state') state = ev;
        send('svc', ev);
    });
    readline.createInterface({ input: svc.stderr }).on('line', line => send('svc', { ev: 'log', line }));
    svc.on('error', err => send('svc', { ev: 'error', message: `The launcher service did not start: ${err.message}` }));
    svc.on('exit', code => {
        svc = null;
        if (!app.isQuitting) send('svc', { ev: 'error', message: `The launcher service stopped (exit ${code}).` });
    });
}

function command(cmd) {
    if (svc && svc.stdin.writable) svc.stdin.write(JSON.stringify(cmd) + '\n');
}

// a new or removed .iso beside the launcher is looked at again
function watchGames() {
    let timer = null;
    try {
        fs.watch(LAUNCHER_DIR, (_, name) => {
            if (!name || !/\.iso$/i.test(name)) return;
            clearTimeout(timer);
            timer = setTimeout(() => { if (!(state && state.game && state.game.ok)) command({ cmd: 'scan' }); }, 1500);
        });
    } catch { /* a folder that cannot be watched: the Scan again button */ }
}

function createWindow() {
    win = new BrowserWindow({
        width: 1180, height: 760, minWidth: 960, minHeight: 640,
        frame: false, backgroundColor: '#07080b', show: false,
        title: '007: NightFire Launcher',
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
            for (const page of (process.env.NF_SHOT_PAGES || 'home,settings,textures').split(',')) {
                const [name, tab] = page.split(':');
                await win.webContents.executeJavaScript(`show(${JSON.stringify(name)});` + (tab ? `tab=${JSON.stringify(tab)};renderSettings();` : ''));
                await wait(900);
                const img = await win.webContents.capturePage();
                fs.writeFileSync(path.join(process.env.NF_SHOT, `launcher_${page.replace(':', '_')}.png`), img.toPNG());
            }
            app.quit();
        });
    }
    win.on('maximize', () => send('win', { maximized: true }));
    win.on('unmaximize', () => send('win', { maximized: false }));
    win.webContents.setWindowOpenHandler(({ url }) => { shell.openExternal(url); return { action: 'deny' }; });
}

// ---- the game's settings: settings.toml and graphics.toml in play\, beside the exe
function playDir() { return (state && state.play_dir) || path.join(ROOT, 'play'); }

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
ipcMain.handle('textures:info', async () => {
    const dir = playDir(), tex = path.join(dir, 'mods', 'textures');
    return {
        dumped: countPng(path.join(tex, 'dump')),
        pack: countPng(tex, n => !/_(n|r)\.png$/i.test(n)),
        maps: countPng(tex, n => /_n\.png$/i.test(n)),
        tool: fs.existsSync(path.join(dir, 'tools', 'hd_pack' + EXE)),
        folder: tex,
        running: !!pack,
    };
});
ipcMain.handle('textures:run', async (_, o) => {
    if (pack) return { ok: false, message: 'Already making the pack.' };
    const dir = playDir(), exe = path.join(dir, 'tools', 'hd_pack' + EXE);
    if (!fs.existsSync(exe)) return { ok: false, message: WINDOWS ? 'hd_pack is not there yet: build the game first.' : 'The HD texture pack tool is Windows only so far.' };
    const args = ['--dir', dir, '--scale', String(o.scale === 2 ? 2 : 4), '--model', o.model === 'anime' ? 'anime' : 'x4plus'];
    if (o.pbr) args.push('--pbr');
    if (o.force) args.push('--force');
    pack = spawn(exe, args, { cwd: dir, windowsHide: true });
    const out = line => send('pack', { line });
    readline.createInterface({ input: pack.stdout }).on('line', out);
    readline.createInterface({ input: pack.stderr }).on('line', out);
    pack.on('exit', code => { pack = null; send('pack', { done: true, code }); });
    pack.on('error', e => { pack = null; send('pack', { done: true, code: -1, line: e.message }); });
    return { ok: true };
});
ipcMain.handle('textures:cancel', async () => { if (pack) pack.kill(); });

// ---- the rest
ipcMain.on('svc', (_, cmd) => command(cmd));
ipcMain.handle('svc:state', () => state);
ipcMain.handle('paths', () => ({ launcher: LAUNCHER_DIR, root: ROOT, platform: process.platform }));
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
    startService();
    watchGames();
});
app.on('before-quit', () => {
    app.isQuitting = true;
    if (svc) { command({ cmd: 'quit' }); svc.stdin.end(); }
});
app.on('window-all-closed', () => app.quit());
