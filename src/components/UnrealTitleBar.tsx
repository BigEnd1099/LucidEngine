import React, { useState } from 'react';
import {
  FileText, FolderPlus, FolderOpen, Save, Download, Upload, Box,
  Settings, HelpCircle, Layers, Plus, Terminal, Cpu, Check,
  Maximize2, Minus, X
} from 'lucide-react';
import { Project } from '../types/project';

interface UnrealTitleBarProps {
  project: Project;
  onNewProject: () => void;
  onOpenProjectModal: () => void;
  onSaveProject: () => void;
  onExportProject: () => void;
  onImportProjectClick: () => void;
  onImportGlbClick: () => void;
  onOpenSettings: () => void;
  onPackageGame?: () => void;
  onUndo?: () => void;
  onRedo?: () => void;
  onTogglePlaceActors?: () => void;
  onToggleOutliner?: () => void;
  onToggleDetails?: () => void;
  onToggleContentDrawer?: () => void;
  onToggleOutputLog?: () => void;
  onExecuteScript?: () => void;
  onPhysicsDiagnostics?: () => void;
  onVulkanProfiler?: () => void;
  onExportTypes?: () => void;
  onBuildAllLevels?: () => void;
  onBuildLighting?: () => void;
  onBuildCollisionMeshes?: () => void;
  onSelectAll?: () => void;
  onDeselect?: () => void;
  onInvertSelection?: () => void;
  onFocusSelected?: () => void;
  onDuplicateSelected?: () => void;
  onDeleteSelected?: () => void;
  onAttachScript?: () => void;
  activeLevelName: string;
  scenes?: Array<{ id: string; name: string }>;
  activeSceneId?: string;
  onSelectScene?: (id: string) => void;
  onCloseScene?: (id: string) => void;
  onNewScene?: () => void;
}

export const UnrealTitleBar: React.FC<UnrealTitleBarProps> = ({
  project,
  onNewProject,
  onOpenProjectModal,
  onSaveProject,
  onExportProject,
  onImportProjectClick,
  onImportGlbClick,
  onOpenSettings,
  onPackageGame,
  onUndo,
  onRedo,
  onTogglePlaceActors,
  onToggleOutliner,
  onToggleDetails,
  onToggleContentDrawer,
  onToggleOutputLog,
  onExecuteScript,
  onPhysicsDiagnostics,
  onVulkanProfiler,
  onExportTypes,
  onBuildAllLevels,
  onBuildLighting,
  onBuildCollisionMeshes,
  onSelectAll,
  onDeselect,
  onInvertSelection,
  onFocusSelected,
  onDuplicateSelected,
  onDeleteSelected,
  onAttachScript,
  activeLevelName,
  scenes,
  activeSceneId,
  onSelectScene,
  onCloseScene,
  onNewScene,
}) => {
  const [activeMenu, setActiveMenu] = useState<string | null>(null);
  const [showHelp, setShowHelp] = useState(false);

  const menus = [
    {
      id: 'file', label: 'File', items: [
        { label: 'New Project...', shortcut: 'Ctrl+N', action: onNewProject, icon: <FolderPlus className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Open Project...', shortcut: 'Ctrl+O', action: onOpenProjectModal, icon: <FolderOpen className="w-3.5 h-3.5 text-amber-400" /> },
        { label: 'Save Level / Project', shortcut: 'Ctrl+S', action: onSaveProject, icon: <Save className="w-3.5 h-3.5 text-emerald-400" /> },
        { divider: true },
        { label: 'Kompiluj pełną grę (.exe)', shortcut: 'Ctrl+Shift+P', action: onPackageGame || (() => {}), icon: <Download className="w-3.5 h-3.5 text-emerald-400" /> },
        { label: 'Export Project (.lucidproject)', action: onExportProject, icon: <Download className="w-3.5 h-3.5 text-indigo-400" /> },
        { label: 'Import Project (.lucidproject)', action: onImportProjectClick, icon: <Upload className="w-3.5 h-3.5 text-indigo-400" /> },
        { label: 'Import 3D Mesh (.glb)', action: onImportGlbClick, icon: <Box className="w-3.5 h-3.5 text-rose-400" /> },
      ]
    },
    {
      id: 'edit', label: 'Edit', items: [
        { label: 'Undo', shortcut: 'Ctrl+Z', action: onUndo || (() => {}), icon: <FileText className="w-3.5 h-3.5 text-slate-400" /> },
        { label: 'Redo', shortcut: 'Ctrl+Y', action: onRedo || (() => {}), icon: <FileText className="w-3.5 h-3.5 text-slate-400" /> },
        { divider: true },
        { label: 'Project Settings...', action: onOpenSettings, icon: <Settings className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Editor Preferences...', action: onOpenSettings, icon: <Settings className="w-3.5 h-3.5 text-indigo-400" /> },
      ]
    },
    {
      id: 'window', label: 'Window', items: [
        { label: 'Place Actors', action: onTogglePlaceActors || (() => {}), icon: <Box className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Outliner', action: onToggleOutliner || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Details', action: onToggleDetails || (() => {}), icon: <FileText className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Content Drawer', shortcut: 'Ctrl+Space', action: onToggleContentDrawer || (() => {}), icon: <FolderOpen className="w-3.5 h-3.5 text-amber-400" /> },
        { label: 'Output Log', action: onToggleOutputLog || (() => {}), icon: <Terminal className="w-3.5 h-3.5 text-slate-400" /> },
      ]
    },
    {
      id: 'tools', label: 'Tools', items: [
        { label: 'Execute QuickJS Script', action: onExecuteScript || (() => {}), icon: <Terminal className="w-3.5 h-3.5 text-indigo-400" /> },
        { label: 'Jolt Physics Diagnostics', action: onPhysicsDiagnostics || (() => {}), icon: <Cpu className="w-3.5 h-3.5 text-amber-400" /> },
        { label: 'Vulkan Shader Profiler', action: onVulkanProfiler || (() => {}), icon: <Cpu className="w-3.5 h-3.5 text-emerald-400" /> },
        { label: 'Export TypeScript Definitions (.d.ts)', action: onExportTypes || (() => {}), icon: <FileText className="w-3.5 h-3.5 text-cyan-400" /> },
      ]
    },
    {
      id: 'build', label: 'Build', items: [
        { label: 'Build All Levels', shortcut: 'Ctrl+Shift+B', action: onBuildAllLevels || (() => {}), icon: <Box className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Build Lighting & Shadows', action: onBuildLighting || (() => {}), icon: <Box className="w-3.5 h-3.5 text-yellow-400" /> },
        { label: 'Build Jolt Collision Meshes', action: onBuildCollisionMeshes || (() => {}), icon: <Box className="w-3.5 h-3.5 text-emerald-400" /> },
      ]
    },
    {
      id: 'platforms', label: 'Platforms', items: [
        { label: 'Windows (x86_64 Vulkan 1.3)', action: onPackageGame || (() => {}), icon: <Check className="w-3.5 h-3.5 text-emerald-400" /> },
        { label: 'WebAssembly (WebGL2)', action: () => {}, icon: <Check className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Package Windows Release Build (.exe)...', action: onPackageGame || (() => {}), icon: <Download className="w-3.5 h-3.5 text-emerald-400" /> },
      ]
    },
    {
      id: 'select', label: 'Select', items: [
        { label: 'Select All', shortcut: 'Ctrl+A', action: onSelectAll || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-slate-400" /> },
        { label: 'Deselect', shortcut: 'Esc', action: onDeselect || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-slate-400" /> },
        { label: 'Invert Selection', shortcut: 'Ctrl+I', action: onInvertSelection || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-slate-400" /> },
        { label: 'Focus Selected Actor', shortcut: 'F', action: onFocusSelected || (() => {}), icon: <Box className="w-3.5 h-3.5 text-cyan-400" /> },
      ]
    },
    {
      id: 'actor', label: 'Actor', items: [
        { label: 'Duplicate Actor', shortcut: 'Ctrl+D', action: onDuplicateSelected || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Delete Actor', shortcut: 'Del', action: onDeleteSelected || (() => {}), icon: <Layers className="w-3.5 h-3.5 text-rose-400" /> },
        { label: 'Attach QuickJS Script', action: onAttachScript || (() => {}), icon: <FileText className="w-3.5 h-3.5 text-indigo-400" /> },
      ]
    },
    {
      id: 'help', label: 'Help', items: [
        { label: 'Lucid Engine Documentation', action: () => setShowHelp(true), icon: <HelpCircle className="w-3.5 h-3.5 text-cyan-400" /> },
        { label: 'Keyboard Shortcuts & Viewport Navigation', action: () => setShowHelp(true), icon: <HelpCircle className="w-3.5 h-3.5 text-indigo-400" /> },
      ]
    },
  ];

  return (
    <div className="bg-[#0b0b10] border-b border-white/[0.08] select-none z-30 shrink-0">
      {/* 1. Top Window Bar: UE Icon, Menus, Prototype Badge, Window Controls */}
      <div className="h-7 px-2.5 flex items-center justify-between text-[11px] font-sans text-slate-300">
        <div className="flex items-center gap-1">
          {/* Unreal Engine Brand Badge */}
          <div className="flex items-center gap-1.5 mr-2 pr-2 border-r border-white/10">
            <div className="w-4 h-4 rounded-full bg-gradient-to-tr from-cyan-500 to-indigo-600 flex items-center justify-center font-bold text-[9px] text-white shadow-sm shadow-cyan-500/30">
              U
            </div>
            <span className="font-semibold text-white tracking-wider text-[11px]">LUCID 5.8</span>
          </div>

          {/* Menus */}
          {menus.map(menu => (
            <div key={menu.id} className="relative">
              <button
                onClick={() => setActiveMenu(activeMenu === menu.id ? null : menu.id)}
                className={`px-2 py-0.5 rounded text-[11px] transition-colors ${
                  activeMenu === menu.id ? 'bg-white/15 text-white' : 'hover:bg-white/10 text-slate-300'
                }`}
              >
                {menu.label}
              </button>

              {activeMenu === menu.id && menu.items.length > 0 && (
                <div
                  className="absolute top-full left-0 mt-0.5 w-60 bg-[#14141f] border border-white/15 rounded-md shadow-2xl py-1 z-50 backdrop-blur-2xl"
                  onMouseLeave={() => setActiveMenu(null)}
                >
                  {menu.items.map((item: any, idx: number) => {
                    if (item.divider) return <div key={idx} className="h-px bg-white/10 my-1" />;
                    return (
                      <button
                        key={idx}
                        onClick={() => { item.action?.(); setActiveMenu(null); }}
                        className="w-full px-3 py-1.5 text-left text-[11px] flex items-center justify-between hover:bg-cyan-500/20 hover:text-cyan-200 text-slate-200 transition-colors"
                      >
                        <div className="flex items-center gap-2">
                          {item.icon}
                          <span>{item.label}</span>
                        </div>
                        {item.shortcut && <span className="text-[9px] font-mono text-slate-500">{item.shortcut}</span>}
                      </button>
                    );
                  })}
                </div>
              )}
            </div>
          ))}
        </div>

        {/* Center Project Indicator */}
        <div className="flex items-center gap-2 text-slate-400 font-mono text-[10px]">
          <span className="text-slate-500 font-sans">Project:</span>
          <span className="text-cyan-300 font-semibold">{project.name}</span>
          <span className="text-slate-600">|</span>
          <span className="text-slate-400">UE 5.8 / Linear Hybrid</span>
        </div>

        {/* Window controls (Prototype + Min/Max/Close matching Screenshot) */}
        <div className="flex items-center gap-3">
          <span className="text-[10px] text-slate-500 font-sans tracking-wide">Prototype</span>
          <div className="flex items-center gap-2 text-slate-400">
            <button className="hover:text-white p-0.5"><Minus className="w-3 h-3" /></button>
            <button className="hover:text-white p-0.5"><Maximize2 className="w-2.5 h-2.5" /></button>
            <button className="hover:text-rose-400 p-0.5"><X className="w-3 h-3" /></button>
          </div>
        </div>
      </div>

      {/* 2. Level Tabs Bar (Exact Unreal Engine 5 Tab Row) */}
      <div className="h-7 bg-[#101017] border-t border-white/[0.04] flex items-center px-2 gap-1 overflow-x-auto">
        {(scenes && scenes.length > 0 ? scenes : [{ id: 'default', name: activeLevelName || 'Lvl_MainLevel' }]).map((sc) => {
          const isActive = sc.id === activeSceneId || (!activeSceneId && sc.name === activeLevelName);
          return (
            <div
              key={sc.id}
              onClick={() => onSelectScene?.(sc.id)}
              className={`flex items-center gap-1.5 px-3 py-1 rounded-t-sm text-[11px] font-medium transition-all cursor-pointer select-none ${
                isActive
                  ? 'bg-[#181824] border-t-2 border-cyan-400 text-white shadow-sm'
                  : 'bg-[#12121c] hover:bg-[#151522] text-slate-400 hover:text-slate-200'
              }`}
            >
              <span className="text-amber-400 text-xs font-bold">▲</span>
              <span>{sc.name}</span>
              {isActive && <span className="text-[9px] text-slate-500 font-mono ml-1">(Persistent)</span>}
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  onCloseScene?.(sc.id);
                }}
                className="ml-1.5 text-slate-500 hover:text-rose-400 hover:bg-white/10 rounded px-1 text-[10px] transition-colors"
                title="Zamknij poziom"
              >
                ✕
              </button>
            </div>
          );
        })}

        <button
          onClick={onNewScene || onNewProject}
          className="p-1 rounded text-slate-500 hover:text-white hover:bg-white/5 transition-colors"
          title="Dodaj nowy poziom / scenę"
        >
          <Plus className="w-3.5 h-3.5" />
        </button>
      </div>

      {/* Help Modal */}
      {showHelp && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4">
          <div className="bg-[#12121a] border border-white/20 rounded-xl max-w-lg w-full p-4 shadow-2xl space-y-3 text-xs text-slate-300">
            <div className="flex items-center justify-between border-b border-white/10 pb-2">
              <span className="font-bold text-white text-sm">Sterowanie i Skróty klawiszowe (Blender / UE5)</span>
              <button onClick={() => setShowHelp(false)} className="text-slate-500 hover:text-white text-sm">✕</button>
            </div>
            <div className="space-y-2 text-[11px] leading-relaxed">
              <div className="p-2 rounded bg-white/5 border border-white/5 space-y-1">
                <div className="font-semibold text-cyan-300">Nawigacja kamerą (Viewport):</div>
                <div>• <b className="text-white">PPM (Prawy Przycisk Myszy):</b> Obrót kamery (Look Around / Yaw & Pitch)</div>
                <div>• <b className="text-white">PPM + WASD / QE:</b> Latanie kamerą (Fly Camera, Shift = przyspieszenie)</div>
                <div>• <b className="text-white">ŚPM (Środkowy Przycisk):</b> Przesuwanie kamery w płaszczyźnie (Pan)</div>
                <div>• <b className="text-white">Kółko myszy (Scroll):</b> Przybliżanie / Oddalanie (Zoom)</div>
                <div>• <b className="text-white">LPM:</b> Zaznaczenie obiektu w świecie 3D</div>
                <div>• <b className="text-white">F:</b> Skupienie kamery na wybranym obiekcie (Focus)</div>
              </div>
              <div className="p-2 rounded bg-white/5 border border-white/5 space-y-1">
                <div className="font-semibold text-emerald-300">Manipulacja obiektami (Gizmo):</div>
                <div>• <b className="text-white">W:</b> Narzędzie Przesuwania (Translate)</div>
                <div>• <b className="text-white">E:</b> Narzędzie Obracania (Rotate)</div>
                <div>• <b className="text-white">R:</b> Narzędzie Skalowania (Scale)</div>
                <div>• <b className="text-white">Q:</b> Przełącznik układu: World / Local</div>
                <div>• <b className="text-white">Del:</b> Usunięcie wybranego obiektu</div>
                <div>• <b className="text-white">Ctrl + D:</b> Duplikacja wybranego obiektu</div>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
