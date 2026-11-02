import { Project, ScriptFile, MaterialAsset, ParticleEmitterConfig, SoundAsset, WidgetBlueprint } from '../types/project';

const STORAGE_KEY = 'lucid_engine_projects';
const ACTIVE_PROJECT_KEY = 'lucid_engine_active_project_id';

export const DEFAULT_SOUNDS: SoundAsset[] = [
  {
    id: 'cue_jump',
    name: 'S_PlayerJump.cue',
    type: 'synth_cue',
    cueConfig: {
      waveType: 'sine',
      startFreq: 140,
      endFreq: 520,
      attack: 0.01,
      decay: 0.15,
      sustain: 0.2,
      release: 0.1,
      volume: 0.8,
    },
    createdAt: Date.now(),
  },
  {
    id: 'cue_coin',
    name: 'S_ItemPickup.cue',
    type: 'synth_cue',
    cueConfig: {
      waveType: 'triangle',
      startFreq: 987,
      endFreq: 1318,
      attack: 0.005,
      decay: 0.1,
      sustain: 0.4,
      release: 0.25,
      volume: 0.8,
    },
    createdAt: Date.now(),
  },
  {
    id: 'cue_hit',
    name: 'S_Impact.cue',
    type: 'synth_cue',
    cueConfig: {
      waveType: 'square',
      startFreq: 220,
      endFreq: 60,
      attack: 0.005,
      decay: 0.15,
      sustain: 0.1,
      release: 0.2,
      volume: 0.85,
    },
    createdAt: Date.now(),
  },
  {
    id: 'cue_explosion',
    name: 'S_Explosion.cue',
    type: 'synth_cue',
    cueConfig: {
      waveType: 'noise',
      startFreq: 180,
      endFreq: 40,
      attack: 0.01,
      decay: 0.3,
      sustain: 0.1,
      release: 0.4,
      volume: 0.9,
    },
    createdAt: Date.now(),
  },
];

export const DEFAULT_WIDGETS: WidgetBlueprint[] = [
  {
    id: 'wbp_hud',
    name: 'WBP_PlayerHUD',
    elements: [
      {
        id: 'elem_score',
        name: 'ScoreText',
        type: 'text',
        x: 3,
        y: 3,
        width: 25,
        height: 6,
        text: 'WYNIK: 0',
        fontSize: 16,
        color: '#fbbf24',
        bindVariable: 'score',
      },
      {
        id: 'elem_hp',
        name: 'HealthBar',
        type: 'progressBar',
        x: 3,
        y: 10,
        width: 20,
        height: 4,
        color: '#10b981',
        backgroundColor: '#1e293b',
        value: 100,
        bindVariable: 'health',
      }
    ],
    createdAt: Date.now(),
  }
];

const DEFAULT_SCRIPTS: ScriptFile[] = [
  {
    id: 'script_platformer',
    name: 'BP_PlatformerController.js',
    code: `// 🏃 BP_PlatformerController - Ruch kapsułą, skoki, monety i wrogowie
export function update(entities, dt, input, contacts) {
  const player = entities.find(e => e.tag === 'Player');
  if (!player || !player.transform) return;

  // 1. Sprawdzenie stanu gry
  if (typeof engine !== 'undefined' && engine.getGameStatus) {
    const status = engine.getGameStatus();
    if (status === 'GAMEOVER' || status === 'VICTORY') return;
  }

  const speed = 12.0;
  if (!player.velocity) player.velocity = { vx: 0, vy: 0, vz: 0 };

  // 2. Sterowanie horyzontalne (WASD / Strzałki)
  let moveX = 0, moveZ = 0;
  if (input?.keys?.KeyW || input?.keys?.ArrowUp)    moveZ -= 1;
  if (input?.keys?.KeyS || input?.keys?.ArrowDown)  moveZ += 1;
  if (input?.keys?.KeyA || input?.keys?.ArrowLeft)  moveX -= 1;
  if (input?.keys?.KeyD || input?.keys?.ArrowRight) moveX += 1;

  const len = Math.hypot(moveX, moveZ);
  if (len > 0) {
    player.transform.x += (moveX / len) * speed * dt;
    player.transform.z += (moveZ / len) * speed * dt;
    player.transform.ry = Math.atan2(moveX, moveZ) * (180 / Math.PI);
  }

  // 3. Skok (Spacja gdy gracz dotyka ziemi)
  const isGrounded = player.transform.y <= 1.05;
  if ((input?.keys?.Space) && isGrounded) {
    player.velocity.vy = 12.0;
    if (typeof engine !== 'undefined' && engine.playSfx) {
      engine.playSfx('jump', 0.8);
    }
  }

  // Grawitacja i ruch w pionie
  player.velocity.vy -= 26.0 * dt;
  player.transform.y += player.velocity.vy * dt;

  if (player.transform.y <= 1.0) {
    player.transform.y = 1.0;
    player.velocity.vy = 0;
  }

  // 4. Ruchome platformy
  const time = typeof engine !== 'undefined' && engine.getTime ? engine.getTime() : Date.now() * 0.001;
  const movingPlat = entities.find(e => e.tag === 'MovingPlatform');
  if (movingPlat && movingPlat.transform) {
    movingPlat.transform.x = Math.sin(time * 2.0) * 5.0;
  }

  // 5. Zbieranie monet (Sensory)
  const coins = entities.filter(e => e.tag?.startsWith('Coin'));
  let remainingCoins = 0;
  for (const coin of coins) {
    if (!coin.transform || coin.transform.y < -100) continue;
    remainingCoins++;

    // Ciągły obrót
    coin.transform.ry = ((coin.transform.ry || 0) + 120.0 * dt) % 360;

    const dist = Math.hypot(
      player.transform.x - coin.transform.x,
      player.transform.z - coin.transform.z
    );

    if (dist < 1.6 && Math.abs(player.transform.y - coin.transform.y) < 1.8) {
      coin.transform.y = -999; // Ukryj monetę
      if (typeof engine !== 'undefined') {
        if (engine.playSfx) engine.playSfx('coin', 0.9);
        if (engine.setScore) {
          const s = engine.getScore ? engine.getScore() : 0;
          engine.setScore(s + 50);
          engine.log('🪙 Zebrano monetę! Wynik: ' + (s + 50));
        }
      }
    }
  }

  // Sprawdzenie zwycięstwa
  if (remainingCoins === 0 && coins.length > 0) {
    if (typeof engine !== 'undefined') {
      if (engine.setGameStatus) engine.setGameStatus('VICTORY');
      if (engine.playSfx) engine.playSfx('victory', 1.0);
      engine.log('🎉 ZWYCIĘSTWO! Wszystkie monety zebrane!');
    }
  }

  // 6. Przeciwnik patrolujący (kontakt = obrażenia)
  const enemy = entities.find(e => e.tag === 'Enemy');
  if (enemy && enemy.transform) {
    enemy.transform.x = Math.sin(time * 1.5) * 6.0;
    const eDist = Math.hypot(
      player.transform.x - enemy.transform.x,
      player.transform.z - enemy.transform.z
    );
    if (eDist < 1.5 && Math.abs(player.transform.y - enemy.transform.y) < 1.5) {
      if (typeof engine !== 'undefined') {
        const curHp = engine.getHealth ? engine.getHealth() : 100;
        const newHp = Math.max(0, curHp - 35);
        if (engine.setHealth) engine.setHealth(newHp);
        if (engine.playSfx) engine.playSfx('hit', 1.0);
        if (engine.shakeCamera) engine.shakeCamera(0.8, 0.3);
        
        // Odrzut gracza
        player.transform.x += (player.transform.x - enemy.transform.x) * 2.0;
        player.velocity.vy = 8.0;

        if (newHp <= 0) {
          if (engine.setGameStatus) engine.setGameStatus('GAMEOVER');
          if (engine.playSfx) engine.playSfx('gameover', 1.0);
          engine.log('☠️ KONIEC GRY! Pokonany przez przeciwnika.');
        }
      }
    }
  }

  // 7. Strefa śmierci KillZ (upadek w próżnię)
  if (player.transform.y < -6.0) {
    if (typeof engine !== 'undefined') {
      if (engine.setHealth) engine.setHealth(0);
      if (engine.setGameStatus) engine.setGameStatus('GAMEOVER');
      if (engine.playSfx) engine.playSfx('gameover', 1.0);
      engine.log('☠️ Gracz spadł w otchłań! Game Over.');
    }
  }
}`,
    createdAt: Date.now()
  },
  {
    id: 'script_breakout',
    name: 'BP_BreakoutController.js',
    code: `// 🧱 BP_BreakoutController - Zaawansowany Arkanoid 3D z fizyką i dźwiękiem
export function update(entities, dt, input, contacts) {
  const paddle = entities.find(e => e.tag === 'Paddle');
  const ball = entities.find(e => e.tag === 'Ball');
  
  if (typeof engine !== 'undefined' && engine.getGameStatus) {
    const status = engine.getGameStatus();
    if (status === 'GAMEOVER' || status === 'VICTORY') return;
  }

  // 1. Sterowanie paletką (A / D lub Mysz)
  if (paddle && paddle.transform) {
    const speed = 20.0;
    if (input?.keys?.KeyA || input?.keys?.ArrowLeft) {
      paddle.transform.x = Math.max(-9.0, paddle.transform.x - speed * dt);
    }
    if (input?.keys?.KeyD || input?.keys?.ArrowRight) {
      paddle.transform.x = Math.min(9.0, paddle.transform.x + speed * dt);
    }
  }

  // 2. Logika odbić piłki i fizyki
  if (ball && ball.transform) {
    if (!ball.velocity) ball.velocity = { vx: 7.0, vy: 9.0, vz: 0.0 };

    // Odbicia od ścian bocznych
    if (ball.transform.x <= -10.0 && ball.velocity.vx < 0) {
      ball.transform.x = -10.0;
      ball.velocity.vx = -ball.velocity.vx;
      if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('hit', 0.5);
    }
    if (ball.transform.x >= 10.0 && ball.velocity.vx > 0) {
      ball.transform.x = 10.0;
      ball.velocity.vx = -ball.velocity.vx;
      if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('hit', 0.5);
    }
    if (ball.transform.y >= 12.0 && ball.velocity.vy > 0) {
      ball.transform.y = 12.0;
      ball.velocity.vy = -ball.velocity.vy;
      if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('hit', 0.5);
    }

    // Odbicie od paletki z nadaniem dynamicznego kąta
    if (paddle && paddle.transform) {
      const dx = ball.transform.x - paddle.transform.x;
      const dy = ball.transform.y - paddle.transform.y;
      if (Math.abs(dx) <= 2.5 && dy >= 0.0 && dy <= 1.2 && ball.velocity.vy < 0) {
        ball.velocity.vy = Math.abs(ball.velocity.vy);
        ball.velocity.vx = dx * 4.8;
        if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('hit', 0.9);
      }
    }

    // Kolizje z klockami
    const bricks = entities.filter(e => e.tag?.startsWith('Brick'));
    let activeBricks = 0;
    for (const brick of bricks) {
      if (!brick.transform || brick.transform.y < -100) continue;
      activeBricks++;
      const bdx = Math.abs(ball.transform.x - brick.transform.x);
      const bdy = Math.abs(ball.transform.y - brick.transform.y);
      if (bdx < 1.4 && bdy < 0.9) {
        brick.transform.y = -999; // Zniszcz klocek
        ball.velocity.vy = -ball.velocity.vy;

        if (typeof engine !== 'undefined') {
          if (engine.playSfx) engine.playSfx('explosion', 0.7);
          if (engine.shakeCamera) engine.shakeCamera(0.3, 0.15);
          if (engine.setScore) {
            const cur = engine.getScore ? engine.getScore() : 0;
            engine.setScore(cur + 20);
          }
        }
        break;
      }
    }

    // Sprawdzenie wygranej (rozbito wszystkie klocki)
    if (activeBricks === 0 && bricks.length > 0) {
      if (typeof engine !== 'undefined') {
        if (engine.setGameStatus) engine.setGameStatus('VICTORY');
        if (engine.playSfx) engine.playSfx('victory', 1.0);
        engine.log('🎉 ZWYCIĘSTWO! Wszystkie klocki zniszczone!');
      }
    }

    // Piłka spadła poniżej paletki (Utrata życia)
    if (ball.transform.y < -11.0) {
      if (typeof engine !== 'undefined') {
        const curHp = engine.getHealth ? engine.getHealth() : 100;
        const newHp = Math.max(0, curHp - 34);
        if (engine.setHealth) engine.setHealth(newHp);
        if (engine.playSfx) engine.playSfx('hit', 1.0);

        if (newHp <= 0) {
          if (engine.setGameStatus) engine.setGameStatus('GAMEOVER');
          if (engine.playSfx) engine.playSfx('gameover', 1.0);
          engine.log('☠️ Koniec gry! Wykorzystano wszystkie szanse.');
        } else {
          // Reset pozycji piłki
          ball.transform.x = 0;
          ball.transform.y = -5.0;
          ball.velocity.vx = (Math.random() > 0.5 ? 1 : -1) * 7.0;
          ball.velocity.vy = 9.0;
        }
      }
    }
  }
}`,
    createdAt: Date.now()
  },
  {
    id: 'script_shooter',
    name: 'BP_SpaceShooterController.js',
    code: `// 🚀 BP_SpaceShooterController - Kosmiczna strzelanka 3D ze strzelaniem i meteorytami
let lastShootTime = 0;
export function update(entities, dt, input, contacts) {
  const ship = entities.find(e => e.tag === 'PlayerShip');
  if (!ship || !ship.transform) return;

  if (typeof engine !== 'undefined' && engine.getGameStatus) {
    const status = engine.getGameStatus();
    if (status === 'GAMEOVER' || status === 'VICTORY') return;
  }

  const speed = 18.0;
  // 1. Sterowanie statkiem WASD w płaszczyźnie ekranu
  if (input?.keys?.KeyA || input?.keys?.ArrowLeft)  ship.transform.x = Math.max(-12.0, ship.transform.x - speed * dt);
  if (input?.keys?.KeyD || input?.keys?.ArrowRight) ship.transform.x = Math.min(12.0, ship.transform.x + speed * dt);
  if (input?.keys?.KeyW || input?.keys?.ArrowUp)    ship.transform.z = Math.max(0.0, ship.transform.z - speed * dt);
  if (input?.keys?.KeyS || input?.keys?.ArrowDown)  ship.transform.z = Math.min(14.0, ship.transform.z + speed * dt);

  const time = typeof engine !== 'undefined' && engine.getTime ? engine.getTime() : Date.now() * 0.001;

  // 2. Strzelanie laserem (Spacja / LPM)
  if ((input?.keys?.Space || input?.mouse?.left) && (time - lastShootTime > 0.22)) {
    lastShootTime = time;
    if (typeof engine !== 'undefined') {
      if (engine.playSfx) engine.playSfx('laser', 0.7);
      if (engine.spawnEntity) {
        engine.spawnEntity({
          tag: 'LaserBullet',
          transform: { x: ship.transform.x, y: ship.transform.y, z: ship.transform.z - 1.5, sx: 0.3, sy: 0.3, sz: 1.2 },
          velocity: { vx: 0, vy: 0, vz: -40.0 },
          rigidBody: { mass: 0.1, shapeType: 0, isSensor: true }
        });
      }
    }
  }

  // 3. Ruch pocisków i kolizje
  const bullets = entities.filter(e => e.tag === 'LaserBullet');
  const meteors = entities.filter(e => e.tag?.startsWith('Meteor'));

  for (const b of bullets) {
    if (!b.transform) continue;
    if (b.transform.z < -30.0) {
      if (typeof engine !== 'undefined' && engine.destroyEntity) engine.destroyEntity(b.id);
      continue;
    }

    for (const m of meteors) {
      if (!m.transform || m.transform.y < -100) continue;
      const dist = Math.hypot(b.transform.x - m.transform.x, b.transform.z - m.transform.z);
      if (dist < 1.6) {
        // Zniszcz meteoryt i pocisk
        m.transform.z = -25.0 - Math.random() * 15.0;
        m.transform.x = (Math.random() - 0.5) * 20.0;
        if (typeof engine !== 'undefined') {
          if (engine.destroyEntity) engine.destroyEntity(b.id);
          if (engine.playSfx) engine.playSfx('explosion', 0.8);
          if (engine.shakeCamera) engine.shakeCamera(0.4, 0.2);
          if (engine.setScore) {
            const cur = engine.getScore ? engine.getScore() : 0;
            const newScore = cur + 50;
            engine.setScore(newScore);
            if (newScore >= 500 && engine.setGameStatus) {
              engine.setGameStatus('VICTORY');
              if (engine.playSfx) engine.playSfx('victory', 1.0);
            }
          }
        }
        break;
      }
    }
  }

  // 4. Ruch meteorytów w stronę gracza
  for (const m of meteors) {
    if (!m.transform) continue;
    m.transform.z += 10.0 * dt;
    m.transform.ry = ((m.transform.ry || 0) + 60.0 * dt) % 360;

    // Reset meteorytu poza ekranem
    if (m.transform.z > 16.0) {
      m.transform.z = -25.0 - Math.random() * 10.0;
      m.transform.x = (Math.random() - 0.5) * 20.0;
    }

    // Kolizja meteorytu ze statkiem gracza
    const pDist = Math.hypot(ship.transform.x - m.transform.x, ship.transform.z - m.transform.z);
    if (pDist < 1.8) {
      m.transform.z = -25.0;
      if (typeof engine !== 'undefined') {
        const curHp = engine.getHealth ? engine.getHealth() : 100;
        const newHp = Math.max(0, curHp - 35);
        if (engine.setHealth) engine.setHealth(newHp);
        if (engine.playSfx) engine.playSfx('hit', 1.0);
        if (engine.shakeCamera) engine.shakeCamera(1.0, 0.4);

        if (newHp <= 0) {
          if (engine.setGameStatus) engine.setGameStatus('GAMEOVER');
          if (engine.playSfx) engine.playSfx('gameover', 1.0);
        }
      }
    }
  }
}`,
    createdAt: Date.now()
  },
  {
    id: 'script_racer',
    name: 'BP_HoverRacerController.js',
    code: `// 🏎️ BP_HoverRacerController - Wyścigi ścigaczy, punkty kontrolne i nitro boost
let speed = 0.0;
let currentLap = 1;
export function update(entities, dt, input, contacts) {
  const car = entities.find(e => e.tag === 'PlayerCar');
  if (!car || !car.transform) return;

  if (typeof engine !== 'undefined' && engine.getGameStatus) {
    const status = engine.getGameStatus();
    if (status === 'GAMEOVER' || status === 'VICTORY') return;
  }

  // 1. Akceleracja i hamowanie
  const maxSpeed = 35.0;
  const accel = 18.0;
  const drag = 8.0;

  if (input?.keys?.KeyW || input?.keys?.ArrowUp) {
    speed = Math.min(maxSpeed, speed + accel * dt);
  } else if (input?.keys?.KeyS || input?.keys?.ArrowDown) {
    speed = Math.max(-10.0, speed - accel * 1.5 * dt);
  } else {
    // Hamowanie naturalne (drag)
    if (speed > 0) speed = Math.max(0, speed - drag * dt);
    else if (speed < 0) speed = Math.min(0, speed + drag * dt);
  }

  // 2. Sterowanie skrętem (A / D)
  const turnSpeed = 65.0;
  if (input?.keys?.KeyA || input?.keys?.ArrowLeft) {
    car.transform.ry = (car.transform.ry || 0) + turnSpeed * dt * (speed >= 0 ? 1 : -1);
  }
  if (input?.keys?.KeyD || input?.keys?.ArrowRight) {
    car.transform.ry = (car.transform.ry || 0) - turnSpeed * dt * (speed >= 0 ? 1 : -1);
  }

  // Przeliczenie wektora prędkości z rotacji Y
  const rad = (car.transform.ry || 0) * (Math.PI / 180);
  car.transform.x -= Math.sin(rad) * speed * dt;
  car.transform.z -= Math.cos(rad) * speed * dt;

  // Lewitacja (falowanie ścigacza)
  const time = typeof engine !== 'undefined' && engine.getTime ? engine.getTime() : Date.now() * 0.001;
  car.transform.y = 0.6 + Math.sin(time * 6.0) * 0.1;

  // 3. Sprawdzanie bramek kontrolnych (Checkpoints)
  const checkpoints = entities.filter(e => e.tag?.startsWith('Checkpoint'));
  for (const cp of checkpoints) {
    if (!cp.transform) continue;
    const dist = Math.hypot(car.transform.x - cp.transform.x, car.transform.z - cp.transform.z);
    if (dist < 4.0) {
      if (typeof engine !== 'undefined') {
        if (engine.playSfx) engine.playSfx('powerup', 0.8);
        if (engine.setScore) {
          const s = (engine.getScore ? engine.getScore() : 0) + 100;
          engine.setScore(s);
          if (s >= 300 && engine.setGameStatus) {
            engine.setGameStatus('VICTORY');
            if (engine.playSfx) engine.playSfx('victory', 1.0);
          }
        }
      }
    }
  }
}`,
    createdAt: Date.now()
  }
];

const DEFAULT_MATERIALS: MaterialAsset[] = [
  { id: 'mat_gold',    name: 'M_Gold',       color: '#facc15', metallic: 1.0, roughness: 0.15 },
  { id: 'mat_chrome',  name: 'M_Chrome',     color: '#e2e8f0', metallic: 1.0, roughness: 0.05 },
  { id: 'mat_ruby',    name: 'MI_Ruby',      color: '#f43f5e', metallic: 0.1, roughness: 0.15 },
  { id: 'mat_emerald', name: 'MI_Emerald',   color: '#10b981', metallic: 0.2, roughness: 0.20 },
  { id: 'mat_neon',    name: 'MI_NeonCyan',  color: '#00f0ff', metallic: 0.0, roughness: 0.10, emissive: '#00b4d8', emissiveIntensity: 1.0 },
  { id: 'mat_copper',  name: 'M_Copper',     color: '#f97316', metallic: 0.9, roughness: 0.25 }
];

const DEFAULT_EMITTERS: ParticleEmitterConfig[] = [
  {
    id: 'emitter_fire',
    name: 'NS_Campfire',
    type: 'fire',
    position: { x: 0, y: 0.5, z: 0 },
    color: '#ff5500',
    count: 250,
    speed: 2.2,
    size: 0.35
  },
  {
    id: 'emitter_portal',
    name: 'NS_GlowPortal',
    type: 'portal',
    position: { x: 5, y: 1.5, z: -5 },
    color: '#00f0ff',
    count: 350,
    speed: 3.0,
    size: 0.25
  }
];

export function createEmptyProject(name: string = 'Czysty Projekt', folderPath?: string): Project {
  const id = 'proj_' + Date.now();
  const defaultLoc = (typeof localStorage !== 'undefined' && localStorage.getItem('lucid_project_location')) || 'projects';
  return {
    id,
    name,
    description: 'Czysty projekt gry od zera bez starych obiektów',
    folderPath: folderPath || `${defaultLoc}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    activeSceneId: 'scene_main',
    scenes: [
      {
        id: 'scene_main',
        name: 'Lvl_MainLevel.scene',
        entities: [
          {
            id: 1,
            tag: 'GroundPlane',
            transform: { x: 0, y: -0.5, z: 0, sx: 30, sy: 1, sz: 30 },
            mesh: { meshId: 1 },
            rigidBody: { mass: 0, shapeType: 0, restitution: 0.5, friction: 0.5 }
          }
        ],
        emitters: []
      }
    ],
    scripts: [
      {
        id: 'script_main',
        name: 'BP_GameLogic.js',
        code: `// 🎮 Główna logika gry w Lucid Engine\nexport function update(entities, dt, input, contacts) {\n  // Twój kod wykonywany w każdej klatce od zera\n}`,
        createdAt: Date.now()
      }
    ],
    materials: [...DEFAULT_MATERIALS],
    sounds: [],
    widgets: [],
    settings: {
      gravity: -9.81,
      ambientColor: '#ffffff',
      ambientIntensity: 0.5,
      sunColor: '#ffffff',
      sunIntensity: 1.5,
      fogEnabled: false,
      fogColor: '#0a0a14',
      fogNear: 25,
      fogFar: 120
    }
  };
}

export function createPlatformerProject(name: string = 'Platformer 3D (Coin Runner)', folderPath?: string): Project {
  const id = 'proj_' + Date.now();
  const script = DEFAULT_SCRIPTS.find(s => s.name === 'BP_PlatformerController.js') || DEFAULT_SCRIPTS[0];
  const defaultLoc = (typeof localStorage !== 'undefined' && localStorage.getItem('lucid_project_location')) || 'projects';
  return {
    id,
    name,
    description: 'Platformówka 3D z kapsułową fizyką gracza, skokami, zbieraniem monet i wrogami',
    folderPath: folderPath || `${defaultLoc}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    activeSceneId: 'scene_main',
    scenes: [
      {
        id: 'scene_main',
        name: 'Lvl_Platformer.scene',
        entities: [
          // Ground
          { id: 1, tag: 'GroundPlane', transform: { x: 0, y: -0.5, z: 0, sx: 40, sy: 1, sz: 40 }, mesh: { meshId: 1 }, rigidBody: { mass: 0, shapeType: 0, restitution: 0.2, friction: 0.8 } },
          // Player (Capsule with locked rotations)
          { id: 2, tag: 'Player', transform: { x: 0, y: 1.0, z: 8, sx: 1.2, sy: 1.2, sz: 1.2 }, velocity: { vx: 0, vy: 0, vz: 0 }, mesh: { meshId: 2 }, rigidBody: { mass: 1.0, shapeType: 2, capsuleRadius: 0.6, capsuleHalfHeight: 0.8, lockRotationX: true, lockRotationZ: true, restitution: 0.0, friction: 0.8 }, scriptName: 'BP_PlatformerController.js' },
          // Moving platform
          { id: 3, tag: 'MovingPlatform', transform: { x: 0, y: 2.0, z: -5, sx: 5, sy: 0.6, sz: 4 }, rigidBody: { mass: 0, shapeType: 0 } },
          // Enemy
          { id: 4, tag: 'Enemy', transform: { x: 0, y: 1.0, z: 0, sx: 1.4, sy: 1.4, sz: 1.4 }, rigidBody: { mass: 2.0, shapeType: 0, lockRotationX: true, lockRotationZ: true } },
          // Coins (Sensors)
          { id: 5, tag: 'Coin_1', transform: { x: -4, y: 1.2, z: 4, sx: 0.7, sy: 0.7, sz: 0.7 }, rigidBody: { mass: 0, shapeType: 1, isSensor: true } },
          { id: 6, tag: 'Coin_2', transform: { x: 4, y: 1.2, z: 4, sx: 0.7, sy: 0.7, sz: 0.7 }, rigidBody: { mass: 0, shapeType: 1, isSensor: true } },
          { id: 7, tag: 'Coin_3', transform: { x: 0, y: 3.5, z: -5, sx: 0.7, sy: 0.7, sz: 0.7 }, rigidBody: { mass: 0, shapeType: 1, isSensor: true } },
          { id: 8, tag: 'Coin_4', transform: { x: -6, y: 1.2, z: -8, sx: 0.7, sy: 0.7, sz: 0.7 }, rigidBody: { mass: 0, shapeType: 1, isSensor: true } },
          { id: 9, tag: 'Coin_5', transform: { x: 6, y: 1.2, z: -8, sx: 0.7, sy: 0.7, sz: 0.7 }, rigidBody: { mass: 0, shapeType: 1, isSensor: true } },
        ],
        emitters: [...DEFAULT_EMITTERS]
      }
    ],
    scripts: [script],
    materials: [...DEFAULT_MATERIALS],
    sounds: [...DEFAULT_SOUNDS],
    widgets: [...DEFAULT_WIDGETS],
    activeWidgetId: 'wbp_hud',
    settings: {
      gravity: -9.81,
      ambientColor: '#ffffff',
      ambientIntensity: 0.6,
      sunColor: '#ffffff',
      sunIntensity: 1.4,
      fogEnabled: false,
      fogColor: '#0a0a14',
      fogNear: 25,
      fogFar: 120
    }
  };
}

export function createBreakoutProject(name: string = 'Breakout 3D Extended', folderPath?: string): Project {
  const id = 'proj_' + Date.now();
  const breakoutScript = DEFAULT_SCRIPTS.find(s => s.name === 'BP_BreakoutController.js') || DEFAULT_SCRIPTS[1];
  const defaultLoc = (typeof localStorage !== 'undefined' && localStorage.getItem('lucid_project_location')) || 'projects';
  
  const bricks: any[] = [];
  let bId = 10;
  for (let r = 0; r < 4; ++r) {
    for (let c = -4; c <= 4; ++c) {
      bricks.push({
        id: bId++,
        tag: `Brick_${r}_${c}`,
        transform: { x: c * 2.2, y: 5.0 + r * 1.3, z: 0, sx: 1.8, sy: 0.8, sz: 0.8 },
        rigidBody: { mass: 0, shapeType: 0, restitution: 1.0, friction: 0 }
      });
    }
  }

  return {
    id,
    name,
    description: 'Klasyczna arkadówka Breakout 3D z uderzeniami, niszczeniem klocków i życiami',
    folderPath: folderPath || `${defaultLoc}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    activeSceneId: 'scene_main',
    scenes: [
      {
        id: 'scene_main',
        name: 'Lvl_Breakout.scene',
        entities: [
          { id: 1, tag: 'Paddle', transform: { x: 0, y: -8, z: 0, sx: 4.5, sy: 0.8, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, restitution: 1.0, friction: 0 } },
          { id: 2, tag: 'Ball', transform: { x: 0, y: -5, z: 0, sx: 0.8, sy: 0.8, sz: 0.8 }, velocity: { vx: 7, vy: 9, vz: 0 }, rigidBody: { mass: 1, shapeType: 1, restitution: 1.0, friction: 0 } },
          { id: 3, tag: 'WallLeft', transform: { x: -11, y: 2, z: 0, sx: 0.8, sy: 22, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, restitution: 1.0, friction: 0 } },
          { id: 4, tag: 'WallRight', transform: { x: 11, y: 2, z: 0, sx: 0.8, sy: 22, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, restitution: 1.0, friction: 0 } },
          { id: 5, tag: 'WallTop', transform: { x: 0, y: 13, z: 0, sx: 23, sy: 0.8, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, restitution: 1.0, friction: 0 } },
          ...bricks
        ],
        emitters: []
      }
    ],
    scripts: [breakoutScript],
    materials: [...DEFAULT_MATERIALS],
    sounds: [...DEFAULT_SOUNDS],
    widgets: [...DEFAULT_WIDGETS],
    activeWidgetId: 'wbp_hud',
    settings: {
      gravity: 0, // No gravity for 2D arcade breakout plane
      ambientColor: '#ffffff',
      ambientIntensity: 0.7,
      sunColor: '#ffffff',
      sunIntensity: 1.2,
      fogEnabled: false,
      fogColor: '#0a0a14',
      fogNear: 25,
      fogFar: 120
    }
  };
}

export function createSpaceShooterProject(name: string = 'Retro Space Shooter 3D', folderPath?: string): Project {
  const id = 'proj_' + Date.now();
  const script = DEFAULT_SCRIPTS.find(s => s.name === 'BP_SpaceShooterController.js') || DEFAULT_SCRIPTS[2];
  const defaultLoc = (typeof localStorage !== 'undefined' && localStorage.getItem('lucid_project_location')) || 'projects';
  
  const meteors: any[] = [];
  for (let i = 0; i < 6; ++i) {
    meteors.push({
      id: 10 + i,
      tag: `Meteor_${i}`,
      transform: { x: ((i % 3) - 1.0) * 7.0, y: 0.5, z: -10.0 - i * 5.0, sx: 1.4, sy: 1.4, sz: 1.4 },
      rigidBody: { mass: 1.0, shapeType: 1, isSensor: true }
    });
  }

  return {
    id,
    name,
    description: 'Dynamiczna strzelanka 3D ze statkiem, laserami i deszczem meteorytów',
    folderPath: folderPath || `${defaultLoc}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    activeSceneId: 'scene_main',
    scenes: [
      {
        id: 'scene_main',
        name: 'Lvl_SpaceShooter.scene',
        entities: [
          { id: 1, tag: 'PlayerShip', transform: { x: 0, y: 0.5, z: 12, sx: 2.2, sy: 0.6, sz: 2.2 }, velocity: { vx: 0, vy: 0, vz: 0 }, mesh: { meshId: 2 }, rigidBody: { mass: 1.0, shapeType: 0, lockRotationX: true, lockRotationZ: true } },
          ...meteors
        ],
        emitters: [
          { id: 'emit_stars', name: 'NS_SpaceDust', type: 'portal', position: { x: 0, y: 0, z: -10 }, color: '#38bdf8', count: 300, speed: 4.0, size: 0.2 }
        ]
      }
    ],
    scripts: [script],
    materials: [...DEFAULT_MATERIALS],
    sounds: [...DEFAULT_SOUNDS],
    widgets: [...DEFAULT_WIDGETS],
    activeWidgetId: 'wbp_hud',
    settings: {
      gravity: 0,
      ambientColor: '#1e1e38',
      ambientIntensity: 0.4,
      sunColor: '#60a5fa',
      sunIntensity: 2.0,
      fogEnabled: true,
      fogColor: '#050510',
      fogNear: 15,
      fogFar: 60
    }
  };
}

export function createHoverRacerProject(name: string = 'Arcade Hover Racer', folderPath?: string): Project {
  const id = 'proj_' + Date.now();
  const script = DEFAULT_SCRIPTS.find(s => s.name === 'BP_HoverRacerController.js') || DEFAULT_SCRIPTS[3];
  const defaultLoc = (typeof localStorage !== 'undefined' && localStorage.getItem('lucid_project_location')) || 'projects';
  return {
    id,
    name,
    description: 'Wyścigi ścigaczy z punktami kontrolnymi i lewitacją nad torem',
    folderPath: folderPath || `${defaultLoc}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    activeSceneId: 'scene_main',
    scenes: [
      {
        id: 'scene_main',
        name: 'Lvl_Track.scene',
        entities: [
          { id: 1, tag: 'GroundTrack', transform: { x: 0, y: -0.5, z: 0, sx: 60, sy: 1, sz: 120 }, mesh: { meshId: 1 }, rigidBody: { mass: 0, shapeType: 0, friction: 0.2 } },
          { id: 2, tag: 'PlayerCar', transform: { x: 0, y: 0.8, z: 20, sx: 2.0, sy: 0.8, sz: 3.5 }, velocity: { vx: 0, vy: 0, vz: 0 }, mesh: { meshId: 2 }, rigidBody: { mass: 2.0, shapeType: 2, capsuleRadius: 0.8, capsuleHalfHeight: 1.2, lockRotationX: true, lockRotationZ: true } },
          { id: 3, tag: 'Checkpoint_1', transform: { x: 0, y: 1.5, z: -10, sx: 10, sy: 3, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, isSensor: true } },
          { id: 4, tag: 'Checkpoint_2', transform: { x: 0, y: 1.5, z: -40, sx: 10, sy: 3, sz: 1 }, rigidBody: { mass: 0, shapeType: 0, isSensor: true } },
          { id: 5, tag: 'WallL', transform: { x: -8, y: 1.0, z: 0, sx: 1, sy: 2, sz: 100 }, rigidBody: { mass: 0, shapeType: 0 } },
          { id: 6, tag: 'WallR', transform: { x: 8, y: 1.0, z: 0, sx: 1, sy: 2, sz: 100 }, rigidBody: { mass: 0, shapeType: 0 } },
        ],
        emitters: [
          { id: 'emit_speed', name: 'NS_SpeedTrail', type: 'fire', position: { x: 0, y: 0.5, z: 22 }, color: '#00f0ff', count: 150, speed: 3.0, size: 0.2 }
        ]
      }
    ],
    scripts: [script],
    materials: [...DEFAULT_MATERIALS],
    sounds: [...DEFAULT_SOUNDS],
    widgets: [...DEFAULT_WIDGETS],
    activeWidgetId: 'wbp_hud',
    settings: {
      gravity: -9.81,
      ambientColor: '#ffffff',
      ambientIntensity: 0.6,
      sunColor: '#f59e0b',
      sunIntensity: 1.5,
      fogEnabled: false,
      fogColor: '#0a0a14',
      fogNear: 25,
      fogFar: 120
    }
  };
}

export class ProjectManager {
  public static getAllProjects(): Project[] {
    try {
      const data = localStorage.getItem(STORAGE_KEY);
      if (data) {
        const list = JSON.parse(data);
        if (Array.isArray(list) && list.length > 0) {
          return list.map(p => ({
            ...p,
            sounds: p.sounds || [...DEFAULT_SOUNDS],
            widgets: p.widgets || [...DEFAULT_WIDGETS],
          }));
        }
      }
    } catch {}
    // Initialize platformer project as default rich game
    const def = createPlatformerProject();
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify([def]));
      localStorage.setItem(ACTIVE_PROJECT_KEY, def.id);
    } catch {}
    return [def];
  }

  public static getActiveProject(): Project {
    const list = this.getAllProjects();
    const activeId = localStorage.getItem(ACTIVE_PROJECT_KEY);
    const found = list.find(p => p.id === activeId);
    return found || list[0];
  }

  public static saveProject(project: Project): void {
    project.updatedAt = Date.now();
    let list: Project[] = [];
    try {
      const data = localStorage.getItem(STORAGE_KEY);
      if (data) {
        const parsed = JSON.parse(data);
        if (Array.isArray(parsed)) list = parsed;
      }
    } catch {}

    const idx = list.findIndex(p => p.id === project.id);
    if (idx >= 0) list[idx] = project;
    else list.push(project);

    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
      localStorage.setItem(ACTIVE_PROJECT_KEY, project.id);
    } catch {}
  }

  public static deleteProject(id: string): Project[] {
    let list: Project[] = [];
    try {
      const data = localStorage.getItem(STORAGE_KEY);
      if (data) {
        const parsed = JSON.parse(data);
        if (Array.isArray(parsed)) list = parsed;
      }
    } catch {}

    list = list.filter(p => p.id !== id);
    if (list.length === 0) list.push(createPlatformerProject());

    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
      localStorage.setItem(ACTIVE_PROJECT_KEY, list[0].id);
    } catch {}
    return list;
  }

  public static exportProjectToJson(project: Project): void {
    const dataStr = 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(project, null, 2));
    const dlAnchor = document.createElement('a');
    dlAnchor.setAttribute('href', dataStr);
    dlAnchor.setAttribute('download', `${project.name.toLowerCase().replace(/[^a-z0-9]/g, '_')}.lucidproject`);
    dlAnchor.click();
    dlAnchor.remove();
  }

  public static importProjectFromJson(jsonString: string): Project {
    const parsed = JSON.parse(jsonString);
    if (!parsed.id || !parsed.name || !Array.isArray(parsed.scripts)) {
      throw new Error('Nieprawidłowy format pliku .lucidproject');
    }
    parsed.id = 'proj_' + Date.now();
    this.saveProject(parsed);
    return parsed;
  }
}
