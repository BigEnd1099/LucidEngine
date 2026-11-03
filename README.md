# ⚡ Lucid Engine

<div align="center">

**Wysokowydajny, natywny silnik gier 3D nowej generacji**  
*(Vulkan 1.3 Swapchain + C++20 + Jolt Physics + QuickJS Runtime + Native ImGui Studio)*

[![Vulkan](https://img.shields.io/badge/Vulkan-1.3%20HDR-red.svg)](https://www.vulkan.org/)
[![C++20](https://img.shields.io/badge/C++-20-00599C.svg)](https://en.cppreference.com/)
[![Jolt Physics](https://img.shields.io/badge/Jolt%20Physics-Realtime-green.svg)](https://github.com/jrouwe/JoltPhysics)
[![QuickJS](https://img.shields.io/badge/QuickJS-Live%20Coding-yellow.svg)](https://bellard.org/quickjs/)
[![ImGui](https://img.shields.io/badge/ImGui-1.90.8-blue.svg)](https://github.com/ocornut/imgui)

</div>

---

## 🌟 Architektura: 100% Natywny Silnik C++ / Vulkan

**Lucid Engine** stawia na **maksymalną wydajność i zero opóźnień (0ms input lag)**:
- **Domyślny tryb uruchomienia**: 100% natywna aplikacja C++ renderująca bezpośrednio do swapchaina Vulkan 1.3 przy pełnej częstotliwości odświeżania monitora (144Hz+).
- **Zero streamowania klatek wideo**: Obraz nie jest kompresowany ani przesyłany przez WebSocket! Klatki trafiają bezpośrednio na GPU (Vulkan Dynamic Rendering).
- **Zintegrowane Studio Edycyjne**: Wbudowany, natywny edytor **Dear ImGui** (Dark Slate theme) renderowany bezpośrednio w oknie Vulkan z obsługą hierarchii sceny (Outliner), inspektora właściwości (Details), skryptowania QuickJS w locie (Hot-Reload) oraz manipulacji oświetleniem i środowiskiem.
- **Port 9000 (Opcjonalny Zdalny Interfejs / MCP)**: Serwer w tle służy wyłącznie jako zdalna telemetria, interfejs JSON-RPC oraz mostek MCP dla agentów AI i narzędzi CLI. W trybie `--no-remote` silnik nie otwiera żadnych portów sieciowych.

---

## 🏗️ Diagram Architektury

```
┌────────────────────────────────────────────────────────────────────────┐
│             LUCID ENGINE NATIVE WINDOW (GLFW / VULKAN 1.3)             │
│                                                                        │
│   ┌──────────────────────────────────────────────────────────────┐     │
│   │               DEAR IMGUI NATIVE STUDIO OVERLAY               │     │
│   │  - Pasek Menu (Plik, Presety, Symulacja, Widok, Metryki FPS) │     │
│   │  - Hierarchia Sceny (Outliner + Filtrowanie + Szybki Spawn)  │     │
│   │  - Właściwości (Transform + Jolt RigidBody + PBR + Akcje AAA)│     │
│   │  - Studio Skryptów (QuickJS ESM Live Coding & Hot-Reload)    │     │
│   │  - Środowisko & Oświetlenie (Słońce, Mgła, Grawitacja)       │     │
│   │  - Pasek Nawigacji (PPM+WASD Kamera, F1-F7 Skróty)           │     │
│   └──────────────────────────────┬───────────────────────────────┘     │
│                                  │                                     │
│   ┌──────────────────────────────▼───────────────────────────────┐     │
│   │                 VULKAN 1.3 HDR RENDER PIPELINE               │     │
│   │  - Direct Swapchain Presentation (0ms Latency)               │     │
│   │  - Cascaded Shadow Mapping (CSM) & Virtual Shadow Maps       │     │
│   │  - Dynamic Rendering (KHR_dynamic_rendering)                 │     │
│   │  - Niagara Particle Compute Pipeline                         │     │
│   └──────────────────────────────────────────────────────────────┘     │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
 ┌─────────────────────────────────┼─────────────────────────────────┐
 │ SYSTEMY RDZENIA C++20           │ OPCJONALNY BACKGROUND TOOLING   │
 ├─────────────────────────────────┼─────────────────────────────────┤
 │ • Jolt Physics (Wielowątkowa)   │ • Port 9000 JSON-RPC / MCP      │
 │ • QuickJS Runtime (ESM)         │ • Agent Orchestrator (Gemini)   │
 │ • Spatial Audio Engine (3D)     │ • Dual-Transport SharedRing     │
 │ • Emergent Chemistry (TotK)     │ • Opcjonalne Studio Web/Electron│
 │ • Voronoi Mesh Fracture         │   (LucidEngine_WebRemote.bat)   │
 └─────────────────────────────────┴─────────────────────────────────┘
```

---

## ⌨️ Sterowanie i Skróty Klawiszowe

| Klawisz / Skrót | Akcja |
|-----------------|-------|
| `PPM` + `W` `A` `S` `D` | **Swobodna Kamera Lotu (Fly Camera)** |
| `PPM` + `Q` / `E` | Ruch kamery w dół / w górę |
| `Shift` (przy locie) | 3x przyspieszenie kamery |
| `LPM` | Wybór obiektów w oknie lub na liście |
| `F1` | **Włącz / Wyłącz Studio Edycyjne** (Przełącznik Pełny Ekran 3D) |
| `F2` | Przełącz okno **Hierarchii Sceny (Outliner)** |
| `F3` | Przełącz okno **Właściwości (Inspector)** |
| `F4` | Przełącz okno **Studia Skryptów (QuickJS)** |
| `F5` | **Uruchom Symulację (Play)** |
| `F6` | **Pauza Symulacji (Pause)** |
| `F7` | **Zatrzymaj i Przywróć Scenę (Stop)** |
| `Delete` | Usunięcie zaznaczonej encji |

---

## 🚀 Szybki Start

### 1. Uruchomienie natywnego silnika (Zalecane):
Kliknij dwukrotnie plik:
```bash
LucidEngine.bat
```
*Plik automatycznie zweryfikuje obecność skompilowanego silnika i uruchomi bezpośrednie, natywne okno 144Hz Vulkan 1.3 ze studiem ImGui.*

### 2. Kompilacja silnika C++ (MSVC / CMake):
```bash
.\build_engine.bat release
```
*Wielowątkowa kompilacja w Visual Studio 2022 Community z pełną optymalizacją Release.*

### 3. Uruchomienie pełnego pakietu testów (12/12):
```bash
npm test
```
*Weryfikuje m.in. parzystość struktur C++, fizykę Jolt, QuickJS hot-reload, reakcje chemiczne i kompresję delta.*

### 4. Opcjonalne uruchomienie zdalnego studia Web / Electron:
```bash
LucidEngine_WebRemote.bat
```

---

## 📦 Struktura Katalogów

- **`engine/`** — Źródła natywnego silnika C++20:
  - **`src/renderer/`** — Vulkan 1.3 Renderer, Swapchain, PBR, Shaders, ImGui Studio.
  - **`src/core/`** — Główna pętla `Engine.cpp`, sterowanie kamerą, zarządzanie sceną.
  - **`src/physics/`** — Jolt Physics, kolizje, reakcje emergentne, destrukcja Voronoi.
  - **`src/script/`** — Środowisko wykonawcze QuickJS, bindingi C++ do JavaScript.
  - **`include/`** — Nagłówki publiczne ECS (`Components.hpp`, `Registry.hpp`).
- **`src/`** — Opcjonalne studio webowe w React 18, Three.js i Tailwind CSS.
- **`tests/`** — Zestaw zautomatyzowanych testów E2E sprawdzających każdy podsystem silnika.
- **`tools/`** — Skrypty pomocnicze kompilacji i generatory typów.

---

## 📄 Licencja

Wszelkie prawa zastrzeżone © 2026 Lucid Engine Team.
