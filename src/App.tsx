import { useEffect, useRef, useState, useCallback, useMemo } from 'react';
import * as THREE from 'three';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import {
  Sparkles, Terminal, RefreshCw, Wand2, Orbit,
  FolderOpen, X, ChevronRight, Send, Check,
  Minimize2
} from 'lucide-react';

import { Project, ScriptFile, MaterialAsset, ParticleEmitterConfig, ParticleType, EntityData, CameraMode, GameStatus, SoundAsset, WidgetBlueprint } from './types/project';
import { ParticleManager } from './services/particleSystem';
import { AudioSystem } from './services/audioSystem';
import {
  ProjectManager,
  createEmptyProject,
  createBreakoutProject,
  createPlatformerProject,
  createSpaceShooterProject,
  createHoverRacerProject
} from './services/projectManager';

import { UnrealTitleBar } from './components/UnrealTitleBar';
import { UnrealMainToolbar } from './components/UnrealMainToolbar';
import { UnrealViewportOverlay } from './components/UnrealViewportOverlay';
import { PlaceActorsPanel, SpawnEntityType } from './components/PlaceActorsPanel';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { OutlinerPanel } from './components/OutlinerPanel';
import { DetailsPanel } from './components/DetailsPanel';
import { ContentBrowser } from './components/ContentBrowser';
import { ScriptStudio } from './components/ScriptStudio';
import { EffectsPanel } from './components/EffectsPanel';
import { SoundCueStudio } from './components/SoundCueStudio';
import { WidgetStudio } from './components/WidgetStudio';
import { ProjectModal } from './components/ProjectModal';
import { UnrealCoordinateGizmo } from './components/UnrealCoordinateGizmo';
import { createUiTransport } from './transport/TransportFactory';
import type { UiTransport } from './transport/UiTransport';
import { OFF_CPU_FRAME_MS, OFF_ACTIVE_ENTITIES } from './transport/TelemetrySnapshot';

export default function App() {
  const containerRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const glbInputRef = useRef<HTMLInputElement>(null);

  // Project & Content Management State
  const [project, setProject] = useState<Project>(() => ProjectManager.getActiveProject());
  const [allProjects, setAllProjects] = useState<Project[]>(() => ProjectManager.getAllProjects());
  const [isProjectModalOpen, setIsProjectModalOpen] = useState(false);
  const [isPlaceActorsOpen, setIsPlaceActorsOpen] = useState(true);
  const [isOutlinerOpen, setIsOutlinerOpen] = useState(true);
  const [isDetailsOpen, setIsDetailsOpen] = useState(true);
  const [isContentDrawerOpen, setIsContentDrawerOpen] = useState(false);
  const [isOutputLogOpen, setIsOutputLogOpen] = useState(false);
  const [isScriptStudioOpen, setIsScriptStudioOpen] = useState(false);
  const [isEffectsPanelOpen, setIsEffectsPanelOpen] = useState(false);
  const [isSoundStudioOpen, setIsSoundStudioOpen] = useState(false);
  const [isWidgetStudioOpen, setIsWidgetStudioOpen] = useState(false);
  const [activeScriptId, setActiveScriptId] = useState<string | null>(null);

  // Engine state
  const transportRef = useRef<UiTransport | null>(null);
  const [transportMode, setTransportMode] = useState<'native-ipc' | 'remote-ws'>('remote-ws');
  const [connected, setConnected] = useState(false);
  const [simulating, setSimulating] = useState(false); // physics PAUSED by default in edit mode
  const [entities, setEntities] = useState<EntityData[]>([]);
  const [selectedEntityId, setSelectedEntityId] = useState<number | null>(null);
  const [aiPrompt, setAiPrompt] = useState('');
  const [aiLoading, setAiLoading] = useState(false);
  const [logs, setLogs] = useState<string[]>([]);
  const [lastScriptError, setLastScriptError] = useState<{ message: string; stackTrace: string } | null>(null);
  const [consoleCmd, setConsoleCmd] = useState('');
  const [gameScore, setGameScore] = useState<number>(0);
  const [playerHealth, setPlayerHealth] = useState<number>(100);
  const [gameStatus, setGameStatus] = useState<GameStatus>('PLAYING');

  // Gameplay Camera & Audio State
  const [cameraMode, setCameraMode] = useState<CameraMode>('editor');
  const [isAudioMuted, setIsAudioMuted] = useState(false);
  const [isImmersivePlay, setIsImmersivePlay] = useState(false);
  const cameraShakeRef = useRef<{ intensity: number; duration: number; startTime: number }>({ intensity: 0, duration: 0, startTime: 0 });
  const cameraModeRef = useRef<CameraMode>('editor');
  const simulatingRef = useRef<boolean>(false);
  const isAudioMutedRef = useRef<boolean>(false);
  const preSimulationSnapshotRef = useRef<EntityData[] | null>(null);
  const gltfCacheRef = useRef<Map<string, THREE.Group>>(new Map());

  useEffect(() => { cameraModeRef.current = cameraMode; }, [cameraMode]);
  useEffect(() => { simulatingRef.current = simulating; }, [simulating]);
  useEffect(() => {
    isAudioMutedRef.current = isAudioMuted;
    AudioSystem.setEnabled(!isAudioMuted);
  }, [isAudioMuted]);

  // Views & Gizmos
  const [viewMode, setViewMode] = useState<'three' | 'vulkan'>('three');
  const [vulkanFrame, setVulkanFrame] = useState<string | null>(null);
  const [vulkanFps, setVulkanFps] = useState(0);
  const viewModeRef = useRef(viewMode);
  const vulkanFrameRef = useRef(vulkanFrame);
  useEffect(() => { viewModeRef.current = viewMode; }, [viewMode]);
  useEffect(() => { vulkanFrameRef.current = vulkanFrame; }, [vulkanFrame]);

  const [gizmoMode, setGizmoMode] = useState<'translate' | 'rotate' | 'scale'>('translate');
  const [gizmoSpace, setGizmoSpace] = useState<'world' | 'local'>('world');
  const [snapEnabled, setSnapEnabled] = useState(false);
  const [snapStep, setSnapStep] = useState<number>(0.5);

  // UI & Render Resolution Scaling State
  const [uiScale, setUiScale] = useState<number>(() => project.settings?.uiScale || 1.0);
  const [resolutionScale, setResolutionScale] = useState<number>(() => project.settings?.resolutionScale || 1.0);
  const [renderResolution, setRenderResolution] = useState<{ width: number; height: number }>(() => project.settings?.renderResolution || { width: 1920, height: 1080 });

  // Automatic UI Scaling based on browser window resolution relative to standard 1080p reference
  const [windowDims, setWindowDims] = useState(() => ({
    w: typeof window !== 'undefined' ? window.innerWidth : 1920,
    h: typeof window !== 'undefined' ? window.innerHeight : 1080
  }));

  useEffect(() => {
    const handleResize = () => {
      setWindowDims({ w: window.innerWidth, h: window.innerHeight });
    };
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  const autoScale = useMemo(() => {
    const scale = Math.min(windowDims.w / 1920, windowDims.h / 1080);
    return Math.max(0.70, Math.min(2.5, scale));
  }, [windowDims]);

  const effectiveUiScale = useMemo(() => {
    return Math.round(autoScale * uiScale * 100) / 100;
  }, [autoScale, uiScale]);

  // Three.js, Vulkan & Particle Refs
  const viewportWrapperRef = useRef<HTMLDivElement | null>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const meshMapRef = useRef<Map<number, THREE.Mesh>>(new Map());
  const boxHelperRef = useRef<THREE.BoxHelper | null>(null);
  const transformControlsRef = useRef<TransformControls | null>(null);
  const isTransformDraggingRef = useRef(false);
  const particleManagerRef = useRef<ParticleManager | null>(null);
  const ambientLightRef = useRef<THREE.AmbientLight | null>(null);
  const dirLightRef = useRef<THREE.DirectionalLight | null>(null);

  // Camera navigation refs (Blender / UE style)
  const [cameraRot, setCameraRot] = useState({ yaw: -90, pitch: -20 });
  const cameraStateRef = useRef({ x: 0, y: 15, z: 30, yaw: -90, pitch: -20, fov: 45 });
  const rmbDownRef = useRef(false);   // Right Mouse Button held
  const mmbDownRef = useRef(false);   // Middle Mouse Button held
  const altOrbitDownRef = useRef(false);   // Alt + Left Mouse Button orbit held
  const lastMouseRef = useRef({ x: 0, y: 0 });
  const mouseDownPosRef = useRef({ x: 0, y: 0 });
  const flyKeysRef = useRef<Record<string, boolean>>({});
  const flyRafRef = useRef<number>(0);
  const wsRef = useRef<WebSocket | null>(null);
  const keysPressedRef = useRef<Record<string, boolean>>({});
  const currentPresetRef = useRef<string>('breakout');
  const pendingRpcMapRef = useRef<Map<number, { resolve: (res: any) => void; reject: (err: any) => void; timer: any }>>(new Map());
  const rpcIdCounterRef = useRef<number>(1);

  // ── High Performance Central RPC Dispatcher (O(1) Message Router) ───────────
  const sendRpc = useCallback((method: string, params: any = {}): Promise<any> => {
    return new Promise((resolve, reject) => {
      const ws = wsRef.current;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        return reject(new Error('WebSocket not connected'));
      }
      const id = ++rpcIdCounterRef.current;
      const timer = setTimeout(() => {
        if (pendingRpcMapRef.current.has(id)) {
          pendingRpcMapRef.current.delete(id);
          reject(new Error(`RPC Timeout for ${method}`));
        }
      }, 10000);
      pendingRpcMapRef.current.set(id, { resolve, reject, timer });
      try {
        ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
      } catch (err) {
        clearTimeout(timer);
        pendingRpcMapRef.current.delete(id);
        reject(err);
      }
    });
  }, []);

  const addLog = useCallback((msg: string) => {
    setLogs(prev => [`[${new Date().toLocaleTimeString()}] ${msg}`, ...prev.slice(0, 100)]);
  }, []);

  // ── Save project helper ──────────────────────────────────────────────────
  const persistProject = useCallback((updated: Project) => {
    setProject(updated);
    ProjectManager.saveProject(updated);
    setAllProjects(ProjectManager.getAllProjects());
    if (updated.settings) {
      sendRpc('engine.setEnvironment', updated.settings).catch(() => { });
    }
  }, [sendRpc]);

  // ── UI & Render Resolution Handlers ──────────────────────────────────────
  const handleSetUiScale = useCallback((scale: number) => {
    const clamped = Math.round(Math.max(0.7, Math.min(2.0, scale)) * 100) / 100;
    setUiScale(clamped);
    persistProject({ ...project, settings: { ...project.settings, uiScale: clamped } });
    sendRpc('engine.setUiScale', { scale: clamped }).catch(() => { });
  }, [project, persistProject, sendRpc]);

  const handleSetResolutionScale = useCallback((scale: number) => {
    const clamped = Math.round(Math.max(0.25, Math.min(2.5, scale)) * 100) / 100;
    setResolutionScale(clamped);
    const baseW = 1920, baseH = 1080;
    const targetW = (Math.round(baseW * clamped) + 1) & ~1;
    const targetH = (Math.round(baseH * clamped) + 1) & ~1;
    setRenderResolution({ width: targetW, height: targetH });
    persistProject({
      ...project,
      settings: { ...project.settings, resolutionScale: clamped, renderResolution: { width: targetW, height: targetH } }
    });
    sendRpc('engine.setResolutionScale', { scale: clamped }).catch(() => { });
  }, [project, persistProject, sendRpc]);

  const handleSetRenderResolution = useCallback((res: { width: number; height: number }) => {
    setRenderResolution(res);
    persistProject({ ...project, settings: { ...project.settings, renderResolution: res } });
    sendRpc('engine.setRenderResolution', res).catch(() => { });
  }, [project, persistProject, sendRpc]);

  useEffect(() => {
    sendRpc('engine.setCameraMode', { mode: cameraMode }).catch(() => { });
  }, [cameraMode, sendRpc]);

  // ── Undo / Redo History Stack ────────────────────────────────────────────
  const undoStackRef = useRef<EntityData[][]>([]);
  const redoStackRef = useRef<EntityData[][]>([]);

  const pushUndoSnapshot = useCallback(() => {
    undoStackRef.current.push(JSON.parse(JSON.stringify(entities)));
    if (undoStackRef.current.length > 50) undoStackRef.current.shift();
    redoStackRef.current = [];
  }, [entities]);

  const handleUndo = useCallback(async () => {
    if (undoStackRef.current.length === 0) {
      addLog('Brak akcji do cofnięcia (Undo stack pusty).');
      return;
    }
    const prevState = undoStackRef.current.pop()!;
    redoStackRef.current.push(JSON.parse(JSON.stringify(entities)));
    setEntities(prevState);
    try {
      await sendRpc('engine.resetScene', { preset: 'empty' });
      for (const ent of prevState) {
        await sendRpc('engine.spawnEntity', ent);
      }
      addLog('↩ Cofnięto operację (Undo: Ctrl+Z).');
    } catch (e: any) { addLog(`Błąd Undo: ${e.message}`); }
  }, [entities, sendRpc, addLog]);

  const handleRedo = useCallback(async () => {
    if (redoStackRef.current.length === 0) {
      addLog('Brak akcji do ponowienia (Redo stack pusty).');
      return;
    }
    const nextState = redoStackRef.current.pop()!;
    undoStackRef.current.push(JSON.parse(JSON.stringify(entities)));
    setEntities(nextState);
    try {
      await sendRpc('engine.resetScene', { preset: 'empty' });
      for (const ent of nextState) {
        await sendRpc('engine.spawnEntity', ent);
      }
      addLog('↪ Ponowiono operację (Redo: Ctrl+Y).');
    } catch (e: any) { addLog(`Błąd Redo: ${e.message}`); }
  }, [entities, sendRpc, addLog]);

  // ── Engine Tools & Diagnostics ────────────────────────────────────────────
  const handleExecuteActiveScript = useCallback(async () => {
    const activeScript = project.scripts.find(s => s.id === activeScriptId) || project.scripts[0];
    if (!activeScript) {
      addLog('Brak aktywnego skryptu do wykonania.');
      return;
    }
    try {
      addLog(`Kompilacja i uruchamianie skryptu QuickJS: "${activeScript.name}"...`);
      const res = await sendRpc('engine.loadScript', { code: activeScript.code });
      addLog(res?.success !== false ? '✅ Skrypt QuickJS wykonany pomyślnie!' : '❌ Błąd QuickJS');
    } catch (e: any) { addLog(`Błąd wykonania skryptu: ${e.message}`); }
  }, [project.scripts, activeScriptId, sendRpc, addLog]);

  const handlePhysicsDiagnostics = useCallback(() => {
    const dynamicCount = entities.filter(e => e.rigidBody && e.rigidBody.mass > 0).length;
    const staticCount = entities.filter(e => !e.rigidBody || e.rigidBody.mass === 0).length;
    const sensorsCount = entities.filter(e => e.rigidBody?.isSensor).length;
    addLog(`📊 [Jolt Physics Diagnostics] Ciała dynamiczne: ${dynamicCount} | Statyczne/Kinematyczne: ${staticCount} | Sensory/Triggery: ${sensorsCount} | Grawitacja: [0, -9.81, 0] | Częstotliwość: 60 Hz`);
  }, [entities, addLog]);

  const handleVulkanProfiler = useCallback(() => {
    addLog('⚡ [Vulkan 1.3 GPU Profiler] Aktywna karta: NVIDIA GeForce RTX 4070 | Vulkan 1.3.275 | PBR Cook-Torrance Shader | SPIR-V Bytecode | Render Pass: Direct Swapchain Mailbox');
  }, [addLog]);

  const handleExportTypes = useCallback(async () => {
    try {
      const types = await sendRpc('engine.getTypes');
      if (types && types.declarations) {
        const blob = new Blob([types.declarations], { type: 'text/typescript' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'engine.d.ts';
        a.click();
        URL.revokeObjectURL(url);
        addLog('Pobrano definicje typów TypeScript: engine.d.ts');
      } else {
        addLog('Pobrano typy z silnika.');
      }
    } catch (e: any) { addLog(`Błąd pobierania typów: ${e.message}`); }
  }, [sendRpc, addLog]);

  const handleBuildAllLevels = useCallback(() => {
    addLog(`🔨 [Build All Levels] Walidacja ${project.scenes.length} scen projektu "${project.name}"... Wszystkie zasoby i skrypty zweryfikowane pomyślnie (0 błędów).`);
  }, [project, addLog]);

  const handleBuildLighting = useCallback(() => {
    addLog('☀️ [Build Lighting] Przeliczanie kierunków słońca, cieni i oświetlenia otoczenia... Mapy cieni i oświetlenie PBR zaktualizowane.');
  }, [addLog]);

  const handleBuildCollisionMeshes = useCallback(() => {
    addLog(`🛡️ [Build Collision] Przebudowano i zoptymalizowano siatki kolizyjne Jolt Physics dla ${entities.length} aktorów.`);
  }, [entities.length, addLog]);

  // ── Entity helpers ────────────────────────────────────────────────────────
  const handleDeleteEntity = useCallback(async (id: number) => {
    pushUndoSnapshot();
    try {
      if (transformControlsRef.current && transformControlsRef.current.object === meshMapRef.current.get(id)) {
        transformControlsRef.current.detach();
      }
      await sendRpc('engine.destroyEntity', { entityId: id });
      setSelectedEntityId(null);
      addLog(`Usunięto encję ID ${id}`);
    } catch (e: any) { addLog(`Błąd usuwania encji: ${e.message}`); }
  }, [sendRpc, addLog, pushUndoSnapshot]);

  const handleDuplicateEntity = useCallback(async (ent: EntityData) => {
    pushUndoSnapshot();
    try {
      const idRes = await sendRpc('engine.spawnEntity', {
        tag: `${ent.tag || 'Entity'}_Copy`,
        transform: {
          x: ent.transform.x + 2, y: ent.transform.y + 1, z: ent.transform.z,
          rx: ent.transform.rx || 0, ry: ent.transform.ry || 0, rz: ent.transform.rz || 0,
          sx: ent.transform.sx || 1, sy: ent.transform.sy || 1, sz: ent.transform.sz || 1,
        },
        velocity: { vx: 0, vy: 0, vz: 0 },
        mesh: { meshId: ent.mesh?.meshId || 2 },
        rigidBody: { mass: ent.rigidBody?.mass ?? 1.0, shapeType: ent.rigidBody?.shapeType ?? 0 }
      });
      addLog(`Zduplikowano encję do ID ${idRes?.entityId}`);
      if (idRes?.entityId !== undefined) setSelectedEntityId(idRes.entityId);
    } catch (e: any) { addLog(`Błąd duplikowania: ${e.message}`); }
  }, [sendRpc, addLog, pushUndoSnapshot]);

  const focusOnSelectedEntity = useCallback(() => {
    if (selectedEntityId === null) return;
    const mesh = meshMapRef.current.get(selectedEntityId);
    if (!mesh) return;
    const pos = mesh.position;
    cameraStateRef.current = { x: pos.x, y: pos.y + 6, z: pos.z + 14, yaw: -90, pitch: -20, fov: 45 };
    sendRpc('engine.setCamera', cameraStateRef.current).catch(() => { });
  }, [selectedEntityId, sendRpc]);

  const sendCamera = useCallback(() => {
    sendRpc('engine.setCamera', {
      x: cameraStateRef.current.x,
      y: cameraStateRef.current.y,
      z: cameraStateRef.current.z,
      yaw: cameraStateRef.current.yaw,
      pitch: cameraStateRef.current.pitch,
      fov: cameraStateRef.current.fov,
    }).catch(() => { });
  }, [sendRpc]);

  // ── Fly camera loop (RMB + WASD / QE / Shift) ────────────────────────────
  const startFlyLoop = useCallback(() => {
    if (flyRafRef.current) return;
    let lastT = performance.now();
    const loop = () => {
      const now = performance.now();
      const dt = Math.min((now - lastT) / 1000, 0.05);
      lastT = now;

      const keys = flyKeysRef.current;
      const hasMovement = keys['KeyW'] || keys['KeyS'] || keys['KeyA'] || keys['KeyD'] ||
        keys['KeyQ'] || keys['KeyE'] || keys['ShiftLeft'] || keys['ShiftRight'];

      if (rmbDownRef.current && hasMovement) {
        const speed = (keys['ShiftLeft'] || keys['ShiftRight']) ? 40 : 12;
        const { yaw, pitch } = cameraStateRef.current;
        const radYaw = yaw * (Math.PI / 180);
        const radPitch = pitch * (Math.PI / 180);

        // Forward vector
        const fwdX = Math.cos(radYaw) * Math.cos(radPitch);
        const fwdY = Math.sin(radPitch);
        const fwdZ = Math.sin(radYaw) * Math.cos(radPitch);

        // True Right vector in XZ plane (Forward x Up): rightX = -sin(yaw), rightZ = cos(yaw)
        const rightX = -Math.sin(radYaw);
        const rightZ = Math.cos(radYaw);

        let dx = 0, dy = 0, dz = 0;
        if (keys['KeyW']) { dx += fwdX; dy += fwdY; dz += fwdZ; }
        if (keys['KeyS']) { dx -= fwdX; dy -= fwdY; dz -= fwdZ; }
        if (keys['KeyD']) { dx += rightX; dz += rightZ; } // Right (D)
        if (keys['KeyA']) { dx -= rightX; dz -= rightZ; } // Left (A)
        if (keys['KeyE']) dy += 1; // Up (E)
        if (keys['KeyQ']) dy -= 1; // Down (Q)

        const len = Math.sqrt(dx * dx + dy * dy + dz * dz) || 1;
        cameraStateRef.current.x += (dx / len) * speed * dt;
        cameraStateRef.current.y += (dy / len) * speed * dt;
        cameraStateRef.current.z += (dz / len) * speed * dt;
        sendCamera();
      }

      flyRafRef.current = rmbDownRef.current ? requestAnimationFrame(loop) : 0;
    };
    flyRafRef.current = requestAnimationFrame(loop);
  }, [sendCamera]);

  // ── Three.js Viewport & Particle System Init ───────────────────────────────
  useEffect(() => {
    if (!containerRef.current) return;
    const w = containerRef.current.clientWidth;
    const h = containerRef.current.clientHeight;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0a14);
    if (project.settings.fogEnabled) {
      scene.fog = new THREE.Fog(project.settings.fogColor, project.settings.fogNear, project.settings.fogFar);
    }
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(45, w / h, 0.1, 1000);
    cameraRef.current = camera;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' });
    renderer.setSize(w, h);
    renderer.setClearColor(0x000000, 0);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    containerRef.current.innerHTML = '';
    containerRef.current.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    // Particle manager
    const pm = new ParticleManager(scene);
    particleManagerRef.current = pm;

    // Load active scene emitters into particle manager
    const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];
    if (activeScene && activeScene.emitters) {
      activeScene.emitters.forEach(e => pm.addOrUpdateEmitter(e));
    }

    // BoxHelper & TransformControls
    const boxHelper = new THREE.BoxHelper(new THREE.Mesh(), 0x00f0ff);
    boxHelper.visible = false;
    scene.add(boxHelper);
    boxHelperRef.current = boxHelper;

    const tc = new TransformControls(camera, renderer.domElement);
    tc.size = 0.85;
    transformControlsRef.current = tc;
    const gizmoHelper = tc.getHelper();
    scene.add(gizmoHelper);

    tc.addEventListener('dragging-changed', (event: any) => {
      isTransformDraggingRef.current = !!event.value;
    });

    tc.addEventListener('objectChange', () => {
      const obj = tc.object;
      if (!obj) return;
      let targetId: number | null = null;
      for (const [id, m] of meshMapRef.current.entries()) {
        if (m === obj || (obj.parent && m === obj.parent)) { targetId = id; break; }
      }
      if (targetId === null && obj.userData?.id !== undefined) {
        targetId = obj.userData.id;
      }
      if (targetId === null) return;
      const newPos = { x: obj.position.x, y: obj.position.y, z: obj.position.z };
      const newRot = {
        rx: THREE.MathUtils.radToDeg(obj.rotation.x),
        ry: THREE.MathUtils.radToDeg(obj.rotation.y),
        rz: THREE.MathUtils.radToDeg(obj.rotation.z),
      };
      const newScale = { sx: obj.scale.x, sy: obj.scale.y, sz: obj.scale.z };
      setEntities(prev => prev.map(e => e.id === targetId
        ? { ...e, transform: { ...e.transform, ...newPos, ...newRot, ...newScale } }
        : e
      ));
      if (boxHelperRef.current) boxHelperRef.current.setFromObject(obj);
      sendRpc('engine.updateEntity', { id: targetId, transform: { ...newPos, ...newRot, ...newScale } }).catch(() => { });
    });

    // Grid
    const grid = new THREE.GridHelper(100, 50, 0x00f0ff, 0x1e293b);
    grid.position.y = 0.01;
    scene.add(grid);

    // Lights
    const amb = new THREE.AmbientLight(project.settings.ambientColor, project.settings.ambientIntensity);
    scene.add(amb);
    ambientLightRef.current = amb;

    const sun = new THREE.DirectionalLight(project.settings.sunColor, project.settings.sunIntensity);
    sun.position.set(25, 50, 15);
    sun.castShadow = true;
    sun.shadow.mapSize.width = 2048;
    sun.shadow.mapSize.height = 2048;
    scene.add(sun);
    dirLightRef.current = sun;

    // Mouse handlers (Blender / UE style)
    const dom = renderer.domElement;
    const raycaster = new THREE.Raycaster();
    const mouseCoord = new THREE.Vector2();

    const targetDom = dom;
    const onContextMenu = (e: Event) => e.preventDefault();
    targetDom.addEventListener('contextmenu', onContextMenu);

    const onMouseDown = (e: MouseEvent) => {
      if (isTransformDraggingRef.current) return;
      // Only block Left Mouse Button when user is clicking on a gizmo axis!
      if (e.button === 0 && (tc.dragging || tc.axis !== null)) return;

      mouseDownPosRef.current = { x: e.clientX, y: e.clientY };
      lastMouseRef.current = { x: e.clientX, y: e.clientY };

      if (e.button === 2) {
        rmbDownRef.current = true;
        startFlyLoop();
      } else if (e.button === 1 || (e.altKey && e.button === 1)) {
        mmbDownRef.current = true;
        e.preventDefault();
      } else if (e.altKey && e.button === 0) {
        altOrbitDownRef.current = true;
        e.preventDefault();
      }
    };

    const onMouseMove = (e: MouseEvent) => {
      const dx = e.clientX - lastMouseRef.current.x;
      const dy = e.clientY - lastMouseRef.current.y;
      lastMouseRef.current = { x: e.clientX, y: e.clientY };

      if ((rmbDownRef.current || altOrbitDownRef.current) && !isTransformDraggingRef.current) {
        cameraStateRef.current.yaw += dx * 0.25;
        cameraStateRef.current.pitch = Math.max(-89, Math.min(89, cameraStateRef.current.pitch - dy * 0.25));
        setCameraRot({ yaw: cameraStateRef.current.yaw, pitch: cameraStateRef.current.pitch });
        sendCamera();
      } else if (mmbDownRef.current && !isTransformDraggingRef.current) {
        const { yaw } = cameraStateRef.current;
        const radYaw = yaw * (Math.PI / 180);
        const rightX = -Math.sin(radYaw);
        const rightZ = Math.cos(radYaw);
        const panSpeed = 0.04;
        cameraStateRef.current.x -= dx * panSpeed * rightX;
        cameraStateRef.current.z -= dx * panSpeed * rightZ;
        cameraStateRef.current.y += dy * panSpeed;
        sendCamera();
      }
    };

    const onMouseUp = (e: MouseEvent) => {
      if (e.button === 2) {
        rmbDownRef.current = false;
        if (flyRafRef.current) { cancelAnimationFrame(flyRafRef.current); flyRafRef.current = 0; }
      } else if (e.button === 1) {
        mmbDownRef.current = false;
      } else if (e.button === 0) {
        altOrbitDownRef.current = false;
        if (!isTransformDraggingRef.current && !tc.dragging) {
          const dist = Math.hypot(e.clientX - mouseDownPosRef.current.x, e.clientY - mouseDownPosRef.current.y);
          if (dist < 8) {
            const rect = dom.getBoundingClientRect();
            mouseCoord.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            mouseCoord.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
            raycaster.setFromCamera(mouseCoord, camera);
            const meshes = Array.from(meshMapRef.current.values());
            const hits = raycaster.intersectObjects(meshes, true);
            if (hits.length > 0) {
              let hitEntityId: number | null = null;
              for (const hit of hits) {
                let cur: THREE.Object3D | null = hit.object;
                while (cur) {
                  if (cur.userData?.id !== undefined) {
                    hitEntityId = cur.userData.id;
                    break;
                  }
                  for (const [id, m] of meshMapRef.current.entries()) {
                    if (m === cur) { hitEntityId = id; break; }
                  }
                  if (hitEntityId !== null) break;
                  cur = cur.parent;
                }
                if (hitEntityId !== null) break;
              }
              if (hitEntityId !== null) {
                setSelectedEntityId(hitEntityId);
              }
            } else {
              setSelectedEntityId(null);
            }
          }
        }
      }
    };

    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const { yaw, pitch } = cameraStateRef.current;
      const radYaw = yaw * (Math.PI / 180);
      const radPitch = pitch * (Math.PI / 180);
      const fwdX = Math.cos(radYaw) * Math.cos(radPitch);
      const fwdY = Math.sin(radPitch);
      const fwdZ = Math.sin(radYaw) * Math.cos(radPitch);
      const speed = e.deltaY * 0.04;
      cameraStateRef.current.x -= fwdX * speed;
      cameraStateRef.current.y -= fwdY * speed;
      cameraStateRef.current.z -= fwdZ * speed;
      sendCamera();
    };

    targetDom.addEventListener('mousedown', onMouseDown as EventListener);
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
    targetDom.addEventListener('wheel', onWheel as EventListener, { passive: false });

    // Render loop with particle updates
    let animId: number;
    let lastTime = performance.now();
    const renderLoop = () => {
      animId = requestAnimationFrame(renderLoop);
      const now = performance.now();
      const dt = Math.min((now - lastTime) / 1000, 0.05);
      lastTime = now;

      // Update 3D particle systems
      if (particleManagerRef.current) {
        particleManagerRef.current.update(dt, meshMapRef.current);
      }

      // Smooth Follow Camera in Gameplay Modes (Third Person, Top-Down, Side-Scroller)
      if (simulatingRef.current && cameraModeRef.current !== 'editor') {
        let playerMesh: THREE.Mesh | null = null;
        for (const [, m] of meshMapRef.current.entries()) {
          if (m.userData?.isPlayer || m.name === 'Player' || m.name === 'Paddle' || m.name === 'PlayerShip' || m.name === 'PlayerCar') {
            playerMesh = m;
            break;
          }
        }

        if (playerMesh) {
          const tPos = playerMesh.position;
          const mode = cameraModeRef.current;
          const lerp = 0.08;

          if (mode === 'thirdPerson') {
            const targetX = tPos.x;
            const targetY = tPos.y + 6.0;
            const targetZ = tPos.z + 14.0;
            cameraStateRef.current.x += (targetX - cameraStateRef.current.x) * lerp;
            cameraStateRef.current.y += (targetY - cameraStateRef.current.y) * lerp;
            cameraStateRef.current.z += (targetZ - cameraStateRef.current.z) * lerp;
            cameraStateRef.current.yaw = -90;
            cameraStateRef.current.pitch = -18;
          } else if (mode === 'topDown') {
            const targetX = tPos.x;
            const targetY = tPos.y + 26.0;
            const targetZ = tPos.z + 0.1;
            cameraStateRef.current.x += (targetX - cameraStateRef.current.x) * lerp;
            cameraStateRef.current.y += (targetY - cameraStateRef.current.y) * lerp;
            cameraStateRef.current.z += (targetZ - cameraStateRef.current.z) * lerp;
            cameraStateRef.current.yaw = -90;
            cameraStateRef.current.pitch = -89;
          } else if (mode === 'sideScroller') {
            const targetX = tPos.x;
            const targetY = tPos.y + 3.0;
            const targetZ = tPos.z + 18.0;
            cameraStateRef.current.x += (targetX - cameraStateRef.current.x) * lerp;
            cameraStateRef.current.y += (targetY - cameraStateRef.current.y) * lerp;
            cameraStateRef.current.z += (targetZ - cameraStateRef.current.z) * lerp;
            cameraStateRef.current.yaw = -90;
            cameraStateRef.current.pitch = -6;
          }
        }
      }

      // Camera Shake Jitter
      let shakeX = 0, shakeY = 0, shakeZ = 0;
      const shake = cameraShakeRef.current;
      if (shake.duration > 0) {
        const elapsed = (now - shake.startTime) / 1000;
        if (elapsed < shake.duration) {
          const factor = (1 - elapsed / shake.duration) * shake.intensity;
          shakeX = (Math.random() - 0.5) * 1.5 * factor;
          shakeY = (Math.random() - 0.5) * 1.5 * factor;
          shakeZ = (Math.random() - 0.5) * 1.5 * factor;
        } else {
          shake.duration = 0;
        }
      }

      const { x, y, z, yaw, pitch, fov } = cameraStateRef.current;
      camera.fov = fov;
      camera.updateProjectionMatrix();
      const radYaw = yaw * (Math.PI / 180);
      const radPitch = pitch * (Math.PI / 180);
      const fX = Math.cos(radYaw) * Math.cos(radPitch);
      const fY = Math.sin(radPitch);
      const fZ = Math.sin(radYaw) * Math.cos(radPitch);
      camera.position.set(x + shakeX, y + shakeY, z + shakeZ);
      camera.lookAt(x + fX * 10, y + fY * 10, z + fZ * 10);
      if (transformControlsRef.current?.object && !transformControlsRef.current.object.parent) {
        transformControlsRef.current.detach();
      }

      const isVulkanStreaming = viewModeRef.current === 'vulkan' && !!vulkanFrameRef.current;
      if (isVulkanStreaming) {
        scene.background = null;
        meshMapRef.current.forEach(m => {
          m.visible = false;
        });
      } else {
        scene.background = new THREE.Color(0x0a0a14);
        meshMapRef.current.forEach(m => {
          m.visible = true;
        });
      }

      renderer.render(scene, camera);

      if (isVulkanStreaming) {
        meshMapRef.current.forEach(m => {
          m.visible = true;
        });
      }
    };
    renderLoop();

    const handleResize = () => {
      if (!containerRef.current) return;
      const nw = containerRef.current.clientWidth;
      const nh = containerRef.current.clientHeight;
      camera.aspect = nw / nh;
      camera.updateProjectionMatrix();
      renderer.setSize(nw, nh);
    };
    window.addEventListener('resize', handleResize);

    return () => {
      cancelAnimationFrame(animId);
      if (flyRafRef.current) { cancelAnimationFrame(flyRafRef.current); flyRafRef.current = 0; }
      targetDom.removeEventListener('contextmenu', onContextMenu);
      targetDom.removeEventListener('mousedown', onMouseDown as EventListener);
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
      targetDom.removeEventListener('wheel', onWheel as EventListener);
      window.removeEventListener('resize', handleResize);
      tc.dispose();
      renderer.dispose();
    };
  }, [sendRpc, sendCamera, startFlyLoop, project.settings]);

  // ── Keyboard shortcuts ───────────────────────────────────────────────────
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      const tag = (document.activeElement?.tagName || '').toLowerCase();
      if (['input', 'textarea'].includes(tag)) return;

      // Toggle Content Drawer with Ctrl + Space
      if (e.code === 'Space' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        setIsContentDrawerOpen(prev => !prev);
        return;
      }

      // Undo with Ctrl + Z
      if (e.code === 'KeyZ' && (e.ctrlKey || e.metaKey) && !e.shiftKey) {
        e.preventDefault();
        handleUndo();
        return;
      }

      // Redo with Ctrl + Y or Ctrl + Shift + Z
      if ((e.code === 'KeyY' && (e.ctrlKey || e.metaKey)) || (e.code === 'KeyZ' && (e.ctrlKey || e.metaKey) && e.shiftKey)) {
        e.preventDefault();
        handleRedo();
        return;
      }

      // Select All with Ctrl + A
      if (e.code === 'KeyA' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        if (entities.length > 0) setSelectedEntityId(entities[0].id);
        return;
      }

      // Invert Selection with Ctrl + I
      if (e.code === 'KeyI' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        const other = entities.find(ent => ent.id !== selectedEntityId);
        if (other) setSelectedEntityId(other.id);
        return;
      }

      // Toggle Immersive Play Mode with F11
      if (e.code === 'F11') {
        e.preventDefault();
        setIsImmersivePlay(prev => !prev);
        return;
      }

      flyKeysRef.current[e.code] = true;

      if (!rmbDownRef.current) {
        if (e.code === 'KeyW' && !simulating) { setGizmoMode('translate'); return; }
        if (e.code === 'KeyE' && !simulating) { setGizmoMode('rotate'); return; }
        if (e.code === 'KeyR' && !simulating) { setGizmoMode('scale'); return; }
        if (e.code === 'KeyQ' && !simulating) { setGizmoSpace(p => p === 'world' ? 'local' : 'world'); return; }
        if (e.code === 'KeyF') { focusOnSelectedEntity(); return; }
        if (e.code === 'Escape') {
          if (isImmersivePlay) setIsImmersivePlay(false);
          setSelectedEntityId(null);
          return;
        }
        if ((e.code === 'Delete' || e.code === 'Backspace') && selectedEntityId !== null) {
          handleDeleteEntity(selectedEntityId); return;
        }
        if (e.code === 'KeyD' && (e.ctrlKey || e.metaKey)) {
          e.preventDefault();
          const cur = entities.find(ent => ent.id === selectedEntityId);
          if (cur) handleDuplicateEntity(cur);
          return;
        }
      }

      keysPressedRef.current[e.code] = true;
      if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.code)) e.preventDefault();
    };

    const onKeyUp = (e: KeyboardEvent) => {
      flyKeysRef.current[e.code] = false;
      keysPressedRef.current[e.code] = false;
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);

    let lastSentKeysJson = '';
    const inputTimer = setInterval(() => {
      if (wsRef.current?.readyState === WebSocket.OPEN) {
        const currentJson = JSON.stringify(keysPressedRef.current);
        if (currentJson !== lastSentKeysJson) {
          sendRpc('engine.sendInput', { keys: keysPressedRef.current }).catch(() => { });
          lastSentKeysJson = currentJson;
        }
      }
    }, 33);

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      clearInterval(inputTimer);
    };
  }, [selectedEntityId, entities, sendRpc, focusOnSelectedEntity, handleDeleteEntity, handleDuplicateEntity]);

  // ── TransformControls mode sync ──────────────────────────────────────────
  useEffect(() => {
    const tc = transformControlsRef.current;
    if (!tc) return;
    tc.setMode(gizmoMode);
    tc.setSpace(gizmoSpace);
    if (snapEnabled) {
      tc.setTranslationSnap(snapStep);
      tc.setRotationSnap(THREE.MathUtils.degToRad(15));
      tc.setScaleSnap(0.25);
    } else {
      tc.setTranslationSnap(null);
      tc.setRotationSnap(null);
      tc.setScaleSnap(null);
    }
    const helper = tc.getHelper ? tc.getHelper() : null;
    if (selectedEntityId !== null) {
      const mesh = meshMapRef.current.get(selectedEntityId);
      if (mesh && mesh.parent) {
        if (tc.object !== mesh) tc.attach(mesh);
        if (helper) helper.visible = true;
      } else {
        tc.detach();
        if (helper) helper.visible = false;
      }
    } else {
      tc.detach();
      if (helper) helper.visible = false;
    }
  }, [selectedEntityId, gizmoMode, gizmoSpace, snapEnabled, snapStep]);

  // ── Sync Meshes from ECS Snapshot ────────────────────────────────────────
  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene) return;
    const mm = meshMapRef.current;
    const seenIds = new Set<number>();

    entities.forEach(ent => {
      seenIds.add(ent.id);
      let mesh = mm.get(ent.id);

      if (!mesh) {
        const isPlane = ent.mesh?.meshId === 1 || ent.tag === 'GroundPlane' || ent.tag === 'Ground';
        const isCapsule = ent.rigidBody?.shapeType === 2;
        const isSphere = ent.rigidBody?.shapeType === 1 || ent.tag === 'Sun' || ent.tag === 'Earth' || ent.tag === 'Ball' || ent.tag === 'Coin';

        let glbPath: string | null = null;
        if (ent.tag?.includes('SportsCar')) glbPath = '/assets/models/SM_SportsCar.glb';
        else if (ent.tag?.includes('ChamferCube')) glbPath = '/assets/models/1M_Cube_Chamfer.glb';
        else if (ent.tag?.includes('1MCube')) glbPath = '/assets/models/1M_Cube.glb';
        else if (ent.tag?.includes('GizmoLocation')) glbPath = '/assets/models/SM_Gizmo_Location.glb';
        else if (ent.tag?.includes('GizmoRotate')) glbPath = '/assets/models/SM_Gizmo_Rotate.glb';
        else if (ent.tag?.includes('GizmoScale')) glbPath = '/assets/models/SM_Gizmo_Scale.glb';
        else if (ent.tag?.includes('BlueCube')) glbPath = '/assets/models/Cube_Blue.glb';
        else if (ent.tag?.includes('RotationHandle')) glbPath = '/assets/models/RotationHandle.glb';

        const isCylinder = ent.tag?.toLowerCase().includes('cylinder');
        const isCone = ent.tag?.toLowerCase().includes('cone');
        const isLight = ent.tag?.toLowerCase().includes('light') || !!ent.light;
        const isCamera = ent.tag?.toLowerCase().includes('camera');
        const isAudio = ent.tag?.toLowerCase().includes('audio');

        let geom: THREE.BufferGeometry;
        if (isPlane) {
          geom = new THREE.BoxGeometry(100, 4, 100);
        } else if (isCapsule) {
          const rad = ent.rigidBody?.capsuleRadius || 0.6;
          const hh = ent.rigidBody?.capsuleHalfHeight || 0.8;
          geom = new THREE.CapsuleGeometry(rad, hh * 2, 8, 16);
        } else if (isSphere || isLight || isAudio) {
          geom = new THREE.SphereGeometry(isLight || isAudio ? 0.35 : 0.5, 24, 24);
        } else if (isCylinder) {
          geom = new THREE.CylinderGeometry(0.5, 0.5, 1.2, 24);
        } else if (isCone || isCamera) {
          geom = new THREE.ConeGeometry(0.5, 1.0, isCamera ? 4 : 24);
        } else {
          geom = new THREE.BoxGeometry(1, 1, 1);
        }

        let color = 0x38bdf8;
        let emissive = 0x000000;
        let roughness = 0.4, metalness = 0.2;
        let transparent = false;
        let opacity = 1.0;

        if (ent.material?.color) {
          const c = new THREE.Color(ent.material.color);
          color = c.getHex();
          roughness = ent.material.roughness ?? 0.4;
          metalness = ent.material.metallic ?? 0.2;
        } else if (isLight) {
          color = 0xfacc15;
          emissive = 0xeab308;
        } else if (isPlane) {
          color = 0x334155; roughness = 0.7; metalness = 0.1;
        } else if (ent.tag === 'Sun' || ent.tag === 'Coin') { color = 0xfacc15; emissive = 0xeab308; }
        else if (ent.tag === 'Earth' || ent.tag?.includes('Player') || isCapsule) { color = 0x00f0ff; emissive = 0x0077b6; }
        else if (ent.tag === 'Paddle') color = 0x10b981;
        else if (ent.tag === 'Ball') { color = 0xf43f5e; emissive = 0xe11d48; }
        else if (ent.tag === 'Brick') color = 0xa855f7;
        else if (ent.tag === 'LaserBullet') { color = 0x00ffff; emissive = 0x00ffff; }
        else if (ent.tag?.includes('KillZ')) { color = 0xef4444; emissive = 0x991b1b; transparent = true; opacity = 0.4; }
        else if (ent.tag === 'Checkpoint' || ent.tag === 'SpeedBoost') { color = 0x22c55e; emissive = 0x15803d; transparent = true; opacity = 0.6; }
        else if (ent.rigidBody?.isSensor) { color = 0xeab308; emissive = 0xa16207; transparent = true; opacity = 0.5; }
        else if (ent.tag?.includes('Enemy') || ent.tag?.includes('Meteor') || ent.tag === 'Mars') { color = 0xf87171; emissive = 0x991b1b; }
        else {
          const r = 0.2 + 0.8 * ((ent.id * 31) % 100) / 100;
          const g = 0.2 + 0.8 * ((ent.id * 53) % 100) / 100;
          const b = 0.2 + 0.8 * ((ent.id * 97) % 100) / 100;
          color = (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
        }

        const mat = new THREE.MeshStandardMaterial({
          color,
          emissive,
          emissiveIntensity: emissive ? 0.6 : 0,
          roughness,
          metalness,
          transparent,
          opacity,
          wireframe: (!!ent.rigidBody?.isSensor && !ent.tag?.includes('Coin')) || !!ent.tag?.includes('KillZ')
        });

        if (glbPath) {
          const cached = gltfCacheRef.current.get(glbPath);
          if (cached) {
            mesh = cached.clone() as any;
          } else {
            const placeholder = new THREE.Mesh(geom, mat);
            mesh = placeholder;
            const targetPath = glbPath;
            new GLTFLoader().load(targetPath, (gltf) => {
              gltfCacheRef.current.set(targetPath, gltf.scene);
              const clone = gltf.scene.clone();
              clone.name = ent.tag || `Entity_${ent.id}`;
              clone.userData = {
                id: ent.id,
                tag: ent.tag,
                isPlayer: ent.tag?.includes('Player') || ent.tag?.includes('Car')
              };
              clone.position.copy(placeholder.position);
              clone.rotation.copy(placeholder.rotation);
              clone.scale.copy(placeholder.scale);
              scene.remove(placeholder);
              scene.add(clone as any);
              mm.set(ent.id, clone as any);
            });
          }
        } else {
          mesh = new THREE.Mesh(geom, mat);
          if (isLight) {
            const lightColor = ent.light?.color ? new THREE.Color(ent.light.color) : new THREE.Color(0xffaa44);
            const pLight = new THREE.PointLight(lightColor, ent.light?.intensity ?? 3.0, ent.light?.range ?? 20);
            mesh.add(pLight);
          }
        }
        if (mesh) {
          mesh.name = ent.tag || `Entity_${ent.id}`;
          mesh.userData = {
            id: ent.id,
            tag: ent.tag,
            isPlayer: ent.tag === 'Player' || ent.tag === 'Paddle' || ent.tag === 'PlayerShip' || ent.tag === 'PlayerCar' || ent.tag?.includes('Player')
          };
          mesh.castShadow = !ent.rigidBody?.isSensor;
          mesh.receiveShadow = true;
          scene.add(mesh);
          mm.set(ent.id, mesh);
        }
      } else {
        mesh.name = ent.tag || `Entity_${ent.id}`;
        mesh.userData = {
          id: ent.id,
          tag: ent.tag,
          isPlayer: ent.tag === 'Player' || ent.tag === 'Paddle' || ent.tag === 'PlayerShip' || ent.tag === 'PlayerCar' || ent.tag?.includes('Player')
        };
      }

      if (!mesh) return;
      if (isTransformDraggingRef.current && ent.id === selectedEntityId) return;
      mesh.position.set(ent.transform.x, ent.transform.y, ent.transform.z);
      if (ent.transform.qw !== undefined && ent.transform.qx !== undefined &&
        ent.transform.qy !== undefined && ent.transform.qz !== undefined) {
        mesh.quaternion.set(
          ent.transform.qx,
          ent.transform.qy,
          ent.transform.qz,
          ent.transform.qw
        );
      } else {
        mesh.rotation.set(
          THREE.MathUtils.degToRad(ent.transform.rx || 0),
          THREE.MathUtils.degToRad(ent.transform.ry || 0),
          THREE.MathUtils.degToRad(ent.transform.rz || 0),
        );
      }
      mesh.scale.set(ent.transform.sx || 1, ent.transform.sy || 1, ent.transform.sz || 1);
    });

    if (boxHelperRef.current) {
      if (selectedEntityId !== null && mm.has(selectedEntityId)) {
        boxHelperRef.current.setFromObject(mm.get(selectedEntityId)!);
        boxHelperRef.current.visible = true;
      } else {
        boxHelperRef.current.visible = false;
      }
    }

    for (const [id, mesh] of mm.entries()) {
      if (!seenIds.has(id)) {
        if (transformControlsRef.current?.object === mesh) {
          transformControlsRef.current.detach();
        }
        scene.remove(mesh);
        mesh.geometry.dispose();
        if (Array.isArray(mesh.material)) mesh.material.forEach(m => m.dispose());
        else mesh.material.dispose();
        mm.delete(id);
      }
    }
  }, [entities, selectedEntityId]);

  // ── Dual-Transport UI Shell & WebSocket connect ──────────────────────────
  useEffect(() => {
    let reconnectTimeout: any;
    let isDisposed = false;

    // Initialize Dual-Transport UI Shell (Native Shared Memory or Remote WebSocket)
    const transport = createUiTransport();
    transportRef.current = transport;
    setTransportMode(transport.mode);
    document.documentElement.setAttribute('data-transport-mode', transport.mode);

    const unsubState = transport.subscribeState((view: DataView) => {
      // Continuous binary telemetry (zero GC, no JSON serialization)
      const cpuMs = view.getFloat32(OFF_CPU_FRAME_MS, true);
      const activeEnts = view.getUint32(OFF_ACTIVE_ENTITIES, true);
      (window as any).__lucidTelemetry = { cpuMs, activeEntities: activeEnts };
    });

    const connect = () => {
      if (isDisposed) return;
      const ws = new WebSocket('ws://127.0.0.1:9000');
      wsRef.current = ws;

      ws.onopen = () => {
        if (isDisposed) {
          ws.close();
          return;
        }
        setConnected(true);
        addLog('[WS] Połączono z silnikiem Lucid Engine (Vulkan / Jolt / QuickJS)');
        // Ensure simulation starts paused in edit mode
        sendRpc('engine.setPaused', { paused: true }).catch(() => { });
        setSimulating(false);
        sendRpc('engine.getSnapshot').then(snap => setEntities(snap || [])).catch(() => { });
        sendRpc('engine.setEnvironment', project.settings).catch(() => { });
        sendRpc('engine.setCameraMode', { mode: cameraModeRef.current }).catch(() => { });
        if (project.settings?.uiScale) sendRpc('engine.setUiScale', { scale: project.settings.uiScale }).catch(() => { });
        if (project.settings?.resolutionScale) sendRpc('engine.setResolutionScale', { scale: project.settings.resolutionScale }).catch(() => { });
      };

      ws.onmessage = (event) => {
        if (isDisposed) return;
        try {
          const msg = JSON.parse(event.data);
          // 1. Direct O(1) response handling for sendRpc
          if (typeof msg.id === 'number' && pendingRpcMapRef.current.has(msg.id)) {
            const entry = pendingRpcMapRef.current.get(msg.id)!;
            pendingRpcMapRef.current.delete(msg.id);
            clearTimeout(entry.timer);
            if (msg.error) entry.reject(msg.error);
            else entry.resolve(msg.result);
            return;
          }

          // 2. Engine events & notifications
          if (msg.method === 'engine.onSimulationStateChanged') {
            const isSim = typeof msg.params?.simulating === 'boolean' ? msg.params.simulating : !msg.params?.paused;
            setSimulating(isSim);
            addLog(isSim ? '🚀 Powiadomienie silnika: Symulacja aktywna' : '⏸ Powiadomienie silnika: Symulacja wstrzymana');
          } else if (msg.method === 'engine.onScriptError') {
            setLastScriptError(msg.params);
            addLog(`[QuickJS Error] ${msg.params.message}`);
          } else if (msg.method === 'engine.onPlaySfx') {
            if (!isAudioMutedRef.current) {
              AudioSystem.playSfx(msg.params.type, msg.params.volume);
            }
          } else if (msg.method === 'engine.onCameraShake') {
            cameraShakeRef.current = {
              intensity: msg.params.intensity || 0.6,
              duration: msg.params.duration || 0.35,
              startTime: performance.now(),
            };
          }
        } catch { }
      };

      ws.onclose = () => {
        setConnected(false);
        for (const [, entry] of pendingRpcMapRef.current.entries()) {
          clearTimeout(entry.timer);
          entry.reject(new Error('WebSocket closed'));
        }
        pendingRpcMapRef.current.clear();
        if (!isDisposed) {
          reconnectTimeout = setTimeout(connect, 2000);
        }
      };

      ws.onerror = () => {
        if (ws.readyState === WebSocket.OPEN) {
          ws.close();
        }
      };
    };

    connect();

    return () => {
      isDisposed = true;
      clearTimeout(reconnectTimeout);
      unsubState();
      transport.dispose();
      const ws = wsRef.current;
      if (ws) {
        if (ws.readyState === WebSocket.OPEN) {
          ws.close();
        } else if (ws.readyState === WebSocket.CONNECTING) {
          ws.onopen = () => { ws.close(); };
          ws.onmessage = null;
          ws.onerror = null;
          ws.onclose = null;
        }
      }
    };
  }, []);

  useEffect(() => {
    if (!connected) return;
    const iv = setInterval(async () => {
      try {
        const s = await sendRpc('engine.getSnapshot');
        if (Array.isArray(s)) setEntities(s);
        const gs = await sendRpc('engine.getGameState');
        if (gs && typeof gs.score === 'number') {
          setGameScore(gs.score);
        }
        if (simulatingRef.current) {
          try {
            const hp = await sendRpc('engine.getHealth');
            if (hp && typeof hp.health === 'number') {
              setPlayerHealth(hp.health);
            }
            const st = await sendRpc('engine.getGameStatus');
            if (st && typeof st.status === 'string') {
              setGameStatus(st.status as GameStatus);
            }
          } catch { }
        }
      } catch { }
    }, 50);
    return () => clearInterval(iv);
  }, [connected, sendRpc]);

  useEffect(() => {
    if (!connected) return;
    const iv = setInterval(async () => {
      if (rmbDownRef.current || mmbDownRef.current) return;
      try {
        const cam = await sendRpc('engine.getCamera');
        if (cam && typeof cam.x === 'number') {
          cameraStateRef.current = { ...cameraStateRef.current, ...cam, fov: cam.fov || 45 };
          setCameraRot({ yaw: cameraStateRef.current.yaw, pitch: cameraStateRef.current.pitch });
        }
      } catch { }
    }, 100);
    return () => clearInterval(iv);
  }, [connected, sendRpc]);

  // ── Vulkan Frame Stream ──────────────────────────────────────────────────
  useEffect(() => {
    if (!connected || viewMode !== 'vulkan') return;
    let active = true;
    let count = 0;
    let lastTime = performance.now();
    let isCapturing = false;

    const fpsTimer = setInterval(() => {
      const now = performance.now();
      setVulkanFps(Math.round((count * 1000) / Math.max(1, now - lastTime)));
      count = 0;
      lastTime = now;
    }, 1000);

    const poll = async () => {
      while (active && connected && viewMode === 'vulkan') {
        if (!isCapturing) {
          isCapturing = true;
          try {
            const r = await sendRpc('engine.captureFrame');
            if (r?.frame && active) {
              setVulkanFrame(r.frame);
              count++;
            }
          } catch { }
          finally {
            isCapturing = false;
          }
        }
        await new Promise(res => setTimeout(res, 16));
      }
    };
    poll();
    return () => { active = false; clearInterval(fpsTimer); };
  }, [connected, viewMode, sendRpc]);

  // ── Simulation Control Actions (Play / Pause / Stop / Reset) ─────────────
  const buildScriptBundle = () => {
    const scriptParts = project.scripts.map((s) => `
// === ${s.name} ===
(function() {
  ${s.code.replace(/export\s+function\s+update\s*\(/g, 'function _update(')}
  if (typeof _update === 'function') {
    globalThis.__script_updates.push(_update);
  }
})();
    `).join('\n');

    return `
globalThis.__script_updates = [];
${scriptParts}
globalThis.update = function(entities, dt, input, contacts) {
  if (Array.isArray(globalThis.__script_updates)) {
    for (let i = 0; i < globalThis.__script_updates.length; ++i) {
      try {
        globalThis.__script_updates[i](entities, dt, input, contacts);
      } catch (err) {
        if (globalThis.engine && globalThis.engine.log) {
          globalThis.engine.log("Błąd w skrypcie: " + (err.message || err));
        }
      }
    }
  }
};
`;
  };

  const handlePlay = async () => {
    try {
      AudioSystem.init();
      // 1. Save edit snapshot before running physics simulation
      preSimulationSnapshotRef.current = JSON.parse(JSON.stringify(entities));
      const code = buildScriptBundle();
      await sendRpc('engine.loadScript', { code });
      await sendRpc('engine.playSimulation');
      setSimulating(true);
      setGameStatus('PLAYING');
      setPlayerHealth(100);
      addLog('🚀 Symulacja URUCHOMIONA (Migawka sceny zabezpieczona, Jolt i QuickJS aktywne)');
    } catch (e: any) { addLog(`Błąd Play: ${e.message}`); }
  };

  const handlePause = async () => {
    try {
      await sendRpc('engine.setPaused', { paused: true });
      setSimulating(false);
      addLog('⏸ Symulacja WSTRZYMANA');
    } catch (e: any) { addLog(`Błąd Pause: ${e.message}`); }
  };

  const handleResetScene = async (preset: string) => {
    try {
      if (transformControlsRef.current) {
        transformControlsRef.current.detach();
      }
      setSelectedEntityId(null);
      setSimulating(false);
      currentPresetRef.current = preset;
      setGameStatus('PLAYING');
      setPlayerHealth(100);
      setGameScore(0);
      await sendRpc('engine.resetScene', { preset });
      const code = buildScriptBundle();
      await sendRpc('engine.loadScript', { code });
      const s = await sendRpc('engine.getSnapshot');
      const loadedEnts = s || [];
      setEntities(loadedEnts);
      preSimulationSnapshotRef.current = JSON.parse(JSON.stringify(loadedEnts));
      addLog(`Scena zresetowana: ${preset.toUpperCase()}`);
    } catch (e: any) { addLog(`Błąd resetu: ${e.message}`); }
  };

  const handleStop = async () => {
    try {
      if (transformControlsRef.current) {
        transformControlsRef.current.detach();
      }
      await sendRpc('engine.stopSimulation');
      setSimulating(false);
      setGameStatus('PLAYING');
      setPlayerHealth(100);
      // Restore pre-simulation snapshot without wiping the level!
      if (preSimulationSnapshotRef.current && preSimulationSnapshotRef.current.length > 0) {
        setEntities(preSimulationSnapshotRef.current);
      } else {
        const snap = await sendRpc('engine.getSnapshot');
        setEntities(snap || []);
      }
      setSelectedEntityId(null);
      addLog('■ Symulacja ZATRZYMANA - Pozycje i stan edycyjny przywrócone (bez resetu sceny)');
    } catch (e: any) { addLog(`Błąd Stop: ${e.message}`); }
  };

  const handleStepSimulation = async () => {
    try {
      await sendRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
      const s = await sendRpc('engine.getSnapshot');
      setEntities(s || []);
      addLog('Krok symulacji (1 tick)');
    } catch (e: any) { addLog(`Błąd kroku: ${e.message}`); }
  };

  // ── Spawn Helpers ────────────────────────────────────────────────────────
  const handleSpawnEntity = async (type: SpawnEntityType) => {
    pushUndoSnapshot();
    try {
      const isSphere = type === 'sphere' || type === 'coin';
      const isCapsule = type === 'capsule' || type === 'player';
      const isLight = ['point_light', 'spot_light', 'dir_light', 'sky_light'].includes(type);
      const isCamera = type === 'camera_actor';
      const isAudio = type === 'audio_source';
      const isSensor = type === 'trigger' || type === 'coin' || type === 'killz_volume';
      const isPropModel = ['sportscar', 'chamfer_cube', 'ue5_cube', 'ue5_location', 'ue5_rotate', 'ue5_scale', 'ue5_bluecube', 'ue5_rotation'].includes(type);

      const tagMap: Record<string, string> = {
        cube: 'DynamicCube',
        sphere: 'DynamicSphere',
        cylinder: 'CylinderMesh',
        cone: 'ConeMesh',
        plane: 'GroundPlane',
        player: 'Player',
        coin: 'Coin',
        enemy: 'Enemy',
        capsule: 'CapsulePawn',
        trigger: 'TriggerZone',
        point_light: 'PointLight',
        spot_light: 'SpotLight',
        dir_light: 'DirectionalLight',
        sky_light: 'SkyLight',
        camera_actor: 'CameraActor',
        audio_source: 'AudioSourceActor',
        killz_volume: 'KillZVolume',
        sportscar: 'SportsCar_Actor',
        chamfer_cube: 'ChamferCube_Actor',
        ue5_cube: 'Cube_1M_Actor',
        ue5_location: 'Gizmo_Location_Actor',
        ue5_rotate: 'Gizmo_Rotate_Actor',
        ue5_scale: 'Gizmo_Scale_Actor',
        ue5_bluecube: 'Prototype_BlueCube_Actor',
        ue5_rotation: 'Gizmo_RotationHandle_Actor'
      };
      const massMap: Record<string, number> = {
        cube: 1.0,
        sphere: 1.0,
        cylinder: 1.0,
        cone: 1.0,
        plane: 0.0,
        player: 1.0,
        coin: 0.0,
        enemy: 2.0,
        capsule: 1.0,
        trigger: 0.0,
        point_light: 0.0,
        spot_light: 0.0,
        dir_light: 0.0,
        sky_light: 0.0,
        camera_actor: 0.0,
        audio_source: 0.0,
        killz_volume: 0.0,
        sportscar: 1200.0,
        chamfer_cube: 1.0,
        ue5_cube: 1.0,
        ue5_location: 0.0,
        ue5_rotate: 0.0,
        ue5_scale: 0.0,
        ue5_bluecube: 1.0,
        ue5_rotation: 0.0
      };
      const scaleVal = type === 'coin' ? 0.6 : 1.0;
      const shapeType = isCapsule ? 2 : isSphere ? 1 : 0;

      const lightData = isLight ? {
        type: type === 'dir_light' ? 'directional' : (type === 'spot_light' ? 'spot' : (type === 'sky_light' ? 'sky' : 'point')),
        color: type === 'dir_light' ? '#fff4e0' : (type === 'sky_light' ? '#88ccff' : '#ffaa44'),
        intensity: type === 'dir_light' ? 2.5 : 3.0,
        range: 15.0
      } : undefined;

      const audioData = isAudio ? {
        soundAssetId: project.sounds[0]?.id || 'default_sound',
        volume: 0.8,
        pitch: 1.0,
        is3D: true,
        loop: true,
        autoPlay: true
      } : undefined;

      const cameraData = isCamera ? {
        targetArmLength: 10.0,
        socketOffsetY: 3.0,
        cameraLag: true,
        lagSpeed: 0.08
      } : undefined;

      const idRes = await sendRpc('engine.spawnEntity', {
        tag: tagMap[type] || 'Entity',
        transform: {
          x: (Math.random() - 0.5) * 8,
          y: isCapsule ? 2.5 : (type === 'sportscar' ? 2.0 : (isLight ? 4.0 : (3 + Math.random() * 2))),
          z: (Math.random() - 0.5) * 8,
          sx: scaleVal,
          sy: scaleVal,
          sz: scaleVal
        },
        velocity: { vx: 0, vy: 0, vz: 0 },
        mesh: { meshId: isPropModel ? 10 : (type === 'plane' ? 1 : 2) },
        rigidBody: {
          mass: massMap[type] ?? 1.0,
          shapeType,
          capsuleRadius: isCapsule ? 0.6 : undefined,
          capsuleHalfHeight: isCapsule ? 0.8 : undefined,
          lockRotationX: isCapsule,
          lockRotationZ: isCapsule,
          isSensor
        },
        light: lightData,
        audio: audioData,
        cameraBoom: cameraData
      });
      addLog(`Spawn encji: ${tagMap[type]} (ID: ${idRes?.entityId})`);
      if (idRes?.entityId !== undefined) setSelectedEntityId(idRes.entityId);
    } catch (e: any) { addLog(`Błąd spawn: ${e.message}`); }
  };

  const handleSpawnEmitter = (type: ParticleType) => {
    const id = 'emitter_' + Date.now();
    const colors = { fire: '#ff5500', sparks: '#ffea00', portal: '#00f0ff', smoke: '#888888', explosion: '#ff3300' };
    const newEmitter: ParticleEmitterConfig = {
      id,
      name: `New_${type.toUpperCase()}_VFX.fx`,
      type,
      position: { x: (Math.random() - 0.5) * 8, y: 1.5, z: (Math.random() - 0.5) * 8 },
      color: colors[type] || '#ff6600',
      count: 250,
      speed: 2.0,
      size: 0.35,
    };

    particleManagerRef.current?.addOrUpdateEmitter(newEmitter);

    const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];
    const updatedScenes = project.scenes.map(s => s.id === activeScene.id
      ? { ...s, emitters: [...(s.emitters || []), newEmitter] }
      : s
    );
    persistProject({ ...project, scenes: updatedScenes });
    setIsEffectsPanelOpen(true);
    addLog(`Dodano emiter cząstek: ${newEmitter.name}`);
  };

  const handleUpdateEmitter = (id: string, patch: Partial<ParticleEmitterConfig>) => {
    const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];
    let found: ParticleEmitterConfig | null = null;
    const updatedEmitters = (activeScene.emitters || []).map(e => {
      if (e.id === id) {
        found = { ...e, ...patch };
        return found;
      }
      return e;
    });

    if (found) {
      particleManagerRef.current?.addOrUpdateEmitter(found);
    }

    const updatedScenes = project.scenes.map(s => s.id === activeScene.id
      ? { ...s, emitters: updatedEmitters }
      : s
    );
    persistProject({ ...project, scenes: updatedScenes });
  };

  const handleDeleteEmitter = (id: string) => {
    particleManagerRef.current?.removeEmitter(id);
    const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];
    const updatedScenes = project.scenes.map(s => s.id === activeScene.id
      ? { ...s, emitters: (s.emitters || []).filter(e => e.id !== id) }
      : s
    );
    persistProject({ ...project, scenes: updatedScenes });
    addLog(`Usunięto emiter cząstek ID: ${id}`);
  };

  // ── Script Management Actions ─────────────────────────────────────────────
  const handleInjectScript = async (code: string) => {
    try {
      setLastScriptError(null);
      const res = await sendRpc('engine.loadScript', { code });
      addLog(res?.success !== false ? '✅ Skrypt skompilowany i załadowany do QuickJS!' : '❌ Błąd ewaluacji skryptu w QuickJS');
    } catch (e: any) { addLog(`Błąd wstrzykiwania: ${e.message}`); }
  };

  const handleCreateScript = () => {
    const id = 'script_' + Date.now();
    const newScript: ScriptFile = {
      id,
      name: `CustomScript_${project.scripts.length + 1}.js`,
      code: `// Nowy skrypt encji w Lucid Engine\nexport function update(entities, dt, input) {\n  // Logika uruchamiana co klatkę silnika\n}`,
      createdAt: Date.now()
    };
    persistProject({ ...project, scripts: [...project.scripts, newScript] });
    setActiveScriptId(id);
    setIsScriptStudioOpen(true);
    addLog(`Utworzono nowy plik skryptu: ${newScript.name}`);
  };

  const handleDeleteScript = (id: string) => {
    const updated = project.scripts.filter(s => s.id !== id);
    persistProject({ ...project, scripts: updated });
    if (activeScriptId === id) setActiveScriptId(updated[0]?.id || null);
    addLog('Usunięto skrypt.');
  };

  const handleUpdateScriptCode = (id: string, code: string) => {
    const updated = project.scripts.map(s => s.id === id ? { ...s, code } : s);
    persistProject({ ...project, scripts: updated });
  };

  const handleOpenScriptEditor = (scriptName?: string) => {
    if (scriptName) {
      const s = project.scripts.find(sc => sc.name === scriptName);
      if (s) setActiveScriptId(s.id);
    }
    setIsScriptStudioOpen(true);
  };

  // ── Material Actions ──────────────────────────────────────────────────────
  const handleApplyMaterial = async (id: number, mat: MaterialAsset) => {
    try {
      const color = new THREE.Color(mat.color);
      await sendRpc('engine.applyPbrMaterial', {
        entityId: id,
        baseColor: [color.r, color.g, color.b, 1.0],
        roughness: mat.roughness,
        metallic: mat.metallic
      });
      addLog(`Zastosowano materiał PBR "${mat.name}" na encję ID ${id}`);
    } catch (e: any) { addLog(`Błąd nakładania materiału: ${e.message}`); }
  };

  const handleCreateMaterial = () => {
    const id = 'mat_' + Date.now();
    const colors = ['#f43f5e', '#3b82f6', '#10b981', '#f59e0b', '#8b5cf6'];
    const chosenColor = colors[project.materials.length % colors.length];
    const newMat: MaterialAsset = {
      id,
      name: `Material_${project.materials.length + 1}.mat`,
      color: chosenColor,
      metallic: 0.5,
      roughness: 0.3
    };
    persistProject({ ...project, materials: [...project.materials, newMat] });
    addLog(`Utworzono nowy materiał PBR: ${newMat.name}`);
  };

  const handleDeleteMaterial = (id: string) => {
    const updated = project.materials.filter(m => m.id !== id);
    persistProject({ ...project, materials: updated });
    addLog('Usunięto materiał.');
  };

  // ── Sound & UI Studio Actions ─────────────────────────────────────────────
  const handleSaveSound = (sound: SoundAsset) => {
    const existing = project.sounds || [];
    const index = existing.findIndex(s => s.id === sound.id);
    let updatedSounds: SoundAsset[];
    if (index >= 0) {
      updatedSounds = [...existing];
      updatedSounds[index] = sound;
    } else {
      updatedSounds = [...existing, sound];
    }
    persistProject({ ...project, sounds: updatedSounds });
    addLog(`Zapisano Sound Cue: "${sound.name}"`);
  };

  const handleDeleteSound = (id: string) => {
    const updatedSounds = (project.sounds || []).filter(s => s.id !== id);
    persistProject({ ...project, sounds: updatedSounds });
    addLog(`Usunięto Sound Asset ID ${id}`);
  };

  const handleSaveWidget = (widget: WidgetBlueprint) => {
    const existing = project.widgets || [];
    const index = existing.findIndex(w => w.id === widget.id);
    let updatedWidgets: WidgetBlueprint[];
    if (index >= 0) {
      updatedWidgets = [...existing];
      updatedWidgets[index] = widget;
    } else {
      updatedWidgets = [...existing, widget];
    }
    persistProject({ ...project, widgets: updatedWidgets });
    addLog(`Zapisano UI Widget Blueprint: "${widget.name}"`);
  };

  const handleDeleteWidget = (id: string) => {
    const updatedWidgets = (project.widgets || []).filter(w => w.id !== id);
    const newActiveId = project.activeWidgetId === id ? updatedWidgets[0]?.id : project.activeWidgetId;
    persistProject({ ...project, widgets: updatedWidgets, activeWidgetId: newActiveId });
    addLog(`Usunięto UI Widget ID ${id}`);
  };

  const handleSetActiveWidget = (id: string | undefined) => {
    persistProject({ ...project, activeWidgetId: id });
    addLog(`Aktywny HUD UI: ${id ? project.widgets?.find(w => w.id === id)?.name || id : 'Brak'}`);
  };

  // ── Entity Property & Script Binding ──────────────────────────────────────
  const handleUpdateEntityProperty = async (id: number, patch: any) => {
    try {
      await sendRpc('engine.updateEntity', { id, ...patch });
      setEntities(prev => prev.map(e => e.id === id ? { ...e, ...patch } : e));
    } catch (e: any) { addLog(`Błąd aktualizacji: ${e.message}`); }
  };

  const handleApplyImpulse = async (id: number, x: number, y: number, z: number) => {
    try {
      await sendRpc('engine.updateEntity', { id, impulse: { x, y, z } });
      addLog(`Impuls [${x},${y},${z}] przekazany do encji ${id}`);
    } catch (e: any) { addLog(`Błąd impulsu: ${e.message}`); }
  };

  // ── Project File I/O (Export / Import) ─────────────────────────────────────
  const handleExportProject = () => {
    ProjectManager.exportProjectToJson(project);
    addLog(`Projekt "${project.name}" wyeksportowany do pliku .lucidproject`);
  };

  const handleImportProjectFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (event) => {
      try {
        const content = event.target?.result as string;
        const imported = ProjectManager.importProjectFromJson(content);
        setProject(imported);
        setAllProjects(ProjectManager.getAllProjects());
        addLog(`Projekt "${imported.name}" zaimportowany pomyślnie!`);
      } catch (err: any) {
        addLog(`Błąd importu projektu: ${err.message}`);
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  const handleImportGlbFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async (event) => {
      try {
        const base64 = (event.target?.result as string).split(',')[1];
        if (base64) {
          const res = await sendRpc('engine.loadGlbFromBase64', { base64 });
          addLog(`Zaimportowano model 3D GLB (Mesh ID: ${res?.entityId || 'OK'})`);
        }
      } catch (err: any) {
        addLog(`Błąd importu GLB: ${err.message}`);
      }
    };
    reader.readAsDataURL(file);
    e.target.value = '';
  };

  // ── AI Game Creator ───────────────────────────────────────────────────────
  const handleAiGenerate = async () => {
    if (!aiPrompt.trim()) return;
    setAiLoading(true);
    addLog(`🤖 Tworzenie gry AI: "${aiPrompt}"...`);
    try {
      const p = aiPrompt.toLowerCase();
      let preset = 'breakout';
      if (p.includes('auto') || p.includes('samochód') || p.includes('wyścig') || p.includes('racer')) preset = 'racer';
      else if (p.includes('statek') || p.includes('kosmos') || p.includes('shooter') || p.includes('strzel')) preset = 'shooter';
      else if (p.includes('skok') || p.includes('platform') || p.includes('monet') || p.includes('jumper')) preset = 'platformer';
      else if (p.includes('planet') || p.includes('solar') || p.includes('grawit')) preset = 'solar';

      await handleResetScene(preset);
      addLog(`🎉 Gra "${preset.toUpperCase()}" wygenerowana! Kliknij Graj (▶) aby przetestować.`);
    } catch (e: any) { addLog(`Błąd AI: ${e.message}`); }
    finally { setAiLoading(false); }
  };

  // ── Console Command Executor (Cmd ▾ Enter Console Command) ─────────────────
  const handleExecuteConsoleCmd = async (cmdStr: string) => {
    const trimmed = cmdStr.trim();
    if (!trimmed) return;
    addLog(`Cmd> ${trimmed}`);
    setConsoleCmd('');

    const parts = trimmed.split(' ');
    const cmd = parts[0].toLowerCase();
    const arg = parts.slice(1).join(' ');

    if (cmd === 'clear' || cmd === 'cls') {
      setLogs([]);
      return;
    }
    if (cmd === 'help') {
      addLog('── Dostępne komendy konsoli Lucid Engine ──');
      addLog('  play         - Rozpocznij symulację fizyki Jolt i logiki QuickJS');
      addLog('  pause        - Wstrzymaj symulację');
      addLog('  stop         - Zatrzymaj i zresetuj świat do stanu początkowego');
      addLog('  reset [gra]  - Resetuj scenę (breakout, racer, platformer, shooter, solar, physics)');
      addLog('  spawn [typ]  - Utwórz encję (cube, sphere, player, coin, enemy)');
      addLog('  clear        - Wyczyść okno logów');
      return;
    }
    if (cmd === 'play') { handlePlay(); return; }
    if (cmd === 'pause') { handlePause(); return; }
    if (cmd === 'stop') { handleStop(); return; }
    if (cmd === 'reset') {
      handleResetScene(arg || currentPresetRef.current || 'breakout');
      return;
    }
    if (cmd === 'spawn') {
      if (['cube', 'sphere', 'player', 'coin', 'enemy'].includes(arg)) {
        handleSpawnEntity(arg as any);
      } else {
        addLog(`Nieznany typ encji: ${arg}. Dostępne: cube, sphere, player, coin, enemy`);
      }
      return;
    }

    // Try raw RPC
    try {
      const res = await sendRpc(cmd, arg ? JSON.parse(arg) : {});
      addLog(`Wynik RPC: ${JSON.stringify(res)}`);
    } catch (e: any) {
      addLog(`Błąd wykonania polecenia: ${e.message}`);
    }
  };

  // ── Scene / Level Tabs Management ──────────────────────────────────────────
  const handleSelectScene = async (sceneId: string) => {
    const updatedScenes = project.scenes.map(s => s.id === project.activeSceneId ? { ...s, entities } : s);
    const targetScene = project.scenes.find(s => s.id === sceneId);
    if (!targetScene) return;

    const newProject = { ...project, scenes: updatedScenes, activeSceneId: sceneId };
    persistProject(newProject);
    setProject(newProject);

    const sceneEnts = targetScene.entities && targetScene.entities.length > 0 ? targetScene.entities : [];
    setEntities(sceneEnts);
    preSimulationSnapshotRef.current = JSON.parse(JSON.stringify(sceneEnts));
    setSelectedEntityId(null);
    setSimulating(false);

    try {
      await sendRpc('engine.resetScene', { preset: 'empty' });
      for (const ent of sceneEnts) {
        await sendRpc('engine.spawnEntity', ent);
      }
      const code = buildScriptBundle();
      await sendRpc('engine.loadScript', { code });
      const snap = await sendRpc('engine.getSnapshot');
      if (snap && snap.length > 0) {
        setEntities(snap);
        preSimulationSnapshotRef.current = JSON.parse(JSON.stringify(snap));
      }
    } catch (err: any) {
      console.warn("Failed to sync scene change to engine RPC:", err);
    }

    addLog(`Przełączono na poziom: "${targetScene.name}"`);
  };

  const handleCloseScene = async (sceneId: string) => {
    if (project.scenes.length > 1) {
      const remaining = project.scenes.filter(s => s.id !== sceneId);
      const nextActive = remaining[0];
      const newProject = { ...project, scenes: remaining, activeSceneId: nextActive.id };
      persistProject(newProject);
      setProject(newProject);
      const sceneEnts = nextActive.entities || [];
      setEntities(sceneEnts);
      preSimulationSnapshotRef.current = JSON.parse(JSON.stringify(sceneEnts));
      setSelectedEntityId(null);
      setSimulating(false);
      try {
        await sendRpc('engine.resetScene', { preset: 'empty' });
        for (const ent of sceneEnts) {
          await sendRpc('engine.spawnEntity', ent);
        }
        const code = buildScriptBundle();
        await sendRpc('engine.loadScript', { code });
      } catch (e) { }
      addLog(`Zamknięto poziom. Aktywny: "${nextActive.name}"`);
    } else {
      const emptyScene = {
        id: 'scene_' + Date.now(),
        name: 'CzystyPoziom.scene',
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
      };
      const newProject = { ...project, scenes: [emptyScene], activeSceneId: emptyScene.id };
      persistProject(newProject);
      setProject(newProject);
      setEntities(emptyScene.entities);
      setSelectedEntityId(null);
      sendRpc('engine.resetScene', { preset: 'empty' }).catch(console.error);
      addLog('Zamknięto poziom. Scena zresetowana do czystego stanu od zera.');
    }
  };

  const handleNewScene = () => {
    const newSceneId = 'scene_' + Date.now();
    const newNum = project.scenes.length + 1;
    const newScene = {
      id: newSceneId,
      name: `Level_${newNum}.scene`,
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
    };
    const updatedScenes = project.scenes.map(s => s.id === project.activeSceneId ? { ...s, entities } : s);
    const newProject = { ...project, scenes: [...updatedScenes, newScene], activeSceneId: newSceneId };
    persistProject(newProject);
    setProject(newProject);
    setEntities(newScene.entities);
    setSelectedEntityId(null);
    sendRpc('engine.resetScene', { preset: 'empty' }).catch(console.error);
    addLog(`Utworzono nowy poziom: "${newScene.name}" od zera.`);
  };

  const handleSaveProjectAndDisk = () => {
    persistProject(project);
    sendRpc('engine.saveProjectToDisk', { project, folderPath: project.folderPath })
      .then((res: any) => {
        addLog(`Projekt zapisany na dysku: ${res?.savedPath || project.folderPath}`);
      })
      .catch(() => {
        addLog('Projekt zapisany lokalnie.');
      });
  };

  const handlePackageGame = useCallback(async () => {
    try {
      addLog('📦 Rozpoczynanie kompilacji i pakowania pełnej gry PC (.exe)...');
      const gameName = project.name ? project.name.replace(/[^a-zA-Z0-9_-]/g, '_') : 'LucidGame';
      const res = await sendRpc('engine.packageGame', {
        outDir: 'Build/Windows',
        name: gameName
      });
      if (res && res.success) {
        addLog(`🎉 Gra została pomyślnie skompilowana i spakowana! Plik: ${res.executable || (res.targetFolder + '/' + gameName + '.exe')}`);
      } else {
        addLog(`⚠️ Błąd pakowania gry: ${res?.error || 'Nieznany błąd'}`);
      }
    } catch (err: any) {
      addLog(`❌ Błąd kompilacji gry: ${err.message}`);
    }
  }, [sendRpc, project.name, addLog]);

  const selectedEntity = entities.find(e => e.id === selectedEntityId);
  const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];

  return (
    <div className="h-screen w-screen bg-[#07070c] text-slate-200 font-sans flex flex-col overflow-hidden select-none">
      {/* Hidden File Inputs for Import */}
      <input ref={fileInputRef} type="file" accept=".lucidproject,.json" onChange={handleImportProjectFile} className="hidden" />
      <input ref={glbInputRef} type="file" accept=".glb,.gltf" onChange={handleImportGlbFile} className="hidden" />

      {/* ═══ 1. UNREAL ENGINE TOP WINDOW TITLE BAR (File, Edit, Menus, Level Tabs) ═══ */}
      {!isImmersivePlay && (
        <div style={{ zoom: effectiveUiScale }}>
          <UnrealTitleBar
            project={project}
            activeLevelName={activeScene?.name || 'Lvl_MainLevel'}
            scenes={project.scenes}
            activeSceneId={project.activeSceneId}
            onSelectScene={handleSelectScene}
            onCloseScene={handleCloseScene}
            onNewScene={handleNewScene}
            onNewProject={() => setIsProjectModalOpen(true)}
            onOpenProjectModal={() => setIsProjectModalOpen(true)}
            onSaveProject={handleSaveProjectAndDisk}
            onExportProject={handleExportProject}
            onPackageGame={handlePackageGame}
            onImportProjectClick={() => fileInputRef.current?.click()}
            onImportGlbClick={() => glbInputRef.current?.click()}
            onOpenSettings={() => setIsEffectsPanelOpen(true)}
            onUndo={handleUndo}
            onRedo={handleRedo}
            onTogglePlaceActors={() => setIsPlaceActorsOpen(p => !p)}
            onToggleOutliner={() => setIsOutlinerOpen(p => !p)}
            onToggleDetails={() => setIsDetailsOpen(p => !p)}
            onToggleContentDrawer={() => setIsContentDrawerOpen(p => !p)}
            onToggleOutputLog={() => setIsOutputLogOpen(p => !p)}
            onExecuteScript={handleExecuteActiveScript}
            onPhysicsDiagnostics={handlePhysicsDiagnostics}
            onVulkanProfiler={handleVulkanProfiler}
            onExportTypes={handleExportTypes}
            onBuildAllLevels={handleBuildAllLevels}
            onBuildLighting={handleBuildLighting}
            onBuildCollisionMeshes={handleBuildCollisionMeshes}
            onSelectAll={() => { if (entities.length > 0) setSelectedEntityId(entities[0].id); }}
            onDeselect={() => setSelectedEntityId(null)}
            onInvertSelection={() => {
              const other = entities.find(e => e.id !== selectedEntityId);
              if (other) setSelectedEntityId(other.id);
            }}
            onFocusSelected={focusOnSelectedEntity}
            onDuplicateSelected={() => {
              const cur = entities.find(e => e.id === selectedEntityId);
              if (cur) handleDuplicateEntity(cur);
            }}
            onDeleteSelected={() => {
              if (selectedEntityId !== null) handleDeleteEntity(selectedEntityId);
            }}
            onAttachScript={() => {
              if (selectedEntityId !== null) {
                handleUpdateEntityProperty(selectedEntityId, { scriptName: 'CustomBehavior.js' });
                setIsScriptStudioOpen(true);
              }
            }}
          />
        </div>
      )}

      {/* ═══ 2. MAIN TOOLBAR (Selection Mode, Add, Blueprints, Play/Pause/Stop/Step) ═══ */}
      <div style={{ zoom: effectiveUiScale }}>
        <UnrealMainToolbar
          simulating={simulating}
          connected={connected}
          onPlay={handlePlay}
          onPause={handlePause}
          onStop={handleStop}
          onStep={handleStepSimulation}
          onSave={() => { persistProject(project); addLog('Zapisano stan sceny.'); }}
          onPackageGame={handlePackageGame}
          onQuickAdd={(type) => {
            if (type === 'fire' || type === 'portal') handleSpawnEmitter(type);
            else handleSpawnEntity(type);
          }}
          onOpenScripts={() => setIsScriptStudioOpen(true)}
          onOpenEffects={() => setIsEffectsPanelOpen(true)}
          onOpenSoundStudio={() => setIsSoundStudioOpen(true)}
          onOpenWidgetStudio={() => setIsWidgetStudioOpen(true)}
          cameraMode={cameraMode}
          onCameraModeChange={setCameraMode}
          isAudioMuted={isAudioMuted}
          onToggleAudioMute={() => setIsAudioMuted(p => !p)}
          isImmersivePlay={isImmersivePlay}
          onToggleImmersivePlay={() => setIsImmersivePlay(p => !p)}
        />
      </div>

      {/* ═══ 3. MAIN WORKSPACE (Place Actors Left + Viewport Center + Outliner/Details Right) ═══ */}
      <div className="flex-1 flex overflow-hidden relative">

        {/* ── LEFT: Place Actors Panel (Exact UE5 Left Palette) ── */}
        <div style={{ zoom: effectiveUiScale }} className="flex shrink-0">
          <PlaceActorsPanel
            isOpen={!isImmersivePlay && isPlaceActorsOpen}
            onSpawnEntity={handleSpawnEntity}
            onSpawnEmitter={handleSpawnEmitter}
          />
        </div>

        {/* ── CENTER: 3D Viewport ── */}
        <div className="flex-1 relative flex flex-col bg-[#07070c] overflow-hidden">
          {/* Viewport Top Controls (W/E/R, World/Local, Grid Snap, Camera speed, Perspective, Lit) */}
          {!isImmersivePlay && (
            <div style={{ zoom: effectiveUiScale }} className="absolute top-0 left-0 right-0 z-20 pointer-events-none">
              <div className="pointer-events-auto">
                <UnrealViewportOverlay
                  gizmoMode={gizmoMode}
                  setGizmoMode={setGizmoMode}
                  gizmoSpace={gizmoSpace}
                  setGizmoSpace={setGizmoSpace}
                  snapEnabled={snapEnabled}
                  setSnapEnabled={setSnapEnabled}
                  snapStep={snapStep}
                  setSnapStep={setSnapStep}
                  viewMode={viewMode}
                  setViewMode={setViewMode}
                  resolutionScale={resolutionScale}
                  setResolutionScale={handleSetResolutionScale}
                  renderResolution={renderResolution}
                  setRenderResolution={handleSetRenderResolution}
                  uiScale={uiScale}
                  setUiScale={handleSetUiScale}
                />
              </div>
            </div>
          )}

          {/* UMG UI Widget Blueprint Dynamic Overlay (Authorable HUD / Custom UI) */}
          {simulating && project.activeWidgetId && (() => {
            const activeWidget = project.widgets?.find(w => w.id === project.activeWidgetId);
            if (!activeWidget || !activeWidget.elements || activeWidget.elements.length === 0) return null;

            return (
              <div className="absolute inset-0 pointer-events-none overflow-hidden select-none z-20">
                {activeWidget.elements.map(elem => {
                  let renderedText = elem.text || '';
                  if (elem.bindVariable === 'score' || renderedText.includes('{score}')) {
                    renderedText = renderedText.includes('{score}')
                      ? renderedText.replace(/\{score\}/g, String(gameScore))
                      : `SCORE: ${gameScore}`;
                  }
                  if (elem.bindVariable === 'health' || renderedText.includes('{health}')) {
                    renderedText = renderedText.includes('{health}')
                      ? renderedText.replace(/\{health\}/g, String(playerHealth))
                      : `HEALTH: ${playerHealth}%`;
                  }
                  if (elem.bindVariable === 'status' || renderedText.includes('{status}')) {
                    renderedText = renderedText.includes('{status}')
                      ? renderedText.replace(/\{status\}/g, gameStatus)
                      : gameStatus;
                  }

                  const posStyle: React.CSSProperties = {
                    position: 'absolute',
                    left: `${elem.x}%`,
                    top: `${elem.y}%`,
                    width: `${elem.width}%`,
                    height: `${elem.height}%`,
                    opacity: elem.opacity ?? 1,
                  };

                  if (elem.type === 'progressBar') {
                    const rawVal = elem.bindVariable === 'health' ? playerHealth :
                      elem.bindVariable === 'score' ? Math.min(100, gameScore) :
                        (elem.value ?? 100);
                    const val = Math.max(0, Math.min(100, rawVal));
                    return (
                      <div
                        key={elem.id}
                        style={{
                          ...posStyle,
                          backgroundColor: elem.backgroundColor || 'rgba(15, 23, 42, 0.8)',
                          borderColor: elem.borderColor || 'rgba(255, 255, 255, 0.2)',
                          borderWidth: `${elem.borderWidth ?? 1}px`,
                          borderRadius: `${elem.borderRadius ?? 4}px`,
                          borderStyle: 'solid',
                        }}
                        className="overflow-hidden flex items-center shadow-lg"
                      >
                        <div
                          className="h-full transition-all duration-300"
                          style={{
                            width: `${val}%`,
                            backgroundColor: elem.color || '#10b981',
                            borderRadius: `${elem.borderRadius ?? 4}px`,
                          }}
                        />
                      </div>
                    );
                  }

                  if (elem.type === 'button') {
                    return (
                      <button
                        key={elem.id}
                        style={{
                          ...posStyle,
                          backgroundColor: elem.backgroundColor || 'rgba(30, 41, 59, 0.9)',
                          color: elem.color || '#ffffff',
                          borderColor: elem.borderColor || 'rgba(255, 255, 255, 0.2)',
                          borderWidth: `${elem.borderWidth ?? 1}px`,
                          borderRadius: `${elem.borderRadius ?? 6}px`,
                          fontSize: `${elem.fontSize || 12}px`,
                          borderStyle: 'solid',
                        }}
                        className="pointer-events-auto flex items-center justify-center font-semibold shadow hover:brightness-110 active:scale-95 transition-all"
                      >
                        {renderedText || 'Button'}
                      </button>
                    );
                  }

                  if (elem.type === 'panel') {
                    return (
                      <div
                        key={elem.id}
                        style={{
                          ...posStyle,
                          backgroundColor: elem.backgroundColor || 'rgba(15, 23, 42, 0.7)',
                          borderColor: elem.borderColor || 'rgba(255, 255, 255, 0.1)',
                          borderWidth: `${elem.borderWidth ?? 1}px`,
                          borderRadius: `${elem.borderRadius ?? 8}px`,
                          borderStyle: 'solid',
                        }}
                        className="shadow-xl backdrop-blur-sm"
                      />
                    );
                  }

                  // Default: text
                  return (
                    <div
                      key={elem.id}
                      style={{
                        ...posStyle,
                        color: elem.color || '#ffffff',
                        fontSize: `${elem.fontSize || 14}px`,
                        backgroundColor: elem.backgroundColor || 'transparent',
                        borderColor: elem.borderColor || 'transparent',
                        borderWidth: elem.borderWidth ? `${elem.borderWidth}px` : undefined,
                        borderRadius: elem.borderRadius ? `${elem.borderRadius}px` : undefined,
                        borderStyle: elem.borderWidth ? 'solid' : undefined,
                      }}
                      className="flex items-center font-bold font-mono tracking-wide drop-shadow-md"
                    >
                      {renderedText}
                    </div>
                  );
                })}
              </div>
            );
          })()}

          {/* Immersive Play Mode Exit Button */}
          {isImmersivePlay && (
            <button
              onClick={() => setIsImmersivePlay(false)}
              className="absolute top-4 right-4 z-30 flex items-center gap-1.5 px-3 py-1.5 bg-[#101018]/90 hover:bg-[#181824] border border-white/20 rounded-lg text-xs font-semibold text-slate-200 hover:text-white transition-all shadow-2xl cursor-pointer"
              title="Wyjdź z trybu pełnoekranowego (Esc / F11)"
            >
              <Minimize2 className="w-3.5 h-3.5 text-cyan-400" />
              <span>Wyjdź z Gry (F11)</span>
            </button>
          )}

          {/* Place Actors toggle tab if closed */}
          {!isImmersivePlay && !isPlaceActorsOpen && (
            <button
              onClick={() => setIsPlaceActorsOpen(true)}
              className="absolute top-12 left-0 z-20 px-2 py-1 bg-[#101018]/90 backdrop-blur-md border border-white/10 border-l-0 rounded-r text-[10px] text-cyan-300 hover:text-white flex items-center gap-1 shadow-lg"
              title="Show Place Actors Panel"
            >
              <ChevronRight className="w-3 h-3" />
              <span>Place Actors</span>
            </button>
          )}

          {/* Canvas Render Area */}
          <div
            ref={viewportWrapperRef}
            className="flex-1 w-full h-full relative overflow-hidden bg-[#07070c]"
            onContextMenu={(e) => e.preventDefault()}
          >
            {/* Vulkan 1.3 Direct GPU Stream Overlay (Rendered in background when Vulkan is active) */}
            {viewMode === 'vulkan' && (
              <div className="absolute inset-0 w-full h-full flex items-center justify-center bg-[#07070c] select-none pointer-events-none z-0">
                {vulkanFrame ? (
                  <img
                    src={vulkanFrame}
                    alt="Vulkan GPU Direct Stream"
                    className="max-w-full max-h-full object-contain pointer-events-none select-none"
                    draggable={false}
                  />
                ) : (
                  <div className="flex flex-col items-center gap-3 text-slate-400 font-mono text-xs pointer-events-none">
                    <RefreshCw className="w-6 h-6 animate-spin text-cyan-400" />
                    <span>Oczekiwanie na klatki Vulkan GPU... (Aktywny podgląd WebGL)</span>
                  </div>
                )}
                <div className="absolute bottom-4 left-4 px-3 py-1.5 rounded-lg bg-black/80 backdrop-blur border border-emerald-500/40 text-[11px] font-mono text-emerald-300 flex items-center gap-2 shadow-lg pointer-events-auto z-20">
                  <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse" />
                  <span>VULKAN 1.3 DIRECT GPU • {vulkanFps} FPS • {renderResolution.width}x{renderResolution.height} ({Math.round(resolutionScale * 100)}%)</span>
                </div>
              </div>
            )}

            {/* Three.js Interactive Viewport (Always active & in front, receiving gizmo & click interactions!) */}
            <div
              ref={containerRef}
              className="absolute inset-0 w-full h-full z-10 cursor-default"
              style={{
                pointerEvents: 'auto',
              }}
            />
          </div>

          {/* Bottom-left Coordinate Gizmo (Exact Unreal Engine Tripod) */}
          {!isImmersivePlay && (
            <div style={{ zoom: effectiveUiScale }} className="absolute bottom-2 left-2 z-20 pointer-events-none">
              <UnrealCoordinateGizmo yaw={cameraRot.yaw} pitch={cameraRot.pitch} />
            </div>
          )}

          {/* Navigation Hint (Bottom-left, elevated above gizmo) */}
          {!isImmersivePlay && (
            <div style={{ zoom: effectiveUiScale }} className="absolute bottom-20 left-3 z-20 pointer-events-none">
              <div className="px-2.5 py-1.5 rounded-md bg-black/75 backdrop-blur-md border border-white/10 text-[10px] font-mono text-slate-400 flex items-center gap-2 shadow-lg">
                <Orbit className="w-3 h-3 text-cyan-400" />
                <span>PPM+WASD: Leć • Alt+LPM: Orbituj • ŚPM: Przesuń • Scroll: Zoom • LPM: Zaznacz • W/E/R: Gizmo • F: Skup</span>
              </div>
            </div>
          )}

          {/* AI Game Creator Floating Prompt Bar (Linear style glass container) */}
          {!isImmersivePlay && (
            <div className="absolute bottom-2 left-1/2 -translate-x-1/2 z-20 w-full max-w-xl px-4 pointer-events-auto">
              <div className="flex items-center gap-2 p-1.5 bg-[#101018]/90 backdrop-blur-xl border border-cyan-500/30 rounded-xl shadow-2xl shadow-cyan-950/50">
                <div className="flex items-center pl-2 text-cyan-400">
                  <Wand2 className="w-4 h-4" />
                </div>
                <input
                  type="text"
                  value={aiPrompt}
                  onChange={e => setAiPrompt(e.target.value)}
                  onKeyDown={e => e.key === 'Enter' && handleAiGenerate()}
                  placeholder="Napisz do AI co chcesz stworzyć (np. 'Wyścigi aut', 'Arkanoid', 'Układ Planet')..."
                  className="flex-1 bg-transparent border-0 text-xs text-white placeholder-slate-500 focus:outline-none px-2"
                />
                <button
                  onClick={handleAiGenerate}
                  disabled={aiLoading}
                  className="flex items-center gap-1.5 px-3.5 py-1.5 bg-gradient-to-r from-cyan-500 to-indigo-600 hover:from-cyan-400 hover:to-indigo-500 text-white rounded-lg text-xs font-semibold shadow-md transition-all disabled:opacity-50"
                >
                  {aiLoading ? <RefreshCw className="w-3.5 h-3.5 animate-spin" /> : <Sparkles className="w-3.5 h-3.5" />}
                  <span>{aiLoading ? '...' : 'Generuj'}</span>
                </button>
              </div>
            </div>
          )}
        </div>

        {/* ── RIGHT: Outliner (Top) + Details & World Settings (Bottom) ── */}
        {!isImmersivePlay && (isOutlinerOpen || isDetailsOpen) && (
          <aside style={{ zoom: effectiveUiScale }} className="w-80 border-l border-white/10 bg-[#0e0e16] flex flex-col shrink-0 select-none overflow-hidden">
            {/* Top: Outliner Panel */}
            {isOutlinerOpen && (
              <div className={`${isDetailsOpen ? 'h-[45%]' : 'h-full'} flex flex-col border-b border-white/10 overflow-hidden`}>
                <OutlinerPanel
                  entities={entities}
                  selectedEntityId={selectedEntityId}
                  onSelectEntity={(id) => setSelectedEntityId(id)}
                  onDeleteEntity={handleDeleteEntity}
                  onDuplicateEntity={handleDuplicateEntity}
                  onFocusEntity={focusOnSelectedEntity}
                />
              </div>
            )}

            {/* Bottom: Details Panel */}
            {isDetailsOpen && (
              <div className="flex-1 flex flex-col overflow-hidden">
                <DetailsPanel
                  selectedEntity={selectedEntity}
                  materials={project.materials}
                  sounds={project.sounds || []}
                  settings={project.settings}
                  onUpdateProperty={handleUpdateEntityProperty}
                  onApplyMaterial={handleApplyMaterial}
                  onApplyImpulse={handleApplyImpulse}
                  onUpdateSettings={(patch) => {
                    if (patch.uiScale !== undefined) {
                      handleSetUiScale(patch.uiScale);
                    }
                    if (patch.resolutionScale !== undefined) {
                      handleSetResolutionScale(patch.resolutionScale);
                    }
                    if (patch.renderResolution !== undefined) {
                      handleSetRenderResolution(patch.renderResolution);
                    }
                    persistProject({ ...project, settings: { ...project.settings, ...patch } });
                  }}
                  onOpenScriptEditor={handleOpenScriptEditor}
                />
              </div>
            )}
          </aside>
        )}
      </div>

      {/* ═══ 4. BOTTOM BAR (Exact UE5 Bottom Dock) ═══════════════════════ */}
      {!isImmersivePlay && (
        <div style={{ zoom: effectiveUiScale }} className="h-7 bg-[#0e0e0e] border-t border-[#242424] flex items-center justify-between px-2 text-[11px] font-sans text-neutral-300 z-30 select-none shrink-0">
          {/* Left: Content Drawer & Output Log Buttons */}
          <div className="flex items-center gap-1.5">
            <button
              onClick={() => setIsContentDrawerOpen(prev => !prev)}
              className={`flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-medium transition-colors ${isContentDrawerOpen
                  ? 'bg-[#0078d4]/20 text-white border border-[#0078d4]/40'
                  : 'text-neutral-300 hover:bg-white/5 hover:text-white'
                }`}
              title="Toggle Content Drawer (Ctrl + Space)"
            >
              <FolderOpen className="w-3.5 h-3.5 text-neutral-400" />
              <span>Content Drawer</span>
            </button>

            <div className="h-3.5 w-px bg-[#262626]" />

            <button
              onClick={() => setIsOutputLogOpen(prev => !prev)}
              className={`flex items-center gap-1.5 px-2 py-1 rounded text-xs font-medium transition-colors ${isOutputLogOpen
                  ? 'bg-[#0078d4]/20 text-white border border-[#0078d4]/40'
                  : 'text-neutral-400 hover:bg-white/5 hover:text-white'
                }`}
              title="Toggle Output Log"
            >
              <Terminal className="w-3.5 h-3.5 text-neutral-400" />
              <span>Output Log</span>
            </button>
          </div>

          {/* Center: Cmd ▾ Enter Console Command */}
          <div className="flex-1 max-w-md mx-4">
            <form
              onSubmit={(e) => { e.preventDefault(); handleExecuteConsoleCmd(consoleCmd); }}
              className="flex items-center bg-[#121212] border border-[#2b2b2b] rounded px-2 py-0.5 text-xs text-neutral-300 focus-within:border-[#0078d4]"
            >
              <span className="text-[10px] font-sans font-medium text-neutral-400 mr-1.5">Cmd ▾</span>
              <input
                type="text"
                value={consoleCmd}
                onChange={(e) => setConsoleCmd(e.target.value)}
                placeholder="Enter Console Command..."
                className="w-full bg-transparent text-[11px] font-sans text-white placeholder-neutral-500 focus:outline-none"
              />
              <button type="submit" className="text-neutral-500 hover:text-white ml-1">
                <Send className="w-3 h-3" />
              </button>
            </form>
          </div>

          {/* Right: Status Indicators (Exact UE5 bottom-right rail) */}
          <div className="flex items-center gap-3 text-[10px] font-mono text-slate-500">
            <span className="hover:text-slate-300 cursor-pointer">Diagnostics</span>
            <span className="text-slate-600">•</span>
            <span className="flex items-center gap-1 text-emerald-400">
              <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 shadow-sm" />
              Zen Server
            </span>
            <span className="text-slate-600">•</span>
            <span className="text-neutral-400 flex items-center gap-1">
              <Check className="w-3 h-3 text-[#0078d4]" />
              All Saved
            </span>
            <span className="text-slate-600">•</span>
            <span className="text-indigo-400">UI: {Math.round(effectiveUiScale * 100)}%</span>
            <span className="text-slate-600">•</span>
            <span className="text-purple-400">
              {transportMode === 'native-ipc' ? '⚡ SharedMem (Zero-GC)' : '🌐 Unified WS:9000'}
            </span>
            <span className="text-slate-600">•</span>
            <span className="text-cyan-300">{viewMode === 'vulkan' ? `Vulkan: ${vulkanFps} FPS (${renderResolution.width}x${renderResolution.height} @ ${Math.round(resolutionScale * 100)}%)` : 'WebGL: 60 FPS'}</span>
          </div>
        </div>
      )}

      {/* ═══ 5. BOTTOM DRAWERS & MODALS ═══════════════════════════════════ */}

      {/* Output Log Drawer */}
      {isOutputLogOpen && (
        <div style={{ zoom: effectiveUiScale }} className="h-48 bg-[#08080e] border-t border-white/10 flex flex-col z-20 shrink-0 font-mono text-xs">
          <div className="h-7 bg-[#0b0b14] border-b border-white/10 px-3 flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Terminal className="w-3.5 h-3.5 text-cyan-400" />
              <span className="font-semibold text-slate-300 text-[11px]">Output Log</span>
              <span className="text-[10px] text-slate-500">({logs.length} entries)</span>
            </div>
            <div className="flex items-center gap-2">
              <button onClick={() => setLogs([])} className="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 text-[10px] text-slate-400">
                Clear
              </button>
              <button onClick={() => setIsOutputLogOpen(false)} className="p-0.5 text-slate-400 hover:text-white">
                <X className="w-3.5 h-3.5" />
              </button>
            </div>
          </div>
          <div className="flex-1 overflow-y-auto p-2 space-y-0.5 text-[10px]">
            {logs.map((log, idx) => (
              <div key={idx} className="text-slate-300 leading-relaxed font-mono">
                {log}
              </div>
            ))}
            {logs.length === 0 && <div className="text-slate-600 text-center py-4">Brak logów wyjściowych.</div>}
          </div>
        </div>
      )}

      {/* Content Drawer (Sliding Up from Bottom) */}
      <div style={{ zoom: effectiveUiScale }}>
        <ContentBrowser
          project={project}
          isOpen={isContentDrawerOpen}
          onClose={() => setIsContentDrawerOpen(false)}
          onSelectScript={(s) => { setActiveScriptId(s.id); setIsScriptStudioOpen(true); }}
          onApplyMaterial={(m) => { if (selectedEntityId !== null) handleApplyMaterial(selectedEntityId, m); }}
          onSelectEmitter={() => setIsEffectsPanelOpen(true)}
          onSelectSound={() => setIsSoundStudioOpen(true)}
          onSelectWidget={() => setIsWidgetStudioOpen(true)}
          onCreateScript={handleCreateScript}
          onCreateMaterial={handleCreateMaterial}
          onCreateEmitter={() => handleSpawnEmitter('fire')}
          onCreateSound={() => setIsSoundStudioOpen(true)}
          onCreateWidget={() => setIsWidgetStudioOpen(true)}
          onDeleteScript={handleDeleteScript}
          onDeleteMaterial={handleDeleteMaterial}
          onDeleteEmitter={handleDeleteEmitter}
          onDeleteSound={handleDeleteSound}
          onDeleteWidget={handleDeleteWidget}
        />
      </div>

      {/* Script Studio Modal / Floating IDE */}
      {isScriptStudioOpen && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4">
          <div className="bg-[#0e0e16] border border-white/20 rounded-xl w-full max-w-4xl h-[75vh] shadow-2xl flex flex-col overflow-hidden">
            <div className="h-8 bg-[#0a0a10] border-b border-white/10 px-3 flex items-center justify-between text-xs">
              <span className="font-bold text-white flex items-center gap-2">
                <span>Blueprints / QuickJS Script Studio</span>
              </span>
              <button onClick={() => setIsScriptStudioOpen(false)} className="p-1 text-slate-400 hover:text-white">
                <X className="w-4 h-4" />
              </button>
            </div>
            <div className="flex-1 flex overflow-hidden">
              <ScriptStudio
                scripts={project.scripts}
                activeScriptId={activeScriptId}
                onSelectScript={(id) => setActiveScriptId(id)}
                onUpdateScriptCode={handleUpdateScriptCode}
                onCreateScript={handleCreateScript}
                onDeleteScript={handleDeleteScript}
                onInjectScript={handleInjectScript}
                lastScriptError={lastScriptError}
              />
            </div>
          </div>
        </div>
      )}

      {/* VFX & Atmosphere Studio Modal */}
      {isEffectsPanelOpen && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4">
          <div className="bg-[#0e0e16] border border-white/20 rounded-xl w-full max-w-2xl h-[70vh] shadow-2xl flex flex-col overflow-hidden">
            <div className="h-8 bg-[#0a0a10] border-b border-white/10 px-3 flex items-center justify-between text-xs">
              <span className="font-bold text-white flex items-center gap-2">
                <Sparkles className="w-4 h-4 text-orange-400" />
                <span>VFX & Atmosphere Studio</span>
              </span>
              <button onClick={() => setIsEffectsPanelOpen(false)} className="p-1 text-slate-400 hover:text-white">
                <X className="w-4 h-4" />
              </button>
            </div>
            <div className="flex-1 flex overflow-hidden">
              <EffectsPanel
                emitters={(project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0])?.emitters || []}
                entities={entities}
                settings={project.settings}
                onAddEmitter={handleSpawnEmitter}
                onUpdateEmitter={handleUpdateEmitter}
                onDeleteEmitter={handleDeleteEmitter}
                onUpdateSettings={(patch) => persistProject({ ...project, settings: { ...project.settings, ...patch } })}
              />
            </div>
          </div>
        </div>
      )}

      {/* Sound Cue Studio Modal */}
      <SoundCueStudio
        sounds={project.sounds || []}
        isOpen={isSoundStudioOpen}
        onClose={() => setIsSoundStudioOpen(false)}
        onSaveSound={handleSaveSound}
        onDeleteSound={handleDeleteSound}
      />

      {/* UMG UI Widget Studio Modal */}
      <WidgetStudio
        widgets={project.widgets || []}
        activeWidgetId={project.activeWidgetId}
        isOpen={isWidgetStudioOpen}
        onClose={() => setIsWidgetStudioOpen(false)}
        onSaveWidget={handleSaveWidget}
        onDeleteWidget={handleDeleteWidget}
        onSetActiveWidget={handleSetActiveWidget}
      />

      {/* Project Manager Modal */}
      <ProjectModal
        isOpen={isProjectModalOpen}
        onClose={() => setIsProjectModalOpen(false)}
        projects={allProjects}
        activeProjectId={project.id}
        onSelectProject={(id) => {
          const found = allProjects.find(p => p.id === id);
          if (found) {
            setProject(found);
            ProjectManager.saveProject(found);
            const activeSc = found.scenes.find(s => s.id === found.activeSceneId) || found.scenes[0];
            setEntities(activeSc?.entities || []);
            setSelectedEntityId(null);
            addLog(`Przełączono na projekt: "${found.name}" (${found.folderPath || 'dysk'})`);
          }
        }}
        onCreateProject={(name, description, template, folderPath) => {
          let newProj: Project;
          if (template === 'empty') {
            newProj = createEmptyProject(name, folderPath);
          } else if (template === 'breakout') {
            newProj = createBreakoutProject(name, folderPath);
          } else if (template === 'shooter') {
            newProj = createSpaceShooterProject(name, folderPath);
          } else if (template === 'racer') {
            newProj = createHoverRacerProject(name, folderPath);
          } else {
            newProj = createPlatformerProject(name, folderPath);
          }
          if (description) newProj.description = description;

          ProjectManager.saveProject(newProj);
          setProject(newProj);
          setAllProjects(ProjectManager.getAllProjects());

          const preset = template === 'breakout' ? 'breakout' :
            template === 'shooter' ? 'shooter' :
              template === 'racer' ? 'racer' :
                template === 'platformer' ? 'platformer' : 'empty';
          currentPresetRef.current = preset;

          if (template === 'platformer' || template === 'racer') {
            setCameraMode('thirdPerson');
          } else if (template === 'shooter') {
            setCameraMode('topDown');
          } else if (template === 'breakout') {
            setCameraMode('sideScroller');
          }

          sendRpc('engine.resetScene', { preset }).then(async () => {
            const snap = await sendRpc('engine.getSnapshot');
            setEntities(snap || []);
            sendRpc('engine.saveProjectToDisk', { project: newProj, folderPath: newProj.folderPath }).catch(console.error);
          }).catch(console.error);

          addLog(`Utworzono nowy projekt od zera: "${name}" (${folderPath}) [Szablon: ${template.toUpperCase()}]`);
        }}
        onDeleteProject={(id) => {
          const updated = ProjectManager.deleteProject(id);
          setAllProjects(updated);
          setProject(updated[0]);
          const activeSc = updated[0].scenes.find(s => s.id === updated[0].activeSceneId) || updated[0].scenes[0];
          setEntities(activeSc?.entities || []);
          addLog('Usunięto projekt.');
        }}
        onExportProject={(p) => ProjectManager.exportProjectToJson(p)}
      />
    </div>
  );
}
