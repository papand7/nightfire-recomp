// What the window's page may ask of the launcher (main.js); nothing else of Node reaches it
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('nf', {
    revealAndroid: () => ipcRenderer.invoke('android:reveal'),
    makeAndroid: opts => ipcRenderer.invoke('android:run', opts),
    onAndroid: fn => ipcRenderer.on('android', (_, ev) => fn(ev)),
    command: cmd => ipcRenderer.send('svc', cmd),
    state: game => ipcRenderer.invoke('svc:state', game),
    setGame: game => ipcRenderer.invoke('game:set', game),
    paths: () => ipcRenderer.invoke('paths'),
    onService: fn => ipcRenderer.on('svc', (_, ev) => fn(ev)),
    onWindow: fn => ipcRenderer.on('win', (_, ev) => fn(ev)),
    window: what => ipcRenderer.send('win', what),
    pick: kind => ipcRenderer.invoke('pick', kind),
    open: target => ipcRenderer.send('open', target),
    loadSettings: () => ipcRenderer.invoke('settings:load'),
    saveSettings: patch => ipcRenderer.invoke('settings:save', patch),
    textureInfo: () => ipcRenderer.invoke('textures:info'),
    makeTextures: opts => ipcRenderer.invoke('textures:run', opts),
    stopTextures: () => ipcRenderer.invoke('textures:cancel'),
    collectTextures: () => ipcRenderer.invoke('textures:collect'),
    onCollect: fn => ipcRenderer.on('collect', (_, ev) => fn(ev)),
    onPack: fn => ipcRenderer.on('pack', (_, ev) => fn(ev)),
    launcherUpdate: () => ipcRenderer.invoke('launcher:update'),
});
