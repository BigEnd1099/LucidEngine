import React, { useState } from 'react';
import {
  Layout, Plus, Trash2, X, Type, Percent, MousePointer, Square
} from 'lucide-react';
import { WidgetBlueprint, UIElement, UIElementType } from '../types/project';

interface WidgetStudioProps {
  widgets: WidgetBlueprint[];
  activeWidgetId: string | undefined;
  isOpen: boolean;
  onClose: () => void;
  onSaveWidget: (widget: WidgetBlueprint) => void;
  onDeleteWidget: (id: string) => void;
  onSetActiveWidget: (id: string | undefined) => void;
}

export const WidgetStudio: React.FC<WidgetStudioProps> = ({
  widgets,
  activeWidgetId,
  isOpen,
  onClose,
  onSaveWidget,
  onDeleteWidget,
  onSetActiveWidget,
}) => {
  const [selectedWidgetId, setSelectedWidgetId] = useState<string | null>(widgets[0]?.id || null);
  const [selectedElementId, setSelectedElementId] = useState<string | null>(null);

  const selectedWidget = widgets.find(w => w.id === selectedWidgetId) || widgets[0];
  const selectedElement = selectedWidget?.elements.find(e => e.id === selectedElementId);

  if (!isOpen) return null;

  const handleCreateNewWidget = () => {
    const id = 'widget_' + Date.now();
    const newWidget: WidgetBlueprint = {
      id,
      name: `WBP_Widget_${widgets.length + 1}`,
      elements: [
        {
          id: 'elem_' + Date.now(),
          name: 'ScoreText',
          type: 'text',
          x: 4,
          y: 4,
          width: 20,
          height: 6,
          text: 'PUNKTY: 0',
          fontSize: 14,
          color: '#fbbf24',
          bindVariable: 'score',
        }
      ],
      createdAt: Date.now(),
    };
    onSaveWidget(newWidget);
    setSelectedWidgetId(id);
    setSelectedElementId(newWidget.elements[0].id);
  };

  const handleAddElement = (type: UIElementType) => {
    if (!selectedWidget) return;
    const newId = 'elem_' + Date.now();
    let newElem: UIElement;

    if (type === 'text') {
      newElem = {
        id: newId,
        name: `TextBlock_${selectedWidget.elements.length + 1}`,
        type: 'text',
        x: 10,
        y: 10 + selectedWidget.elements.length * 8,
        width: 25,
        height: 6,
        text: 'Custom Text',
        fontSize: 14,
        color: '#ffffff',
        bindVariable: '',
      };
    } else if (type === 'progressBar') {
      newElem = {
        id: newId,
        name: `ProgressBar_${selectedWidget.elements.length + 1}`,
        type: 'progressBar',
        x: 10,
        y: 20 + selectedWidget.elements.length * 6,
        width: 30,
        height: 4,
        color: '#10b981',
        backgroundColor: '#1e293b',
        value: 100,
        minValue: 0,
        maxValue: 100,
        bindVariable: 'health',
      };
    } else if (type === 'button') {
      newElem = {
        id: newId,
        name: `ActionButton_${selectedWidget.elements.length + 1}`,
        type: 'button',
        x: 40,
        y: 45,
        width: 20,
        height: 8,
        text: 'Kliknij Mnie',
        fontSize: 13,
        color: '#ffffff',
        backgroundColor: '#3b82f6',
        borderRadius: 6,
      };
    } else {
      newElem = {
        id: newId,
        name: `Panel_${selectedWidget.elements.length + 1}`,
        type: 'panel',
        x: 25,
        y: 25,
        width: 50,
        height: 50,
        backgroundColor: 'rgba(15, 23, 42, 0.85)',
        borderColor: 'rgba(255, 255, 255, 0.1)',
        borderRadius: 8,
      };
    }

    const updated = {
      ...selectedWidget,
      elements: [...selectedWidget.elements, newElem],
    };
    onSaveWidget(updated);
    setSelectedElementId(newId);
  };

  const handleUpdateSelectedElement = (patch: Partial<UIElement>) => {
    if (!selectedWidget || !selectedElementId) return;
    const updatedElems = selectedWidget.elements.map(e => (e.id === selectedElementId ? { ...e, ...patch } : e));
    onSaveWidget({ ...selectedWidget, elements: updatedElems });
  };

  const handleDeleteSelectedElement = () => {
    if (!selectedWidget || !selectedElementId) return;
    const updatedElems = selectedWidget.elements.filter(e => e.id !== selectedElementId);
    onSaveWidget({ ...selectedWidget, elements: updatedElems });
    setSelectedElementId(updatedElems[0]?.id || null);
  };

  return (
    <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-md flex items-center justify-center p-4 select-none font-sans">
      <div className="bg-[#0e0e18] border border-white/20 rounded-xl w-full max-w-5xl h-[80vh] shadow-2xl flex flex-col overflow-hidden">
        {/* Header */}
        <div className="h-9 bg-[#090910] border-b border-white/10 px-3 flex items-center justify-between text-xs">
          <div className="flex items-center gap-2">
            <Layout className="w-4 h-4 text-indigo-400" />
            <span className="font-bold text-white tracking-wide">UMG / UI Widget Blueprint Designer</span>
            <span className="text-[10px] text-slate-500 font-mono">({widgets.length} layoutów)</span>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={handleCreateNewWidget}
              className="flex items-center gap-1 px-2.5 py-1 rounded bg-indigo-600/30 hover:bg-indigo-600/50 text-indigo-200 border border-indigo-500/40 text-[11px] font-semibold transition-all"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>+ Nowy Widget HUD</span>
            </button>
            <button onClick={onClose} className="p-1 text-slate-400 hover:text-white">
              <X className="w-4 h-4" />
            </button>
          </div>
        </div>

        {/* Main Body */}
        <div className="flex-1 flex overflow-hidden">
          {/* 1. Left: Widgets List & Element Palette */}
          <div className="w-60 border-r border-white/10 bg-[#0a0a12] flex flex-col p-2 space-y-3 shrink-0 overflow-y-auto">
            <div>
              <div className="text-[9px] uppercase font-mono tracking-wider text-slate-500 px-1 mb-1">Widget Blueprints</div>
              <div className="space-y-1">
                {widgets.map(w => (
                  <div
                    key={w.id}
                    onClick={() => { setSelectedWidgetId(w.id); setSelectedElementId(w.elements[0]?.id || null); }}
                    className={`p-2 rounded cursor-pointer transition-all flex items-center justify-between group ${
                      selectedWidget?.id === w.id
                        ? 'bg-indigo-600/25 border border-indigo-500/50 text-indigo-200'
                        : 'bg-white/[0.02] hover:bg-white/5 border border-transparent text-slate-300'
                    }`}
                  >
                    <div className="flex items-center gap-1.5 truncate">
                      <Layout className="w-3.5 h-3.5 text-indigo-400" />
                      <span className="text-[11px] font-medium truncate">{w.name}</span>
                    </div>
                    <div className="flex items-center gap-1">
                      {activeWidgetId === w.id ? (
                        <span className="text-[9px] px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-bold">AKTYWNY</span>
                      ) : (
                        <button
                          onClick={(e) => { e.stopPropagation(); onSetActiveWidget(w.id); }}
                          className="opacity-0 group-hover:opacity-100 text-[9px] text-slate-400 hover:text-white"
                          title="Ustaw jako aktywny HUD"
                        >
                          Włącz
                        </button>
                      )}
                      <button
                        onClick={(e) => { e.stopPropagation(); onDeleteWidget(w.id); }}
                        className="opacity-0 group-hover:opacity-100 p-0.5 hover:text-rose-400"
                        title="Usuń"
                      >
                        <Trash2 className="w-3 h-3" />
                      </button>
                    </div>
                  </div>
                ))}
                {widgets.length === 0 && (
                  <div className="text-center py-4 text-slate-600 text-[10px]">Brak zdefiniowanych widżetów.</div>
                )}
              </div>
            </div>

            {/* Element Palette */}
            {selectedWidget && (
              <div className="pt-2 border-t border-white/10 space-y-2">
                <div className="text-[9px] uppercase font-mono tracking-wider text-slate-500 px-1">Dodaj Element do HUD</div>
                <div className="grid grid-cols-2 gap-1.5">
                  <button
                    onClick={() => handleAddElement('text')}
                    className="p-2 rounded bg-white/5 hover:bg-white/10 border border-white/10 text-left text-xs text-slate-200 flex flex-col gap-1 transition-all"
                  >
                    <Type className="w-3.5 h-3.5 text-cyan-400" />
                    <span className="text-[10px] font-medium">Text Block</span>
                  </button>
                  <button
                    onClick={() => handleAddElement('progressBar')}
                    className="p-2 rounded bg-white/5 hover:bg-white/10 border border-white/10 text-left text-xs text-slate-200 flex flex-col gap-1 transition-all"
                  >
                    <Percent className="w-3.5 h-3.5 text-emerald-400" />
                    <span className="text-[10px] font-medium">Progress Bar</span>
                  </button>
                  <button
                    onClick={() => handleAddElement('button')}
                    className="p-2 rounded bg-white/5 hover:bg-white/10 border border-white/10 text-left text-xs text-slate-200 flex flex-col gap-1 transition-all"
                  >
                    <MousePointer className="w-3.5 h-3.5 text-amber-400" />
                    <span className="text-[10px] font-medium">Button</span>
                  </button>
                  <button
                    onClick={() => handleAddElement('panel')}
                    className="p-2 rounded bg-white/5 hover:bg-white/10 border border-white/10 text-left text-xs text-slate-200 flex flex-col gap-1 transition-all"
                  >
                    <Square className="w-3.5 h-3.5 text-indigo-400" />
                    <span className="text-[10px] font-medium">Panel</span>
                  </button>
                </div>
              </div>
            )}
          </div>

          {/* 2. Center: 16:9 Viewport Canvas */}
          <div className="flex-1 flex flex-col bg-[#08080f] overflow-hidden">
            <div className="h-8 bg-[#0b0b14] border-b border-white/10 px-3 flex items-center justify-between text-xs text-slate-400">
              <span className="font-mono text-[10px]">Viewport Screen Preview (16:9 1920x1080)</span>
              {selectedWidget && (
                <div className="flex items-center gap-2">
                  <button
                    onClick={() => onSetActiveWidget(activeWidgetId === selectedWidget.id ? undefined : selectedWidget.id)}
                    className={`px-2 py-0.5 rounded text-[10px] font-bold transition-all ${
                      activeWidgetId === selectedWidget.id
                        ? 'bg-emerald-600 text-white'
                        : 'bg-white/10 text-slate-300 hover:bg-white/20'
                    }`}
                  >
                    {activeWidgetId === selectedWidget.id ? '✓ Aktywny w grze' : 'Ustaw jako HUD Gry'}
                  </button>
                </div>
              )}
            </div>

            <div className="flex-1 flex items-center justify-center p-4 overflow-hidden relative">
              {/* 16:9 Game Screen Container */}
              <div className="w-full max-w-2xl aspect-video bg-[#050508] border-2 border-white/15 rounded-lg relative overflow-hidden shadow-2xl">
                {selectedWidget?.elements.map(el => (
                  <div
                    key={el.id}
                    onClick={() => setSelectedElementId(el.id)}
                    style={{
                      left: `${el.x}%`,
                      top: `${el.y}%`,
                      width: `${el.width}%`,
                      height: `${el.height}%`,
                    }}
                    className={`absolute cursor-pointer transition-all flex items-center justify-center p-1 ${
                      selectedElementId === el.id ? 'ring-2 ring-indigo-400 ring-offset-1 ring-offset-black' : 'hover:outline hover:outline-1 hover:outline-white/30'
                    }`}
                  >
                    {el.type === 'text' && (
                      <span style={{ fontSize: `${el.fontSize || 12}px`, color: el.color || '#fff' }} className="font-bold font-mono truncate w-full">
                        {el.bindVariable ? `{${el.bindVariable}}` : (el.text || 'Text')}
                      </span>
                    )}

                    {el.type === 'progressBar' && (
                      <div className="w-full h-full rounded overflow-hidden flex flex-col justify-center" style={{ backgroundColor: el.backgroundColor || '#1e293b' }}>
                        <div
                          className="h-full transition-all"
                          style={{
                            width: `${el.value ?? 80}%`,
                            backgroundColor: el.color || '#10b981',
                          }}
                        />
                      </div>
                    )}

                    {el.type === 'button' && (
                      <button
                        style={{
                          backgroundColor: el.backgroundColor || '#3b82f6',
                          color: el.color || '#fff',
                          borderRadius: `${el.borderRadius || 4}px`,
                        }}
                        className="w-full h-full text-xs font-bold shadow transition-all truncate"
                      >
                        {el.text || 'Button'}
                      </button>
                    )}

                    {el.type === 'panel' && (
                      <div
                        style={{
                          backgroundColor: el.backgroundColor || 'rgba(15, 23, 42, 0.8)',
                          borderColor: el.borderColor || 'rgba(255, 255, 255, 0.1)',
                          borderRadius: `${el.borderRadius || 6}px`,
                        }}
                        className="w-full h-full border"
                      />
                    )}
                  </div>
                ))}
              </div>
            </div>
          </div>

          {/* 3. Right: Inspector & Variable Binding */}
          {selectedElement ? (
            <div className="w-64 border-l border-white/10 bg-[#0d0d16] p-3 flex flex-col space-y-3 overflow-y-auto text-xs shrink-0">
              <div className="flex items-center justify-between pb-2 border-b border-white/10">
                <span className="font-bold text-white text-[11px] truncate">{selectedElement.name}</span>
                <button onClick={handleDeleteSelectedElement} className="text-rose-400 hover:text-rose-300 p-0.5" title="Usuń element">
                  <Trash2 className="w-3.5 h-3.5" />
                </button>
              </div>

              {/* Element Name */}
              <div className="space-y-1">
                <label className="text-[10px] text-slate-400">Nazwa Elementu</label>
                <input
                  type="text"
                  value={selectedElement.name}
                  onChange={e => handleUpdateSelectedElement({ name: e.target.value })}
                  className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white text-xs"
                />
              </div>

              {/* Position (X%, Y%) */}
              <div className="grid grid-cols-2 gap-2">
                <div className="space-y-1">
                  <label className="text-[10px] text-slate-400">Pozycja X (%)</label>
                  <input
                    type="number"
                    min="0"
                    max="100"
                    value={selectedElement.x}
                    onChange={e => handleUpdateSelectedElement({ x: parseFloat(e.target.value) || 0 })}
                    className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white font-mono text-xs"
                  />
                </div>
                <div className="space-y-1">
                  <label className="text-[10px] text-slate-400">Pozycja Y (%)</label>
                  <input
                    type="number"
                    min="0"
                    max="100"
                    value={selectedElement.y}
                    onChange={e => handleUpdateSelectedElement({ y: parseFloat(e.target.value) || 0 })}
                    className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white font-mono text-xs"
                  />
                </div>
              </div>

              {/* Size (W%, H%) */}
              <div className="grid grid-cols-2 gap-2">
                <div className="space-y-1">
                  <label className="text-[10px] text-slate-400">Szerokość W (%)</label>
                  <input
                    type="number"
                    min="1"
                    max="100"
                    value={selectedElement.width}
                    onChange={e => handleUpdateSelectedElement({ width: parseFloat(e.target.value) || 5 })}
                    className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white font-mono text-xs"
                  />
                </div>
                <div className="space-y-1">
                  <label className="text-[10px] text-slate-400">Wysokość H (%)</label>
                  <input
                    type="number"
                    min="1"
                    max="100"
                    value={selectedElement.height}
                    onChange={e => handleUpdateSelectedElement({ height: parseFloat(e.target.value) || 5 })}
                    className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white font-mono text-xs"
                  />
                </div>
              </div>

              {/* Dynamic Variable Binding (Key Engine Feature!) */}
              <div className="pt-2 border-t border-white/10 space-y-1">
                <label className="text-[10px] font-bold text-amber-400 uppercase tracking-wider">Dynamiczne Wiązanie (Variable Binding)</label>
                <input
                  type="text"
                  placeholder="np. health, score, mana, ammo, speed..."
                  value={selectedElement.bindVariable || ''}
                  onChange={e => handleUpdateSelectedElement({ bindVariable: e.target.value })}
                  className="w-full bg-[#181824] border border-amber-500/40 rounded px-2 py-1 text-amber-300 font-mono text-xs focus:outline-none focus:border-amber-400"
                />
                <p className="text-[9px] text-slate-500">
                  Wartość będzie automatycznie pobierana ze skryptów gry w czasie rzeczywistym!
                </p>
              </div>

              {/* Specific Properties based on Type */}
              {selectedElement.type === 'text' && (
                <div className="space-y-2 pt-2 border-t border-white/10">
                  <div className="space-y-1">
                    <label className="text-[10px] text-slate-400">Domyślny Tekst</label>
                    <input
                      type="text"
                      value={selectedElement.text || ''}
                      onChange={e => handleUpdateSelectedElement({ text: e.target.value })}
                      className="w-full bg-[#181824] border border-white/10 rounded px-2 py-1 text-white text-xs"
                    />
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-[10px] text-slate-400">Kolor Tekstu</span>
                    <input
                      type="color"
                      value={selectedElement.color || '#ffffff'}
                      onChange={e => handleUpdateSelectedElement({ color: e.target.value })}
                      className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                    />
                  </div>
                </div>
              )}

              {selectedElement.type === 'progressBar' && (
                <div className="space-y-2 pt-2 border-t border-white/10">
                  <div className="flex items-center justify-between">
                    <span className="text-[10px] text-slate-400">Kolor Paska</span>
                    <input
                      type="color"
                      value={selectedElement.color || '#10b981'}
                      onChange={e => handleUpdateSelectedElement({ color: e.target.value })}
                      className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                    />
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-[10px] text-slate-400">Kolor Tła</span>
                    <input
                      type="color"
                      value={selectedElement.backgroundColor || '#1e293b'}
                      onChange={e => handleUpdateSelectedElement({ backgroundColor: e.target.value })}
                      className="w-7 h-5 rounded cursor-pointer border border-white/20 bg-transparent"
                    />
                  </div>
                </div>
              )}
            </div>
          ) : (
            <div className="w-64 border-l border-white/10 bg-[#0d0d16] p-4 flex flex-col items-center justify-center text-center text-slate-500 text-xs shrink-0">
              Wybierz element na podglądzie, aby skonfigurować jego pozycję, styl i zmienne gry.
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
