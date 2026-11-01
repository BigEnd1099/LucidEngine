import React, { useState } from 'react';
import {
  Layers, Search, Eye, EyeOff, Folder, FolderOpen,
  Box, Circle, Sun, Sparkles, User, Trash2, Copy,
  ChevronDown, ChevronRight, Settings, Filter
} from 'lucide-react';
import { EntityData } from '../types/project';

interface OutlinerPanelProps {
  entities: EntityData[];
  selectedEntityId: number | null;
  onSelectEntity: (id: number) => void;
  onDeleteEntity: (id: number) => void;
  onDuplicateEntity: (ent: EntityData) => void;
  onFocusEntity: () => void;
}

export const OutlinerPanel: React.FC<OutlinerPanelProps> = ({
  entities,
  selectedEntityId,
  onSelectEntity,
  onDeleteEntity,
  onDuplicateEntity,
  onFocusEntity,
}) => {
  const [search, setSearch] = useState('');
  const [hiddenEntities, setHiddenEntities] = useState<Set<number>>(new Set());
  const [openFolders, setOpenFolders] = useState<Record<string, boolean>>({
    world: true,
    lighting: true,
    actors: true,
    vfx: true,
  });

  const toggleFolder = (key: string) => {
    setOpenFolders(prev => ({ ...prev, [key]: !prev[key] }));
  };

  const toggleVisibility = (id: number, e: React.MouseEvent) => {
    e.stopPropagation();
    setHiddenEntities(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const getActorIcon = (tag?: string) => {
    const t = (tag || '').toLowerCase();
    if (t.includes('light') || t.includes('sun')) return <Sun className="w-3.5 h-3.5 text-yellow-300" />;
    if (t.includes('player') || t.includes('character')) return <User className="w-3.5 h-3.5 text-emerald-400" />;
    if (t.includes('coin')) return <Circle className="w-3.5 h-3.5 text-yellow-400" />;
    if (t.includes('vfx') || t.includes('particle') || t.includes('fire') || t.includes('portal')) return <Sparkles className="w-3.5 h-3.5 text-orange-400" />;
    return <Box className="w-3.5 h-3.5 text-cyan-400" />;
  };

  const getActorType = (ent: EntityData) => {
    const t = (ent.tag || '').toLowerCase();
    if (t.includes('sun') || t.includes('directional')) return 'DirectionalLight';
    if (t.includes('point') || t.includes('light')) return 'PointLight';
    if (t.includes('sky')) return 'SkyLight';
    if (t.includes('player') || t.includes('character')) return 'Character';
    if (t.includes('coin')) return 'PickupActor';
    if (t.includes('ground') || t.includes('terrain')) return 'Landscape/StaticMesh';
    if (ent.rigidBody) return 'RigidBodyActor';
    return 'StaticMeshActor';
  };

  // Group entities into Lighting vs Actors vs VFX
  const q = search.toLowerCase();
  const allFiltered = entities.filter(ent =>
    (ent.tag || `Entity_${ent.id}`).toLowerCase().includes(q)
  );

  const lightingEntities = allFiltered.filter(e => {
    const t = (e.tag || '').toLowerCase();
    return t.includes('light') || t.includes('sun') || t.includes('sky') || t.includes('fog');
  });

  const vfxEntities = allFiltered.filter(e => {
    const t = (e.tag || '').toLowerCase();
    return t.includes('particle') || t.includes('emitter') || t.includes('vfx') || t.includes('fire') || t.includes('portal') || t.includes('smoke');
  });

  const standardEntities = allFiltered.filter(e =>
    !lightingEntities.includes(e) && !vfxEntities.includes(e)
  );

  const renderEntityRow = (ent: EntityData, indent = false) => {
    const isSelected = ent.id === selectedEntityId;
    const isHidden = hiddenEntities.has(ent.id);

    return (
      <div
        key={ent.id}
        onClick={() => onSelectEntity(ent.id)}
        onDoubleClick={onFocusEntity}
        className={`flex items-center justify-between px-2 py-0.5 text-[11px] cursor-pointer transition-colors group ${
          indent ? 'pl-6' : 'pl-2'
        } ${
          isSelected
            ? 'bg-[#005fb8] text-white font-medium shadow-sm'
            : 'hover:bg-white/5 text-slate-300'
        }`}
      >
        {/* Eye Visibility */}
        <button
          onClick={(e) => toggleVisibility(ent.id, e)}
          className="w-5 text-slate-500 hover:text-white shrink-0"
          title={isHidden ? 'Hidden in Viewport' : 'Visible in Viewport'}
        >
          {isHidden ? <EyeOff className="w-3 h-3 text-slate-600" /> : <Eye className="w-3 h-3" />}
        </button>

        {/* Pin placeholder */}
        <span className="w-3 text-[10px] text-slate-600 shrink-0">·</span>

        {/* Item Label with Icon */}
        <div className="flex items-center gap-1.5 flex-1 truncate pl-1">
          {getActorIcon(ent.tag)}
          <span className="truncate">{ent.tag || `Entity_${ent.id}`}</span>
          {ent.rigidBody && (
            <span className="text-[8px] font-mono px-1 rounded bg-black/30 text-amber-300/90 ml-1">PHYS</span>
          )}
        </div>

        {/* Type Column */}
        <span className={`text-[10px] font-mono truncate w-28 text-right shrink-0 ${isSelected ? 'text-cyan-200' : 'text-slate-500'}`}>
          {getActorType(ent)}
        </span>

        {/* Context duplicate/delete on hover */}
        {isSelected && (
          <div className="hidden group-hover:flex items-center gap-1 pl-1" onClick={e => e.stopPropagation()}>
            <button onClick={() => onDuplicateEntity(ent)} className="p-0.5 rounded hover:bg-black/40 text-slate-200" title="Duplicate">
              <Copy className="w-2.5 h-2.5" />
            </button>
            <button onClick={() => onDeleteEntity(ent.id)} className="p-0.5 rounded hover:bg-rose-500/40 text-rose-200" title="Delete">
              <Trash2 className="w-2.5 h-2.5" />
            </button>
          </div>
        )}
      </div>
    );
  };

  return (
    <div className="flex-1 flex flex-col bg-[#111119] select-none overflow-hidden h-full">
      {/* 1. Header: Outliner + Settings Gear + Search */}
      <div className="p-2 border-b border-white/[0.08] space-y-1.5 bg-[#0e0e15] shrink-0">
        <div className="flex items-center justify-between text-xs">
          <div className="flex items-center gap-1.5 font-bold text-white tracking-wide">
            <Layers className="w-3.5 h-3.5 text-cyan-400" />
            <span>Outliner</span>
          </div>
          <div className="flex items-center gap-1.5 text-slate-400">
            <button className="p-1 rounded hover:bg-white/10 hover:text-white" title="Filter Classes">
              <Filter className="w-3 h-3" />
            </button>
            <button className="p-1 rounded hover:bg-white/10 hover:text-white" title="Outliner Settings">
              <Settings className="w-3 h-3" />
            </button>
          </div>
        </div>

        {/* Search */}
        <div className="flex items-center gap-1.5 bg-[#07070c] border border-white/10 rounded px-2 py-0.5 focus-within:border-cyan-500/50">
          <Search className="w-3 h-3 text-slate-500" />
          <input
            type="text"
            placeholder="Search..."
            value={search}
            onChange={e => setSearch(e.target.value)}
            className="w-full bg-transparent text-[11px] text-white focus:outline-none placeholder-slate-600 font-sans"
          />
        </div>
      </div>

      {/* 2. Table Column Headers: 👁 📌 Item Label | Type */}
      <div className="h-6 bg-[#0a0a0f] border-b border-white/[0.06] flex items-center px-2 text-[10px] font-semibold text-slate-400 uppercase tracking-wider shrink-0">
        <span className="w-5 text-center">👁</span>
        <span className="w-3 text-center">📌</span>
        <span className="flex-1 pl-1">Item Label</span>
        <span className="w-28 text-right pr-1">Type</span>
      </div>

      {/* 3. Folder Tree Structure */}
      <div className="flex-1 overflow-y-auto p-0.5 space-y-0.5 font-sans">
        {/* Root World Folder */}
        <div
          onClick={() => toggleFolder('world')}
          className="flex items-center justify-between px-1.5 py-0.5 hover:bg-white/5 rounded cursor-pointer text-[11px] text-slate-300 font-medium"
        >
          <div className="flex items-center gap-1">
            {openFolders.world ? <ChevronDown className="w-3 h-3 text-slate-500" /> : <ChevronRight className="w-3 h-3 text-slate-500" />}
            <span className="text-amber-400 text-xs">▲</span>
            <span className="font-semibold text-slate-200">Lvl_MainLevel (Editor)</span>
          </div>
          <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">World</span>
        </div>

        {openFolders.world && (
          <div className="space-y-0.5 pl-2">
            {/* Lighting Folder */}
            <div
              onClick={() => toggleFolder('lighting')}
              className="flex items-center justify-between px-1.5 py-0.5 hover:bg-white/5 rounded cursor-pointer text-[11px] text-slate-400"
            >
              <div className="flex items-center gap-1">
                {openFolders.lighting ? <ChevronDown className="w-3 h-3 text-slate-500" /> : <ChevronRight className="w-3 h-3 text-slate-500" />}
                {openFolders.lighting ? <FolderOpen className="w-3.5 h-3.5 text-amber-400" /> : <Folder className="w-3.5 h-3.5 text-amber-400" />}
                <span>Lighting</span>
              </div>
              <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">Folder</span>
            </div>

            {openFolders.lighting && (
              <div className="space-y-0.5">
                {/* Default Environment Lighting Elements */}
                <div className="flex items-center justify-between px-2 py-0.5 text-[11px] text-slate-400 hover:bg-white/5 rounded pl-6">
                  <span className="w-5 text-center text-slate-600">👁</span>
                  <span className="w-3 text-center text-slate-600">·</span>
                  <div className="flex items-center gap-1.5 flex-1 truncate pl-1">
                    <Sun className="w-3.5 h-3.5 text-yellow-300" />
                    <span>DirectionalLight (Sun)</span>
                  </div>
                  <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">DirectionalLight</span>
                </div>
                <div className="flex items-center justify-between px-2 py-0.5 text-[11px] text-slate-400 hover:bg-white/5 rounded pl-6">
                  <span className="w-5 text-center text-slate-600">👁</span>
                  <span className="w-3 text-center text-slate-600">·</span>
                  <div className="flex items-center gap-1.5 flex-1 truncate pl-1">
                    <Sun className="w-3.5 h-3.5 text-cyan-300" />
                    <span>SkyLight (Ambient)</span>
                  </div>
                  <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">SkyLight</span>
                </div>
                {lightingEntities.map(e => renderEntityRow(e, true))}
              </div>
            )}

            {/* Geometry & Actors Folder */}
            <div
              onClick={() => toggleFolder('actors')}
              className="flex items-center justify-between px-1.5 py-0.5 hover:bg-white/5 rounded cursor-pointer text-[11px] text-slate-400"
            >
              <div className="flex items-center gap-1">
                {openFolders.actors ? <ChevronDown className="w-3 h-3 text-slate-500" /> : <ChevronRight className="w-3 h-3 text-slate-500" />}
                {openFolders.actors ? <FolderOpen className="w-3.5 h-3.5 text-cyan-400" /> : <Folder className="w-3.5 h-3.5 text-cyan-400" />}
                <span>Actors & Geometry</span>
              </div>
              <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">Folder</span>
            </div>

            {openFolders.actors && (
              <div className="space-y-0.5">
                {standardEntities.map(e => renderEntityRow(e, true))}
              </div>
            )}

            {/* VFX Folder */}
            {vfxEntities.length > 0 && (
              <>
                <div
                  onClick={() => toggleFolder('vfx')}
                  className="flex items-center justify-between px-1.5 py-0.5 hover:bg-white/5 rounded cursor-pointer text-[11px] text-slate-400"
                >
                  <div className="flex items-center gap-1">
                    {openFolders.vfx ? <ChevronDown className="w-3 h-3 text-slate-500" /> : <ChevronRight className="w-3 h-3 text-slate-500" />}
                    {openFolders.vfx ? <FolderOpen className="w-3.5 h-3.5 text-orange-400" /> : <Folder className="w-3.5 h-3.5 text-orange-400" />}
                    <span>Visual Effects</span>
                  </div>
                  <span className="text-[10px] font-mono text-slate-500 w-28 text-right pr-1">Folder</span>
                </div>

                {openFolders.vfx && (
                  <div className="space-y-0.5">
                    {vfxEntities.map(e => renderEntityRow(e, true))}
                  </div>
                )}
              </>
            )}
          </div>
        )}
      </div>

      {/* 4. Exact UE5 Footer: '2 751 actors (1 selected)' */}
      <div className="h-6 bg-[#09090f] border-t border-white/[0.06] px-2.5 flex items-center justify-between text-[10px] font-mono text-slate-500 shrink-0">
        <span>{entities.length} actors</span>
        <span>{selectedEntityId !== null ? `(1 selected)` : '(0 selected)'}</span>
      </div>
    </div>
  );
};
