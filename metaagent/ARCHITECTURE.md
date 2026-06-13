# metaagent — Architecture

Portable C++17 library for MetaAgent **particle pattern mechanics**. Unreal integration is a thin type bridge + Niagara I/O in the UE plugin.

---

## Design goals

| Goal | How |
|------|-----|
| Portability | C++17, `metaagent::core::*` value types (no `FVector` / `FString`) |
| Single source of truth | FSM, solvers, phase curves, actuation compose, scheduler, shape/mask algorithms |
| Testability | CMake + unit tests without the editor |
| Engine bridge | UE converts types and supplies I/O callbacks (Niagara, PNG, world queries) |

---

## Repository layout

```
metaagent/
├── include/metaagent/
│   ├── metaagent.hpp              Umbrella include
│   ├── initialize.hpp             initialize_defaults()
│   ├── core/                      Vec3, math, log_sink
│   └── particle/                  Pattern domain
├── src/metaagent/                 One .cpp per module
├── tests/
├── CMakeLists.txt
├── README.md
└── ARCHITECTURE.md
```

---

## Module map

### Core (`metaagent/core/`)

| Header | Role |
|--------|------|
| `types.hpp` | `Vec2`, `Vec3`, `Rotator`, `String`, `Array<T>` |
| `math.hpp` | `clamp`, `smooth_step01`, `evaluate_curve01`, `lerp` |
| `log_sink.hpp` | Injectable log callbacks |

### Particle domain (`metaagent/particle/`)

| Module | Key API | Role |
|--------|---------|------|
| `pattern_types` | `PatternConfig`, `PatternRuntime` | FSM state, buffers, **form/return asset curve samples** |
| `transition_graph` | `TransitionGraph` | FSM table (scheduler-internal) |
| `scheduler` | `ParticleScheduler`, `SchedulerCallbacks` | Tick, transitions, representation frame |
| `forming_solver` | `FormingSolverRegistry` | Per-particle forming / return motion |
| `actuation_math` | `ActuationMath` | Anticipation, blend alpha, **`evaluate_phase_for_state`**, **`compose_particle_world_position`** |
| `representation_actuation` | `RepresentationActuationPolicy` | Direct / Parameters / Hybrid delivery |
| `representation_types` | `RepresentationMapping` | Macro phases |
| `shape_builder` | `ShapeBuilder` | Targets, frames, silhouette assignment |
| `image_mask_processor` | `image_mask::build_mask_from_rgba` | Mask + stratified scatter |
| `forming_types` / `return_types` | Settings structs | Mode enums and curve sample arrays |

---

## Pattern FSM

States: `Idle`, `Preparing`, `Anticipating`, `Forming`, `Holding`, `Returning`, `Dissipating`, `Releasing`.

The scheduler advances via `TransitionGraph::evaluate_transition()`. UE calls `DispatchPatternTransition()` on the runtime, which bridges to the scheduler — **no parallel FSM table in the plugin**.

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Anticipating: start pattern
    Anticipating --> Forming: mask ready
    Forming --> Holding: form complete
    Holding --> Returning: hold timeout / advance
    Holding --> Forming: morph
    Holding --> Dissipating: dissipate mode
    Returning --> Idle: return complete
    Dissipating --> Idle: dissipate complete
```

---

## Phase evaluation

`ActuationMath::evaluate_phase_for_state()` is the default for forming and returning ticks:

- **Forming:** `PatternRuntime.form_curve_samples` (from UE `ActiveFormCurve`) or smoothstep
- **Returning:** mode curve from `ReturnSettings` → asset fallback (`asset_return_curve_samples`) → inverted smoothstep

UE samples `UCurveFloat` into core arrays in `SyncRuntimeToCore`; the scheduler no longer requires an `evaluate_phase_for_state` callback.

---

## Actuation composition

`ActuationMath::compose_particle_world_position()` implements the full per-particle blend path:

- Return hold lerp or return forming solver
- Anticipation motion
- Dissipate toward center
- Forming solvers + anticipation→forming carryover
- Default lerp + forming steering offsets

UE builds `ActuationComposeInput` via `MetaAgentTypeBridge::build_actuation_compose_input()` and only writes results into Niagara buffers.

---

## Representation actuation policy

`RepresentationActuationPolicy::resolve()` chooses:

| Delivery | When |
|----------|------|
| `ParametersWithTargets` | Parameters-only mode, or return release below threshold |
| `DirectWrite` | Direct mode |
| `HybridDirectWithScalars` | Hybrid: direct buffer write + scalar User params (no target UObject push) |

Hybrid resolves to Direct in editor builds and Parameters in shipping (`hybrid_use_direct_path` flag).

---

## Scheduler callbacks (UE bridge)

Engine-specific work still uses `SchedulerCallbacks`:

```cpp
struct SchedulerCallbacks {
    std::function<bool()> build_pattern_targets;
    std::function<bool()> begin_pattern_start;
    std::function<void(PatternState, PatternState)> enter_pattern_state;
    // evaluate_phase_for_state optional — default uses ActuationMath + synced curves
    // ...
};
```

Implemented in `FMetaAgentCoreBridgeFriend` (`MetaAgentTypeBridge.cpp`).

---

## UE plugin split (18 flat source files)

| Plugin file | Role |
|-------------|------|
| `MetaAgentCoreAggregate.cpp` | Embeds all `metaagent/src/**/*.cpp` |
| `MetaAgentTypeBridge.*` | UE ↔ core conversion, scheduler bridge, compose scratch |
| `MetaAgentParticleRuntime.*` | UObject instance, Niagara tick glue |
| `MetaAgentParticleControl.*` | Orchestrator, drivers, Niagara profiles, representation apply |
| `MetaAgentParticleShapes.*` | PNG load, mask cache, shape providers → core `ShapeBuilder` |
| `MetaAgentParticleTypes.h` | USTRUCT/UENUM mirrors |
| `MetaAgentPlayerController.*` | Input, camera, GUI, recording, AI, particles UI |
| `MetaAgentGameplay.*` | Game mode, character, networking game instance |
| `MetaAgentHUD.h` | HUD panel types |
| `MetaAgentPlugin.*` | Module startup, settings, Blueprint library |

**Stays in UE by design:** Niagara RHI/GPU, orchestrator UX, HUD, networking HTTP, gameplay, world/PNG I/O.

---

## Build

### Standalone

```powershell
cd metaagent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Unreal

`MetaAgentCoreAggregate.cpp` includes portable sources; `MetaAgentPlugin.Build.cs` adds `metaagent/include`.

---

## Extension points

1. **New forming mode** — add solver in `forming_solver.cpp`, register in `initialize_defaults()`, mirror enum in TypeBridge.
2. **New shape source** — UE provider in shape registry (world I/O); portable assignment in `shape_builder.cpp`.
3. **Custom phase curves** — sample curves into `PatternRuntime.form_curve_samples` / `asset_return_curve_samples` (or optional scheduler callback override).

Product usage and keyboard controls: repository root [`README.md`](../README.md).
