import React from 'react';
import { FileCode, Play, Plus, AlertCircle } from 'lucide-react';
import { ScriptFile } from '../types/project';

interface ScriptStudioProps {
  scripts: ScriptFile[];
  activeScriptId: string | null;
  onSelectScript: (id: string) => void;
  onUpdateScriptCode: (id: string, code: string) => void;
  onCreateScript: () => void;
  onDeleteScript: (id: string) => void;
  onInjectScript: (code: string) => void;
  lastScriptError: { message: string; stackTrace: string } | null;
}

export const ScriptStudio: React.FC<ScriptStudioProps> = ({
  scripts,
  activeScriptId,
  onSelectScript,
  onUpdateScriptCode,
  onCreateScript,
  onDeleteScript,
  onInjectScript,
  lastScriptError,
}) => {
  const activeScript = scripts.find(s => s.id === activeScriptId) || scripts[0];

  if (!activeScript) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center p-6 text-slate-500 font-mono text-xs gap-3">
        <FileCode className="w-10 h-10 text-slate-600 animate-pulse" />
        <span>Brak skryptów w projekcie. Kliknij poniżej aby utworzyć pierwszy skrypt logiczny.</span>
        <button
          onClick={onCreateScript}
          className="px-4 py-2 rounded bg-emerald-600 hover:bg-emerald-500 text-white font-sans text-xs font-semibold"
        >
          + Utwórz Skrypt QuickJS
        </button>
      </div>
    );
  }

  return (
    <div className="flex-1 flex flex-col overflow-hidden bg-[#0a0a12]">
      {/* Script Tabs Bar */}
      <div className="h-9 border-b border-white/10 bg-[#07070a] flex items-center px-2 gap-1 overflow-x-auto shrink-0">
        {scripts.map(s => {
          const isActive = s.id === activeScript.id;
          return (
            <div
              key={s.id}
              onClick={() => onSelectScript(s.id)}
              className={`flex items-center gap-1.5 px-3 py-1 rounded-t-md text-xs font-mono cursor-pointer transition-all border-b-2 ${
                isActive
                  ? 'bg-[#12121c] text-emerald-300 border-emerald-400 font-semibold'
                  : 'text-slate-400 hover:text-white border-transparent hover:bg-white/5'
              }`}
            >
              <FileCode className="w-3.5 h-3.5 text-emerald-400" />
              <span>{s.name}</span>
              {scripts.length > 1 && (
                <button
                  onClick={(e) => { e.stopPropagation(); onDeleteScript(s.id); }}
                  className="hover:text-rose-400 p-0.5 rounded ml-1"
                  title="Usuń ten skrypt"
                >
                  ✕
                </button>
              )}
            </div>
          );
        })}

        <button
          onClick={onCreateScript}
          className="p-1.5 rounded hover:bg-white/10 text-slate-400 hover:text-emerald-300 transition-colors ml-1"
          title="Dodaj nowy skrypt (+)"
        >
          <Plus className="w-3.5 h-3.5" />
        </button>

        <div className="ml-auto flex items-center gap-2">
          <button
            onClick={() => onInjectScript(activeScript.code)}
            className="flex items-center gap-1.5 px-3 py-1 rounded bg-emerald-600/30 hover:bg-emerald-600/50 border border-emerald-500/40 text-emerald-200 text-xs font-semibold shadow-sm transition-all"
            title="Skompiluj i wstrzyknij kod do silnika QuickJS w locie"
          >
            <Play className="w-3 h-3 text-emerald-300 fill-emerald-300" />
            <span>Kompiluj & Uruchom (QuickJS)</span>
          </button>
        </div>
      </div>

      {/* Error banner if present */}
      {lastScriptError && (
        <div className="p-2.5 bg-rose-950/60 border-b border-rose-500/40 text-rose-200 text-xs font-mono flex items-start gap-2 shrink-0">
          <AlertCircle className="w-4 h-4 text-rose-400 shrink-0 mt-0.5" />
          <div className="flex-1 overflow-hidden">
            <div className="font-bold">Błąd kompilacji QuickJS: {lastScriptError.message}</div>
            {lastScriptError.stackTrace && (
              <pre className="text-[10px] text-rose-300/80 mt-1 overflow-x-auto whitespace-pre-wrap">
                {lastScriptError.stackTrace}
              </pre>
            )}
          </div>
        </div>
      )}

      {/* Script Editor Body */}
      <div className="flex-1 flex overflow-hidden relative">
        {/* Line Numbers Simulation */}
        <div className="w-10 bg-[#07070a] border-r border-white/5 p-3 text-[11px] font-mono text-slate-600 text-right select-none shrink-0 leading-[1.625rem]">
          {activeScript.code.split('\n').map((_, idx) => (
            <div key={idx}>{idx + 1}</div>
          ))}
        </div>

        {/* Text Area */}
        <textarea
          value={activeScript.code}
          onChange={(e) => onUpdateScriptCode(activeScript.id, e.target.value)}
          className="flex-1 h-full bg-[#0d0d16] text-cyan-200 font-mono text-xs p-3 resize-none focus:outline-none focus:ring-1 focus:ring-cyan-500/30 leading-[1.625rem] selection:bg-cyan-500/30"
          spellCheck={false}
        />
      </div>
    </div>
  );
};
