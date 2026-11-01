import React, { useState } from 'react';
import {
  Box, Sun, Shapes, Film, Sparkles,
  Search, Plus, User, Circle, Flame,
  Star, Clock, Tv, Compass, Layers, X, GripVertical, ShieldAlert
} from 'lucide-react';

export type SpawnEntityType = 
  | 'cube' | 'sphere' | 'cylinder' | 'cone' | 'plane' | 'player' | 'coin' | 'enemy' | 'capsule' | 'trigger'
  | 'point_light' | 'spot_light' | 'dir_light' | 'sky_light'
  | 'camera_actor' | 'audio_source' | 'killz_volume'
  | 'sportscar' | 'chamfer_cube' | 'ue5_cube' | 'ue5_location' | 'ue5_rotate' | 'ue5_scale' | 'ue5_bluecube' | 'ue5_rotation';

interface PlaceActorsPanelProps {
  isOpen: boolean;
  onClose?: () => void;
  onSpawnEntity: (type: SpawnEntityType) => void;
  onSpawnEmitter: (type: 'fire' | 'sparks' | 'smoke' | 'portal') => void;
}

export const PlaceActorsPanel: React.FC<PlaceActorsPanelProps> = ({
  isOpen,
  onClose,
  onSpawnEntity,
  onSpawnEmitter,
}) => {
  const [selectedCategory, setSelectedCategory] = useState<
    'favorites' | 'recent' | 'ue5' | 'basic' | 'lights' | 'shapes' | 'cinematic' | 'media' | 'vfx' | 'geometry' | 'volumes' | 'all'
  >('basic');
  const [search, setSearch] = useState('');

  if (!isOpen) return null;

  // Rail categories matching Unreal Engine 5.8 Place Actors pane
  const categories = [
    { id: 'favorites' as const, label: 'Favorites', icon: <Star className="w-3.5 h-3.5" /> },
    { id: 'ue5' as const,       label: 'UE 5.8',    icon: <span className="text-[10px] font-extrabold text-cyan-400">UE5</span> },
    { id: 'recent' as const,    label: 'Recent',    icon: <Clock className="w-3.5 h-3.5" /> },
    { id: 'basic' as const,     label: 'Basic',     icon: <Box className="w-3.5 h-3.5" /> },
    { id: 'lights' as const,    label: 'Lights',    icon: <Sun className="w-3.5 h-3.5" /> },
    { id: 'shapes' as const,    label: 'Shapes',    icon: <Shapes className="w-3.5 h-3.5" /> },
    { id: 'cinematic' as const, label: 'Cinematic', icon: <Film className="w-3.5 h-3.5" /> },
    { id: 'media' as const,     label: 'Media',     icon: <Tv className="w-3.5 h-3.5" /> },
    { id: 'vfx' as const,       label: 'VFX',       icon: <Sparkles className="w-3.5 h-3.5" /> },
    { id: 'geometry' as const,  label: 'Geometry',  icon: <Compass className="w-3.5 h-3.5" /> },
    { id: 'volumes' as const,   label: 'Volumes',   icon: <Layers className="w-3.5 h-3.5" /> },
    { id: 'all' as const,       label: 'All',       icon: <span className="text-[10px] font-bold">☰</span> },
  ];

  const itemsByCategory: Record<string, Array<{ name: string; desc: string; icon: any; action: () => void }>> = {
    favorites: [
      { name: 'Sports Car (UE 5.8)', desc: 'Unreal Engine 5.8 Sports Car FBX Model', icon: <Box className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('sportscar') },
      { name: '1M Chamfer Cube (UE 5.8)', desc: 'Unreal Engine 5.8 Standard Chamfer Box', icon: <Box className="w-3.5 h-3.5 text-indigo-400" />, action: () => onSpawnEntity('chamfer_cube') },
      { name: 'Location Gizmo (UE 5.8)', desc: 'Unreal Engine 5.8 Transform 3D Gizmo', icon: <Compass className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('ue5_location') },
      { name: 'Character (Player)', desc: 'WASD controllable character', icon: <User className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('player') },
      { name: 'Capsule Character', desc: 'Jolt upright capsule pawn', icon: <User className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('capsule') },
      { name: 'Coin (Sensor Trigger)', desc: 'Collectible trigger volume', icon: <Star className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('coin') },
      { name: 'Trigger Zone', desc: 'Invisible event sensor volume', icon: <Box className="w-3.5 h-3.5 text-amber-500" />, action: () => onSpawnEntity('trigger') },
      { name: 'Fire Emitter (Niagara)', desc: 'Rising burning flame particles', icon: <Flame className="w-3.5 h-3.5 text-orange-400" />, action: () => onSpawnEmitter('fire') },
    ],
    ue5: [
      { name: 'Sports Car (UE 5.8)', desc: 'D:\\UE_5.8 Vehicles\\Content\\SportsCar\\SM_SportsCar.FBX', icon: <Box className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('sportscar') },
      { name: '1M Chamfer Cube (UE 5.8)', desc: 'D:\\UE_5.8 Standard 1M Chamfered Cube FBX', icon: <Box className="w-3.5 h-3.5 text-indigo-400" />, action: () => onSpawnEntity('chamfer_cube') },
      { name: '1M Standard Cube (UE 5.8)', desc: 'D:\\UE_5.8 TemplateResources Standard 1M_Cube.FBX', icon: <Box className="w-3.5 h-3.5 text-blue-400" />, action: () => onSpawnEntity('ue5_cube') },
      { name: 'Location Gizmo (UE 5.8)', desc: 'D:\\UE_5.8 SM_Gizmo_Location.fbx Model', icon: <Compass className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('ue5_location') },
      { name: 'Rotate Gizmo (UE 5.8)', desc: 'D:\\UE_5.8 SM_Gizmo_Rotate.fbx Model', icon: <Compass className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('ue5_rotate') },
      { name: 'Scale Gizmo (UE 5.8)', desc: 'D:\\UE_5.8 SM_Gizmo_Scale.fbx Model', icon: <Compass className="w-3.5 h-3.5 text-rose-400" />, action: () => onSpawnEntity('ue5_scale') },
      { name: 'Blue Cube (UE 5.8)', desc: 'D:\\UE_5.8 FbxEditorAutomation Cube_Blue.fbx', icon: <Box className="w-3.5 h-3.5 text-sky-400" />, action: () => onSpawnEntity('ue5_bluecube') },
      { name: 'Rotation Handle (UE 5.8)', desc: 'D:\\UE_5.8 VREditor RotationHandle.fbx', icon: <Circle className="w-3.5 h-3.5 text-violet-400" />, action: () => onSpawnEntity('ue5_rotation') },
    ],
    recent: [
      { name: 'Capsule Character', desc: 'Upright character collider', icon: <User className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('capsule') },
      { name: 'Trigger Zone', desc: 'Sensor overlap volume', icon: <Box className="w-3.5 h-3.5 text-amber-500" />, action: () => onSpawnEntity('trigger') },
      { name: 'Actor (StaticMesh)', desc: 'StaticMeshActor primitive', icon: <Box className="w-3.5 h-3.5 text-slate-300" />, action: () => onSpawnEntity('cube') },
      { name: 'Sphere Pawn', desc: 'Dynamic Jolt physics sphere', icon: <Circle className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('sphere') },
      { name: 'Portal Vortex', desc: 'Swirling quantum portal effect', icon: <Sparkles className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEmitter('portal') },
    ],
    basic: [
      { name: 'Actor (StaticMesh)', desc: 'Empty actor container (Cube)', icon: <Box className="w-3.5 h-3.5 text-slate-400" />, action: () => onSpawnEntity('cube') },
      { name: 'Character (Player)', desc: 'WASD controllable player pawn', icon: <User className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('player') },
      { name: 'Capsule Pawn', desc: 'Upright Jolt capsule character', icon: <User className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('capsule') },
      { name: 'Point Light', desc: 'Omnidirectional point light', icon: <Sun className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('point_light') },
      { name: 'Directional Light', desc: 'Main sunlight source', icon: <Sun className="w-3.5 h-3.5 text-yellow-300" />, action: () => onSpawnEntity('dir_light') },
      { name: 'Coin (Trigger)', desc: 'Pickable sensor item', icon: <Star className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('coin') },
      { name: 'Trigger Zone', desc: 'Overlap sensor volume', icon: <Box className="w-3.5 h-3.5 text-amber-500" />, action: () => onSpawnEntity('trigger') },
      { name: 'Pawn (Sphere)', desc: 'Dynamic physics rigid body pawn', icon: <Circle className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('sphere') },
      { name: 'Player Start', desc: 'Initial player spawn transform', icon: <User className="w-3.5 h-3.5 text-indigo-400" />, action: () => onSpawnEntity('player') },
    ],
    lights: [
      { name: 'Directional Light', desc: 'Sunlight with cascaded shadows', icon: <Sun className="w-3.5 h-3.5 text-yellow-300" />, action: () => onSpawnEntity('dir_light') },
      { name: 'Point Light', desc: 'Spherical falloff light source', icon: <Sun className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('point_light') },
      { name: 'Spot Light', desc: 'Conical focused light beam', icon: <Sun className="w-3.5 h-3.5 text-orange-400" />, action: () => onSpawnEntity('spot_light') },
      { name: 'Sky Light', desc: 'Atmospheric sky reflection light', icon: <Sun className="w-3.5 h-3.5 text-cyan-300" />, action: () => onSpawnEntity('sky_light') },
    ],
    shapes: [
      { name: 'Capsule', desc: 'Jolt upright capsule collider', icon: <User className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('capsule') },
      { name: 'Cube', desc: 'Parametric box static mesh', icon: <Box className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('cube') },
      { name: 'Sphere', desc: 'Parametric sphere static mesh', icon: <Circle className="w-3.5 h-3.5 text-indigo-400" />, action: () => onSpawnEntity('sphere') },
      { name: 'Cylinder', desc: 'Parametric cylinder static mesh', icon: <Box className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('cylinder') },
      { name: 'Cone', desc: 'Parametric cone static mesh', icon: <Shapes className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('cone') },
      { name: 'Plane', desc: 'Flat 1-sided polygon mesh', icon: <Box className="w-3.5 h-3.5 text-slate-400" />, action: () => onSpawnEntity('plane') },
    ],
    cinematic: [
      { name: 'Camera Actor', desc: 'Standard perspective viewport camera', icon: <Film className="w-3.5 h-3.5 text-slate-300" />, action: () => onSpawnEntity('camera_actor') },
      { name: 'Cine Camera Actor', desc: 'Cinema camera with FOV & SpringArm', icon: <Film className="w-3.5 h-3.5 text-rose-400" />, action: () => onSpawnEntity('camera_actor') },
    ],
    media: [
      { name: 'Media Sound Component', desc: 'Spatialized audio source', icon: <Tv className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('audio_source') },
    ],
    vfx: [
      { name: 'Niagara Fire Emitter', desc: 'Rising burning flame particles', icon: <Flame className="w-3.5 h-3.5 text-orange-400" />, action: () => onSpawnEmitter('fire') },
      { name: 'Portal Vortex', desc: 'Swirling quantum cyan portal', icon: <Sparkles className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEmitter('portal') },
      { name: 'Sparks Fountain', desc: 'Gravity-driven bouncing sparks', icon: <Sparkles className="w-3.5 h-3.5 text-yellow-300" />, action: () => onSpawnEmitter('sparks') },
      { name: 'Smoke Plume', desc: 'Volumetric expanding smoke', icon: <Flame className="w-3.5 h-3.5 text-slate-400" />, action: () => onSpawnEmitter('smoke') },
    ],
    geometry: [
      { name: 'Box Brush', desc: 'Box geometry volume', icon: <Compass className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('cube') },
      { name: 'Plane Surface', desc: 'Flat ground plane', icon: <Compass className="w-3.5 h-3.5 text-slate-400" />, action: () => onSpawnEntity('plane') },
      { name: 'Cylinder Column', desc: 'Architectural cylinder column', icon: <Compass className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('cylinder') },
    ],
    volumes: [
      { name: 'Blocking Volume', desc: 'Invisible physics wall collision', icon: <Layers className="w-3.5 h-3.5 text-rose-400" />, action: () => onSpawnEntity('trigger') },
      { name: 'Kill Z Volume', desc: 'Sensor zone destroying actors on enter', icon: <ShieldAlert className="w-3.5 h-3.5 text-red-500" />, action: () => onSpawnEntity('killz_volume') },
      { name: 'Trigger Zone', desc: 'Sensory trigger volume', icon: <Layers className="w-3.5 h-3.5 text-amber-500" />, action: () => onSpawnEntity('trigger') },
    ],
    all: [
      { name: 'Actor (StaticMesh)', desc: 'Standard StaticMesh container', icon: <Box className="w-3.5 h-3.5 text-slate-400" />, action: () => onSpawnEntity('cube') },
      { name: 'Character (Player)', desc: 'WASD controllable player pawn', icon: <User className="w-3.5 h-3.5 text-emerald-400" />, action: () => onSpawnEntity('player') },
      { name: 'Pawn (RigidBody Sphere)', desc: 'Dynamic Jolt physics sphere', icon: <Circle className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEntity('sphere') },
      { name: 'Point Light', desc: 'Omnidirectional point light', icon: <Sun className="w-3.5 h-3.5 text-amber-400" />, action: () => onSpawnEntity('point_light') },
      { name: 'Fire Emitter (Niagara)', desc: 'Rising burning flame particles', icon: <Flame className="w-3.5 h-3.5 text-orange-400" />, action: () => onSpawnEmitter('fire') },
      { name: 'Portal Vortex', desc: 'Swirling quantum portal effect', icon: <Sparkles className="w-3.5 h-3.5 text-cyan-400" />, action: () => onSpawnEmitter('portal') },
    ]
  };

  const activeList = (itemsByCategory[selectedCategory] || []).filter(item =>
    item.name.toLowerCase().includes(search.toLowerCase()) ||
    item.desc.toLowerCase().includes(search.toLowerCase())
  );

  return (
    <div className="w-64 bg-[#121218] border-r border-white/[0.08] flex flex-col z-10 shrink-0 select-none">
      {/* 1. Header Tab: Exact Unreal Engine "Place Actors" Tab */}
      <div className="h-7 bg-[#0d0d12] border-b border-white/[0.08] flex items-center justify-between px-2 text-[11px] font-sans">
        <div className="flex items-center gap-1.5 px-2 py-0.5 bg-[#181822] border-t-2 border-cyan-400 rounded-t-sm text-slate-200 font-semibold shadow-sm">
          <span>Place Actors</span>
          {onClose && (
            <button onClick={onClose} className="p-0.5 text-slate-500 hover:text-white ml-1">
              <X className="w-3 h-3" />
            </button>
          )}
        </div>
        <span className="text-[9px] font-mono text-slate-600 uppercase">Palette</span>
      </div>

      {/* 2. Search Classes Bar */}
      <div className="p-2 border-b border-white/[0.06] bg-[#101016]">
        <div className="flex items-center gap-1.5 bg-[#09090e] border border-white/10 rounded px-2 py-1 focus-within:border-cyan-500/60">
          <Search className="w-3 h-3 text-slate-500" />
          <input
            type="text"
            placeholder="Search Classes..."
            value={search}
            onChange={e => setSearch(e.target.value)}
            className="w-full bg-transparent text-[11px] text-white focus:outline-none placeholder-slate-600 font-sans"
          />
          {search && (
            <button onClick={() => setSearch('')} className="text-slate-500 hover:text-white">
              <X className="w-3 h-3" />
            </button>
          )}
        </div>
      </div>

      {/* 3. Main Area: Left Rail Icons + Right Item List */}
      <div className="flex-1 flex overflow-hidden">
        {/* Exact UE5 Left Category Rail */}
        <div className="w-11 bg-[#0a0a0f] border-r border-white/[0.06] py-1 flex flex-col items-center gap-0.5 shrink-0 overflow-y-auto">
          {categories.map(cat => (
            <button
              key={cat.id}
              onClick={() => setSelectedCategory(cat.id)}
              className={`w-8 h-8 rounded flex flex-col items-center justify-center transition-all ${
                selectedCategory === cat.id
                  ? 'bg-cyan-500/25 text-cyan-300 border border-cyan-500/40 shadow-sm'
                  : 'text-slate-500 hover:text-slate-200 hover:bg-white/5 border border-transparent'
              }`}
              title={cat.label}
            >
              {cat.icon}
            </button>
          ))}
        </div>

        {/* Item List (Right) */}
        <div className="flex-1 overflow-y-auto p-1.5 space-y-1 bg-[#12121a]">
          <div className="px-1.5 py-0.5 text-[9px] font-mono text-slate-500 uppercase tracking-wider flex items-center justify-between">
            <span>{categories.find(c => c.id === selectedCategory)?.label}</span>
            <span className="text-[9px] text-slate-600">({activeList.length})</span>
          </div>

          {activeList.map((item, idx) => (
            <div
              key={idx}
              onClick={item.action}
              className="p-1.5 rounded bg-[#171722] hover:bg-[#20202e] border border-white/[0.04] hover:border-cyan-500/40 cursor-pointer transition-all flex items-center justify-between group shadow-sm"
              title={`Kliknij, aby umieścić ${item.name} w scenie`}
            >
              <div className="flex items-center gap-2 truncate">
                <div className="p-1 rounded bg-[#0c0c12] border border-white/5">
                  {item.icon}
                </div>
                <div className="truncate">
                  <div className="text-[11px] font-medium text-slate-200 group-hover:text-cyan-300 truncate">
                    {item.name}
                  </div>
                  <div className="text-[9px] text-slate-500 truncate">
                    {item.desc}
                  </div>
                </div>
              </div>

              {/* Drag / Spawn Handle */}
              <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
                <button
                  onClick={(e) => { e.stopPropagation(); item.action(); }}
                  className="p-1 rounded bg-cyan-600/30 hover:bg-cyan-600/60 text-cyan-300 transition-colors"
                  title="Spawn"
                >
                  <Plus className="w-3 h-3" />
                </button>
                <GripVertical className="w-3 h-3 text-slate-500 cursor-grab" />
              </div>
            </div>
          ))}

          {activeList.length === 0 && (
            <div className="text-center py-8 text-slate-600 text-[11px]">
              Brak pasujących klas.
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
