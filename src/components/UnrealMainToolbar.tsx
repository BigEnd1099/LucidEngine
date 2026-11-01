import React, { useState } from 'react';
import {
  Play, Pause, Square, StepForward, Plus,
  ChevronDown, Check, Save, Sparkles, Code2,
  Camera, Volume2, VolumeX, Maximize2, Minimize2, Gamepad2, Layout, Download
} from 'lucide-react';
import { CameraMode } from '../types/project';

interface UnrealMainToolbarProps {
  simulating: boolean;
  connected: boolean;
  onPlay: () => void;
  onPause: () => void;
  onStop: () => void;
  onStep: () => void;
  onSave: () => void;
  onPackageGame?: () => void;
  onQuickAdd: (type: 'cube' | 'sphere' | 'player' | 'coin' | 'enemy' | 'capsule' | 'trigger' | 'fire' | 'portal') => void;
  onOpenScripts: () => void;
  onOpenEffects: () => void;
  onOpenSoundStudio?: () => void;
  onOpenWidgetStudio?: () => void;
  cameraMode?: CameraMode;
  onCameraModeChange?: (mode: CameraMode) => void;
  isAudioMuted?: boolean;
  onToggleAudioMute?: () => void;
  isImmersivePlay?: boolean;
  onToggleImmersivePlay?: () => void;
}

export const UnrealMainToolbar: React.FC<UnrealMainToolbarProps> = ({
  simulating,
  connected,
  onPlay,
  onPause,
  onStop,
  onStep,
  onSave,
  onPackageGame,
  onQuickAdd,
  onOpenScripts,
  onOpenEffects,
  onOpenSoundStudio,
  onOpenWidgetStudio,
  cameraMode = 'editor',
  onCameraModeChange,
  isAudioMuted = false,
  onToggleAudioMute,
  isImmersivePlay = false,
  onToggleImmersivePlay,
}) => {
  const [showAddMenu, setShowAddMenu] = useState(false);
  const [selectionMode, setSelectionMode] = useState('Select');
  const [showModeMenu, setShowModeMenu] = useState(false);
  const [showCameraMenu, setShowCameraMenu] = useState(false);

  const cameraModeLabels: Record<string, string> = {
    editor: 'Editor Cam',
    thirdPerson: 'TPP (Gracz)',
    topDown: 'Top-Down',
    sideScroller: 'Side View',
  };

  return (
    <div className="h-10 bg-[#13131c] border-b border-white/[0.08] px-3 flex items-center justify-between z-20 shrink-0 select-none">
      {/* ── Left side: Selection Mode & Quick Add ── */}
      <div className="flex items-center gap-2">
        {/* Selection Mode Dropdown */}
        <div className="relative">
          <button
            onClick={() => setShowModeMenu(!showModeMenu)}
            className="flex items-center gap-1.5 px-2.5 py-1 rounded bg-[#1c1c2b] hover:bg-[#252538] border border-white/10 text-xs font-medium text-slate-200 transition-colors shadow-sm"
          >
            <div className="w-2 h-2 rounded-sm bg-cyan-400" />
            <span>{selectionMode} Mode</span>
            <ChevronDown className="w-3 h-3 text-slate-400 ml-0.5" />
          </button>

          {showModeMenu && (
            <div
              className="absolute top-full left-0 mt-1 w-44 bg-[#181824] border border-white/15 rounded-md shadow-2xl py-1 z-50"
              onMouseLeave={() => setShowModeMenu(false)}
            >
              {['Select', 'Landscape', 'Foliage', 'Modeling', 'Animation'].map(m => (
                <button
                  key={m}
                  onClick={() => { setSelectionMode(m); setShowModeMenu(false); }}
                  className="w-full px-3 py-1.5 text-left text-xs text-slate-200 hover:bg-cyan-500/20 hover:text-cyan-300 flex items-center justify-between"
                >
                  <span>{m} Mode</span>
                  {selectionMode === m && <Check className="w-3 h-3 text-cyan-400" />}
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Save Button */}
        <button
          onClick={onSave}
          className="p-1.5 rounded hover:bg-white/10 text-slate-400 hover:text-white transition-colors"
          title="Save Level (Ctrl+S)"
        >
          <Save className="w-4 h-4" />
        </button>

        {/* Quick Add Actor to Project (+) */}
        <div className="relative">
          <button
            onClick={() => setShowAddMenu(!showAddMenu)}
            className="flex items-center gap-1 px-2 py-1 rounded bg-[#1c1c2b] hover:bg-[#252538] border border-white/10 text-xs font-semibold text-cyan-300 transition-colors shadow-sm"
            title="Quickly add to project (Actor, Shapes, Lights, VFX)"
          >
            <Plus className="w-3.5 h-3.5 text-cyan-400" />
            <span className="text-[11px]">Add</span>
            <ChevronDown className="w-3 h-3 text-slate-400" />
          </button>

          {showAddMenu && (
            <div
              className="absolute top-full left-0 mt-1 w-52 bg-[#181824] border border-white/15 rounded-md shadow-2xl py-1.5 z-50"
              onMouseLeave={() => setShowAddMenu(false)}
            >
              <div className="px-3 py-1 text-[9px] font-mono uppercase text-slate-500">Actors & Shapes</div>
              <button onClick={() => { onQuickAdd('cube'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Cube (StaticMeshActor)
              </button>
              <button onClick={() => { onQuickAdd('sphere'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Sphere (RigidBody)
              </button>
              <button onClick={() => { onQuickAdd('player'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Player Character (BP_Player)
              </button>
              <button onClick={() => { onQuickAdd('coin'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Coin Pickable (BP_Coin)
              </button>
              <button onClick={() => { onQuickAdd('enemy'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Enemy AI (BP_Enemy)
              </button>

              <div className="h-px bg-white/10 my-1" />
              <div className="px-3 py-1 text-[9px] font-mono uppercase text-slate-500">Visual Effects</div>
              <button onClick={() => { onQuickAdd('fire'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Fire Particle Emitter (Niagara/VFX)
              </button>
              <button onClick={() => { onQuickAdd('portal'); setShowAddMenu(false); }} className="w-full px-3 py-1 text-left text-xs hover:bg-cyan-500/20 text-slate-200">
                Portal Vortex Emitter
              </button>
            </div>
          )}
        </div>

        {/* Blueprints / QuickJS script button */}
        <button
          onClick={onOpenScripts}
          className="flex items-center gap-1.5 px-2.5 py-1 rounded hover:bg-white/10 text-slate-300 text-xs font-medium transition-colors"
          title="Open Script / Blueprint Editor"
        >
          <Code2 className="w-3.5 h-3.5 text-indigo-400" />
          <span>Blueprints / Scripts</span>
        </button>

        {/* Cinematics / VFX */}
        <button
          onClick={onOpenEffects}
          className="flex items-center gap-1.5 px-2.5 py-1 rounded hover:bg-white/10 text-slate-300 text-xs font-medium transition-colors"
          title="Open Visual Effects and Atmosphere"
        >
          <Sparkles className="w-3.5 h-3.5 text-orange-400" />
          <span>Cinematics & VFX</span>
        </button>

        {/* UI Designer (UMG) */}
        {onOpenWidgetStudio && (
          <button
            onClick={onOpenWidgetStudio}
            className="flex items-center gap-1.5 px-2.5 py-1 rounded hover:bg-white/10 text-slate-300 text-xs font-medium transition-colors"
            title="Open UMG UI Widget Blueprint Designer"
          >
            <Layout className="w-3.5 h-3.5 text-indigo-400" />
            <span>UI Designer (UMG)</span>
          </button>
        )}

        {/* Sound Cue Studio */}
        {onOpenSoundStudio && (
          <button
            onClick={onOpenSoundStudio}
            className="flex items-center gap-1.5 px-2.5 py-1 rounded hover:bg-white/10 text-slate-300 text-xs font-medium transition-colors"
            title="Open Sound Cue Synthesizer & Audio Studio"
          >
            <Volume2 className="w-3.5 h-3.5 text-amber-400" />
            <span>Sound Cue Studio</span>
          </button>
        )}

        {/* Package Full Game (.exe) */}
        {onPackageGame && (
          <button
            onClick={onPackageGame}
            className="flex items-center gap-1.5 px-2.5 py-1 rounded bg-emerald-950/70 hover:bg-emerald-900 border border-emerald-500/40 text-emerald-300 text-xs font-semibold transition-all shadow-[0_0_12px_rgba(16,185,129,0.2)] hover:shadow-[0_0_18px_rgba(16,185,129,0.4)]"
            title="Kompiluj pełną autonomiczną grę PC (.exe) z Vulkan 1.3 i Jolt Physics"
          >
            <Download className="w-3.5 h-3.5 text-emerald-400" />
            <span>Kompiluj grę (.exe)</span>
          </button>
        )}
      </div>

      {/* ── Center: Exact Unreal Engine 5 Transport Bar ── */}
      <div className="flex items-center gap-1 bg-[#1a1a27] border border-white/10 rounded-md p-0.5 shadow-inner">
        {/* Play Button (Green UE5 icon) */}
        <button
          onClick={onPlay}
          disabled={simulating}
          className={`flex items-center gap-1 px-3 py-1 rounded text-xs font-semibold transition-all ${
            simulating
              ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 shadow-sm'
              : 'text-slate-300 hover:text-emerald-400 hover:bg-emerald-500/10'
          }`}
          title="Play this level in active viewport"
        >
          <Play className="w-3.5 h-3.5 fill-current text-emerald-400" />
          <span>Play</span>
        </button>

        {/* Pause Button */}
        <button
          onClick={onPause}
          disabled={!simulating}
          className={`flex items-center gap-1 px-2.5 py-1 rounded text-xs font-semibold transition-all ${
            !simulating && connected
              ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'
              : 'text-slate-400 hover:text-amber-300 hover:bg-amber-500/10'
          }`}
          title="Pause simulation"
        >
          <Pause className="w-3.5 h-3.5 fill-current text-amber-400" />
          <span>Pause</span>
        </button>

        {/* Stop Button (Red, automatically resets scene) */}
        <button
          onClick={onStop}
          className="flex items-center gap-1 px-2.5 py-1 rounded text-xs font-semibold text-slate-300 hover:text-rose-400 hover:bg-rose-500/10 transition-all"
          title="Stop simulation and reset world to edit state"
        >
          <Square className="w-3 h-3 fill-current text-rose-400" />
          <span>Stop</span>
        </button>

        <div className="h-4 w-px bg-white/10 mx-0.5" />

        {/* Step Forward (1 Tick) */}
        <button
          onClick={onStep}
          className="p-1 rounded text-slate-400 hover:text-cyan-300 hover:bg-white/10 transition-colors"
          title="Simulate 1 step forward (16ms)"
        >
          <StepForward className="w-3.5 h-3.5" />
        </button>
      </div>

      {/* ── Right side: Camera Mode, Audio, Immersive Mode, Network & FPS ── */}
      <div className="flex items-center gap-2 text-xs">
        {/* Camera Mode Selector */}
        <div className="relative">
          <button
            onClick={() => setShowCameraMenu(!showCameraMenu)}
            className="flex items-center gap-1.5 px-2 py-1 rounded bg-[#1c1c2b] hover:bg-[#252538] border border-white/10 text-xs text-slate-200 transition-colors shadow-sm"
            title="Wybierz tryb kamery"
          >
            <Camera className="w-3.5 h-3.5 text-cyan-400" />
            <span className="font-mono text-[11px]">{cameraModeLabels[cameraMode] || 'Editor Cam'}</span>
            <ChevronDown className="w-3 h-3 text-slate-400" />
          </button>

          {showCameraMenu && (
            <div
              className="absolute top-full right-0 mt-1 w-44 bg-[#181824] border border-white/15 rounded-md shadow-2xl py-1 z-50"
              onMouseLeave={() => setShowCameraMenu(false)}
            >
              {[
                { id: 'editor' as const, label: 'Editor (Fly Cam)' },
                { id: 'thirdPerson' as const, label: 'Third Person (TPP)' },
                { id: 'topDown' as const, label: 'Top-Down (Z góry)' },
                { id: 'sideScroller' as const, label: 'Side-Scroller (2.5D)' },
              ].map(m => (
                <button
                  key={m.id}
                  onClick={() => {
                    if (onCameraModeChange) onCameraModeChange(m.id);
                    setShowCameraMenu(false);
                  }}
                  className="w-full px-3 py-1.5 text-left text-xs text-slate-200 hover:bg-cyan-500/20 hover:text-cyan-300 flex items-center justify-between"
                >
                  <span>{m.label}</span>
                  {cameraMode === m.id && <Check className="w-3 h-3 text-cyan-400" />}
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Audio Mute/Unmute */}
        {onToggleAudioMute && (
          <button
            onClick={onToggleAudioMute}
            className={`p-1.5 rounded transition-colors ${
              isAudioMuted
                ? 'bg-rose-500/20 text-rose-400 border border-rose-500/30'
                : 'hover:bg-white/10 text-slate-300 hover:text-cyan-400'
            }`}
            title={isAudioMuted ? 'Wyłącz wyciszenie SFX' : 'Wycisz dźwięki SFX'}
          >
            {isAudioMuted ? <VolumeX className="w-3.5 h-3.5" /> : <Volume2 className="w-3.5 h-3.5" />}
          </button>
        )}

        {/* Immersive Play / Fullscreen Game Mode */}
        {onToggleImmersivePlay && (
          <button
            onClick={onToggleImmersivePlay}
            className={`flex items-center gap-1 px-2 py-1 rounded text-xs font-semibold transition-all ${
              isImmersivePlay
                ? 'bg-indigo-600 text-white shadow-[0_0_12px_rgba(99,102,241,0.6)]'
                : 'hover:bg-white/10 text-slate-300 hover:text-indigo-300'
            }`}
            title="Tryb Gry / Pełny Ekran"
          >
            <Gamepad2 className="w-3.5 h-3.5 text-indigo-400" />
            <span className="hidden sm:inline text-[11px]">{isImmersivePlay ? 'Zwiń' : 'Tryb Gry'}</span>
            {isImmersivePlay ? <Minimize2 className="w-3 h-3" /> : <Maximize2 className="w-3 h-3" />}
          </button>
        )}

        <div className="h-4 w-px bg-white/10 mx-0.5" />

        <div className="flex items-center gap-1.5 px-2 py-0.5 rounded bg-black/40 border border-white/5 font-mono">
          <div className={`w-2 h-2 rounded-full ${connected ? 'bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.8)]' : 'bg-rose-500'}`} />
          <span className={connected ? 'text-emerald-400 text-[10px]' : 'text-rose-400 text-[10px]'}>
            {connected ? 'WS: 9000 ONLINE' : 'WS: DISCONNECTED'}
          </span>
        </div>
      </div>
    </div>
  );
};
