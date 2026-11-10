#include "ecs/TypeExporter.hpp"
#include <fstream>
#include "core/Logger.hpp"

namespace engine::ecs {
    std::string TypeExporter::generateTypeScriptDefinitions() {
        std::string out;
        out += "/** Lucid Engine Auto-Generated Types for AI Scripting (QuickJS) */\n\n";
        
        out += "export interface TransformComponent {\n";
        out += "  x: number;\n";
        out += "  y: number;\n";
        out += "  z: number;\n";
        out += "  rx?: number;\n";
        out += "  ry?: number;\n";
        out += "  rz?: number;\n";
        out += "  qx?: number;\n";
        out += "  qy?: number;\n";
        out += "  qz?: number;\n";
        out += "  qw?: number;\n";
        out += "  sx?: number;\n";
        out += "  sy?: number;\n";
        out += "  sz?: number;\n";
        out += "}\n\n";

        out += "export interface LODComponent {\n";
        out += "  lodMeshIds: number[];\n";
        out += "  lodDistances: number[];\n";
        out += "  currentLod?: number;\n";
        out += "}\n\n";

        out += "export interface VelocityComponent {\n";
        out += "  vx: number;\n";
        out += "  vy: number;\n";
        out += "  vz: number;\n";
        out += "}\n\n";

        out += "export interface MeshComponent {\n";
        out += "  meshId: number; // 1 = Floor plane, 2 = Cube/Box, 3 = Sphere\n";
        out += "  materialId?: number;\n";
        out += "}\n\n";

        out += "export interface RigidBodyComponent {\n";
        out += "  mass: number; // 0 = Static obstacle, >0 = Dynamic physics body\n";
        out += "  shapeType?: number; // 0 = Box, 1 = Sphere, 2 = Capsule\n";
        out += "  halfExtents?: { x: number; y: number; z: number };\n";
        out += "  capsuleRadius?: number;\n";
        out += "  capsuleHalfHeight?: number;\n";
        out += "  restitution?: number; // Bounciness: 0.0 = completely inelastic, 1.0 = full bounce\n";
        out += "  friction?: number; // Friction coefficient\n";
        out += "  lockRotationX?: boolean;\n";
        out += "  lockRotationY?: boolean;\n";
        out += "  lockRotationZ?: boolean;\n";
        out += "  isSensor?: boolean; // Trigger volume without physics resistance\n";
        out += "}\n\n";

        out += "export interface MaterialComponent {\n";
        out += "  color?: string | [number, number, number, number?];\n";
        out += "  metallic?: number;\n";
        out += "  roughness?: number;\n";
        out += "  emissive?: string | [number, number, number];\n";
        out += "  emissiveIntensity?: number;\n";
        out += "  materialId?: number;\n";
        out += "  albedoTextureId?: number;\n";
        out += "  normalTextureId?: number;\n";
        out += "  ormTextureId?: number;\n";
        out += "  albedoPath?: string;\n";
        out += "  normalPath?: string;\n";
        out += "  ormPath?: string;\n";
        out += "}\n\n";

        out += "export interface LightComponent {\n";
        out += "  type: 'directional' | 'point' | 'spot' | 'sky';\n";
        out += "  color: string | [number, number, number];\n";
        out += "  intensity: number;\n";
        out += "  range?: number;\n";
        out += "}\n\n";

        out += "export interface AudioComponent {\n";
        out += "  soundAssetId: string;\n";
        out += "  volume?: number;\n";
        out += "  pitch?: number;\n";
        out += "  is3D?: boolean;\n";
        out += "  loop?: boolean;\n";
        out += "  autoPlay?: boolean;\n";
        out += "}\n\n";

        out += "export interface CharacterMovementComponent {\n";
        out += "  maxWalkSpeed?: number;\n";
        out += "  jumpZVelocity?: number;\n";
        out += "  gravityScale?: number;\n";
        out += "  airControl?: number;\n";
        out += "}\n\n";

        out += "export interface CameraBoomComponent {\n";
        out += "  targetArmLength?: number;\n";
        out += "  socketOffsetY?: number;\n";
        out += "  cameraLag?: boolean;\n";
        out += "  lagSpeed?: number;\n";
        out += "  isFirstPerson?: boolean;\n";
        out += "}\n\n";

        out += "export interface ParticleEmitterComponent {\n";
        out += "  type: 'fire' | 'sparks' | 'smoke' | 'portal' | 'explosion';\n";
        out += "  color?: string | [number, number, number];\n";
        out += "  count?: number;\n";
        out += "  speed?: number;\n";
        out += "  size?: number;\n";
        out += "}\n\n";

        out += "export interface Contact {\n";
        out += "  a: number; // Entity ID A\n";
        out += "  b: number; // Entity ID B\n";
        out += "  normal?: { x: number; y: number; z: number };\n";
        out += "}\n\n";

        out += "export interface InputState {\n";
        out += "  keys?: { [keyCode: string]: boolean };\n";
        out += "  mouse?: { x: number; y: number; left?: boolean; right?: boolean; middle?: boolean; buttons?: number };\n";
        out += "}\n\n";

        out += "export interface Entity {\n";
        out += "  id: number;\n";
        out += "  tag?: string;\n";
        out += "  transform: TransformComponent;\n";
        out += "  velocity?: VelocityComponent;\n";
        out += "  mesh?: MeshComponent;\n";
        out += "  material?: MaterialComponent;\n";
        out += "  rigidBody?: RigidBodyComponent;\n";
        out += "  light?: LightComponent;\n";
        out += "  audio?: AudioComponent;\n";
        out += "  movement?: CharacterMovementComponent;\n";
        out += "  cameraBoom?: CameraBoomComponent;\n";
        out += "  particles?: ParticleEmitterComponent;\n";
        out += "  [customProp: string]: any; // Allow custom game logic state per entity\n";
        out += "}\n\n";

        out += "export interface EngineAPI {\n";
        out += "  log(message: string): void;\n";
        out += "  spawnEntity(components: Partial<Entity>): number;\n";
        out += "  destroyEntity(entityId: number): boolean;\n";
        out += "  applyImpulse(entityId: number, ix: number, iy: number, iz: number): boolean;\n";
        out += "  setVelocity(entityId: number, vx: number, vy: number, vz: number): boolean;\n";
        out += "  setScore(score: number): void;\n";
        out += "  getScore(): number;\n";
        out += "  setHealth(health: number): void;\n";
        out += "  getHealth(): number;\n";
        out += "  setGameStatus(status: 'PLAYING' | 'PAUSED' | 'GAMEOVER' | 'VICTORY'): void;\n";
        out += "  getGameStatus(): string;\n";
        out += "  playSfx(type: 'jump' | 'coin' | 'hit' | 'explosion' | 'laser' | 'powerup' | 'gameover' | 'victory', volume?: number): void;\n";
        out += "  shakeCamera(intensity?: number, duration?: number): void;\n";
        out += "  setGameState(state: any): void;\n";
        out += "  getGameState(): any;\n";
        out += "  getTime(): number;\n";
        out += "}\n\n";

        out += "declare global {\n";
        out += "  const engine: EngineAPI;\n";
        out += "  const input: InputState;\n";
        out += "  function update(entities: Entity[], dt: number, input?: InputState, contacts?: Contact[]): void;\n";
        out += "}\n";

        return out;
    }

    void TypeExporter::exportToTypeScript(const std::string& filepath) {
        std::ofstream out(filepath);
        if (!out.is_open()) {
            core::Logger::error("Failed to open " + filepath + " for writing engine.d.ts");
            return;
        }
        
        out << generateTypeScriptDefinitions();
        core::Logger::info("Successfully exported TypeScript definitions to " + filepath);
    }
}
