# Architektura i Roadmapa Silnika Lucid Engine

Lucid Engine to zorientowany na współpracę ze sztuczną inteligencją (*AI-Native*) lekki silnik gier 3D czasu rzeczywistego, oparty na C++20, Vulkan 1.3, EnTT (ECS) oraz silniku fizyki Jolt Physics, wyposażony w natywny serwer protokołu **Model Context Protocol (MCP)** oraz mostek **WebSocket JSON-RPC** dla zewnętrznych studiów deweloperskich i autonomicznych agentów LLM.

---

## 1. Architektura Systemu (Zrealizowana)

```
                    ┌──────────────────────────────────────────────┐
                    │      Autonomiczny Agent LLM / Klient MCP     │
                    └──────────────────────┬───────────────────────┘
                                           │ stdio (JSON-RPC)
                                           ▼
┌──────────────────────┐        ┌──────────────────────┐
│  React Studio / Web  │◄──────►│ Natywny Serwer MCP   │
│  (Three.js Viewport) │ WS 9000│ (Wątek IO + Kolejka) │
└──────────────────────┘        └──────────┬───────────┘
                                           │ ThreadQueue (Promise/Future)
                                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           LUCID ENGINE CORE                             │
│                                                                         │
│  ┌───────────────────┐    ┌────────────────────┐    ┌─────────────────┐ │
│  │    EnTT (ECS)     │    │  Jolt Physics 5.0  │    │   QuickJS-ng    │ │
│  │ Rejestr encji     │◄──►│ 60Hz Fixed Timestep│◄──►│ Bezpośredni     │ │
│  │ i komponentów     │    │ Dynamic Rigidbody  │    │ mostek C++ (API)│ │
│  └─────────┬─────────┘    └────────────────────┘    └─────────────────┘ │
│            │                                                            │
│            ▼                                                            │
│  ┌─────────────────────────────────────────────┐                        │
│  │             Vulkan 1.3 Renderer             │                        │
│  │  - Cook-Torrance GGX Specular BRDF          │                        │
│  │  - Disney/Burley Diffuse + ACES Tonemapping │                        │
│  │  - Dynamiczna synchronizacja kamery         │                        │
│  │  - Buforowany Frame Capture dla Web UI      │                        │
│  └─────────────────────────────────────────────┘                        │
└─────────────────────────────────────────────────────────────────────────┘
```

### A. Wątkowość i Model Context Protocol (MCP)
* **Thread-Safe Main Loop Dispatch:** Serwer MCP działający na strumieniu `stdio` nie modyfikuje bezpośrednio zasobów silnika. Wszystkie narzędzia (`spawn_entity`, `destroy_entity`, `reset_scene`, `load_game_script`, `step_simulation`) są szeregowane w kolejce `ThreadQueue` i wykonywane synchronicznie na głównym wątku pętli gry z użyciem `std::promise` i `std::future`.
* **Zero Race Conditions:** Wyeliminowano ryzyko równoczesnego odczytu/zapisu do rejestru `entt::registry` czy kolizji w buforze komend Vulkana.

### B. Środowisko Skryptowe QuickJS (Wydajność)
* **Direct C++ Binding:** Zamiast kosztownej serializacji do formatu tekstowego JSON (`JSON.parse` / `JSON.stringify`) w każdej klatce, silnik manipuluje obiektami JS bezpośrednio przez C API QuickJS (`tickDirect`).
* **Zabezpieczenie pamięci i ochrona przed pętlami:** Sandbox ma sztywny limit pamięci (64 MB) oraz handler przerwań (`JS_SetInterruptHandler`).

### C. Fizyka (Jolt Physics)
* **Determinizm i Fixed Timestep:** Fizyka aktualizuje się w stałym kroku $1/60$ sekundy (`FIXED_TIMESTEP = 0.0166667s`) z akumulatorem czasu, co zapobiega przenikaniu obiektów (tunneling) i błędom numerycznym.
* **Pełna synchronizacja 6DoF:** Synchronizowane są zarówno wektory pozycji i prędkości liniowej, jak i kwaterniony rotacji (przeliczane na kąty Eulera w stopniach).
* **Cykl życia ciał:** Usunięcie encji ze sceny gwarantuje zniszczenie ciała w `BodyInterface` silnika Jolt.

### D. Renderowanie Vulkan 1.3 PBR
* **PBR Forward Pipeline:** Rzeczywisty model oświetlenia fizycznego bazujący na mikro-fasetach Cooka-Torrance’a (GGX / Smith Joint Approximation), dyfuzji Burley/Disney oraz filmowej krzywej mapowania tonów ACES.
* **MaterialComponent:** Kolor i właściwości materiału są odseparowane od logiki gry – brak hardkodowanych nazw obiektów w pętli renderera.
* **Optymalizacja buforów:** Klatki pobierane na potrzeby podglądu webowego korzystają z trwałego bufora `stagingBuffer` (VMA) bez alokacji pamięci na klatkę.

---

## 2. Rzeczywista Roadmapa Rozwoju

### Faza 1: Stabilizacja Fundamentów (Zrealizowano)
- [x] Usunięcie atrap kodowych (stubs) i ujednolicenie potoku renderowania.
- [x] Pełna ochrona współbieżności dla serwera MCP (Main-thread promise synchronization).
- [x] Usunięcie pętli tekstowej JSON w QuickJS i optymalizacja do wywołań bezpośrednich.
- [x] Wprowadzenie stałego kroku czasowego (Fixed Timestep) w Jolt Physics i synchronizacja rotacji.
- [x] Oczyszczenie repozytorium z tymczasowych skryptów i patchy.

### Faza 2: Cienie i Oświetlenie Dynamiczne (Następny Krok)
- [ ] Kaskadowe Mapy Cieni (Cascaded Shadow Maps - CSM) dla słońca/światła kierunkowego.
- [ ] Bufor cieni wielopunktowych (Omnidirectional Point Light Shadow Cubemaps).
- [ ] SSAO (Screen Space Ambient Occlusion) w buforze postprocessingu.

### Faza 3: Skalowanie Wydajności Renderera
- [ ] GPU-Driven Rendering: `vkCmdDrawIndexedIndirect` i frustum culling na GPU (Compute Shader).
- [ ] Płynny LOD (Level of Detail) dla załadowanych siatek GLTF.
- [ ] Descriptor Indexing / Bindless Textures (płaska tablica tekstur w deskryptorze Vulkan).

### Faza 4: Integracja Audio i Rozszerzenia MCP
- [ ] Wbudowany silnik dźwięku 3D (miniaudio) z przestrzennym pozycjonowaniem źródeł dźwięku.
- [ ] Rozszerzenie narzędzi MCP o inspekcję wydajności (profiler klatek, alokacje pamięci).
- [ ] Automatyczne snapshoty stanu do odtwarzania powtórek (deterministic replay recorder).
