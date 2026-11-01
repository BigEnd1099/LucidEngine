import React from 'react';
import {
  Sparkles, Plus, Trash2, Sun, Cloud
} from 'lucide-react';
import { ParticleEmitterConfig, ParticleType, ProjectSettings, EntityData } from '../types/project';

interface EffectsPanelProps {
  emitters: ParticleEmitterConfig[];
  entities: EntityData[];
  settings: ProjectSettings;
  onAddEmitter: (type: ParticleType) => void;
  onUpdateEmitter: (id: string, patch: Partial<ParticleEmitterConfig>) => void;
  onDeleteEmitter: (id: string) => void;
  onUpdateSettings: (patch: Partial<ProjectSettings>) => void;
}

export const EffectsPanel: React.FC<EffectsPanelProps> = ({
  emitters,
  entities,
  settings,
  onAddEmitter,
  onUpdateEmitter,
  onDeleteEmitter,
  onUpdateSettings,
}) => {
  return (
    <div className="flex-1 p-3 overflow-y-auto space-y-4 bg-[#0d0d14] text-xs">
      {/* Header */}
      <div>
        <h3 className="font-bold text-white text-xs uppercase tracking-wider flex items-center gap-1.5">
          <Sparkles className="w-4 h-4 text-orange-400" />
          Efekty Cząsteczkowe (3D VFX)
        </h3>
        <p className="text-[10px] text-slate-400 mt-0.5">
          Żywe emitery cząstek Three.js zintegrowane w czasie rzeczywistym.
        </p>
      </div>

      {/* Add Emitter Buttons */}
      <div className="grid grid-cols-4 gap-1.5">
        {[
          { type: 'fire' as const,      label: 'Ogień',   color: 'text-orange-400', bg: 'hover:bg-orange-500/10' },
          { type: 'sparks' as const,    label: 'Iskry',   color: 'text-yellow-400', bg: 'hover:bg-yellow-500/10' },
          { type: 'portal' as const,    label: 'Portal',  color: 'text-cyan-400',   bg: 'hover:bg-cyan-500/10' },
          { type: 'smoke' as const,     label: 'Dym',     color: 'text-slate-400',  bg: 'hover:bg-slate-500/10' },
        ].map(btn => (
          <button
            key={btn.type}
            onClick={() => onAddEmitter(btn.type)}
            className={`flex items-center justify-center gap-1 py-1.5 px-1 rounded bg-white/5 border border-white/10 ${btn.color} ${btn.bg} transition-colors font-medium text-[11px]`}
          >
            <Plus className="w-3 h-3" />
            {btn.label}
          </button>
        ))}
      </div>

      {/* Emitters List */}
      <div className="space-y-2">
        {emitters.length === 0 && (
          <div className="text-center text-slate-600 font-mono text-[11px] py-4 bg-black/20 rounded-lg border border-white/5">
            Brak emiterów w scenie. Kliknij powyższe przyciski aby dodać efekt.
          </div>
        )}

        {emitters.map(e => (
          <div key={e.id} className="p-2.5 rounded-lg bg-[#141420] border border-white/10 space-y-2">
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <div
                  className="w-3.5 h-3.5 rounded-full border border-white/30"
                  style={{ backgroundColor: e.color }}
                />
                <span className="font-semibold text-white text-[11px]">{e.name}</span>
                <span className="px-1.5 py-0.5 rounded bg-white/5 text-[9px] font-mono text-orange-300 uppercase">
                  {e.type}
                </span>
              </div>
              <button
                onClick={() => onDeleteEmitter(e.id)}
                className="p-1 rounded hover:bg-rose-500/20 text-rose-400 transition-colors"
                title="Usuń ten emiter"
              >
                <Trash2 className="w-3 h-3" />
              </button>
            </div>

            {/* Config Sliders & Color */}
            <div className="grid grid-cols-2 gap-2 text-[10px] font-mono">
              <div>
                <label className="text-slate-400 flex justify-between">
                  <span>Kolor</span>
                  <span>{e.color}</span>
                </label>
                <input
                  type="color"
                  value={e.color}
                  onChange={(ev) => onUpdateEmitter(e.id, { color: ev.target.value })}
                  className="w-full h-6 rounded bg-transparent cursor-pointer border border-white/10"
                />
              </div>

              <div>
                <label className="text-slate-400 flex justify-between">
                  <span>Ilość cząstek</span>
                  <span>{e.count}</span>
                </label>
                <input
                  type="range"
                  min="50"
                  max="1000"
                  step="50"
                  value={e.count}
                  onChange={(ev) => onUpdateEmitter(e.id, { count: parseInt(ev.target.value) })}
                  className="w-full mt-1.5 accent-cyan-400"
                />
              </div>

              <div>
                <label className="text-slate-400 flex justify-between">
                  <span>Prędkość</span>
                  <span>{e.speed.toFixed(1)}x</span>
                </label>
                <input
                  type="range"
                  min="0.5"
                  max="6.0"
                  step="0.5"
                  value={e.speed}
                  onChange={(ev) => onUpdateEmitter(e.id, { speed: parseFloat(ev.target.value) })}
                  className="w-full mt-1.5 accent-cyan-400"
                />
              </div>

              <div>
                <label className="text-slate-400 flex justify-between">
                  <span>Rozmiar</span>
                  <span>{e.size.toFixed(2)}</span>
                </label>
                <input
                  type="range"
                  min="0.1"
                  max="1.0"
                  step="0.05"
                  value={e.size}
                  onChange={(ev) => onUpdateEmitter(e.id, { size: parseFloat(ev.target.value) })}
                  className="w-full mt-1.5 accent-cyan-400"
                />
              </div>
            </div>

            {/* Attach to Entity */}
            <div className="pt-1 border-t border-white/5">
              <label className="text-[10px] text-slate-400 block mb-1">Dołączony do encji:</label>
              <select
                value={e.attachedToEntityId !== undefined ? e.attachedToEntityId : ''}
                onChange={(ev) => {
                  const val = ev.target.value ? parseInt(ev.target.value) : undefined;
                  onUpdateEmitter(e.id, { attachedToEntityId: val });
                }}
                className="w-full bg-black/40 border border-white/10 rounded px-2 py-1 text-[11px] text-white focus:outline-none font-mono"
              >
                <option value="">Pozycja statyczna w świecie</option>
                {entities.map(ent => (
                  <option key={ent.id} value={ent.id}>
                    #{ent.id} - {ent.tag || `Entity_${ent.id}`}
                  </option>
                ))}
              </select>
            </div>
          </div>
        ))}
      </div>

      <div className="h-px bg-white/10 my-2" />

      {/* Environment & Lighting Settings */}
      <div className="space-y-3">
        <h3 className="font-bold text-white text-xs uppercase tracking-wider flex items-center gap-1.5">
          <Sun className="w-4 h-4 text-amber-400" />
          Środowisko & Oświetlenie
        </h3>

        {/* Gravity */}
        <div className="p-2.5 rounded-lg bg-[#141420] border border-white/10 space-y-1">
          <div className="flex items-center justify-between text-[11px]">
            <span className="text-slate-300 font-medium">Grawitacja (Jolt Physics)</span>
            <span className="font-mono text-cyan-400 font-bold">{settings.gravity.toFixed(2)} m/s²</span>
          </div>
          <div className="grid grid-cols-3 gap-1 pt-1">
            <button
              onClick={() => onUpdateSettings({ gravity: -9.81 })}
              className={`py-1 rounded text-[10px] font-mono border ${settings.gravity === -9.81 ? 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' : 'bg-white/5 text-slate-400 border-white/5'}`}
            >
              Ziemia (-9.81)
            </button>
            <button
              onClick={() => onUpdateSettings({ gravity: -1.62 })}
              className={`py-1 rounded text-[10px] font-mono border ${settings.gravity === -1.62 ? 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' : 'bg-white/5 text-slate-400 border-white/5'}`}
            >
              Księżyc (-1.62)
            </button>
            <button
              onClick={() => onUpdateSettings({ gravity: 0.0 })}
              className={`py-1 rounded text-[10px] font-mono border ${settings.gravity === 0.0 ? 'bg-cyan-500/20 text-cyan-300 border-cyan-500/40' : 'bg-white/5 text-slate-400 border-white/5'}`}
            >
              Zero-G (0.0)
            </button>
          </div>
        </div>

        {/* Fog */}
        <div className="p-2.5 rounded-lg bg-[#141420] border border-white/10 space-y-2">
          <div className="flex items-center justify-between text-[11px]">
            <span className="text-slate-300 font-medium flex items-center gap-1.5">
              <Cloud className="w-3.5 h-3.5 text-cyan-400" /> Mgła Atmosferyczna (Fog)
            </span>
            <input
              type="checkbox"
              checked={settings.fogEnabled}
              onChange={(e) => onUpdateSettings({ fogEnabled: e.target.checked })}
              className="accent-cyan-400 rounded cursor-pointer"
            />
          </div>

          {settings.fogEnabled && (
            <div className="space-y-1.5 pt-1 text-[10px] font-mono">
              <div className="flex items-center justify-between">
                <span className="text-slate-400">Zasięg mgły (Near - Far)</span>
                <span className="text-cyan-300">{settings.fogNear}m - {settings.fogFar}m</span>
              </div>
              <input
                type="range"
                min="30"
                max="250"
                step="10"
                value={settings.fogFar}
                onChange={(e) => onUpdateSettings({ fogFar: parseInt(e.target.value) })}
                className="w-full accent-cyan-400"
              />
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
