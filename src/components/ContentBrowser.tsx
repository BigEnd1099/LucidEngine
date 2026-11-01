import React, { useState } from 'react';
import {
  Folder, FolderOpen, FileCode, Palette, Sparkles, Layers,
  Plus, Search, Trash2, X, Volume2, Layout
} from 'lucide-react';
import { Project, ScriptFile, MaterialAsset, ParticleEmitterConfig, SoundAsset, WidgetBlueprint } from '../types/project';

interface ContentBrowserProps {
  project: Project;
  isOpen: boolean;
  onClose: () => void;
  onSelectScript: (script: ScriptFile) => void;
  onApplyMaterial: (mat: MaterialAsset) => void;
  onSelectEmitter: (emitter: ParticleEmitterConfig) => void;
  onSelectSound?: (sound: SoundAsset) => void;
  onSelectWidget?: (widget: WidgetBlueprint) => void;
  onCreateScript: () => void;
  onCreateMaterial: () => void;
  onCreateEmitter: () => void;
  onCreateSound?: () => void;
  onCreateWidget?: () => void;
  onDeleteScript: (id: string) => void;
  onDeleteMaterial: (id: string) => void;
  onDeleteEmitter: (id: string) => void;
  onDeleteSound?: (id: string) => void;
  onDeleteWidget?: (id: string) => void;
}

export const ContentBrowser: React.FC<ContentBrowserProps> = ({
  project,
  isOpen,
  onClose,
  onSelectScript,
  onApplyMaterial,
  onSelectEmitter,
  onSelectSound,
  onSelectWidget,
  onCreateScript,
  onCreateMaterial,
  onCreateEmitter,
  onCreateSound,
  onCreateWidget,
  onDeleteScript,
  onDeleteMaterial,
  onDeleteEmitter,
  onDeleteSound,
  onDeleteWidget,
}) => {
  const [selectedFolder, setSelectedFolder] = useState<'all' | 'scripts' | 'materials' | 'effects' | 'audio' | 'ui' | 'scenes'>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [showAddMenu, setShowAddMenu] = useState(false);

  if (!isOpen) return null;

  const q = searchQuery.toLowerCase();

  const filteredScripts = (project.scripts || []).filter(s => s.name.toLowerCase().includes(q));
  const filteredMaterials = (project.materials || []).filter(m => m.name.toLowerCase().includes(q));
  const activeScene = project.scenes.find(s => s.id === project.activeSceneId) || project.scenes[0];
  const filteredEmitters = (activeScene?.emitters || []).filter(e => e.name.toLowerCase().includes(q));
  const filteredSounds = (project.sounds || []).filter(s => s.name.toLowerCase().includes(q));
  const filteredWidgets = (project.widgets || []).filter(w => w.name.toLowerCase().includes(q));

  return (
    <div className="h-64 bg-[#0d0d16] border-t border-white/10 flex flex-col z-20 shrink-0 select-none shadow-2xl">
      {/* Top Header of Content Drawer */}
      <div className="h-9 px-3 border-b border-white/10 bg-[#09090f] flex items-center justify-between text-xs">
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5 font-semibold text-slate-200">
            <FolderOpen className="w-4 h-4 text-amber-400" />
            <span className="tracking-wide">CONTENT DRAWER</span>
          </div>

          <div className="h-4 w-px bg-white/10" />

          {/* Add Asset Dropdown */}
          <div className="relative">
            <button
              onClick={() => setShowAddMenu(!showAddMenu)}
              className="px-2.5 py-1 rounded bg-cyan-600/30 hover:bg-cyan-600/50 border border-cyan-500/40 text-cyan-200 flex items-center gap-1 text-[11px] font-medium transition-all"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>+ Dodaj Asset</span>
            </button>

            {showAddMenu && (
              <div className="absolute top-full left-0 mt-1 w-56 bg-[#141422] border border-white/15 rounded-lg shadow-2xl py-1 z-50 backdrop-blur-xl">
                <button
                  onClick={() => { onCreateScript(); setShowAddMenu(false); }}
                  className="w-full px-3 py-1.5 text-left text-[11px] flex items-center gap-2 hover:bg-cyan-500/20 text-slate-200"
                >
                  <FileCode className="w-3.5 h-3.5 text-emerald-400" /> Nowy Blueprint (BP_...)
                </button>
                <button
                  onClick={() => { onCreateMaterial(); setShowAddMenu(false); }}
                  className="w-full px-3 py-1.5 text-left text-[11px] flex items-center gap-2 hover:bg-cyan-500/20 text-slate-200"
                >
                  <Palette className="w-3.5 h-3.5 text-rose-400" /> Nowy Materiał (M_ / MI_)
                </button>
                <button
                  onClick={() => { onCreateEmitter(); setShowAddMenu(false); }}
                  className="w-full px-3 py-1.5 text-left text-[11px] flex items-center gap-2 hover:bg-cyan-500/20 text-slate-200"
                >
                  <Sparkles className="w-3.5 h-3.5 text-orange-400" /> Nowy Efekt Niagara (NS_...)
                </button>
                {onCreateSound && (
                  <button
                    onClick={() => { onCreateSound(); setShowAddMenu(false); }}
                    className="w-full px-3 py-1.5 text-left text-[11px] flex items-center gap-2 hover:bg-cyan-500/20 text-slate-200"
                  >
                    <Volume2 className="w-3.5 h-3.5 text-amber-400" /> Nowy Sound Cue (S_...)
                  </button>
                )}
                {onCreateWidget && (
                  <button
                    onClick={() => { onCreateWidget(); setShowAddMenu(false); }}
                    className="w-full px-3 py-1.5 text-left text-[11px] flex items-center gap-2 hover:bg-cyan-500/20 text-slate-200"
                  >
                    <Layout className="w-3.5 h-3.5 text-indigo-400" /> Nowy Widget UI / HUD (WBP_...)
                  </button>
                )}
              </div>
            )}
          </div>

          {/* Unreal Breadcrumb Path */}
          <div className="flex items-center gap-1.5 text-[10px] font-mono text-slate-400 bg-white/5 px-2 py-0.5 rounded border border-white/10 select-none">
            <span className="text-amber-400 font-semibold">Content</span>
            <span className="text-slate-600">/</span>
            <span className="text-cyan-300 font-semibold">
              {selectedFolder === 'all' ? 'All Assets' :
               selectedFolder === 'scenes' ? 'Maps' :
               selectedFolder === 'scripts' ? 'Blueprints' :
               selectedFolder === 'materials' ? 'Materials' :
               selectedFolder === 'audio' ? 'Audio' :
               selectedFolder === 'ui' ? 'UI' : 'FX'}
            </span>
          </div>

          {/* Search bar */}
          <div className="flex items-center gap-1.5 bg-black/40 border border-white/10 rounded px-2 py-0.5 w-48">
            <Search className="w-3 h-3 text-slate-500" />
            <input
              type="text"
              placeholder="Filtruj zasoby..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="bg-transparent border-0 text-[11px] text-white focus:outline-none w-full placeholder-slate-500 font-mono"
            />
          </div>
        </div>

        <button
          onClick={onClose}
          className="p-1 rounded hover:bg-white/10 text-slate-400 hover:text-white transition-colors"
          title="Zwiń Content Drawer (Ctrl+Spacja)"
        >
          <X className="w-4 h-4" />
        </button>
      </div>

      {/* Main split: Folders on Left, Asset Grid on Right */}
      <div className="flex-1 flex overflow-hidden">
        {/* Left Folder Tree (Unreal Engine 5 Content Tree) */}
        <div className="w-52 border-r border-white/10 bg-[#09090f]/80 p-2 text-xs space-y-0.5 overflow-y-auto shrink-0 font-sans">
          <div className="px-2 py-1 text-[10px] font-mono font-bold text-slate-500 uppercase tracking-wider flex items-center gap-1.5 mb-1">
            <Layers className="w-3 h-3 text-cyan-400" /> /Game/Content
          </div>
          {[
            { id: 'all' as const,       label: 'Content (Wszystko)', icon: <Folder className="w-3.5 h-3.5 text-amber-400" /> },
            { id: 'scenes' as const,    label: 'Maps (Lvl_)',        icon: <Layers className="w-3.5 h-3.5 text-cyan-400" /> },
            { id: 'scripts' as const,   label: 'Blueprints (BP_)',   icon: <FileCode className="w-3.5 h-3.5 text-emerald-400" /> },
            { id: 'materials' as const, label: 'Materials (M_ / MI_)', icon: <Palette className="w-3.5 h-3.5 text-rose-400" /> },
            { id: 'audio' as const,     label: 'Audio (S_ / Cue_)',  icon: <Volume2 className="w-3.5 h-3.5 text-amber-400" /> },
            { id: 'ui' as const,        label: 'UI Widgets (WBP_)',  icon: <Layout className="w-3.5 h-3.5 text-indigo-400" /> },
            { id: 'effects' as const,   label: 'FX (NS_)',           icon: <Sparkles className="w-3.5 h-3.5 text-orange-400" /> },
          ].map(f => (
            <button
              key={f.id}
              onClick={() => setSelectedFolder(f.id)}
              className={`w-full flex items-center gap-2 px-2 py-1.5 rounded text-left transition-colors ${
                selectedFolder === f.id
                  ? 'bg-cyan-500/20 text-cyan-300 border border-cyan-500/40 font-semibold'
                  : 'hover:bg-white/5 text-slate-400 border border-transparent'
              }`}
            >
              {f.icon}
              <span className="text-[11px] truncate">{f.label}</span>
            </button>
          ))}
        </div>

        {/* Right Asset Grid */}
        <div className="flex-1 p-3 overflow-y-auto grid grid-cols-6 gap-3 items-start content-start bg-[#0b0b12]">
          {/* SCRIPTS */}
          {(selectedFolder === 'all' || selectedFolder === 'scripts') &&
            filteredScripts.map(script => (
              <div
                key={script.id}
                onDoubleClick={() => onSelectScript(script)}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-emerald-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
              >
                <div className="w-10 h-10 rounded-md bg-emerald-950/60 border border-emerald-500/30 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform">
                  <FileCode className="w-5 h-5 text-emerald-400" />
                </div>
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={script.name}>
                  {script.name}
                </span>
                <span className="text-[8px] font-mono text-emerald-400/80 uppercase">Blueprint</span>
                <div className="absolute top-1 right-1 opacity-0 group-hover:opacity-100 flex gap-0.5">
                  <button
                    onClick={(e) => { e.stopPropagation(); onDeleteScript(script.id); }}
                    className="p-1 rounded bg-rose-500/20 hover:bg-rose-500/40 text-rose-300"
                    title="Usuń skrypt"
                  >
                    <Trash2 className="w-2.5 h-2.5" />
                  </button>
                </div>
              </div>
            ))}

          {/* SOUNDS */}
          {(selectedFolder === 'all' || selectedFolder === 'audio') &&
            filteredSounds.map(sound => (
              <div
                key={sound.id}
                onDoubleClick={() => onSelectSound && onSelectSound(sound)}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-amber-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
              >
                <div className="w-10 h-10 rounded-md bg-amber-950/60 border border-amber-500/30 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform">
                  <Volume2 className="w-5 h-5 text-amber-400" />
                </div>
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={sound.name}>
                  {sound.name}
                </span>
                <span className="text-[8px] font-mono text-amber-400/80 uppercase">{sound.type === 'synth_cue' ? 'Sound Cue' : 'Audio File'}</span>
                {onDeleteSound && (
                  <div className="absolute top-1 right-1 opacity-0 group-hover:opacity-100 flex gap-0.5">
                    <button
                      onClick={(e) => { e.stopPropagation(); onDeleteSound(sound.id); }}
                      className="p-1 rounded bg-rose-500/20 hover:bg-rose-500/40 text-rose-300"
                      title="Usuń dźwięk"
                    >
                      <Trash2 className="w-2.5 h-2.5" />
                    </button>
                  </div>
                )}
              </div>
            ))}

          {/* UI WIDGETS */}
          {(selectedFolder === 'all' || selectedFolder === 'ui') &&
            filteredWidgets.map(widget => (
              <div
                key={widget.id}
                onDoubleClick={() => onSelectWidget && onSelectWidget(widget)}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-indigo-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
              >
                <div className="w-10 h-10 rounded-md bg-indigo-950/60 border border-indigo-500/30 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform">
                  <Layout className="w-5 h-5 text-indigo-400" />
                </div>
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={widget.name}>
                  {widget.name}
                </span>
                <span className="text-[8px] font-mono text-indigo-400/80 uppercase">
                  {widget.elements.length} elem.
                </span>
                {onDeleteWidget && (
                  <div className="absolute top-1 right-1 opacity-0 group-hover:opacity-100 flex gap-0.5">
                    <button
                      onClick={(e) => { e.stopPropagation(); onDeleteWidget(widget.id); }}
                      className="p-1 rounded bg-rose-500/20 hover:bg-rose-500/40 text-rose-300"
                      title="Usuń widget"
                    >
                      <Trash2 className="w-2.5 h-2.5" />
                    </button>
                  </div>
                )}
              </div>
            ))}

          {/* MATERIALS */}
          {(selectedFolder === 'all' || selectedFolder === 'materials') &&
            filteredMaterials.map(mat => (
              <div
                key={mat.id}
                onClick={() => onApplyMaterial(mat)}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-rose-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
                title="Kliknij aby nałożyć na wybrany obiekt"
              >
                <div
                  className="w-10 h-10 rounded-full border border-white/20 shadow-lg mb-1 group-hover:scale-105 transition-transform flex items-center justify-center"
                  style={{ backgroundColor: mat.color }}
                />
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={mat.name}>
                  {mat.name}
                </span>
                <span className="text-[8px] font-mono text-rose-400/80 uppercase">PBR Material</span>
                <div className="absolute top-1 right-1 opacity-0 group-hover:opacity-100 flex gap-0.5">
                  <button
                    onClick={(e) => { e.stopPropagation(); onDeleteMaterial(mat.id); }}
                    className="p-1 rounded bg-rose-500/20 hover:bg-rose-500/40 text-rose-300"
                    title="Usuń materiał"
                  >
                    <Trash2 className="w-2.5 h-2.5" />
                  </button>
                </div>
              </div>
            ))}

          {/* EFFECTS / EMITTERS */}
          {(selectedFolder === 'all' || selectedFolder === 'effects') &&
            filteredEmitters.map(emitter => (
              <div
                key={emitter.id}
                onClick={() => onSelectEmitter(emitter)}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-orange-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
              >
                <div
                  className="w-10 h-10 rounded-md bg-orange-950/60 border border-orange-500/30 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform"
                >
                  <Sparkles className="w-5 h-5" style={{ color: emitter.color }} />
                </div>
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={emitter.name}>
                  {emitter.name}
                </span>
                <span className="text-[8px] font-mono text-orange-400/80 uppercase">{emitter.type}</span>
                <div className="absolute top-1 right-1 opacity-0 group-hover:opacity-100 flex gap-0.5">
                  <button
                    onClick={(e) => { e.stopPropagation(); onDeleteEmitter(emitter.id); }}
                    className="p-1 rounded bg-rose-500/20 hover:bg-rose-500/40 text-rose-300"
                    title="Usuń emiter"
                  >
                    <Trash2 className="w-2.5 h-2.5" />
                  </button>
                </div>
              </div>
            ))}

          {/* SCENES */}
          {(selectedFolder === 'all' || selectedFolder === 'scenes') &&
            project.scenes.map(scene => (
              <div
                key={scene.id}
                className="group p-2 rounded-lg bg-[#141420] hover:bg-[#1c1c2e] border border-white/10 hover:border-cyan-500/50 cursor-pointer flex flex-col items-center text-center transition-all shadow-md relative"
              >
                <div className="w-10 h-10 rounded-md bg-cyan-950/60 border border-cyan-500/30 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform">
                  <Layers className="w-5 h-5 text-cyan-400" />
                </div>
                <span className="text-[10px] font-mono font-medium text-white truncate w-full" title={scene.name}>
                  {scene.name}
                </span>
                <span className="text-[8px] font-mono text-cyan-400/80 uppercase">
                  {scene.entities.length} encji
                </span>
              </div>
            ))}
        </div>
      </div>
    </div>
  );
};
