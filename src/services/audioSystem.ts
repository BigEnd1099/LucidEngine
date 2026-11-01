/**
 * Lucid Engine General Audio Subsystem & Sound Synthesizer
 * Supports custom imported audio assets (.wav, .mp3) and parametric Sound Cues with 3D spatial audio.
 */

import { SoundAsset, SoundCueConfig } from '../types/project';

export class AudioSystem {
  private static ctx: AudioContext | null = null;
  private static masterGain: GainNode | null = null;
  private static enabled: boolean = true;
  private static volume: number = 0.7;
  private static soundRegistry: Map<string, SoundAsset> = new Map();
  private static audioBufferCache: Map<string, AudioBuffer> = new Map();
  private static listenerPos = { x: 0, y: 0, z: 0 };

  public static getContext(): AudioContext | null {
    if (typeof window === 'undefined') return null;
    if (!this.ctx) {
      const AudioCtx = window.AudioContext || (window as any).webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
        this.masterGain = this.ctx.createGain();
        this.masterGain.gain.setValueAtTime(this.volume, this.ctx.currentTime);
        this.masterGain.connect(this.ctx.destination);
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    return this.ctx;
  }

  public static init(): void {
    this.getContext();
  }

  public static setListenerPosition(x: number, y: number, z: number): void {
    this.listenerPos = { x, y, z };
  }

  public static setVolume(vol: number): void {
    this.volume = Math.max(0, Math.min(1, vol));
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setValueAtTime(this.volume, this.ctx.currentTime);
    }
  }

  public static getVolume(): number {
    return this.volume;
  }

  public static setEnabled(enabled: boolean): void {
    this.enabled = enabled;
  }

  public static isEnabled(): boolean {
    return this.enabled;
  }

  public static registerSounds(sounds: SoundAsset[]): void {
    sounds.forEach(s => {
      this.soundRegistry.set(s.id, s);
      this.soundRegistry.set(s.name, s);
    });
  }

  public static registerSound(sound: SoundAsset): void {
    this.soundRegistry.set(sound.id, sound);
    this.soundRegistry.set(sound.name, sound);
  }

  /**
   * Preview a Sound Cue live in the Sound Cue Editor
   */
  public static previewCue(cue: SoundCueConfig, customVolume: number = 1.0): void {
    if (!this.enabled) return;
    const ctx = this.getContext();
    if (!ctx || !this.masterGain) return;

    this.synthesizeSoundCue(cue, customVolume, 1.0, null);
  }

  /**
   * Play any sound asset by name or ID
   */
  public static playSound(
    soundIdentifier: string,
    options: {
      volume?: number;
      pitch?: number;
      position?: { x: number; y: number; z: number };
    } = {}
  ): void {
    if (!this.enabled) return;
    const ctx = this.getContext();
    if (!ctx || !this.masterGain) return;

    const vol = options.volume ?? 1.0;
    const pitch = options.pitch ?? 1.0;
    const sound = this.soundRegistry.get(soundIdentifier);

    if (sound) {
      if (sound.type === 'synth_cue' && sound.cueConfig) {
        this.synthesizeSoundCue(sound.cueConfig, vol, pitch, options.position || null);
        return;
      } else if (sound.type === 'audio_file' && sound.audioBase64) {
        this.playAudioFile(sound.audioBase64, vol, pitch, options.position || null);
        return;
      }
    }

    // Fallback if built-in name passed (for tests and backwards compatibility)
    this.playFallbackSfx(soundIdentifier, vol);
  }

  /**
   * Synthesizes parametric audio based on SoundCueConfig
   */
  private static synthesizeSoundCue(
    cfg: SoundCueConfig,
    volume: number,
    pitch: number,
    pos: { x: number; y: number; z: number } | null
  ): void {
    const ctx = this.getContext();
    if (!ctx || !this.masterGain) return;

    const t = ctx.currentTime;
    const gainNode = ctx.createGain();
    const finalVol = Math.max(0, Math.min(1, cfg.volume * volume));

    // 3D Spatial Audio Attenuation
    let spatialVol = 1.0;
    if (pos) {
      const dx = pos.x - this.listenerPos.x;
      const dy = pos.y - this.listenerPos.y;
      const dz = pos.z - this.listenerPos.z;
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
      spatialVol = 1.0 / (1.0 + dist * 0.08);
    }

    gainNode.gain.setValueAtTime(0, t);
    const attackEnd = t + Math.max(0.005, cfg.attack);
    gainNode.gain.linearRampToValueAtTime(finalVol * spatialVol, attackEnd);

    const decayEnd = attackEnd + Math.max(0.01, cfg.decay);
    const sustainLevel = Math.max(0.001, finalVol * spatialVol * cfg.sustain);
    gainNode.gain.linearRampToValueAtTime(sustainLevel, decayEnd);

    const releaseEnd = decayEnd + Math.max(0.01, cfg.release);
    gainNode.gain.exponentialRampToValueAtTime(0.0001, releaseEnd);

    gainNode.connect(this.masterGain);

    if (cfg.waveType === 'noise') {
      // White / pink noise generator
      const bufferSize = ctx.sampleRate * Math.max(0.1, cfg.attack + cfg.decay + cfg.release);
      const buffer = ctx.createBuffer(1, bufferSize, ctx.sampleRate);
      const data = buffer.getChannelData(0);
      for (let i = 0; i < bufferSize; i++) {
        data[i] = Math.random() * 2 - 1;
      }
      const noise = ctx.createBufferSource();
      noise.buffer = buffer;
      noise.connect(gainNode);
      noise.start(t);
      noise.stop(releaseEnd);
    } else {
      const osc = ctx.createOscillator();
      osc.type = cfg.waveType;
      const startF = Math.max(20, Math.min(10000, cfg.startFreq * pitch));
      const endF = Math.max(20, Math.min(10000, cfg.endFreq * pitch));
      osc.frequency.setValueAtTime(startF, t);
      osc.frequency.exponentialRampToValueAtTime(endF, releaseEnd);

      osc.connect(gainNode);
      osc.start(t);
      osc.stop(releaseEnd);
    }
  }

  /**
   * Plays an imported base64 audio file
   */
  private static async playAudioFile(
    base64: string,
    volume: number,
    pitch: number,
    pos: { x: number; y: number; z: number } | null
  ): Promise<void> {
    const ctx = this.getContext();
    if (!ctx || !this.masterGain) return;

    try {
      let buffer = this.audioBufferCache.get(base64);
      if (!buffer) {
        const cleanBase64 = base64.includes(',') ? base64.split(',')[1] : base64;
        const binary = atob(cleanBase64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) {
          bytes[i] = binary.charCodeAt(i);
        }
        buffer = await ctx.decodeAudioData(bytes.buffer);
        this.audioBufferCache.set(base64, buffer);
      }

      const src = ctx.createBufferSource();
      src.buffer = buffer;
      src.playbackRate.value = pitch;

      const gain = ctx.createGain();
      let spatialVol = 1.0;
      if (pos) {
        const dx = pos.x - this.listenerPos.x;
        const dy = pos.y - this.listenerPos.y;
        const dz = pos.z - this.listenerPos.z;
        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
        spatialVol = 1.0 / (1.0 + dist * 0.08);
      }
      gain.gain.setValueAtTime(volume * spatialVol, ctx.currentTime);

      src.connect(gain);
      gain.connect(this.masterGain);
      src.start();
    } catch (e) {
      console.warn('Failed to play audio file:', e);
    }
  }

  /**
   * Backwards compatibility for tests & preset calls
   */
  public static playSfx(type: string, customVolume: number = 1.0): void {
    this.playSound(type, { volume: customVolume });
  }

  private static playFallbackSfx(type: string, customVolume: number): void {
    const ctx = this.getContext();
    if (!ctx || !this.masterGain) return;
    const t = ctx.currentTime;
    const gainNode = ctx.createGain();
    gainNode.gain.setValueAtTime(customVolume, t);
    gainNode.connect(this.masterGain);

    const name = type.toLowerCase();
    if (name.includes('jump')) {
      const osc = ctx.createOscillator();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(140, t);
      osc.frequency.exponentialRampToValueAtTime(520, t + 0.15);
      gainNode.gain.exponentialRampToValueAtTime(0.01, t + 0.18);
      osc.connect(gainNode);
      osc.start(t);
      osc.stop(t + 0.18);
    } else if (name.includes('coin') || name.includes('bell')) {
      const osc = ctx.createOscillator();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(987.77, t);
      osc.frequency.setValueAtTime(1318.51, t + 0.08);
      gainNode.gain.exponentialRampToValueAtTime(0.01, t + 0.35);
      osc.connect(gainNode);
      osc.start(t);
      osc.stop(t + 0.35);
    } else if (name.includes('laser') || name.includes('shoot')) {
      const osc = ctx.createOscillator();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(880, t);
      osc.frequency.exponentialRampToValueAtTime(110, t + 0.12);
      gainNode.gain.exponentialRampToValueAtTime(0.01, t + 0.14);
      osc.connect(gainNode);
      osc.start(t);
      osc.stop(t + 0.14);
    } else {
      const osc = ctx.createOscillator();
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(220, t);
      osc.frequency.exponentialRampToValueAtTime(60, t + 0.2);
      gainNode.gain.exponentialRampToValueAtTime(0.01, t + 0.25);
      osc.connect(gainNode);
      osc.start(t);
      osc.stop(t + 0.25);
    }
  }
}

export const audioSystem = AudioSystem;
