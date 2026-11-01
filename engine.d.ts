/** Lucid Engine Auto-Generated Types for AI Scripting (QuickJS) */

export interface TransformComponent {
  x: number;
  y: number;
  z: number;
  rx?: number;
  ry?: number;
  rz?: number;
  qx?: number;
  qy?: number;
  qz?: number;
  qw?: number;
  sx?: number;
  sy?: number;
  sz?: number;
}

export interface LODComponent {
  lodMeshIds: number[];
  lodDistances: number[];
  currentLod?: number;
}

export interface VelocityComponent {
  vx: number;
  vy: number;
  vz: number;
}

export interface MeshComponent {
  meshId: number; // 1 = Floor plane, 2 = Cube/Box, 3 = Sphere
  materialId?: number;
}

export interface RigidBodyComponent {
  mass: number; // 0 = Static obstacle, >0 = Dynamic physics body
  shapeType?: number; // 0 = Box, 1 = Sphere, 2 = Capsule
  halfExtents?: { x: number; y: number; z: number };
  capsuleRadius?: number;
  capsuleHalfHeight?: number;
  restitution?: number; // Bounciness: 0.0 = completely inelastic, 1.0 = full bounce
  friction?: number; // Friction coefficient
  lockRotationX?: boolean;
  lockRotationY?: boolean;
  lockRotationZ?: boolean;
  isSensor?: boolean; // Trigger volume without physics resistance
}

export interface MaterialComponent {
  color?: [number, number, number, number?];
  metallic?: number;
  roughness?: number;
}

export interface Contact {
  a: number; // Entity ID A
  b: number; // Entity ID B
  normal?: { x: number; y: number; z: number };
}

export interface InputState {
  keys?: { [keyCode: string]: boolean };
  mouse?: { x: number; y: number; left?: boolean; right?: boolean; middle?: boolean; buttons?: number };
}

export interface Entity {
  id: number;
  tag?: string;
  transform: TransformComponent;
  velocity?: VelocityComponent;
  mesh?: MeshComponent;
  material?: MaterialComponent;
  rigidBody?: RigidBodyComponent;
  [customProp: string]: any; // Allow custom game logic state per entity
}

export interface EngineAPI {
  log(message: string): void;
  spawnEntity(components: Partial<Entity>): number;
  destroyEntity(entityId: number): boolean;
  applyImpulse(entityId: number, ix: number, iy: number, iz: number): boolean;
  setVelocity(entityId: number, vx: number, vy: number, vz: number): boolean;
  setScore(score: number): void;
  getScore(): number;
  setHealth(health: number): void;
  getHealth(): number;
  setGameStatus(status: 'PLAYING' | 'PAUSED' | 'GAMEOVER' | 'VICTORY'): void;
  getGameStatus(): string;
  playSfx(type: 'jump' | 'coin' | 'hit' | 'explosion' | 'laser' | 'powerup' | 'gameover' | 'victory', volume?: number): void;
  shakeCamera(intensity?: number, duration?: number): void;
  setGameState(state: any): void;
  getGameState(): any;
  getTime(): number;
}

declare global {
  const engine: EngineAPI;
  const input: InputState;
  function update(entities: Entity[], dt: number, input?: InputState, contacts?: Contact[]): void;
}
