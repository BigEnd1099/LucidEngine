export interface ScriptFile {
  id: string;
  name: string;
  code: string;
  createdAt: number;
}

export interface MaterialAsset {
  id: string;
  name: string;
  color: string;
  metallic: number;
  roughness: number;
  emissive?: string;
  emissiveIntensity?: number;
}

export type ParticleType = 'fire' | 'sparks' | 'smoke' | 'portal' | 'explosion';

export interface ParticleEmitterConfig {
  id: string;
  name: string;
  type: ParticleType;
  position: { x: number; y: number; z: number };
  color: string;
  count: number;
  speed: number;
  size: number;
  attachedToEntityId?: number;
}

// ── Audio Asset & Sound Cue Synthesizer ───────────────────────────────────────
export type WaveformType = 'sine' | 'square' | 'sawtooth' | 'triangle' | 'noise';

export interface SoundCueConfig {
  waveType: WaveformType;
  startFreq: number; // Hz (20 - 4000)
  endFreq: number; // Hz (sweep target)
  attack: number; // seconds
  decay: number; // seconds
  sustain: number; // ratio (0 - 1)
  release: number; // seconds
  pitchBendSpeed?: number;
  volume: number; // 0 - 1
  noiseMix?: number; // 0 - 1
}

export interface SoundAsset {
  id: string;
  name: string; // e.g. "S_Jump.cue" or "SFX_Laser.wav"
  type: 'synth_cue' | 'audio_file';
  audioBase64?: string; // For imported audio files (WAV, MP3, OGG)
  cueConfig?: SoundCueConfig; // For procedural sound cues
  createdAt: number;
}

// ── UMG / UI Widget Blueprint System ──────────────────────────────────────────
export type UIElementType = 'text' | 'progressBar' | 'button' | 'image' | 'panel';

export interface UIElement {
  id: string;
  name: string;
  type: UIElementType;
  x: number; // % (0 - 100) or px
  y: number; // % (0 - 100) or px
  width: number;
  height: number;
  text?: string;
  fontSize?: number;
  color?: string;
  backgroundColor?: string;
  borderColor?: string;
  borderWidth?: number;
  borderRadius?: number;
  opacity?: number;
  bindVariable?: string; // Variable to observe: e.g. "score", "health", "mana", "ammo", "speed"
  value?: number; // Numeric value for progress bars (0 - 100)
  minValue?: number;
  maxValue?: number;
  visible?: boolean;
}

export interface WidgetBlueprint {
  id: string;
  name: string; // e.g. "WBP_GameHUD"
  elements: UIElement[];
  createdAt: number;
}

// ── Entity Components ─────────────────────────────────────────────────────────
export interface AudioComponentData {
  soundAssetId: string;
  volume: number; // 0 - 1
  pitch: number; // 0.5 - 2.0
  is3D: boolean; // Spatial attenuation
  loop: boolean;
  autoPlay: boolean;
}

export interface CharacterMovementData {
  maxWalkSpeed: number; // m/s
  jumpZVelocity: number; // m/s
  gravityScale: number; // multiplier
  airControl: number; // ratio 0 - 1
}

export interface CameraBoomData {
  targetArmLength: number; // Distance in meters behind target
  socketOffsetY: number; // Height offset
  cameraLag: boolean;
  lagSpeed: number;
}

export interface ProjectSettings {
  gravity: number;
  ambientColor: string;
  ambientIntensity: number;
  sunColor: string;
  sunIntensity: number;
  fogEnabled: boolean;
  fogColor: string;
  fogNear: number;
  fogFar: number;
}

export interface EntityTransform {
  x: number; y: number; z: number;
  rx?: number; ry?: number; rz?: number;
  qx?: number; qy?: number; qz?: number; qw?: number;
  sx?: number; sy?: number; sz?: number;
}

export interface EntityVelocity { vx: number; vy: number; vz: number; }
export interface EntityMesh { meshId: number; materialId?: number; }
export interface EntityRigidBody {
  mass: number;
  shapeType: number; // 0 = Box, 1 = Sphere, 2 = Capsule
  halfExtents?: { x: number; y: number; z: number };
  capsuleRadius?: number;
  capsuleHalfHeight?: number;
  restitution?: number;
  friction?: number;
  lockRotationX?: boolean;
  lockRotationY?: boolean;
  lockRotationZ?: boolean;
  isSensor?: boolean;
}

export type CameraMode = 'editor' | 'thirdPerson' | 'topDown' | 'firstPerson' | 'sideScroller';
export type GameStatus = 'PLAYING' | 'PAUSED' | 'GAMEOVER' | 'VICTORY';

export interface EntityLightData {
  type: 'directional' | 'point' | 'spot' | 'sky';
  color: string;
  intensity: number;
  range?: number;
}

export interface EntityData {
  id: number;
  tag?: string;
  transform: EntityTransform;
  velocity?: EntityVelocity;
  mesh?: EntityMesh;
  material?: MaterialAsset;
  rigidBody?: EntityRigidBody;
  light?: EntityLightData;
  scriptName?: string;
  audio?: AudioComponentData;
  movement?: CharacterMovementData;
  cameraBoom?: CameraBoomData;
  customProperties?: Record<string, any>;
}

export interface SceneData {
  id: string;
  name: string;
  entities: EntityData[];
  emitters: ParticleEmitterConfig[];
}

export interface Project {
  id: string;
  name: string;
  description: string;
  folderPath?: string;
  createdAt: number;
  updatedAt: number;
  activeSceneId: string;
  scenes: SceneData[];
  scripts: ScriptFile[];
  materials: MaterialAsset[];
  sounds: SoundAsset[];
  widgets: WidgetBlueprint[];
  activeWidgetId?: string;
  settings: ProjectSettings;
}
