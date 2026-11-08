// What the window's page may ask of the launcher (main.js); nothing else of Node reaches it
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('nf', {
    command: cmd => ipcRenderer.send('svc', cmd),
    state: () => ipcRenderer.invoke('svc:state'),
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
    onPack: fn => ipcRenderer.on('pack', (_, ev) => fn(ev)),
    launcherUpdate: () => ipcRenderer.invoke('launcher:update'),
});
