import React, { useState } from 'react';
import {
  Folder, FolderPlus, FolderOpen, Download, Trash2, Check, X, Calendar,
  Sparkles, Gamepad2, Layers, HardDrive, Rocket, Gauge
} from 'lucide-react';
import { Project } from '../types/project';

export type ProjectTemplate = 'empty' | 'platformer' | 'breakout' | 'shooter' | 'racer';

const DEFAULT_LOCATION_KEY = 'lucid_project_location';

interface ProjectModalProps {
  isOpen: boolean;
  onClose: () => void;
  projects: Project[];
  activeProjectId: string;
  onSelectProject: (id: string) => void;
  onCreateProject: (name: string, description: string, template: ProjectTemplate, folderPath: string) => void;
  onDeleteProject: (id: string) => void;
  onExportProject: (project: Project) => void;
}

export const ProjectModal: React.FC<ProjectModalProps> = ({
  isOpen,
  onClose,
  projects,
  activeProjectId,
  onSelectProject,
  onCreateProject,
  onDeleteProject,
  onExportProject,
}) => {
  const [isCreating, setIsCreating] = useState(false);
  const [newName, setNewName] = useState('MyProject');
  const [newDesc, setNewDesc] = useState('');
  const [template, setTemplate] = useState<ProjectTemplate>('empty');
  
  // Project location (default relative 'projects' directory)
  const [projectLocation, setProjectLocation] = useState<string>(() => {
    return localStorage.getItem(DEFAULT_LOCATION_KEY) || 'projects';
  });

  if (!isOpen) return null;

  const handleLocationChange = (val: string) => {
    setProjectLocation(val);
    try {
      localStorage.setItem(DEFAULT_LOCATION_KEY, val);
    } catch {}
  };

  const handleBrowseFolder = async () => {
    try {
      if ('showDirectoryPicker' in window) {
        const dirHandle = await (window as any).showDirectoryPicker();
        if (dirHandle && dirHandle.name) {
          // If native folder handle picked, update path
          const currentDrive = projectLocation.match(/^[A-Za-z]:/)?.[0] || 'D:';
          handleLocationChange(`${currentDrive}\\${dirHandle.name}`);
        }
      }
    } catch {
      // User cancelled picker
    }
  };

  const handleStartCreate = () => {
    setIsCreating(true);
    setNewName('MyGame');
    setNewDesc('');
    setTemplate('empty');
  };

  // Compute full target path
  const cleanLoc = (projectLocation || 'projects').trim().replace(/[\\/]+$/, '');
  const cleanName = (newName || 'MyProject').trim().replace(/[^a-zA-Z0-9_-]/g, '_');
  const fullTargetPath = `${cleanLoc}/${cleanName}`;

  const handleCreate = (e: React.FormEvent) => {
    e.preventDefault();
    const finalName = newName.trim();
    if (!finalName) return;
    onCreateProject(finalName, newDesc.trim(), template, fullTargetPath);
    setNewName('MyProject');
    setNewDesc('');
    setIsCreating(false);
  };

  return (
    <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4">
      <div className="bg-[#10101a] border border-white/20 rounded-xl max-w-2xl w-full shadow-2xl overflow-hidden flex flex-col max-h-[88vh]">
        {/* Header */}
        <div className="p-4 border-b border-white/10 flex items-center justify-between bg-[#0a0a10]">
          <div className="flex items-center gap-2">
            <Folder className="w-5 h-5 text-amber-400" />
            <div>
              <h2 className="text-sm font-bold text-white tracking-wide">Menedżer Projektów Lucid Engine</h2>
              <p className="text-[10px] text-slate-400">Wybór folderu na dysku & szablony startowe</p>
            </div>
          </div>
          <button onClick={onClose} className="p-1 rounded text-slate-400 hover:text-white transition-colors">
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Content */}
        <div className="p-4 overflow-y-auto space-y-4 flex-1">
          {/* New Project Form Toggle */}
          {!isCreating ? (
            <button
              onClick={handleStartCreate}
              className="w-full py-2.5 px-4 rounded-lg bg-cyan-600/20 hover:bg-cyan-600/30 border border-cyan-500/40 text-cyan-300 font-semibold text-xs flex items-center justify-center gap-2 transition-all shadow-sm"
            >
              <FolderPlus className="w-4 h-4" />
              <span>+ Utwórz Nowy Projekt (Wybór folderu & Szablonu)</span>
            </button>
          ) : (
            <form onSubmit={handleCreate} className="p-3.5 bg-[#151522] border border-cyan-500/30 rounded-lg space-y-3.5">
              <div className="flex items-center justify-between border-b border-white/10 pb-2">
                <h3 className="text-xs font-bold text-cyan-300 uppercase tracking-wide flex items-center gap-1.5">
                  <FolderPlus className="w-4 h-4 text-cyan-400" /> Utwórz Nowy Projekt Gry
                </h3>
                <span className="text-[10px] text-amber-400/90 font-mono font-semibold">Lucid Project Hub</span>
              </div>

              {/* 1. Project Location */}
              <div>
                <div className="flex items-center justify-between mb-1">
                  <label className="text-[11px] font-semibold text-slate-300 flex items-center gap-1.5">
                    <HardDrive className="w-3.5 h-3.5 text-amber-400" /> Lokalizacja Projektu:
                  </label>
                  <button
                    type="button"
                    onClick={handleBrowseFolder}
                    className="text-[10px] text-cyan-400 hover:text-cyan-300 flex items-center gap-1 px-2 py-0.5 rounded bg-cyan-950/40 border border-cyan-500/30 hover:bg-cyan-900/40 transition-colors"
                  >
                    <FolderOpen className="w-3 h-3" /> Przeglądaj...
                  </button>
                </div>
                <input
                  type="text"
                  required
                  value={projectLocation}
                  onChange={(e) => handleLocationChange(e.target.value)}
                  placeholder="np. projects lub C:/Gry/MojeProjekty"
                  className="w-full bg-black/60 border border-white/20 rounded px-2.5 py-1.5 text-xs text-cyan-300 focus:outline-none focus:border-cyan-400 font-mono"
                />

                {/* Quick Location Preset Pills */}
                <div className="flex items-center flex-wrap gap-1.5 mt-1.5 text-[10px] text-slate-400">
                  <span className="text-slate-500">Szybki wybór:</span>
                  <button
                    type="button"
                    onClick={() => handleLocationChange('projects')}
                    className="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 hover:text-cyan-300 border border-white/10 transition-colors font-mono"
                  >
                    projects
                  </button>
                  <button
                    type="button"
                    onClick={() => handleLocationChange('./projects/games')}
                    className="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 hover:text-cyan-300 border border-white/10 transition-colors font-mono"
                  >
                    ./projects/games
                  </button>
                  <button
                    type="button"
                    onClick={() => handleLocationChange('./workspace')}
                    className="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 hover:text-slate-200 border border-white/10 transition-colors font-mono"
                  >
                    ./workspace
                  </button>
                </div>
              </div>

              {/* 2. Project Name */}
              <div>
                <label className="text-[11px] font-semibold text-slate-300 block mb-1">Nazwa projektu:</label>
                <input
                  type="text"
                  required
                  placeholder="np. CyberpunkRPG"
                  value={newName}
                  onChange={(e) => setNewName(e.target.value)}
                  className="w-full bg-black/60 border border-white/20 rounded px-2.5 py-1.5 text-xs text-white focus:outline-none focus:border-cyan-400 font-mono"
                  autoFocus
                />
              </div>

              {/* 3. Resulting Target Path Preview (Unreal Engine 5 style) */}
              <div className="p-2.5 bg-black/70 border border-cyan-500/30 rounded text-xs space-y-1 font-mono">
                <div className="flex items-center justify-between text-[10px] text-slate-400">
                  <span className="flex items-center gap-1 text-slate-300 font-sans font-semibold">
                    <Folder className="w-3 h-3 text-cyan-400" /> Pełna ścieżka instalacji projektu:
                  </span>
                  <span className="text-emerald-400">Gotowy do zapisu przez C++ RPC</span>
                </div>
                <div className="text-cyan-300 font-bold truncate select-all">{fullTargetPath}</div>
              </div>

              {/* 4. Template Selection */}
              <div>
                <label className="text-[11px] font-semibold text-slate-300 block mb-1.5">Wybierz Szablon Gry:</label>
                <div className="grid grid-cols-1 md:grid-cols-3 gap-2">
                  {/* Platformer Template */}
                  <div
                    onClick={() => setTemplate('platformer')}
                    className={`p-2.5 rounded-lg border cursor-pointer transition-all ${
                      template === 'platformer'
                        ? 'bg-cyan-950/60 border-cyan-400 text-white shadow-md shadow-cyan-950/50'
                        : 'bg-black/30 border-white/10 hover:border-white/25 text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-2 mb-1">
                      <Gamepad2 className="w-4 h-4 text-emerald-400" />
                      <span className="text-xs font-bold">Platformer 3D (Runner)</span>
                    </div>
                    <p className="text-[10px] text-slate-400 leading-tight">
                      Kapsułowy gracz z rotacją, skoki, monety sensoryczne, ruchome platformy, patrolujący wrogowie i strefa śmierci.
                    </p>
                  </div>

                  {/* Breakout Template */}
                  <div
                    onClick={() => setTemplate('breakout')}
                    className={`p-2.5 rounded-lg border cursor-pointer transition-all ${
                      template === 'breakout'
                        ? 'bg-cyan-950/60 border-cyan-400 text-white shadow-md shadow-cyan-950/50'
                        : 'bg-black/30 border-white/10 hover:border-white/25 text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-2 mb-1">
                      <Layers className="w-4 h-4 text-purple-400" />
                      <span className="text-xs font-bold">Breakout 3D Extended</span>
                    </div>
                    <p className="text-[10px] text-slate-400 leading-tight">
                      Paletka, piłka fizyczna Jolt, niszczenie klocków, punkty, żywoty i dźwięki uderzeń.
                    </p>
                  </div>

                  {/* Shooter Template */}
                  <div
                    onClick={() => setTemplate('shooter')}
                    className={`p-2.5 rounded-lg border cursor-pointer transition-all ${
                      template === 'shooter'
                        ? 'bg-cyan-950/60 border-cyan-400 text-white shadow-md shadow-cyan-950/50'
                        : 'bg-black/30 border-white/10 hover:border-white/25 text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-2 mb-1">
                      <Rocket className="w-4 h-4 text-cyan-400" />
                      <span className="text-xs font-bold">Retro Space Shooter</span>
                    </div>
                    <p className="text-[10px] text-slate-400 leading-tight">
                      Myśliwiec kosmiczny, lasery, deszcz meteorytów, fale wrogów i wybuchy cząsteczkowe VFX.
                    </p>
                  </div>

                  {/* Racer Template */}
                  <div
                    onClick={() => setTemplate('racer')}
                    className={`p-2.5 rounded-lg border cursor-pointer transition-all ${
                      template === 'racer'
                        ? 'bg-cyan-950/60 border-cyan-400 text-white shadow-md shadow-cyan-950/50'
                        : 'bg-black/30 border-white/10 hover:border-white/25 text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-2 mb-1">
                      <Gauge className="w-4 h-4 text-amber-400" />
                      <span className="text-xs font-bold">Arcade Hover Racer</span>
                    </div>
                    <p className="text-[10px] text-slate-400 leading-tight">
                      Lewitujący ścigacz, tor wyścigowy, bramki kontrolne (checkpoints), prędkościomierz i nitro.
                    </p>
                  </div>

                  {/* Empty Template */}
                  <div
                    onClick={() => setTemplate('empty')}
                    className={`p-2.5 rounded-lg border cursor-pointer transition-all ${
                      template === 'empty'
                        ? 'bg-cyan-950/60 border-cyan-400 text-white shadow-md shadow-cyan-950/50'
                        : 'bg-black/30 border-white/10 hover:border-white/25 text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-2 mb-1">
                      <Sparkles className="w-4 h-4 text-slate-400" />
                      <span className="text-xs font-bold">Czysty Projekt</span>
                    </div>
                    <p className="text-[10px] text-slate-400 leading-tight">
                      Czysta płaszczyzna podłoża i szablon kodu od zera.
                    </p>
                  </div>
                </div>
              </div>

              {/* 5. Description */}
              <div>
                <label className="text-[11px] text-slate-400 block mb-1">Opis projektu (opcjonalny):</label>
                <input
                  type="text"
                  placeholder="Krótki opis gry lub sceny..."
                  value={newDesc}
                  onChange={(e) => setNewDesc(e.target.value)}
                  className="w-full bg-black/50 border border-white/15 rounded px-2.5 py-1.5 text-xs text-white focus:outline-none focus:border-cyan-500"
                />
              </div>

              {/* Buttons */}
              <div className="flex justify-end gap-2 pt-1 border-t border-white/10">
                <button
                  type="button"
                  onClick={() => setIsCreating(false)}
                  className="px-3 py-1 rounded bg-white/5 hover:bg-white/10 text-xs text-slate-300 transition-colors"
                >
                  Anuluj
                </button>
                <button
                  type="submit"
                  className="px-4 py-1.5 rounded bg-cyan-600 hover:bg-cyan-500 text-white font-semibold text-xs shadow-md transition-all flex items-center gap-1.5"
                >
                  <FolderPlus className="w-3.5 h-3.5" />
                  Stwórz Projekt
                </button>
              </div>
            </form>
          )}

          {/* Project List */}
          <div className="space-y-2">
            <h4 className="text-[11px] font-mono text-slate-400 uppercase tracking-wider flex items-center justify-between">
              <span>Zapisane Projekty ({projects.length})</span>
              <span className="text-[10px] text-slate-500">Dowolne lokalizacje dyskowe</span>
            </h4>
            {projects.map((p) => {
              const isActive = p.id === activeProjectId;
              return (
                <div
                  key={p.id}
                  onClick={() => { onSelectProject(p.id); onClose(); }}
                  className={`p-3 rounded-lg border transition-all cursor-pointer flex items-center justify-between ${
                    isActive
                      ? 'bg-cyan-950/40 border-cyan-500/60 shadow-lg shadow-cyan-950/50'
                      : 'bg-[#141420] border-white/5 hover:border-white/20'
                  }`}
                >
                  <div className="flex items-center gap-3">
                    <div className={`w-9 h-9 rounded-lg flex items-center justify-center ${isActive ? 'bg-cyan-500/20 text-cyan-400' : 'bg-white/5 text-slate-400'}`}>
                      <Folder className="w-5 h-5" />
                    </div>
                    <div>
                      <div className="flex items-center gap-2">
                        <span className="font-bold text-white text-xs">{p.name}</span>
                        {isActive && (
                          <span className="px-1.5 py-0.5 rounded bg-cyan-500/20 text-[9px] font-mono text-cyan-300 border border-cyan-500/30 flex items-center gap-1">
                            <Check className="w-2.5 h-2.5" /> AKTYWNY
                          </span>
                        )}
                      </div>
                      <p className="text-[11px] text-slate-400 line-clamp-1">{p.description || 'Brak opisu'}</p>
                      <div className="flex items-center gap-3 text-[10px] font-mono text-slate-500 mt-1">
                        <span className="text-cyan-400/90 font-mono font-medium">{p.folderPath || 'projects/' + p.name}</span>
                        <span>•</span>
                        <span>{p.scenes?.length || 1} scen</span>
                        <span>•</span>
                        <span>{p.scripts.length} skryptów</span>
                        <span>•</span>
                        <span className="flex items-center gap-1">
                          <Calendar className="w-2.5 h-2.5" /> {new Date(p.updatedAt).toLocaleDateString()}
                        </span>
                      </div>
                    </div>
                  </div>

                  <div className="flex items-center gap-1" onClick={(e) => e.stopPropagation()}>
                    <button
                      onClick={() => onExportProject(p)}
                      className="p-1.5 rounded bg-white/5 hover:bg-white/10 text-slate-300 hover:text-white transition-colors"
                      title="Pobierz projekt (.lucidproject)"
                    >
                      <Download className="w-3.5 h-3.5" />
                    </button>
                    {projects.length > 1 && (
                      <button
                        onClick={() => onDeleteProject(p.id)}
                        className="p-1.5 rounded bg-rose-500/10 hover:bg-rose-500/20 text-rose-400 transition-colors"
                        title="Usuń projekt"
                      >
                        <Trash2 className="w-3.5 h-3.5" />
                      </button>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Footer */}
        <div className="p-3 border-t border-white/10 bg-[#0a0a10] flex justify-end">
          <button
            onClick={onClose}
            className="px-4 py-1.5 rounded bg-white/10 hover:bg-white/15 text-white font-medium text-xs transition-colors"
          >
            Zamknij
          </button>
        </div>
      </div>
    </div>
  );
};
