import * as THREE from 'three';
import { ParticleEmitterConfig } from '../types/project';

interface ParticleData {
  positions: Float32Array;
  velocities: Float32Array;
  lifetimes: Float32Array;
  maxLifetimes: Float32Array;
  colors: Float32Array;
  geometry: THREE.BufferGeometry;
  points: THREE.Points;
  config: ParticleEmitterConfig;
}

export class ParticleManager {
  private scene: THREE.Scene;
  private emitters: Map<string, ParticleData> = new Map();

  constructor(scene: THREE.Scene) {
    this.scene = scene;
  }

  public addOrUpdateEmitter(config: ParticleEmitterConfig, parentMesh?: THREE.Mesh): void {
    this.removeEmitter(config.id);

    const count = Math.min(config.count || 200, 2000);
    const positions = new Float32Array(count * 3);
    const velocities = new Float32Array(count * 3);
    const lifetimes = new Float32Array(count);
    const maxLifetimes = new Float32Array(count);
    const colors = new Float32Array(count * 3);

    const baseColor = new THREE.Color(config.color || '#ff6600');

    for (let i = 0; i < count; i++) {
      this.resetParticle(i, config, positions, velocities, lifetimes, maxLifetimes, colors, baseColor);
      lifetimes[i] = Math.random() * maxLifetimes[i]; // Jitter start
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));

    // Particle texture / point sprite
    const material = new THREE.PointsMaterial({
      size: config.size || 0.4,
      vertexColors: true,
      transparent: true,
      opacity: 0.85,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    const points = new THREE.Points(geometry, material);
    points.frustumCulled = false;

    if (parentMesh) {
      parentMesh.add(points);
    } else {
      points.position.set(config.position.x, config.position.y, config.position.z);
      this.scene.add(points);
    }

    this.emitters.set(config.id, {
      positions,
      velocities,
      lifetimes,
      maxLifetimes,
      colors,
      geometry,
      points,
      config
    });
  }

  public removeEmitter(id: string): void {
    const emitter = this.emitters.get(id);
    if (!emitter) return;

    if (emitter.points.parent) {
      emitter.points.parent.remove(emitter.points);
    } else {
      this.scene.remove(emitter.points);
    }
    emitter.geometry.dispose();
    (emitter.points.material as THREE.Material).dispose();
    this.emitters.delete(id);
  }

  public clearAll(): void {
    for (const id of Array.from(this.emitters.keys())) {
      this.removeEmitter(id);
    }
  }

  public update(dt: number, meshMap?: Map<number, THREE.Mesh>): void {
    const clampedDt = Math.min(dt, 0.05);

    for (const [, emitter] of this.emitters.entries()) {
      const { positions, velocities, lifetimes, maxLifetimes, colors, geometry, config, points } = emitter;
      const count = lifetimes.length;
      const baseColor = new THREE.Color(config.color || '#ff6600');

      // Update position if attached to entity
      if (config.attachedToEntityId !== undefined && meshMap) {
        const parentMesh = meshMap.get(config.attachedToEntityId);
        if (parentMesh && points.parent !== parentMesh) {
          if (points.parent) points.parent.remove(points);
          parentMesh.add(points);
          points.position.set(0, 0, 0);
        } else if (!parentMesh && points.parent !== this.scene) {
          if (points.parent) points.parent.remove(points);
          this.scene.add(points);
          points.position.set(config.position.x, config.position.y, config.position.z);
        }
      }

      for (let i = 0; i < count; i++) {
        lifetimes[i] += clampedDt;
        if (lifetimes[i] >= maxLifetimes[i]) {
          this.resetParticle(i, config, positions, velocities, lifetimes, maxLifetimes, colors, baseColor);
          continue;
        }

        const i3 = i * 3;
        const progress = lifetimes[i] / maxLifetimes[i];

        // Position update
        positions[i3]     += velocities[i3] * clampedDt;
        positions[i3 + 1] += velocities[i3 + 1] * clampedDt;
        positions[i3 + 2] += velocities[i3 + 2] * clampedDt;

        // Type-specific motion
        if (config.type === 'fire') {
          velocities[i3 + 1] += 1.5 * clampedDt; // Buoyancy upwards
          colors[i3]     = baseColor.r * (1.0 - progress * 0.4);
          colors[i3 + 1] = baseColor.g * (1.0 - progress * 0.8);
          colors[i3 + 2] = baseColor.b * (1.0 - progress);
        } else if (config.type === 'sparks') {
          velocities[i3 + 1] -= 9.8 * clampedDt; // Gravity downwards
        } else if (config.type === 'portal') {
          // Circular vortex
          const angle = clampedDt * 3.0;
          const px = positions[i3];
          const pz = positions[i3 + 2];
          positions[i3]     = px * Math.cos(angle) - pz * Math.sin(angle);
          positions[i3 + 2] = px * Math.sin(angle) + pz * Math.cos(angle);
        } else if (config.type === 'smoke') {
          velocities[i3 + 1] += 0.8 * clampedDt;
          const gray = 0.5 * (1.0 - progress);
          colors[i3] = gray;
          colors[i3 + 1] = gray;
          colors[i3 + 2] = gray;
        }
      }

      geometry.attributes.position.needsUpdate = true;
      geometry.attributes.color.needsUpdate = true;
    }
  }

  private resetParticle(
    i: number,
    config: ParticleEmitterConfig,
    pos: Float32Array,
    vel: Float32Array,
    life: Float32Array,
    maxLife: Float32Array,
    col: Float32Array,
    baseColor: THREE.Color
  ): void {
    const i3 = i * 3;
    life[i] = 0;
    const speed = config.speed || 2.0;

    if (config.type === 'fire') {
      pos[i3]     = (Math.random() - 0.5) * 0.4;
      pos[i3 + 1] = 0;
      pos[i3 + 2] = (Math.random() - 0.5) * 0.4;
      vel[i3]     = (Math.random() - 0.5) * 0.5;
      vel[i3 + 1] = 1.0 + Math.random() * speed;
      vel[i3 + 2] = (Math.random() - 0.5) * 0.5;
      maxLife[i]  = 0.6 + Math.random() * 0.6;
    } else if (config.type === 'sparks') {
      pos[i3]     = 0;
      pos[i3 + 1] = 0.2;
      pos[i3 + 2] = 0;
      const theta = Math.random() * Math.PI * 2;
      const phi = Math.random() * Math.PI * 0.5;
      vel[i3]     = Math.cos(theta) * Math.sin(phi) * speed * 3.0;
      vel[i3 + 1] = Math.cos(phi) * speed * 4.0;
      vel[i3 + 2] = Math.sin(theta) * Math.sin(phi) * speed * 3.0;
      maxLife[i]  = 0.8 + Math.random() * 0.8;
    } else if (config.type === 'portal') {
      const radius = 0.5 + Math.random() * 1.5;
      const theta = Math.random() * Math.PI * 2;
      pos[i3]     = Math.cos(theta) * radius;
      pos[i3 + 1] = (Math.random() - 0.5) * 0.5;
      pos[i3 + 2] = Math.sin(theta) * radius;
      vel[i3]     = -Math.sin(theta) * speed;
      vel[i3 + 1] = (Math.random() - 0.5) * 0.5;
      vel[i3 + 2] = Math.cos(theta) * speed;
      maxLife[i]  = 1.5 + Math.random() * 1.0;
    } else if (config.type === 'smoke') {
      pos[i3]     = (Math.random() - 0.5) * 0.6;
      pos[i3 + 1] = 0;
      pos[i3 + 2] = (Math.random() - 0.5) * 0.6;
      vel[i3]     = (Math.random() - 0.5) * 0.4;
      vel[i3 + 1] = 0.5 + Math.random() * speed;
      vel[i3 + 2] = (Math.random() - 0.5) * 0.4;
      maxLife[i]  = 1.8 + Math.random() * 1.2;
    } else {
      // Explosion
      pos[i3]     = 0;
      pos[i3 + 1] = 0;
      pos[i3 + 2] = 0;
      const theta = Math.random() * Math.PI * 2;
      const phi = (Math.random() - 0.5) * Math.PI;
      vel[i3]     = Math.cos(theta) * Math.cos(phi) * speed * 4.0;
      vel[i3 + 1] = Math.sin(phi) * speed * 4.0;
      vel[i3 + 2] = Math.sin(theta) * Math.cos(phi) * speed * 4.0;
      maxLife[i]  = 0.5 + Math.random() * 0.5;
    }

    col[i3]     = baseColor.r;
    col[i3 + 1] = baseColor.g;
    col[i3 + 2] = baseColor.b;
  }
}
