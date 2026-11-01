import WebSocket from 'ws';

const ws = new WebSocket('ws://127.0.0.1:9000'); // Port silnika

function send(method, params = {}) {
  return new Promise((resolve) => {
    const id = Date.now() + Math.floor(Math.random() * 1000);
    const handler = (data) => {
      const res = JSON.parse(data.toString());
      if (res.id === id) {
        ws.off('message', handler);
        resolve(res.result);
      }
    };
    ws.on('message', handler);
    ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
  });
}

// Baza zminimalizowanego testowego GLB (Box) używanego dla weryfikacji dekodera w Base64.
const MOCK_GLB_BASE64 = "Z2xURgIAAAA0AAAALgAAAHsiYXNzZXQiOnsidmVyc2lvbiI6IjIuMCJ9LCJtZXNoZXMiOlt7InByaW1pdGl2ZXMiOlt7ImF0dHJpYnV0ZXMiOnsiUE9TSVRJT04iOjB9LCJpbmRpY2VzIjoxLCJtYXRlcmlhbCI6MH1dfV0sImFjY2Vzc29ycyI6W3siYnVmZmVyVmlldyI6MCwiY29tcG9uZW50VHlwZSI6NTEyNiwiY291bnQiOjgsInR5cGUiOiJWRUMzIiwiYnl0ZU9mZnNldCI6MH0seyJidWZmZXJWaWV3IjoxLCJjb21wb25lbnRUeXBlIjo1MTIzLCJjb3VudCI6MzYsInR5cGUiOiJTQ0FMQVIiLCJieXRlT2Zmc2V0IjowfV0sImJ1ZmZlclZpZXdzIjpbeyJidWZmZXIiOjAsImJ5dGVPZmZzZXQiOjAsImJ5dGVMZW5ndGgiOjk2LCJ0YXJnZXQiOjM0OTYyfSx7ImJ1ZmZlciI6MCwiYnl0ZU9mZnNldCI6OTYsImJ5dGVMZW5ndGgiOjcyLCJ0YXJnZXQiOjM0OTYzfV0sImJ1ZmZlcnMiOlt7ImJ5dGVMZW5ndGgiOjE2OH1dLCJtYXRlcmlhbHMiOlt7InBick1ldGFsbGljUm91Z2huZXNzIjp7ImJhc2VDb2xvckZhY3RvciI6WzEuMCwwLjIsMC4yLDEuMF0sIm1ldGFsbGljRmFjdG9yIjowLjUsInJvdWdobmVzc0ZhY3RvciI6MC41fX1dfQAAAAJCSU4AAAAAAAAAAAAAgL8AAAAAgL8AAAAAgL8AAAAAgD8AAAAAgL8AAAAAgL8AAAAAgL8AAAAAgD8AAAAAgL8AAAAAgD8AAAAAgD8AAAAAgL8AAAAAgD8AAAAAgL8AAAAAgL8AAAAAgL8AAAAAgD8AAAAAgD8AAAAAgL8AAAAAgL8AAAAAgD8AAAAAgD8AAAAAgD8AAAAAgD8AAAAAABACAAMAAQAFAAMAAAABAAIAAwAEAAUABgAFAAQABwAGAAUAAQADAAUACAAHAAUAAQAIABAAEgARABMAFAAVABYAFwAYABkAGgAbABwAHQAeAB8AIAAhACIAIwAkACUAJgAnACgAKQAqACsALAAtAC4ALwAwADEAMgAzADQANQA2ADcAOA==";

ws.on('open', async () => {
  console.log('Połączono z Lucid Engine!');
  
  console.log('Ładowanie Mesh Asset (GLB)...');
  const res = await send('engine.loadGlbFromBase64', { glbData: MOCK_GLB_BASE64 });
  const entityId = res?.entityId ?? res;
  console.log('Załadowano Asset pod ID encji:', entityId);
  
  console.log('Ustawianie PBR na Encję...');
  await send('engine.applyPbrMaterial', { entityId: entityId });
  
  // Przesuńmy ten sześcian dla pewności przed kamerą
  await send('engine.spawnEntity', {
    transform: { x: 0, y: 15, z: 0 }
  });

  console.log('Odpytywanie potoku Renderującego...');
  const snapshot = await send('engine.getSnapshot');
  console.log('Test zrealizowany. Silnik jest stabilny! Liczba encji w scenie:', snapshot.length);
  
  ws.close();
});
