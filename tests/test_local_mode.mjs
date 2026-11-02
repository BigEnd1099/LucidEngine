import { spawn } from 'child_process';
import net from 'net';

console.log('🧪 VERIFYING PURE LOCAL ZERO-PORT MODE (--no-remote)...');

const proc = spawn('engine/build/Release/engine.exe', ['--headless', '--no-remote'], {
  stdio: 'inherit'
});

await new Promise(r => setTimeout(r, 1500));

function checkPort(port) {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    socket.setTimeout(600);
    socket.on('connect', () => {
      socket.destroy();
      resolve(true); // open
    });
    socket.on('timeout', () => {
      socket.destroy();
      resolve(false);
    });
    socket.on('error', () => resolve(false));
    socket.connect(port, '127.0.0.1');
  });
}

const isPort9000Open = await checkPort(9000);
const isPort9001Open = await checkPort(9001);

proc.kill();

if (isPort9000Open || isPort9001Open) {
  console.error(`❌ FAILED: Ports are unexpectedly open: 9000=${isPort9000Open}, 9001=${isPort9001Open}`);
  process.exit(1);
} else {
  console.log('🎉 SUCCESS: Verified 0 network ports opened in --no-remote pure local mode!');
  process.exit(0);
}
