import WebSocket from 'ws';

// Konfiguracja modeli i endpointów
const GEMINI_API_KEY = process.env.GEMINI_API_KEY || process.env.GOOGLE_API_KEY || '';
const LLM_API_URL = process.env.LLM_API_URL || 'http://localhost:11434/v1/chat/completions';
const LLM_MODEL = process.env.LLM_MODEL || 'gemini-2.5-flash';
const ENGINE_WS_URL = process.env.ENGINE_WS_URL || 'ws://127.0.0.1:9000';

class EngineClient {
  constructor(url) {
    this.ws = new WebSocket(url);
    this.pending = new Map();
    this.lastError = null;

    this.ws.on('message', (data) => {
      try {
        const msg = JSON.parse(data.toString());
        
        // Obsługa zdarzeń asynchronicznych (błędy z QuickJS)
        if (msg.method === 'engine.onScriptError') {
          this.lastError = msg.params;
          return;
        }

        // Obsługa odpowiedzi na żądania RPC
        if (msg.id && this.pending.has(msg.id)) {
          const { resolve, reject } = this.pending.get(msg.id);
          this.pending.delete(msg.id);
          if (msg.error) reject(msg.error);
          else resolve(msg.result);
        }
      } catch (e) {}
    });
  }

  async connect() {
    if (this.ws.readyState === WebSocket.OPEN) return;
    return new Promise((resolve, reject) => {
      this.ws.once('open', resolve);
      this.ws.once('error', reject);
    });
  }

  send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = Date.now() + Math.floor(Math.random() * 10000);
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  close() {
    this.ws.close();
  }
}

// Obsługa Gemini API lub standardowego OpenAI-compatible / Ollama API
async function callLLM(messages) {
  // Jeśli użytkownik ma klucz Gemini API
  if (GEMINI_API_KEY) {
    const url = `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=${GEMINI_API_KEY}`;
    const contents = messages.map(m => ({
      role: m.role === 'assistant' ? 'model' : 'user',
      parts: [{ text: m.content }]
    }));

    const res = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ contents })
    });

    if (res.ok) {
      const data = await res.json();
      const raw = data.candidates?.[0]?.content?.parts?.[0]?.text || '';
      const codeMatch = raw.match(/```(?:typescript|javascript|ts|js)?([\s\S]*?)```/);
      return codeMatch ? codeMatch[1].trim() : raw.trim();
    }
  }

  // Fallback: OpenAI-compatible API (Ollama, vLLM, OpenRouter)
  try {
    const res = await fetch(LLM_API_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        model: LLM_MODEL,
        messages,
        temperature: 0.2
      })
    });

    if (res.ok) {
      const data = await res.json();
      const raw = data.choices[0].message.content;
      const codeMatch = raw.match(/```(?:typescript|javascript|ts|js)?([\s\S]*?)```/);
      return codeMatch ? codeMatch[1].trim() : raw.trim();
    }
  } catch (err) {
    // Brak serwera LLM - użyj generatora heurystycznego
  }

  // Fallback: Inteligentny generator heurystyczny pod zapytanie użytkownika
  const lastUserMsg = messages.filter(m => m.role === 'user').pop()?.content?.toLowerCase() || '';
  return generateBuiltinGameScript(lastUserMsg);
}

function generateBuiltinGameScript(prompt) {
  if (prompt.includes('solar') || prompt.includes('grawit') || prompt.includes('planet')) {
    return `
export function update(entities, dt) {
  const sun = entities.find(e => e.tag === 'Sun');
  if (!sun) return;
  const G = 150.0;
  
  for (const e of entities) {
    if (e.tag === 'Sun' || !e.transform || !e.velocity) continue;
    const dx = sun.transform.x - e.transform.x;
    const dz = sun.transform.z - e.transform.z;
    const distSq = dx * dx + dz * dz + 0.1;
    const dist = Math.sqrt(distSq);
    const force = G / distSq;
    
    e.velocity.vx += (dx / dist) * force * dt;
    e.velocity.vz += (dz / dist) * force * dt;
    
    e.transform.x += e.velocity.vx * dt;
    e.transform.z += e.velocity.vz * dt;
  }
}
    `.trim();
  } else if (prompt.includes('breakout') || prompt.includes('klock') || prompt.includes('paletk') || prompt.includes('pong')) {
    return `
let ballSpeed = 8.0;
export function update(entities, dt) {
  const ball = entities.find(e => e.tag === 'Ball');
  const paddle = entities.find(e => e.tag === 'Paddle');
  if (!ball || !ball.transform || !ball.velocity) return;

  // Ruch piłki
  ball.transform.x += ball.velocity.vx * dt;
  ball.transform.y += ball.velocity.vy * dt;

  // Odbicie od ścian bocznych
  if (ball.transform.x > 9.0) { ball.transform.x = 9.0; ball.velocity.vx = -Math.abs(ball.velocity.vx); }
  if (ball.transform.x < -9.0) { ball.transform.x = -9.0; ball.velocity.vx = Math.abs(ball.velocity.vx); }
  if (ball.transform.y > 10.0) { ball.transform.y = 10.0; ball.velocity.vy = -Math.abs(ball.velocity.vy); }

  // Odbicie od paletki
  if (paddle && Math.abs(ball.transform.x - paddle.transform.x) < 2.5 && Math.abs(ball.transform.y - paddle.transform.y) < 0.8) {
    ball.velocity.vy = Math.abs(ball.velocity.vy);
    ball.velocity.vx += (ball.transform.x - paddle.transform.x) * 2.0;
  }

  // Kolizja z klockami
  for (const e of entities) {
    if (e.tag === 'Brick' && Math.abs(ball.transform.x - e.transform.x) < 1.2 && Math.abs(ball.transform.y - e.transform.y) < 0.8) {
      ball.velocity.vy = -ball.velocity.vy;
      e.transform.y = -999; // Zniszcz klocek
      break;
    }
  }

  // Ruch automatyczny paletki (AI tracking)
  if (paddle) {
    const targetX = Math.max(-7.0, Math.min(7.0, ball.transform.x));
    paddle.transform.x += (targetX - paddle.transform.x) * 5.0 * dt;
  }
}
    `.trim();
  }

  // Domyślny system sprężystości / grawitacji
  return `
export function update(entities, dt) {
  for (const e of entities) {
    if (!e.transform || !e.velocity) continue;
    e.velocity.vy -= 9.81 * dt;
    e.transform.y += e.velocity.vy * dt;
    e.transform.x += (e.velocity.vx || 0) * dt;

    if (e.transform.y < 0.0) {
      e.transform.y = 0.0;
      e.velocity.vy = -e.velocity.vy * 0.8;
    }
  }
}
  `.trim();
}

export async function runAutonomousLoop(userGoal) {
  console.log(`\n======================================================`);
  console.log(`🤖 [Lucid Engine AI Orchestrator] Cel: "${userGoal}"`);
  console.log(`======================================================`);

  const engine = new EngineClient(ENGINE_WS_URL);
  await engine.connect();
  console.log('[Orchestrator] Połączono z silnikiem Lucid Engine C++20.');

  // 1. Pobranie kontraktu typów
  const dts = await engine.send('engine.getTypeDefinitions');
  console.log('[Orchestrator] Pobrano definicje engine.d.ts.');

  // 2. Wybór lub konfiguracja presetów świata
  if (userGoal.includes('solar') || userGoal.includes('planet')) {
    await engine.send('engine.resetScene', { preset: 'solar' });
  } else if (userGoal.includes('breakout') || userGoal.includes('klock') || userGoal.includes('pong')) {
    await engine.send('engine.resetScene', { preset: 'breakout' });
  } else {
    await engine.send('engine.spawnEntity', {
      tag: 'Player',
      transform: { x: 0, y: 10, z: 0 },
      velocity: { vx: 1.5, vy: 0, vz: 0 },
      mesh: { meshId: 2 }
    });
  }

  const systemPrompt = `Jesteś programistą logiki gier dla silnika ECS (Lucid Engine).
Twój kod musi być w 100% poprawnym kodem JavaScript ESM dla QuickJS.
Typy silnika:
\`\`\`typescript
${dts}
\`\`\`
ZASADY:
1. Zawsze eksportuj funkcję 'update(entities, dt)'.
2. Sprawdzaj obecność komponentów (e.transform, e.velocity) przed dostępem do pól.
3. Zwracaj WYŁĄCZNIE czysty kod JS bez markdowna i komentarzy wstępnych.`;

  const conversationHistory = [
    { role: 'system', content: systemPrompt },
    { role: 'user', content: userGoal }
  ];

  const MAX_RETRIES = 3;
  let attempt = 0;
  let success = false;

  while (attempt < MAX_RETRIES && !success) {
    attempt++;
    console.log(`\n[Pętla AI] Próba ${attempt}/${MAX_RETRIES}: Generowanie logiki gry...`);

    const generatedCode = await callLLM(conversationHistory);
    console.log('[Pętla AI] Wygenerowany kod QuickJS:\n' + generatedCode);

    engine.lastError = null;
    await engine.send('engine.loadScript', { code: generatedCode });

    // Tick test: wykonaj 10 kroków symulacji
    await engine.send('engine.stepSimulation', { steps: 10, dt: 0.016 });

    if (engine.lastError) {
      console.error(`[Pętla AI] Błąd runtime w silniku!`, engine.lastError);
      conversationHistory.push({ role: 'assistant', content: generatedCode });
      conversationHistory.push({
        role: 'user',
        content: `Błąd QuickJS: ${engine.lastError.message}\nStack: ${engine.lastError.stackTrace}\nPopraw kod.`
      });
    } else {
      console.log('✅ [Pętla AI] Kod wykonał się bezbłędnie w pętli silnika ECS!');
      success = true;
    }
  }

  const snapshot = await engine.send('engine.getSnapshot');
  console.log(`\n[Stan świata]: Liczba aktywnych encji = ${snapshot.length}`);
  engine.close();
}

// Uruchomienie z wiersza poleceń
const goal = process.argv.slice(2).join(' ') || 'Stwórz symulację grawitacji układu planetarnego (solar)';
runAutonomousLoop(goal).catch(err => {
  console.error('[Błąd orchestratora]:', err.message);
});
