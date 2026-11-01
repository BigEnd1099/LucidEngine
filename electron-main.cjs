const { app, BrowserWindow, Menu, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const http = require('http');

const logFile = path.join(__dirname, 'electron.log');
function log(msg) {
  try {
    fs.appendFileSync(logFile, `[${new Date().toISOString()}] ${msg}\n`);
  } catch {}
  console.log(msg);
}

process.on('uncaughtException', (err) => {
  log('FATAL UNCAUGHT EXCEPTION: ' + (err?.stack || err));
});

let mainWindow = null;
let engineProcess = null;

// Hardware acceleration flags for RTX GPU & Vulkan
app.commandLine.appendSwitch('enable-gpu-rasterization');
app.commandLine.appendSwitch('enable-zero-copy');
app.commandLine.appendSwitch('ignore-gpu-blocklist');

function isEngineListening(port = 9000) {
  return new Promise((resolve) => {
    const net = require('net');
    const socket = new net.Socket();
    socket.setTimeout(800);
    socket.on('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.on('timeout', () => {
      socket.destroy();
      resolve(false);
    });
    socket.on('error', () => {
      resolve(false);
    });
    socket.connect(port, '127.0.0.1');
  });
}

async function ensureNativeEngine() {
  const listening = await isEngineListening(9000);
  if (!listening) {
    const engineExe = path.join(__dirname, 'engine', 'build', 'Release', 'engine.exe');
    log('[Electron] Starting native C++ engine: ' + engineExe);
    try {
      engineProcess = spawn(engineExe, ['--headless'], {
        cwd: __dirname,
        stdio: 'inherit',
        windowsHide: true,
      });
      engineProcess.on('exit', (code) => {
        log('[Electron] Native engine process exited with code: ' + code);
        engineProcess = null;
      });
      // Wait for port 9000 to be open
      for (let i = 0; i < 30; i++) {
        await new Promise((r) => setTimeout(r, 200));
        if (await isEngineListening(9000)) {
          log('[Electron] Native C++ engine is now ready on port 9000.');
          break;
        }
      }
    } catch (err) {
      log('[Electron] Failed to start native engine: ' + err);
    }
  } else {
    log('[Electron] Native C++ engine is already running on port 9000.');
  }
}

function isViteRunning() {
  return new Promise((resolve) => {
    const req = http.get('http://127.0.0.1:3000', (res) => {
      resolve(true);
    });
    req.on('error', () => resolve(false));
    req.setTimeout(800, () => {
      req.abort();
      resolve(false);
    });
  });
}

async function createWindow() {
  log('[Electron] App ready, creating window...');
  await ensureNativeEngine();

  mainWindow = new BrowserWindow({
    width: 1600,
    height: 950,
    minWidth: 1024,
    minHeight: 700,
    title: 'Lucid Engine 3.8',
    backgroundColor: '#0a0a0f',
    icon: path.join(__dirname, 'public', 'favicon.ico'),
    autoHideMenuBar: true,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      webSecurity: false, // Allows seamless local WebSocket and asset access
    },
    show: true,
  });

  Menu.setApplicationMenu(null);

  const viteReady = await isViteRunning();
  if (viteReady) {
    log('[Electron] Loading from Vite dev server http://127.0.0.1:3000...');
    mainWindow.loadURL('http://127.0.0.1:3000');
  } else {
    const indexPath = path.join(__dirname, 'dist', 'index.html');
    log('[Electron] Loading from production bundle: ' + indexPath);
    mainWindow.loadFile(indexPath);
  }

  mainWindow.maximize();
  mainWindow.show();

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (engineProcess) {
    console.log('[Electron] Terminating native engine child process...');
    try {
      engineProcess.kill('SIGINT');
    } catch {}
  }
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});
