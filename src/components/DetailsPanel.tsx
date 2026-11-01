import React, { useState } from 'react';
import {
  Sliders, Plus, RotateCcw, Box, Lock, Unlock, Rocket,
  Palette, FileCode, Globe, Sun, Search, ChevronDown, ChevronRight,
  Volume2, Camera, User, Play, Monitor
} from 'lucide-react';
import { EntityData, MaterialAsset, ProjectSettings, SoundAsset } from '../types/project';
import { AudioSystem } from '../services/audioSystem';

function toHexColor(col: any, fallback = '#38bdf8'): string {
  if (typeof col === 'string') {
    if (col.startsWith('#')) return col.length === 7 ? col : col.slice(0, 7);
    return fallback;
  }
  if (Array.isArray(col) && col.length >= 3) {
    const r = Math.round(Math.min(1, Math.max(0, col[0])) * 255).toString(16).padStart(2, '0');
    const g = Math.round(Math.min(1, Math.max(0, col[1])) * 255).toString(16).padStart(2, '0');
    const b = Math.round(Math.min(1, Math.max(0, col[2])) * 255).toString(16).padStart(2, '0');
    return `#${r}${g}${b}`;
  }
  return fallback;
}

interface DetailsPanelProps {
  selectedEntity: EntityData | undefined;
  materials: MaterialAsset[];
  sounds?: SoundAsset[];
  settings: ProjectSettings;
  onUpdateProperty: (id: number, patch: any) => void;
  onApplyMaterial: (id: number, mat: MaterialAsset) => void;
  onApplyImpulse: (id: number, x: number, y: number, z: number) => void;
  onUpdateSettings: (patch: Partial<ProjectSettings>) => void;
  onOpenScriptEditor: (scriptName?: string) => void;
}

export const DetailsPanel: React.FC<DetailsPanelProps> = ({
  selectedEntity,
  materials,
  sounds = [],
  settings,
  onUpdateProperty,
  onApplyMaterial,
  onApplyImpulse,
  onUpdateSettings,
  onOpenScriptEditor,
}) => {
  const [activeTab, setActiveTab] = useState<'details' | 'world'>('details');
  const [lockScale, setLockScale] = useState(false);
  const [filterCategory, setFilterCategory] = useState<'All' | 'General' | 'Actor' | 'LOD' | 'Misc' | 'Physics' | 'Rendering' | 'Streaming'>('All');
  const [search, setSearch] = useState('');
  const [showAddComponentMenu, setShowAddComponentMenu] = useState(false);

  // Collapsible section states
  const [openSections, setOpenSections] = useState<Record<string, boolean>>({
    transform: true,
    staticMesh: true,
    materials: true,
    physics: true,
    movement: true,
    audio: true,
    cameraBoom: true,
    scripts: true,
    light: true,
  });

  const toggleSection = (s: string) => {
    setOpenSections(prev => ({ ...prev, [s]: !prev[s] }));
  };

  const entityTag = selectedEntity?.tag || (selectedEntity ? `Entity_${selectedEntity.id}` : 'None');

  return (
    <div className="flex-1 flex flex-col bg-[#111119] border-t border-white/[0.08] select-none overflow-hidden h-full">
      {/* 1. Header Tabs: Details | World Settings (Exact UE5) */}
      <div className="h-7 bg-[#0b0b10] border-b border-white/[0.08] flex items-center justify-between px-2 text-[11px] font-sans shrink-0">
        <div className="flex items-center gap-1">
          <div
            onClick={() => setActiveTab('details')}
            className={`flex items-center gap-1.5 px-3 py-1 rounded-t-sm cursor-pointer transition-colors ${
              activeTab === 'details'
                ? 'bg-[#181824] text-white border-t-2 border-cyan-400 font-semibold'
                : 'text-slate-400 hover:text-white'
            }`}
          >
            <Sliders className="w-3 h-3 text-cyan-400" />
            <span>Details</span>
            <span className="text-[10px] text-slate-500 hover:text-white ml-1">✕</span>
          </div>

          <div
            onClick={() => setActiveTab('world')}
            className={`flex items-center gap-1.5 px-3 py-1 rounded-t-sm cursor-pointer transition-colors ${
              activeTab === 'world'
                ? 'bg-[#181824] text-white border-t-2 border-cyan-400 font-semibold'
                : 'text-slate-400 hover:text-white'
            }`}
          >
            <Globe className="w-3 h-3 text-indigo-400" />
            <span>World Settings</span>
          </div>
        </div>
      </div>

      {/* ── TAB 1: ACTOR DETAILS ── */}
      {activeTab === 'details' && (
        <div className="flex-1 overflow-y-auto flex flex-col">
          {selectedEntity ? (
            <div className="p-2 space-y-2">
              {/* Actor Header Card */}
              <div className="p-2 rounded bg-[#161622] border border-white/[0.08] space-y-1.5">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-1.5 flex-1 truncate">
                    <span className="text-[11px] font-bold text-white truncate">{entityTag}</span>
                    <span className="text-[9px] font-mono text-cyan-400">#{selectedEntity.id}</span>
                  </div>

                  <div className="relative shrink-0">
                    {/* Add Component Green Button (Exact UE5) */}
                    <button
                      onClick={() => setShowAddComponentMenu(!showAddComponentMenu)}
                      className="flex items-center gap-1 px-2 py-0.5 rounded bg-emerald-600 hover:bg-emerald-500 text-white font-semibold text-[10px] shadow-sm transition-colors"
                      title="Add Component to Actor"
                    >
                      <Plus className="w-3 h-3" />
                      <span>+ Add</span>
                    </button>

                    {showAddComponentMenu && (
                      <div
                        className="absolute right-0 top-full mt-1 w-56 bg-[#161622] border border-white/15 rounded-md shadow-2xl py-1 z-50 text-[10px]"
                        onMouseLeave={() => setShowAddComponentMenu(false)}
                      >
                        <div className="px-2 py-1 font-mono uppercase text-[9px] text-slate-500">Dodaj Komponent</div>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { rigidBody: { mass: 1.0, shapeType: 0, restitution: 0.6, friction: 0.3 } });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <Rocket className="w-3 h-3 text-amber-400" />
                          <span>Jolt RigidBody Component</span>
                        </button>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { movement: { maxWalkSpeed: 12.0, jumpZVelocity: 12.0, gravityScale: 1.0, airControl: 0.35 } });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <User className="w-3 h-3 text-emerald-400" />
                          <span>Character Movement Component</span>
                        </button>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { audio: { soundAssetId: sounds[0]?.id || 'default_sound', volume: 0.8, pitch: 1.0, is3D: true, loop: false, autoPlay: false } });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <Volume2 className="w-3 h-3 text-amber-400" />
                          <span>Audio Component (Sound Cue)</span>
                        </button>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { cameraBoom: { targetArmLength: 12.0, socketOffsetY: 4.0, cameraLag: true, lagSpeed: 0.08 } });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <Camera className="w-3 h-3 text-cyan-400" />
                          <span>Camera Boom / Spring Arm</span>
                        </button>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { scriptName: 'CustomBehavior.js' });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <FileCode className="w-3 h-3 text-indigo-400" />
                          <span>QuickJS Script Component</span>
                        </button>
                        <button
                          onClick={() => {
                            onUpdateProperty(selectedEntity.id, { light: { type: 'point', color: '#ffaa44', intensity: 3.0, range: 15.0 } });
                            setShowAddComponentMenu(false);
                          }}
                          className="w-full px-2.5 py-1 text-left text-slate-200 hover:bg-cyan-500/20 flex items-center gap-1.5"
                        >
                          <Sun className="w-3 h-3 text-yellow-300" />
                          <span>Point Light Component</span>
                        </button>
                      </div>
                    )}
                  </div>
                </div>

                {/* Component Hierarchy Pill */}
                <div className="flex flex-col gap-1 p-1.5 rounded bg-[#0e0e16] border border-white/5 text-[10px] font-mono text-slate-300">
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-1.5 truncate">
                      <Box className="w-3.5 h-3.5 text-cyan-400" />
                      <span className="text-white font-medium truncate">{entityTag} (Instance)</span>
                      <span className="text-slate-500">→ StaticMesh</span>
                    </div>
                    <button
                      onClick={() => onOpenScriptEditor(selectedEntity.scriptName)}
                      className="text-[9px] text-cyan-400 hover:underline hover:text-cyan-300 shrink-0 ml-1"
                    >
                      Edit Script
                    </button>
                  </div>
                  {/* Additional Attached Components Badges */}
                  <div className="flex flex-wrap gap-1 pt-0.5 text-[9px]">
                    {selectedEntity.rigidBody && (
                      <span className="px-1.5 py-0.2 rounded bg-amber-500/20 text-amber-300 border border-amber-500/30">
                        JoltBody ({selectedEntity.rigidBody.mass === 0 ? 'Stationary' : 'Dynamic'})
                      </span>
                    )}
                    {selectedEntity.movement && (
                      <span className="px-1.5 py-0.2 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/30">
                        Movement
                      </span>
                    )}
                    {selectedEntity.audio && (
                      <span className="px-1.5 py-0.2 rounded bg-amber-400/20 text-amber-200 border border-amber-400/30">
                        Audio
                      </span>
                    )}
                    {selectedEntity.cameraBoom && (
                      <span className="px-1.5 py-0.2 rounded bg-cyan-500/20 text-cyan-200 border border-cyan-500/30">
                        SpringArm
                      </span>
                    )}
                    {selectedEntity.light && (
                      <span className="px-1.5 py-0.2 rounded bg-yellow-500/20 text-yellow-200 border border-yellow-500/30">
                        Light ({selectedEntity.light.type})
                      </span>
                    )}
                  </div>
                </div>
              </div>

              {/* Search details */}
              <div className="flex items-center gap-1.5 bg-[#07070c] border border-white/10 rounded px-2 py-0.5">
                <Search className="w-3 h-3 text-slate-500" />
                <input
                  type="text"
                  placeholder="Search Details..."
                  value={search}
                  onChange={e => setSearch(e.target.value)}
                  className="w-full bg-transparent text-[11px] text-white focus:outline-none placeholder-slate-600 font-sans"
                />
              </div>

              {/* Category Filter Pills (Exact UE5: General | Actor | LOD | Misc | Physics | Rendering | Streaming | All) */}
              <div className="flex items-center gap-1 overflow-x-auto py-0.5 text-[9px] font-medium text-slate-400 scrollbar-none">
                {(['General', 'Actor', 'LOD', 'Misc', 'Physics', 'Rendering', 'Streaming', 'All'] as const).map(cat => (
                  <button
                    key={cat}
                    onClick={() => setFilterCategory(cat)}
                    className={`px-2 py-0.5 rounded transition-colors whitespace-nowrap ${
                      filterCategory === cat
                        ? 'bg-[#005fb8] text-white font-semibold shadow-sm'
                        : 'bg-[#181822] hover:bg-white/10 text-slate-400'
                    }`}
                  >
                    {cat}
                  </button>
                ))}
              </div>

              {/* ── 1. TRANSFORM SECTION ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('transform')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.transform ? <ChevronDown className="w-3 h-3 text-slate-400" /> : <ChevronRight className="w-3 h-3 text-slate-400" />}
                    <span>Transform</span>
                  </div>
                  <button
                    onClick={(e) => {
                      e.stopPropagation();
                      onUpdateProperty(selectedEntity.id, {
                        transform: { x: 0, y: 0.5, z: 0, rx: 0, ry: 0, rz: 0, sx: 1, sy: 1, sz: 1 }
                      });
                    }}
                    className="p-0.5 text-slate-400 hover:text-white"
                    title="Reset Transform"
                  >
                    <RotateCcw className="w-3 h-3" />
                  </button>
                </div>

                {openSections.transform && (
                  <div className="p-2 space-y-2 text-[10px]">
                    {/* Location */}
                    <div className="space-y-0.5">
                      <div className="flex items-center justify-between text-slate-400">
                        <span>Location</span>
                        <span className="text-[9px] font-mono text-slate-500">cm / m</span>
                      </div>
                      <div className="grid grid-cols-3 gap-1">
                        {[
                          { axis: 'x' as const, label: 'X', color: 'bg-rose-500/20 text-rose-300 border-rose-500/40' },
                          { axis: 'y' as const, label: 'Y', color: 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40' },
                          { axis: 'z' as const, label: 'Z', color: 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' },
                        ].map(({ axis, label, color }) => (
                          <div key={axis} className="flex items-center bg-[#0d0d14] border border-white/10 rounded overflow-hidden">
                            <span className={`w-4 text-center text-[9px] font-bold border-r ${color}`}>{label}</span>
                            <input
                              type="number"
                              step="0.5"
                              value={Number(((selectedEntity.transform?.[axis]) ?? 0).toFixed(2))}
                              onChange={e => onUpdateProperty(selectedEntity.id, {
                                transform: { ...(selectedEntity.transform || { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0, sx: 1, sy: 1, sz: 1 }), [axis]: parseFloat(e.target.value) || 0 }
                              })}
                              className="w-full bg-transparent px-1 py-0.5 text-[10px] font-mono text-white focus:outline-none"
                            />
                          </div>
                        ))}
                      </div>
                    </div>

                    {/* Rotation */}
                    <div className="space-y-0.5">
                      <div className="flex items-center justify-between text-slate-400">
                        <span>Rotation</span>
                        <span className="text-[9px] font-mono text-slate-500">deg (°)</span>
                      </div>
                      <div className="grid grid-cols-3 gap-1">
                        {[
                          { axis: 'rx' as const, label: 'X', color: 'bg-rose-500/20 text-rose-300 border-rose-500/40' },
                          { axis: 'ry' as const, label: 'Y', color: 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40' },
                          { axis: 'rz' as const, label: 'Z', color: 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' },
                        ].map(({ axis, label, color }) => (
                          <div key={axis} className="flex items-center bg-[#0d0d14] border border-white/10 rounded overflow-hidden">
                            <span className={`w-4 text-center text-[9px] font-bold border-r ${color}`}>{label}</span>
                            <input
                              type="number"
                              step="5"
                              value={Number(((selectedEntity.transform?.[axis]) ?? 0).toFixed(1))}
                              onChange={e => onUpdateProperty(selectedEntity.id, {
                                transform: { ...(selectedEntity.transform || { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0, sx: 1, sy: 1, sz: 1 }), [axis]: parseFloat(e.target.value) || 0 }
                              })}
                              className="w-full bg-transparent px-1 py-0.5 text-[10px] font-mono text-white focus:outline-none"
                            />
                          </div>
                        ))}
                      </div>
                    </div>

                    {/* Scale */}
                    <div className="space-y-0.5">
                      <div className="flex items-center justify-between text-slate-400">
                        <div className="flex items-center gap-1">
                          <span>Scale</span>
                          <button
                            onClick={() => setLockScale(!lockScale)}
                            className="p-0.5 rounded text-slate-400 hover:text-white"
                            title={lockScale ? 'Scale Locked' : 'Scale Free'}
                          >
                            {lockScale ? <Lock className="w-2.5 h-2.5 text-cyan-400" /> : <Unlock className="w-2.5 h-2.5" />}
                          </button>
                        </div>
                      </div>
                      <div className="grid grid-cols-3 gap-1">
                        {[
                          { axis: 'sx' as const, label: 'X', color: 'bg-rose-500/20 text-rose-300 border-rose-500/40' },
                          { axis: 'sy' as const, label: 'Y', color: 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40' },
                          { axis: 'sz' as const, label: 'Z', color: 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' },
                        ].map(({ axis, label, color }) => (
                          <div key={axis} className="flex items-center bg-[#0d0d14] border border-white/10 rounded overflow-hidden">
                            <span className={`w-4 text-center text-[9px] font-bold border-r ${color}`}>{label}</span>
                            <input
                              type="number"
                              step="0.1"
                              value={Number(((selectedEntity.transform?.[axis]) ?? 1).toFixed(2))}
                              onChange={e => {
                                const val = parseFloat(e.target.value) || 1;
                                const base = selectedEntity.transform || { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0, sx: 1, sy: 1, sz: 1 };
                                if (lockScale) {
                                  onUpdateProperty(selectedEntity.id, {
                                    transform: { ...base, sx: val, sy: val, sz: val }
                                  });
                                } else {
                                  onUpdateProperty(selectedEntity.id, {
                                    transform: { ...base, [axis]: val }
                                  });
                                }
                              }}
                              className="w-full bg-transparent px-1 py-0.5 text-[10px] font-mono text-white focus:outline-none"
                            />
                          </div>
                        ))}
                      </div>
                    </div>

                    {/* Mobility: Static | Stationary | Movable */}
                    <div className="pt-1">
                      <div className="text-slate-400 mb-1">Mobility</div>
                      <div className="grid grid-cols-3 gap-1 text-[10px] font-medium text-center">
                        <button
                          onClick={() => onUpdateProperty(selectedEntity.id, { rigidBody: undefined })}
                          className={`py-1 rounded border transition-colors ${
                            !selectedEntity.rigidBody
                              ? 'bg-[#005fb8] text-white border-[#005fb8] font-semibold'
                              : 'bg-white/5 text-slate-400 border-white/5 hover:bg-white/10'
                          }`}
                          title="Statyczny obiekt bez symulacji fizyki"
                        >
                          Static
                        </button>
                        <button
                          onClick={() => onUpdateProperty(selectedEntity.id, { rigidBody: { mass: 0, shapeType: selectedEntity.rigidBody?.shapeType ?? 0 } })}
                          className={`py-1 rounded border transition-colors ${
                            selectedEntity.rigidBody && selectedEntity.rigidBody.mass === 0
                              ? 'bg-amber-600 text-white border-amber-500 font-semibold'
                              : 'bg-white/5 text-slate-400 border-white/5 hover:bg-white/10'
                          }`}
                          title="Kinematyczny lub nieruchomy kolider fizyczny (Masa 0)"
                        >
                          Stationary
                        </button>
                        <button
                          onClick={() => onUpdateProperty(selectedEntity.id, { rigidBody: { mass: (selectedEntity.rigidBody?.mass && selectedEntity.rigidBody.mass > 0) ? selectedEntity.rigidBody.mass : 1.0, shapeType: selectedEntity.rigidBody?.shapeType ?? 0 } })}
                          className={`py-1 rounded border transition-colors ${
                            selectedEntity.rigidBody && selectedEntity.rigidBody.mass > 0
                              ? 'bg-emerald-600 text-white border-emerald-500 font-semibold'
                              : 'bg-white/5 text-slate-400 border-white/5 hover:bg-white/10'
                          }`}
                          title="Dynamiczny obiekt Jolt Physics z pełną symulacją masy i prędkości"
                        >
                          Movable
                        </button>
                      </div>
                    </div>
                  </div>
                )}
              </div>

              {/* ── 2. STATIC MESH SECTION ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('staticMesh')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.staticMesh ? <ChevronDown className="w-3 h-3 text-slate-400" /> : <ChevronRight className="w-3 h-3 text-slate-400" />}
                    <span>Static Mesh</span>
                  </div>
                </div>

                {openSections.staticMesh && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center gap-2 bg-[#0e0e16] p-1.5 rounded border border-white/5">
                      <div className="w-10 h-10 rounded bg-[#1c1c28] border border-white/10 flex items-center justify-center shrink-0">
                        <Box className="w-5 h-5 text-cyan-400" />
                      </div>
                      <div className="flex-1 truncate">
                        <div className="text-white font-medium text-[11px] truncate">
                          {selectedEntity.mesh?.meshId === 1 ? 'Plane_Mesh' :
                           selectedEntity.rigidBody?.shapeType === 1 ? 'Sphere_Mesh' : 'Cube_Mesh'}
                        </div>
                        <div className="text-[9px] text-slate-500">Asset: /Game/Meshes/Default</div>
                      </div>
                    </div>
                  </div>
                )}
              </div>

              {/* ── 3. MATERIALS SECTION ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('materials')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.materials ? <ChevronDown className="w-3 h-3 text-slate-400" /> : <ChevronRight className="w-3 h-3 text-slate-400" />}
                    <span>Materials</span>
                  </div>
                </div>

                {openSections.materials && (
                  <div className="p-2 space-y-2 text-[10px]">
                    <div className="text-slate-400 text-[9px] uppercase tracking-wider font-mono">Element 0 Material</div>
                    <div className="flex items-center gap-2 bg-[#0e0e16] p-1.5 rounded border border-white/5">
                      <div className="w-9 h-9 rounded-full bg-gradient-to-tr from-cyan-600 to-indigo-700 border border-white/20 flex items-center justify-center shadow-inner shrink-0">
                        <Palette className="w-4 h-4 text-white" />
                      </div>
                      <div className="flex-1">
                        <select
                          value={selectedEntity.mesh?.materialId ? String(selectedEntity.mesh.materialId) : materials[0]?.id}
                          onChange={e => {
                            const found = materials.find(m => m.id === e.target.value);
                            if (found) onApplyMaterial(selectedEntity.id, found);
                          }}
                          className="w-full bg-[#1c1c2b] border border-white/10 rounded px-1.5 py-1 text-white text-[10px] focus:outline-none focus:border-cyan-500"
                        >
                          {materials.map(m => (
                            <option key={m.id} value={m.id}>{m.name}</option>
                          ))}
                        </select>
                      </div>
                    </div>

                    {/* Live PBR parameters */}
                    <div className="pt-2 border-t border-white/5 space-y-1.5">
                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Base Color</span>
                        <input
                          type="color"
                          value={toHexColor(selectedEntity.material?.color, '#38bdf8')}
                          onChange={e => {
                            const newMat: MaterialAsset = {
                              id: `custom_${selectedEntity.id}`,
                              name: 'Instance_PBR',
                              color: e.target.value,
                              roughness: selectedEntity.material?.roughness ?? 0.4,
                              metallic: selectedEntity.material?.metallic ?? 0.2,
                              emissive: selectedEntity.material?.emissive,
                              emissiveIntensity: selectedEntity.material?.emissiveIntensity
                            };
                            onApplyMaterial(selectedEntity.id, newMat);
                          }}
                          className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                        />
                      </div>
                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Roughness</span>
                        <input
                          type="range"
                          min="0"
                          max="1"
                          step="0.05"
                          value={selectedEntity.material?.roughness ?? 0.4}
                          onChange={e => {
                            const val = parseFloat(e.target.value);
                            const newMat: MaterialAsset = {
                              id: `custom_${selectedEntity.id}`,
                              name: 'Instance_PBR',
                              color: selectedEntity.material?.color || '#38bdf8',
                              roughness: val,
                              metallic: selectedEntity.material?.metallic ?? 0.2,
                            };
                            onApplyMaterial(selectedEntity.id, newMat);
                          }}
                          className="w-24 accent-cyan-400"
                        />
                      </div>
                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Metallic</span>
                        <input
                          type="range"
                          min="0"
                          max="1"
                          step="0.05"
                          value={selectedEntity.material?.metallic ?? 0.2}
                          onChange={e => {
                            const val = parseFloat(e.target.value);
                            const newMat: MaterialAsset = {
                              id: `custom_${selectedEntity.id}`,
                              name: 'Instance_PBR',
                              color: selectedEntity.material?.color || '#38bdf8',
                              roughness: selectedEntity.material?.roughness ?? 0.4,
                              metallic: val,
                            };
                            onApplyMaterial(selectedEntity.id, newMat);
                          }}
                          className="w-24 accent-indigo-400"
                        />
                      </div>
                    </div>
                  </div>
                )}
              </div>

              {/* ── 4. PHYSICS (JOLT) SECTION ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('physics')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.physics ? <ChevronDown className="w-3 h-3 text-amber-400" /> : <ChevronRight className="w-3 h-3 text-amber-400" />}
                    <span>Physics (Jolt 5.0)</span>
                  </div>
                  <Rocket className="w-3 h-3 text-amber-400" />
                </div>

                {openSections.physics && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center justify-between">
                      <span className="text-slate-300">Simulate Physics</span>
                      <input
                        type="checkbox"
                        checked={!!selectedEntity.rigidBody}
                        onChange={e => {
                          if (e.target.checked) {
                            onUpdateProperty(selectedEntity.id, { rigidBody: { mass: 1.0, shapeType: 0 } });
                          } else {
                            onUpdateProperty(selectedEntity.id, { rigidBody: undefined });
                          }
                        }}
                        className="accent-cyan-400 rounded cursor-pointer"
                      />
                    </div>

                    {selectedEntity.rigidBody && (
                      <div className="space-y-2 pt-1 border-t border-white/5">
                        {/* Shape Type */}
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Collider Shape</span>
                          <select
                            value={selectedEntity.rigidBody.shapeType ?? 0}
                            onChange={e => {
                              const rb = selectedEntity.rigidBody;
                              if (!rb) return;
                              const val = parseInt(e.target.value);
                              onUpdateProperty(selectedEntity.id, {
                                rigidBody: {
                                  ...rb,
                                  shapeType: val,
                                  capsuleRadius: val === 2 ? (rb.capsuleRadius || 0.6) : rb.capsuleRadius,
                                  capsuleHalfHeight: val === 2 ? (rb.capsuleHalfHeight || 0.8) : rb.capsuleHalfHeight,
                                }
                              });
                            }}
                            className="bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 font-mono text-cyan-300 text-[10px] focus:outline-none"
                          >
                            <option value={0}>Box Collider</option>
                            <option value={1}>Sphere Collider</option>
                            <option value={2}>Capsule (Player)</option>
                          </select>
                        </div>

                        {/* Capsule specific options */}
                        {selectedEntity.rigidBody.shapeType === 2 && (
                          <div className="p-1.5 rounded bg-[#0d0d14] border border-cyan-500/20 space-y-1">
                            <div className="text-[9px] font-mono text-cyan-400 font-bold uppercase">Capsule Dimensions</div>
                            <div className="flex items-center justify-between">
                              <span className="text-slate-400">Radius</span>
                              <input
                                type="number"
                                step="0.1"
                                value={selectedEntity.rigidBody.capsuleRadius ?? 0.6}
                                onChange={e => onUpdateProperty(selectedEntity.id, {
                                  rigidBody: { ...selectedEntity.rigidBody, capsuleRadius: parseFloat(e.target.value) || 0.6 }
                                })}
                                className="w-16 bg-[#161622] border border-white/10 rounded px-1 py-0.5 text-right font-mono text-cyan-300"
                              />
                            </div>
                            <div className="flex items-center justify-between">
                              <span className="text-slate-400">Half Height</span>
                              <input
                                type="number"
                                step="0.1"
                                value={selectedEntity.rigidBody.capsuleHalfHeight ?? 0.8}
                                onChange={e => onUpdateProperty(selectedEntity.id, {
                                  rigidBody: { ...selectedEntity.rigidBody, capsuleHalfHeight: parseFloat(e.target.value) || 0.8 }
                                })}
                                className="w-16 bg-[#161622] border border-white/10 rounded px-1 py-0.5 text-right font-mono text-cyan-300"
                              />
                            </div>
                          </div>
                        )}

                        {/* Sensor / Trigger volume */}
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Is Trigger / Sensor</span>
                          <input
                            type="checkbox"
                            checked={!!selectedEntity.rigidBody.isSensor}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              rigidBody: { ...selectedEntity.rigidBody, isSensor: e.target.checked }
                            })}
                            className="accent-amber-400 rounded cursor-pointer"
                          />
                        </div>

                        {/* Rotation Locks (DOFs) */}
                        <div className="space-y-1 pt-1 border-t border-white/5">
                          <span className="text-slate-400 block text-[9px] uppercase tracking-wider font-mono">Lock Rotations (Character)</span>
                          <div className="grid grid-cols-3 gap-1">
                            <label className="flex items-center gap-1 text-[9px] text-slate-300 cursor-pointer">
                              <input
                                type="checkbox"
                                checked={!!selectedEntity.rigidBody.lockRotationX}
                                onChange={e => onUpdateProperty(selectedEntity.id, {
                                  rigidBody: { ...selectedEntity.rigidBody, lockRotationX: e.target.checked }
                                })}
                                className="accent-cyan-400 rounded"
                              />
                              <span>X</span>
                            </label>
                            <label className="flex items-center gap-1 text-[9px] text-slate-300 cursor-pointer">
                              <input
                                type="checkbox"
                                checked={!!selectedEntity.rigidBody.lockRotationY}
                                onChange={e => onUpdateProperty(selectedEntity.id, {
                                  rigidBody: { ...selectedEntity.rigidBody, lockRotationY: e.target.checked }
                                })}
                                className="accent-cyan-400 rounded"
                              />
                              <span>Y</span>
                            </label>
                            <label className="flex items-center gap-1 text-[9px] text-slate-300 cursor-pointer">
                              <input
                                type="checkbox"
                                checked={!!selectedEntity.rigidBody.lockRotationZ}
                                onChange={e => onUpdateProperty(selectedEntity.id, {
                                  rigidBody: { ...selectedEntity.rigidBody, lockRotationZ: e.target.checked }
                                })}
                                className="accent-cyan-400 rounded"
                              />
                              <span>Z</span>
                            </label>
                          </div>
                        </div>

                        {/* Restitution (Bounciness) */}
                        <div className="flex items-center justify-between pt-1">
                          <span className="text-slate-400">Restitution (Bounciness)</span>
                          <input
                            type="range"
                            min="0"
                            max="1"
                            step="0.05"
                            value={selectedEntity.rigidBody.restitution ?? 0.6}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              rigidBody: { ...selectedEntity.rigidBody, restitution: parseFloat(e.target.value) }
                            })}
                            className="w-24 accent-amber-400"
                          />
                        </div>

                        {/* Friction */}
                        <div className="flex items-center justify-between pt-1">
                          <span className="text-slate-400">Friction</span>
                          <input
                            type="range"
                            min="0"
                            max="1"
                            step="0.05"
                            value={selectedEntity.rigidBody.friction ?? 0.3}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              rigidBody: { ...selectedEntity.rigidBody, friction: parseFloat(e.target.value) }
                            })}
                            className="w-24 accent-cyan-400"
                          />
                        </div>

                        {/* Mass (kg) */}
                        <div className="flex items-center justify-between pt-1">
                          <span className="text-slate-400">Mass (kg)</span>
                          <input
                            type="number"
                            step="0.5"
                            value={selectedEntity.rigidBody.mass}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              rigidBody: { ...selectedEntity.rigidBody, mass: parseFloat(e.target.value) || 1 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-cyan-300"
                          />
                        </div>

                        {/* Apply Impulse Buttons */}
                        <div className="pt-1">
                          <span className="text-slate-400 block mb-1">Apply Impulse</span>
                          <div className="grid grid-cols-3 gap-1">
                            <button
                              onClick={() => onApplyImpulse(selectedEntity.id, 0, 8, 0)}
                              className="py-1 rounded bg-cyan-600/20 hover:bg-cyan-600/40 border border-cyan-500/30 text-cyan-300 font-semibold"
                            >
                              Jump Up (+Y)
                            </button>
                            <button
                              onClick={() => onApplyImpulse(selectedEntity.id, 5, 2, 0)}
                              className="py-1 rounded bg-indigo-600/20 hover:bg-indigo-600/40 border border-indigo-500/30 text-indigo-300 font-semibold"
                            >
                              Push (+X)
                            </button>
                            <button
                              onClick={() => onApplyImpulse(selectedEntity.id, 0, 2, 5)}
                              className="py-1 rounded bg-amber-600/20 hover:bg-amber-600/40 border border-amber-500/30 text-amber-300 font-semibold"
                            >
                              Forward (+Z)
                            </button>
                          </div>
                        </div>
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* ── LIGHT COMPONENT SECTION ── */}
              {(selectedEntity.light || selectedEntity.tag?.toLowerCase().includes('light')) && (
                <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                  <div
                    onClick={() => toggleSection('light')}
                    className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                  >
                    <div className="flex items-center gap-1">
                      {openSections.light ? <ChevronDown className="w-3 h-3 text-yellow-300" /> : <ChevronRight className="w-3 h-3 text-yellow-300" />}
                      <span>Light Component</span>
                    </div>
                    <Sun className="w-3.5 h-3.5 text-yellow-300" />
                  </div>

                  {openSections.light && (
                    <div className="p-2 space-y-2 text-[10px]">
                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Light Type</span>
                        <select
                          value={selectedEntity.light?.type || 'point'}
                          onChange={e => onUpdateProperty(selectedEntity.id, {
                            light: { ...(selectedEntity.light || { color: '#ffaa44', intensity: 3.0, range: 15.0 }), type: e.target.value as any }
                          })}
                          className="bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-yellow-300 font-mono text-[10px]"
                        >
                          <option value="point">Point Light</option>
                          <option value="directional">Directional Light</option>
                          <option value="spot">Spot Light</option>
                          <option value="sky">Sky Light</option>
                        </select>
                      </div>

                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Light Color</span>
                        <input
                          type="color"
                          value={toHexColor(selectedEntity.light?.color, '#ffaa44')}
                          onChange={e => onUpdateProperty(selectedEntity.id, {
                            light: { ...(selectedEntity.light || { type: 'point', intensity: 3.0, range: 15.0 }), color: e.target.value }
                          })}
                          className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                        />
                      </div>

                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Intensity (Lux / cd)</span>
                        <input
                          type="range"
                          min="0"
                          max="10"
                          step="0.2"
                          value={selectedEntity.light?.intensity ?? 3.0}
                          onChange={e => onUpdateProperty(selectedEntity.id, {
                            light: { ...(selectedEntity.light || { type: 'point', color: '#ffaa44', range: 15.0 }), intensity: parseFloat(e.target.value) }
                          })}
                          className="w-24 accent-yellow-400"
                        />
                      </div>

                      <div className="flex items-center justify-between">
                        <span className="text-slate-400">Attenuation Radius (m)</span>
                        <input
                          type="number"
                          step="1"
                          value={selectedEntity.light?.range ?? 15.0}
                          onChange={e => onUpdateProperty(selectedEntity.id, {
                            light: { ...(selectedEntity.light || { type: 'point', color: '#ffaa44', intensity: 3.0 }), range: parseFloat(e.target.value) || 15.0 }
                          })}
                          className="w-16 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-yellow-300"
                        />
                      </div>
                    </div>
                  )}
                </div>
              )}

              {/* ── 5. CHARACTER MOVEMENT COMPONENT ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('movement')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.movement ? <ChevronDown className="w-3 h-3 text-emerald-400" /> : <ChevronRight className="w-3 h-3 text-emerald-400" />}
                    <span>Character Movement Component</span>
                  </div>
                  <User className="w-3 h-3 text-emerald-400" />
                </div>

                {openSections.movement && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center justify-between">
                      <span className="text-slate-300">Enable Movement Component</span>
                      <input
                        type="checkbox"
                        checked={!!selectedEntity.movement}
                        onChange={e => {
                          if (e.target.checked) {
                            onUpdateProperty(selectedEntity.id, {
                              movement: { maxWalkSpeed: 12.0, jumpZVelocity: 12.0, gravityScale: 1.0, airControl: 0.35 }
                            });
                          } else {
                            onUpdateProperty(selectedEntity.id, { movement: undefined });
                          }
                        }}
                        className="accent-emerald-400 rounded cursor-pointer"
                      />
                    </div>

                    {selectedEntity.movement && (
                      <div className="space-y-1.5 pt-1 border-t border-white/5">
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Max Walk Speed (m/s)</span>
                          <input
                            type="number"
                            step="1"
                            value={selectedEntity.movement.maxWalkSpeed}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              movement: { ...selectedEntity.movement, maxWalkSpeed: parseFloat(e.target.value) || 12 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-emerald-300"
                          />
                        </div>
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Jump Z Velocity (m/s)</span>
                          <input
                            type="number"
                            step="1"
                            value={selectedEntity.movement.jumpZVelocity}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              movement: { ...selectedEntity.movement, jumpZVelocity: parseFloat(e.target.value) || 12 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-emerald-300"
                          />
                        </div>
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Air Control (0 - 1)</span>
                          <input
                            type="number"
                            step="0.05"
                            min="0"
                            max="1"
                            value={selectedEntity.movement.airControl}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              movement: { ...selectedEntity.movement, airControl: parseFloat(e.target.value) || 0.35 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-emerald-300"
                          />
                        </div>
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* ── 6. AUDIO COMPONENT ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('audio')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.audio ? <ChevronDown className="w-3 h-3 text-amber-400" /> : <ChevronRight className="w-3 h-3 text-amber-400" />}
                    <span>Audio Component</span>
                  </div>
                  <Volume2 className="w-3 h-3 text-amber-400" />
                </div>

                {openSections.audio && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center justify-between">
                      <span className="text-slate-300">Attach Audio Source</span>
                      <input
                        type="checkbox"
                        checked={!!selectedEntity.audio}
                        onChange={e => {
                          if (e.target.checked) {
                            onUpdateProperty(selectedEntity.id, {
                              audio: { soundAssetId: sounds[0]?.id || 'default_sound', volume: 0.8, pitch: 1.0, is3D: true, loop: false, autoPlay: false }
                            });
                          } else {
                            onUpdateProperty(selectedEntity.id, { audio: undefined });
                          }
                        }}
                        className="accent-amber-400 rounded cursor-pointer"
                      />
                    </div>

                    {selectedEntity.audio && (
                      <div className="space-y-1.5 pt-1 border-t border-white/5">
                        <div className="space-y-1">
                          <span className="text-slate-400">Sound Asset / Cue</span>
                          <select
                            value={selectedEntity.audio.soundAssetId}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              audio: { ...selectedEntity.audio, soundAssetId: e.target.value }
                            })}
                            className="w-full bg-[#0d0d14] border border-white/10 rounded px-1.5 py-1 text-white text-[10px]"
                          >
                            {sounds.map(s => (
                              <option key={s.id} value={s.id}>{s.name} ({s.type})</option>
                            ))}
                            {sounds.length === 0 && <option value="none">Brak assetów audio (dodaj w Content Drawer)</option>}
                          </select>
                        </div>

                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Volume</span>
                          <input
                            type="range"
                            min="0"
                            max="1"
                            step="0.05"
                            value={selectedEntity.audio.volume}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              audio: { ...selectedEntity.audio, volume: parseFloat(e.target.value) }
                            })}
                            className="w-24 accent-amber-400"
                          />
                        </div>

                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">3D Spatial Audio</span>
                          <input
                            type="checkbox"
                            checked={selectedEntity.audio.is3D}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              audio: { ...selectedEntity.audio, is3D: e.target.checked }
                            })}
                            className="accent-amber-400 rounded"
                          />
                        </div>

                        <button
                          onClick={() => {
                            if (selectedEntity.audio?.soundAssetId) {
                              AudioSystem.playSound(selectedEntity.audio.soundAssetId, {
                                volume: selectedEntity.audio.volume,
                                position: selectedEntity.transform,
                              });
                            }
                          }}
                          className="w-full py-1 rounded bg-amber-600/30 hover:bg-amber-600/50 text-amber-200 border border-amber-500/40 font-semibold flex items-center justify-center gap-1 transition-all"
                        >
                          <Play className="w-3 h-3 fill-current" />
                          <span>Odtwórz Dźwięk</span>
                        </button>
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* ── 7. CAMERA SPRING ARM COMPONENT ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('cameraBoom')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.cameraBoom ? <ChevronDown className="w-3 h-3 text-cyan-400" /> : <ChevronRight className="w-3 h-3 text-cyan-400" />}
                    <span>Spring Arm / Camera Boom</span>
                  </div>
                  <Camera className="w-3 h-3 text-cyan-400" />
                </div>

                {openSections.cameraBoom && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center justify-between">
                      <span className="text-slate-300">Attach Spring Arm</span>
                      <input
                        type="checkbox"
                        checked={!!selectedEntity.cameraBoom}
                        onChange={e => {
                          if (e.target.checked) {
                            onUpdateProperty(selectedEntity.id, {
                              cameraBoom: { targetArmLength: 12.0, socketOffsetY: 4.0, cameraLag: true, lagSpeed: 0.08 }
                            });
                          } else {
                            onUpdateProperty(selectedEntity.id, { cameraBoom: undefined });
                          }
                        }}
                        className="accent-cyan-400 rounded cursor-pointer"
                      />
                    </div>

                    {selectedEntity.cameraBoom && (
                      <div className="space-y-1.5 pt-1 border-t border-white/5">
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Target Arm Length (m)</span>
                          <input
                            type="number"
                            step="1"
                            value={selectedEntity.cameraBoom.targetArmLength}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              cameraBoom: { ...selectedEntity.cameraBoom, targetArmLength: parseFloat(e.target.value) || 12 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-cyan-300"
                          />
                        </div>
                        <div className="flex items-center justify-between">
                          <span className="text-slate-400">Socket Offset Y (m)</span>
                          <input
                            type="number"
                            step="0.5"
                            value={selectedEntity.cameraBoom.socketOffsetY}
                            onChange={e => onUpdateProperty(selectedEntity.id, {
                              cameraBoom: { ...selectedEntity.cameraBoom, socketOffsetY: parseFloat(e.target.value) || 4 }
                            })}
                            className="w-20 bg-[#0d0d14] border border-white/10 rounded px-1.5 py-0.5 text-right font-mono text-cyan-300"
                          />
                        </div>
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* ── 8. QUICKJS SCRIPT COMPONENT SECTION ── */}
              <div className="rounded bg-[#161622] border border-white/[0.08] overflow-hidden">
                <div
                  onClick={() => toggleSection('scripts')}
                  className="px-2.5 py-1 bg-[#1c1c2b] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
                >
                  <div className="flex items-center gap-1">
                    {openSections.scripts ? <ChevronDown className="w-3 h-3 text-indigo-400" /> : <ChevronRight className="w-3 h-3 text-indigo-400" />}
                    <span>QuickJS Script Component</span>
                  </div>
                  <FileCode className="w-3 h-3 text-indigo-400" />
                </div>

                {openSections.scripts && (
                  <div className="p-2 space-y-1.5 text-[10px]">
                    <div className="flex items-center justify-between">
                      <span className="text-slate-300">Attached Script:</span>
                      <span className="font-mono text-cyan-300 font-medium">
                        {selectedEntity.scriptName || 'DefaultBehavior.js'}
                      </span>
                    </div>
                    <button
                      onClick={() => onOpenScriptEditor(selectedEntity.scriptName)}
                      className="w-full py-1.5 rounded bg-indigo-600/30 hover:bg-indigo-600/50 border border-indigo-500/40 text-indigo-200 font-medium flex items-center justify-center gap-1.5 transition-colors"
                    >
                      <FileCode className="w-3.5 h-3.5 text-indigo-300" />
                      <span>Open in Script Studio</span>
                    </button>
                  </div>
                )}
              </div>
            </div>
          ) : (
            <div className="flex-1 flex flex-col items-center justify-center p-6 text-center text-slate-500 space-y-2">
              <Box className="w-10 h-10 text-slate-600 stroke-[1.5]" />
              <div className="text-xs font-semibold text-slate-400">Brak zaznaczonego aktora</div>
              <div className="text-[10px] text-slate-600 max-w-xs">
                Kliknij dowolny obiekt w Viewporcie lub wybierz go z Outlinera, aby edytować parametry Transform, Jolt Physics, Materiały i Skrypty.
              </div>
            </div>
          )}
        </div>
      )}

      {/* ── TAB 2: WORLD SETTINGS ── */}
      {activeTab === 'world' && (
        <div className="flex-1 overflow-y-auto p-2.5 space-y-3 font-sans text-xs">
          <div className="p-2 rounded bg-[#161622] border border-white/[0.08] space-y-2">
            <span className="font-bold text-white text-[11px] uppercase tracking-wider flex items-center gap-1.5">
              <Sun className="w-3.5 h-3.5 text-yellow-300" />
              <span>Sun & Atmosphere</span>
            </span>

            <div className="space-y-1.5 text-[10px]">
              <div className="flex items-center justify-between">
                <span className="text-slate-300">Sun Light Color</span>
                <input
                  type="color"
                  value={settings.sunColor}
                  onChange={e => onUpdateSettings({ sunColor: e.target.value })}
                  className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                />
              </div>

              <div className="flex items-center justify-between">
                <span className="text-slate-300">Sun Intensity</span>
                <input
                  type="range"
                  min="0"
                  max="5"
                  step="0.1"
                  value={settings.sunIntensity}
                  onChange={e => onUpdateSettings({ sunIntensity: parseFloat(e.target.value) })}
                  className="w-24 accent-cyan-400"
                />
              </div>

              <div className="flex items-center justify-between">
                <span className="text-slate-300">Ambient Sky Intensity</span>
                <input
                  type="range"
                  min="0"
                  max="2"
                  step="0.05"
                  value={settings.ambientIntensity}
                  onChange={e => onUpdateSettings({ ambientIntensity: parseFloat(e.target.value) })}
                  className="w-24 accent-cyan-400"
                />
              </div>
            </div>
          </div>

          <div className="p-2 rounded bg-[#161622] border border-white/[0.08] space-y-2">
            <span className="font-bold text-white text-[11px] uppercase tracking-wider flex items-center gap-1.5">
              <Globe className="w-3.5 h-3.5 text-cyan-400" />
              <span>Exponential Height Fog</span>
            </span>

            <div className="space-y-1.5 text-[10px]">
              <div className="flex items-center justify-between">
                <span className="text-slate-300">Enable Volumetric Fog</span>
                <input
                  type="checkbox"
                  checked={settings.fogEnabled}
                  onChange={e => onUpdateSettings({ fogEnabled: e.target.checked })}
                  className="accent-cyan-400 rounded"
                />
              </div>

              {settings.fogEnabled && (
                <>
                  <div className="flex items-center justify-between">
                    <span className="text-slate-300">Fog Color</span>
                    <input
                      type="color"
                      value={settings.fogColor}
                      onChange={e => onUpdateSettings({ fogColor: e.target.value })}
                      className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                    />
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-slate-300">Fog Distance</span>
                    <input
                      type="range"
                      min="50"
                      max="500"
                      step="10"
                      value={settings.fogFar}
                      onChange={e => onUpdateSettings({ fogFar: parseInt(e.target.value) })}
                      className="w-24 accent-cyan-400"
                    />
                  </div>
                </>
              )}
            </div>
          </div>

          {/* Display & Render Resolution Settings */}
          <div className="p-2 rounded bg-[#161622] border border-white/[0.08] space-y-2">
            <span className="font-bold text-white text-[11px] uppercase tracking-wider flex items-center gap-1.5">
              <Monitor className="w-3.5 h-3.5 text-emerald-400" />
              <span>Display & Render Resolution</span>
            </span>

            <div className="space-y-2 text-[10px]">
              <div className="flex items-center justify-between">
                <span className="text-slate-300">Screen Percentage (Res Scale)</span>
                <span className="font-mono text-emerald-400 font-bold">{Math.round((settings.resolutionScale || 1.0) * 100)}%</span>
              </div>
              <input
                type="range"
                min="0.5"
                max="2.0"
                step="0.05"
                value={settings.resolutionScale || 1.0}
                onChange={e => {
                  const s = parseFloat(e.target.value);
                  const baseW = 1920, baseH = 1080;
                  const w = (Math.round(baseW * s) + 1) & ~1;
                  const h = (Math.round(baseH * s) + 1) & ~1;
                  onUpdateSettings({ resolutionScale: s, renderResolution: { width: w, height: h } });
                }}
                className="w-full accent-emerald-400 cursor-pointer"
              />

              <div className="flex items-center justify-between pt-1">
                <span className="text-slate-300">UI Scale (DPI)</span>
                <span className="font-mono text-cyan-400 font-bold">{Math.round((settings.uiScale || 1.0) * 100)}%</span>
              </div>
              <input
                type="range"
                min="0.75"
                max="1.75"
                step="0.05"
                value={settings.uiScale || 1.0}
                onChange={e => onUpdateSettings({ uiScale: parseFloat(e.target.value) })}
                className="w-full accent-cyan-400 cursor-pointer"
              />

              <div className="grid grid-cols-3 gap-1 pt-1">
                <button
                  onClick={() => onUpdateSettings({ resolutionScale: 0.67, renderResolution: { width: 1280, height: 720 } })}
                  className="py-1 px-1 text-center bg-white/5 hover:bg-white/10 rounded border border-white/5 text-[9px] font-mono text-slate-300 hover:text-white"
                >
                  720p (67%)
                </button>
                <button
                  onClick={() => onUpdateSettings({ resolutionScale: 1.0, renderResolution: { width: 1920, height: 1080 } })}
                  className="py-1 px-1 text-center bg-white/5 hover:bg-white/10 rounded border border-white/5 text-[9px] font-mono text-slate-300 hover:text-white"
                >
                  1080p (100%)
                </button>
                <button
                  onClick={() => onUpdateSettings({ resolutionScale: 2.0, renderResolution: { width: 3840, height: 2160 } })}
                  className="py-1 px-1 text-center bg-white/5 hover:bg-white/10 rounded border border-white/5 text-[9px] font-mono text-slate-300 hover:text-white"
                >
                  4K (200%)
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
