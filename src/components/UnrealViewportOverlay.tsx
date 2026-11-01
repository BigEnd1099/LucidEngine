import React, { useState } from 'react';
import {
  Move, RotateCw, Scaling, Globe, Magnet,
  Camera, ChevronDown, Check, Monitor, Sliders
} from 'lucide-react';

interface UnrealViewportOverlayProps {
  gizmoMode: 'translate' | 'rotate' | 'scale';
  setGizmoMode: (m: 'translate' | 'rotate' | 'scale') => void;
  gizmoSpace: 'world' | 'local';
  setGizmoSpace: (s: 'world' | 'local') => void;
  snapEnabled: boolean;
  setSnapEnabled: (v: boolean) => void;
  snapStep: number;
  setSnapStep: (v: number) => void;
  viewMode: 'three' | 'vulkan';
  setViewMode: (v: 'three' | 'vulkan') => void;
  resolutionScale?: number;
  setResolutionScale?: (scale: number) => void;
  renderResolution?: { width: number; height: number };
  setRenderResolution?: (res: { width: number; height: number }) => void;
  uiScale?: number;
  setUiScale?: (scale: number) => void;
}

export const UnrealViewportOverlay: React.FC<UnrealViewportOverlayProps> = ({
  gizmoMode,
  setGizmoMode,
  gizmoSpace,
  setGizmoSpace,
  snapEnabled,
  setSnapEnabled,
  snapStep,
  setSnapStep,
  viewMode,
  setViewMode,
  resolutionScale = 1.0,
  setResolutionScale,
  renderResolution = { width: 1920, height: 1080 },
  setRenderResolution,
  uiScale = 1.0,
  setUiScale,
}) => {
  const [showSnapMenu, setShowSnapMenu] = useState(false);
  const [showViewMenu, setShowViewMenu] = useState(false);
  const [showResMenu, setShowResMenu] = useState(false);
  const [showUiScaleMenu, setShowUiScaleMenu] = useState(false);
  const [cameraSpeed, setCameraSpeed] = useState(4);
  const [showCameraSpeedMenu, setShowCameraSpeedMenu] = useState(false);

  return (
    <div className="absolute top-2 left-2 right-2 z-10 flex items-center justify-between pointer-events-none select-none">
      {/* ── Left Viewport Controls: Gizmo Tools, Coordinate Space, Snapping ── */}
      <div className="flex items-center gap-1 bg-[#101018]/90 backdrop-blur-md border border-white/10 rounded-md p-0.5 shadow-xl pointer-events-auto text-[11px] font-mono">
        {/* Transform: Translate (W) */}
        <button
          onClick={() => setGizmoMode('translate')}
          className={`p-1.5 rounded transition-all ${
            gizmoMode === 'translate'
              ? 'bg-cyan-500/25 text-cyan-300 border border-cyan-500/40 shadow-sm'
              : 'text-slate-400 hover:text-white'
          }`}
          title="Select and Translate Objects (W)"
        >
          <Move className="w-3.5 h-3.5" />
        </button>

        {/* Transform: Rotate (E) */}
        <button
          onClick={() => setGizmoMode('rotate')}
          className={`p-1.5 rounded transition-all ${
            gizmoMode === 'rotate'
              ? 'bg-indigo-500/25 text-indigo-300 border border-indigo-500/40 shadow-sm'
              : 'text-slate-400 hover:text-white'
          }`}
          title="Select and Rotate Objects (E)"
        >
          <RotateCw className="w-3.5 h-3.5" />
        </button>

        {/* Transform: Scale (R) */}
        <button
          onClick={() => setGizmoMode('scale')}
          className={`p-1.5 rounded transition-all ${
            gizmoMode === 'scale'
              ? 'bg-emerald-500/25 text-emerald-300 border border-emerald-500/40 shadow-sm'
              : 'text-slate-400 hover:text-white'
          }`}
          title="Select and Scale Objects (R)"
        >
          <Scaling className="w-3.5 h-3.5" />
        </button>

        <div className="h-4 w-px bg-white/10 mx-0.5" />

        {/* Coordinate System (World / Local - Q) */}
        <button
          onClick={() => setGizmoSpace(gizmoSpace === 'world' ? 'local' : 'world')}
          className={`flex items-center gap-1 px-2 py-1 rounded text-[10px] transition-all ${
            gizmoSpace === 'world'
              ? 'text-cyan-300 bg-cyan-500/10 border border-cyan-500/30'
              : 'text-indigo-300 bg-indigo-500/10 border border-indigo-500/30'
          }`}
          title="Cycle Transform Coordinate System: World / Local (Q)"
        >
          <Globe className="w-3 h-3" />
          <span>{gizmoSpace === 'world' ? 'World' : 'Local'}</span>
        </button>

        <div className="h-4 w-px bg-white/10 mx-0.5" />

        {/* Surface / Grid Snapping Button */}
        <div className="relative">
          <button
            onClick={() => setSnapEnabled(!snapEnabled)}
            className={`flex items-center gap-1 px-2 py-1 rounded text-[10px] transition-all ${
              snapEnabled
                ? 'text-amber-300 bg-amber-500/20 border border-amber-500/40 shadow-sm'
                : 'text-slate-500 hover:text-slate-300'
            }`}
            title="Toggle Grid Snapping"
          >
            <Magnet className="w-3 h-3 text-amber-400" />
            <span>{snapEnabled ? `${snapStep}m` : 'Snap: Off'}</span>
          </button>
        </div>

        {/* Snap Values Dropdown */}
        <div className="relative">
          <button
            onClick={() => setShowSnapMenu(!showSnapMenu)}
            className="p-1 rounded text-slate-400 hover:text-white"
          >
            <ChevronDown className="w-3 h-3" />
          </button>

          {showSnapMenu && (
            <div
              className="absolute top-full left-0 mt-1 w-28 bg-[#181824] border border-white/15 rounded-md shadow-2xl py-1 z-50 text-[10px]"
              onMouseLeave={() => setShowSnapMenu(false)}
            >
              {[0.1, 0.5, 1.0, 5.0, 10.0].map(val => (
                <button
                  key={val}
                  onClick={() => { setSnapStep(val); setSnapEnabled(true); setShowSnapMenu(false); }}
                  className="w-full px-2.5 py-1 text-left hover:bg-cyan-500/20 text-slate-200 flex items-center justify-between"
                >
                  <span>{val} meters</span>
                  {snapStep === val && snapEnabled && <Check className="w-3 h-3 text-cyan-400" />}
                </button>
              ))}
            </div>
          )}
        </div>

        <div className="h-4 w-px bg-white/10 mx-0.5" />

        {/* Camera Speed (UE5 style) */}
        <div className="relative">
          <button
            onClick={() => setShowCameraSpeedMenu(!showCameraSpeedMenu)}
            className="flex items-center gap-1 px-2 py-1 rounded text-slate-300 hover:text-white text-[10px]"
            title="Camera Speed"
          >
            <Camera className="w-3 h-3 text-cyan-400" />
            <span>{cameraSpeed}</span>
          </button>

          {showCameraSpeedMenu && (
            <div
              className="absolute top-full left-0 mt-1 w-32 bg-[#181824] border border-white/15 rounded-md shadow-2xl p-2 z-50 text-[10px]"
              onMouseLeave={() => setShowCameraSpeedMenu(false)}
            >
              <div className="text-slate-400 mb-1">Camera Speed: {cameraSpeed}</div>
              <input
                type="range"
                min="1"
                max="8"
                step="1"
                value={cameraSpeed}
                onChange={e => setCameraSpeed(parseInt(e.target.value))}
                className="w-full accent-cyan-400"
              />
            </div>
          )}
        </div>
      </div>

      {/* ── Right Viewport Controls: Perspective, Lit Mode, Vulkan toggle ── */}
      <div className="flex items-center gap-1 bg-[#101018]/90 backdrop-blur-md border border-white/10 rounded-md p-0.5 shadow-xl pointer-events-auto text-[11px] font-sans">
        {/* Perspective Mode Button */}
        <div className="px-2 py-1 text-slate-300 text-[10px] font-medium flex items-center gap-1">
          <span>Perspective</span>
        </div>

        <div className="h-4 w-px bg-white/10" />

        {/* View Mode Dropdown (WebGL vs Vulkan GPU) */}
        <div className="relative">
          <button
            onClick={() => setShowViewMenu(!showViewMenu)}
            className="flex items-center gap-1 px-2.5 py-1 rounded text-cyan-300 hover:bg-white/5 text-[10px] font-medium"
          >
            <span>{viewMode === 'three' ? 'WebGL' : 'Vulkan (GPU)'}</span>
            <ChevronDown className="w-3 h-3 text-slate-400" />
          </button>

          {showViewMenu && (
            <div
              className="absolute top-full right-0 mt-1 w-40 bg-[#181824] border border-white/15 rounded-md shadow-2xl py-1 z-50 text-[11px]"
              onMouseLeave={() => setShowViewMenu(false)}
            >
              <button
                onClick={() => { setViewMode('three'); setShowViewMenu(false); }}
                className="w-full px-3 py-1.5 text-left hover:bg-cyan-500/20 text-slate-200 flex items-center justify-between"
              >
                <span>WebGL</span>
                {viewMode === 'three' && <Check className="w-3 h-3 text-cyan-400" />}
              </button>
              <button
                onClick={() => { setViewMode('vulkan'); setShowViewMenu(false); }}
                className="w-full px-3 py-1.5 text-left hover:bg-cyan-500/20 text-slate-200 flex items-center justify-between"
              >
                <span>Vulkan (GPU)</span>
                {viewMode === 'vulkan' && <Check className="w-3 h-3 text-emerald-400" />}
              </button>
            </div>
          )}
        </div>

        <div className="h-4 w-px bg-white/10" />

        {/* Resolution Scaling Dropdown (Screen Percentage) */}
        <div className="relative">
          <button
            onClick={() => setShowResMenu(!showResMenu)}
            className="flex items-center gap-1 px-2 py-1 rounded text-emerald-400 hover:bg-white/5 text-[10px] font-medium"
            title="Screen Percentage / Render Resolution Scaling"
          >
            <Monitor className="w-3 h-3 text-emerald-400" />
            <span>{Math.round(resolutionScale * 100)}% ({renderResolution.width}x{renderResolution.height})</span>
            <ChevronDown className="w-3 h-3 text-slate-400" />
          </button>

          {showResMenu && (
            <div
              className="absolute top-full right-0 mt-1 w-56 bg-[#14141e] border border-white/15 rounded-md shadow-2xl p-2 z-50 text-[10px] font-sans"
              onMouseLeave={() => setShowResMenu(false)}
            >
              <div className="text-[10px] font-mono uppercase text-slate-400 mb-1.5 flex items-center justify-between">
                <span>Screen Percentage</span>
                <span className="text-emerald-400 font-bold">{Math.round(resolutionScale * 100)}%</span>
              </div>
              <input
                type="range"
                min="0.5"
                max="2.0"
                step="0.05"
                value={resolutionScale}
                onChange={e => {
                  const val = parseFloat(e.target.value);
                  setResolutionScale?.(val);
                }}
                className="w-full accent-emerald-400 mb-2 cursor-pointer"
              />
              <div className="h-px bg-white/10 my-1" />
              <div className="text-[9px] uppercase font-mono text-slate-500 mb-1">Resolution Presets</div>
              {[
                { label: '50% (Performance - 960x540)', scale: 0.5, w: 960, h: 540 },
                { label: '67% (FSR Balanced - 1280x720)', scale: 0.67, w: 1280, h: 720 },
                { label: '75% (FSR Quality - 1440x810)', scale: 0.75, w: 1440, h: 810 },
                { label: '100% (Native Full HD - 1920x1080)', scale: 1.0, w: 1920, h: 1080 },
                { label: '133% (2K QHD - 2560x1440)', scale: 1.33, w: 2560, h: 1440 },
                { label: '200% (4K Ultra HD - 3840x2160)', scale: 2.0, w: 3840, h: 2160 },
              ].map(item => (
                <button
                  key={item.label}
                  onClick={() => {
                    setResolutionScale?.(item.scale);
                    setRenderResolution?.({ width: item.w, height: item.h });
                    setShowResMenu(false);
                  }}
                  className="w-full px-2 py-1 text-left hover:bg-emerald-500/20 text-slate-300 hover:text-white rounded flex items-center justify-between"
                >
                  <span>{item.label}</span>
                  {Math.abs(resolutionScale - item.scale) < 0.03 && <Check className="w-3 h-3 text-emerald-400" />}
                </button>
              ))}
            </div>
          )}
        </div>

        <div className="h-4 w-px bg-white/10" />

        {/* UI Scale Dropdown */}
        <div className="relative">
          <button
            onClick={() => setShowUiScaleMenu(!showUiScaleMenu)}
            className="flex items-center gap-1 px-2 py-1 rounded text-cyan-300 hover:bg-white/5 text-[10px] font-medium"
            title="User Interface Scaling"
          >
            <Sliders className="w-3 h-3 text-cyan-400" />
            <span>UI: {Math.round(uiScale * 100)}%</span>
            <ChevronDown className="w-3 h-3 text-slate-400" />
          </button>

          {showUiScaleMenu && (
            <div
              className="absolute top-full right-0 mt-1 w-44 bg-[#14141e] border border-white/15 rounded-md shadow-2xl p-2 z-50 text-[10px] font-sans"
              onMouseLeave={() => setShowUiScaleMenu(false)}
            >
              <div className="text-[10px] font-mono uppercase text-slate-400 mb-1.5 flex items-center justify-between">
                <span>UI Scale</span>
                <span className="text-cyan-400 font-bold">{Math.round(uiScale * 100)}%</span>
              </div>
              <input
                type="range"
                min="0.75"
                max="1.75"
                step="0.05"
                value={uiScale}
                onChange={e => {
                  const val = parseFloat(e.target.value);
                  setUiScale?.(val);
                }}
                className="w-full accent-cyan-400 mb-2 cursor-pointer"
              />
              <div className="h-px bg-white/10 my-1" />
              <div className="text-[9px] uppercase font-mono text-slate-500 mb-1">DPI Presets</div>
              {[
                { label: '80% (Compact)', scale: 0.8 },
                { label: '90% (Dense)', scale: 0.9 },
                { label: '100% (Standard)', scale: 1.0 },
                { label: '110% (Comfortable)', scale: 1.1 },
                { label: '125% (1440p Medium)', scale: 1.25 },
                { label: '150% (4K Large)', scale: 1.5 },
              ].map(item => (
                <button
                  key={item.label}
                  onClick={() => {
                    setUiScale?.(item.scale);
                    setShowUiScaleMenu(false);
                  }}
                  className="w-full px-2 py-1 text-left hover:bg-cyan-500/20 text-slate-300 hover:text-white rounded flex items-center justify-between"
                >
                  <span>{item.label}</span>
                  {Math.abs(uiScale - item.scale) < 0.03 && <Check className="w-3 h-3 text-cyan-400" />}
                </button>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
