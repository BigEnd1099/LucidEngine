import React, { useState } from 'react';
import {
  Volume2, Play, Plus, Trash2, X, Sliders, Music, Upload, Check, Sparkles
} from 'lucide-react';
import { SoundAsset, SoundCueConfig, WaveformType } from '../types/project';
import { AudioSystem } from '../services/audioSystem';

interface SoundCueStudioProps {
  sounds: SoundAsset[];
  isOpen: boolean;
  onClose: () => void;
  onSaveSound: (sound: SoundAsset) => void;
  onDeleteSound: (id: string) => void;
}

export const SoundCueStudio: React.FC<SoundCueStudioProps> = ({
  sounds,
  isOpen,
  onClose,
  onSaveSound,
  onDeleteSound,
}) => {
  const [selectedSoundId, setSelectedSoundId] = useState<string | null>(sounds[0]?.id || null);

  const selectedSound = sounds.find(s => s.id === selectedSoundId) || sounds[0];

  const [activeConfig, setActiveConfig] = useState<SoundCueConfig>(() => {
    return selectedSound?.cueConfig || {
      waveType: 'sine',
      startFreq: 440,
      endFreq: 880,
      attack: 0.02,
      decay: 0.1,
      sustain: 0.3,
      release: 0.2,
      volume: 0.8,
    };
  });

  if (!isOpen) return null;

  const handleSelectSound = (s: SoundAsset) => {
    setSelectedSoundId(s.id);
    if (s.cueConfig) {
      setActiveConfig(s.cueConfig);
    }
  };

  const handlePlayPreview = () => {
    AudioSystem.previewCue(activeConfig);
  };

  const handleCreateNewCue = () => {
    const id = 'cue_' + Date.now();
    const newSound: SoundAsset = {
      id,
      name: `S_SoundCue_${sounds.length + 1}.cue`,
      type: 'synth_cue',
      cueConfig: {
        waveType: 'sine',
        startFreq: 300,
        endFreq: 600,
        attack: 0.01,
        decay: 0.15,
        sustain: 0.5,
        release: 0.2,
        volume: 0.8,
      },
      createdAt: Date.now(),
    };
    onSaveSound(newSound);
    setSelectedSoundId(id);
    setActiveConfig(newSound.cueConfig!);
  };

  const handleSaveCurrent = () => {
    if (!selectedSound) return;
    const updated: SoundAsset = {
      ...selectedSound,
      cueConfig: activeConfig,
    };
    onSaveSound(updated);
  };

  const handleImportAudioFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (event) => {
      const base64 = event.target?.result as string;
      const newSound: SoundAsset = {
        id: 'audio_' + Date.now(),
        name: file.name.replace(/\.[^/.]+$/, '') + '.wav',
        type: 'audio_file',
        audioBase64: base64,
        createdAt: Date.now(),
      };
      onSaveSound(newSound);
      setSelectedSoundId(newSound.id);
    };
    reader.readAsDataURL(file);
    e.target.value = '';
  };

  return (
    <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4 select-none">
      <div className="bg-[#0e0e18] border border-white/20 rounded-xl w-full max-w-4xl h-[75vh] shadow-2xl flex flex-col overflow-hidden font-sans">
        {/* Header */}
        <div className="h-9 bg-[#090910] border-b border-white/10 px-3 flex items-center justify-between text-xs">
          <div className="flex items-center gap-2">
            <Music className="w-4 h-4 text-cyan-400" />
            <span className="font-bold text-white tracking-wide">Sound Cue & Audio Subsystem Designer</span>
            <span className="text-[10px] text-slate-500 font-mono">({sounds.length} assets)</span>
          </div>
          <div className="flex items-center gap-2">
            <label className="flex items-center gap-1.5 px-2.5 py-1 rounded bg-white/5 hover:bg-white/10 text-slate-300 hover:text-white border border-white/10 text-[11px] cursor-pointer transition-all">
              <Upload className="w-3.5 h-3.5 text-cyan-400" />
              <span>Import Audio (.wav / .mp3)</span>
              <input type="file" accept="audio/*" onChange={handleImportAudioFile} className="hidden" />
            </label>
            <button
              onClick={handleCreateNewCue}
              className="flex items-center gap-1 px-2.5 py-1 rounded bg-cyan-600/30 hover:bg-cyan-600/50 text-cyan-200 border border-cyan-500/40 text-[11px] font-semibold transition-all"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>+ Nowy Sound Cue</span>
            </button>
            <button onClick={onClose} className="p-1 text-slate-400 hover:text-white">
              <X className="w-4 h-4" />
            </button>
          </div>
        </div>

        {/* Workspace: Left Asset List + Right Parametric Synth Editor */}
        <div className="flex-1 flex overflow-hidden">
          {/* Left: Sound Asset List */}
          <div className="w-64 border-r border-white/10 bg-[#0a0a12] flex flex-col p-2 space-y-1 overflow-y-auto">
            <div className="text-[9px] uppercase font-mono tracking-wider text-slate-500 px-1 py-0.5">Project Sound Assets</div>
            {sounds.map(s => (
              <div
                key={s.id}
                onClick={() => handleSelectSound(s)}
                className={`p-2 rounded cursor-pointer transition-all flex items-center justify-between group ${
                  selectedSound?.id === s.id
                    ? 'bg-cyan-600/25 border border-cyan-500/50 text-cyan-200'
                    : 'bg-white/[0.02] hover:bg-white/5 border border-transparent text-slate-300'
                }`}
              >
                <div className="flex items-center gap-2 truncate">
                  <Volume2 className={`w-3.5 h-3.5 ${s.type === 'synth_cue' ? 'text-amber-400' : 'text-cyan-400'}`} />
                  <div className="truncate">
                    <div className="text-[11px] font-medium truncate">{s.name}</div>
                    <div className="text-[9px] text-slate-500 font-mono uppercase">{s.type === 'synth_cue' ? 'Sound Cue' : 'Audio File'}</div>
                  </div>
                </div>
                <button
                  onClick={(e) => { e.stopPropagation(); onDeleteSound(s.id); }}
                  className="opacity-0 group-hover:opacity-100 p-1 hover:text-rose-400 transition-opacity"
                  title="Delete Sound"
                >
                  <Trash2 className="w-3 h-3" />
                </button>
              </div>
            ))}
            {sounds.length === 0 && (
              <div className="text-center py-8 text-slate-600 text-[11px]">Brak assetów dźwiękowych w projekcie.</div>
            )}
          </div>

          {/* Right: Sound Synthesizer Node Editor */}
          {selectedSound ? (
            <div className="flex-1 flex flex-col bg-[#0e0e18] p-4 overflow-y-auto space-y-4">
              {/* Asset Title Bar */}
              <div className="flex items-center justify-between pb-3 border-b border-white/10">
                <div className="flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-cyan-500/10 border border-cyan-500/30 text-cyan-400">
                    <Sliders className="w-5 h-5" />
                  </div>
                  <div>
                    <input
                      type="text"
                      value={selectedSound.name}
                      onChange={e => onSaveSound({ ...selectedSound, name: e.target.value })}
                      className="bg-transparent text-sm font-bold text-white focus:outline-none focus:border-b border-cyan-400"
                    />
                    <div className="text-[10px] text-slate-500 font-mono">
                      Asset ID: {selectedSound.id} • Typ: {selectedSound.type.toUpperCase()}
                    </div>
                  </div>
                </div>

                <div className="flex items-center gap-2">
                  <button
                    onClick={handlePlayPreview}
                    className="flex items-center gap-1.5 px-4 py-2 bg-gradient-to-r from-emerald-600 to-teal-600 hover:from-emerald-500 hover:to-teal-500 text-white rounded-lg text-xs font-bold shadow-lg shadow-emerald-950/40 transition-all cursor-pointer"
                  >
                    <Play className="w-3.5 h-3.5 fill-current" />
                    <span>Odtwórz Podgląd</span>
                  </button>
                  <button
                    onClick={handleSaveCurrent}
                    className="flex items-center gap-1.5 px-3.5 py-2 bg-cyan-600/30 hover:bg-cyan-600/50 text-cyan-200 border border-cyan-500/40 rounded-lg text-xs font-semibold transition-all"
                  >
                    <Check className="w-3.5 h-3.5" />
                    <span>Zapisz Asset</span>
                  </button>
                </div>
              </div>

              {/* Parametric Sound Cue Controls */}
              {selectedSound.type === 'synth_cue' ? (
                <div className="grid grid-cols-2 gap-4 text-xs">
                  {/* Waveform & Frequencies */}
                  <div className="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 space-y-3">
                    <div className="font-bold text-white flex items-center gap-2">
                      <Sparkles className="w-4 h-4 text-amber-400" />
                      <span>Generator Fali (Oscillator)</span>
                    </div>

                    <div className="space-y-1">
                      <label className="text-[11px] text-slate-400">Kształt fali (Waveform)</label>
                      <div className="grid grid-cols-5 gap-1 pt-1">
                        {(['sine', 'square', 'sawtooth', 'triangle', 'noise'] as WaveformType[]).map(w => (
                          <button
                            key={w}
                            onClick={() => setActiveConfig({ ...activeConfig, waveType: w })}
                            className={`py-1.5 rounded text-[10px] font-mono capitalize transition-all ${
                              activeConfig.waveType === w
                                ? 'bg-amber-500/30 border border-amber-500 text-amber-300 font-bold'
                                : 'bg-white/5 border border-white/5 text-slate-400 hover:bg-white/10'
                            }`}
                          >
                            {w}
                          </button>
                        ))}
                      </div>
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Częstotliwość początkowa (Start Freq)</span>
                        <span className="font-mono text-cyan-300">{activeConfig.startFreq} Hz</span>
                      </div>
                      <input
                        type="range"
                        min="40"
                        max="2400"
                        step="10"
                        value={activeConfig.startFreq}
                        onChange={e => setActiveConfig({ ...activeConfig, startFreq: parseInt(e.target.value) })}
                        className="w-full accent-cyan-400"
                      />
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Częstotliwość końcowa (Sweep Target)</span>
                        <span className="font-mono text-cyan-300">{activeConfig.endFreq} Hz</span>
                      </div>
                      <input
                        type="range"
                        min="40"
                        max="2400"
                        step="10"
                        value={activeConfig.endFreq}
                        onChange={e => setActiveConfig({ ...activeConfig, endFreq: parseInt(e.target.value) })}
                        className="w-full accent-cyan-400"
                      />
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Głośność bazowa (Gain)</span>
                        <span className="font-mono text-cyan-300">{Math.round(activeConfig.volume * 100)}%</span>
                      </div>
                      <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.05"
                        value={activeConfig.volume}
                        onChange={e => setActiveConfig({ ...activeConfig, volume: parseFloat(e.target.value) })}
                        className="w-full accent-cyan-400"
                      />
                    </div>
                  </div>

                  {/* ADSR Envelope */}
                  <div className="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 space-y-3">
                    <div className="font-bold text-white flex items-center gap-2">
                      <Sliders className="w-4 h-4 text-emerald-400" />
                      <span>Obwiednia ADSR (Envelope)</span>
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Attack (Narastanie)</span>
                        <span className="font-mono text-emerald-300">{activeConfig.attack.toFixed(3)}s</span>
                      </div>
                      <input
                        type="range"
                        min="0.001"
                        max="0.5"
                        step="0.005"
                        value={activeConfig.attack}
                        onChange={e => setActiveConfig({ ...activeConfig, attack: parseFloat(e.target.value) })}
                        className="w-full accent-emerald-400"
                      />
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Decay (Opadanie)</span>
                        <span className="font-mono text-emerald-300">{activeConfig.decay.toFixed(2)}s</span>
                      </div>
                      <input
                        type="range"
                        min="0.01"
                        max="1.0"
                        step="0.01"
                        value={activeConfig.decay}
                        onChange={e => setActiveConfig({ ...activeConfig, decay: parseFloat(e.target.value) })}
                        className="w-full accent-emerald-400"
                      />
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Sustain (Podtrzymanie)</span>
                        <span className="font-mono text-emerald-300">{Math.round(activeConfig.sustain * 100)}%</span>
                      </div>
                      <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.05"
                        value={activeConfig.sustain}
                        onChange={e => setActiveConfig({ ...activeConfig, sustain: parseFloat(e.target.value) })}
                        className="w-full accent-emerald-400"
                      />
                    </div>

                    <div className="space-y-1">
                      <div className="flex justify-between text-[11px]">
                        <span className="text-slate-400">Release (Wybrzmiewanie)</span>
                        <span className="font-mono text-emerald-300">{activeConfig.release.toFixed(2)}s</span>
                      </div>
                      <input
                        type="range"
                        min="0.01"
                        max="1.5"
                        step="0.02"
                        value={activeConfig.release}
                        onChange={e => setActiveConfig({ ...activeConfig, release: parseFloat(e.target.value) })}
                        className="w-full accent-emerald-400"
                      />
                    </div>
                  </div>
                </div>
              ) : (
                <div className="p-6 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col items-center justify-center text-center space-y-3">
                  <Volume2 className="w-12 h-12 text-cyan-400" />
                  <div className="text-sm font-bold text-white">Importowany plik dźwiękowy</div>
                  <div className="text-xs text-slate-400 max-w-md">
                    Asset audio zaimportowany do projektu. Może być odtwarzany przez komponenty aktorów lub skrypty QuickJS za pomocą <code>engine.playSound("{selectedSound.name}")</code>.
                  </div>
                </div>
              )}
            </div>
          ) : (
            <div className="flex-1 flex flex-col items-center justify-center text-slate-500 text-xs">
              Wybierz lub utwórz asset dźwiękowy.
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
