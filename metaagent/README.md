# metaagent

Portable C++17 core for MetaAgent **particle pattern mechanics**: FSM, scheduler, forming/return solvers, phase curves, per-particle actuation composition, representation actuation policy, shape scatter, and image-mask sampling. No Unreal headers.

Full design notes: [`ARCHITECTURE.md`](./ARCHITECTURE.md).

## Layout

```
metaagent/
  metaagent.h                 Public umbrella API (single include)
  metaagent.cpp               Amalgamated implementation (includes all .cpp under include/metaagent/)
  include/metaagent/          Headers + module .cpp implementations
  tests/                      Standalone unit tests (CMake)
  CMakeLists.txt
  ARCHITECTURE.md
```

Embed in another engine: add `metaagent/include` and `metaagent/` to include paths, compile `metaagent.cpp` once (the UE plugin does this via `MetaAgentCoreAggregate.cpp`).

## Layer diagram

```mermaid
flowchart TB
    subgraph UE["Unreal plugin (thin bridge)"]
        RT[UMetaAgentParticleRuntime]
        TB[MetaAgentTypeBridge]
        Bridge[MetaAgentParticleCoreBridge]
        NIAG[Niagara buffer I/O]
        Shapes[PNG / world shape providers]
    end

    subgraph Core["metaagent (portable)"]
        Media[media: decode / store / mask pipeline]
        Cam[camera: rig / controller]
        Sched[ParticleScheduler]
        Graph[TransitionGraph]
        Form[FormingSolverRegistry]
        Act[ActuationMath]
        RepPol[RepresentationActuationPolicy]
        RepMap[RepresentationMapping]
        Shape[ShapeBuilder / ImageMaskProcessor]
    end

    RT --> Bridge
    Bridge --> TB
    TB --> Sched
    Sched --> Graph
    Sched --> Act
    RT --> NIAG
    RT --> Shapes
    Shapes -.->|RGBA buffers| Shape
    Act --> NIAG
    RepPol --> NIAG
```

## Tick flow (scheduler bridge)

```mermaid
sequenceDiagram
    participant RT as UMetaAgentParticleRuntime
    participant BR as MetaAgentParticleCoreBridge
    participant TB as MetaAgentTypeBridge
    participant SC as ParticleScheduler

    RT->>BR: tick_pattern_runtime(dt)
    BR->>TB: SyncRuntimeToCore (incl. curve samples)
    BR->>SC: tick_pattern_runtime(dt, callbacks)
    Note over SC: phase via ActuationMath::evaluate_phase_for_state
    SC-->>BR: updated PatternRuntime
    BR->>TB: SyncCoreToRuntime
```

## Actuation pipeline (core vs UE)

```mermaid
flowchart LR
    subgraph Core
        Frame[build_representation_frame]
        Pol[RepresentationActuationPolicy::resolve]
        Compose[ActuationMath::compose_particle_world_position]
        Phase[ActuationMath::evaluate_phase_for_state]
    end

    subgraph UE
        Req[build_actuation_compose_input]
        Write[Niagara buffer write / User params]
    end

    Frame --> Pol
    Pol --> Req
    Req --> Compose
    Compose --> Write
    Phase --> Frame
```

| Core module | Responsibility |
|-------------|----------------|
| `pattern_types` | `PatternState`, `PatternConfig`, `PatternRuntime` (+ asset curve samples) |
| `transition_graph` | FSM edges (internal to scheduler) |
| `scheduler` | Tick, transitions, representation frame, status text |
| `forming_solver` | Forming / return motion solvers (DirectLerp, ArcLift, SpiralIn, …) |
| `actuation_math` | Anticipation, blend alpha, **phase evaluation**, **position composition** |
| `representation_actuation` | Hybrid / Direct / Parameters delivery policy |
| `representation_types` | Macro phases (Prepare → Express → Sustain → Release) |
| `shape_builder` | Grid, polyline, silhouette assignment, shape frames |
| `image_mask_processor` | CPU mask from RGBA + stratified scatter |

## Standalone build

```powershell
cd metaagent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests: `transition_graph_test`, `forming_types_test`, `shape_builder_polyline_test`, `actuation_composer_test`.

## Unreal integration

The UE plugin embeds this library via `Source/MetaAgentPlugin/MetaAgentCoreAggregate.cpp` and exposes headers through `MetaAgentPlugin.Build.cs`.

- FSM tick, transitions, representation frame, and status strings → `metaagent::particle::ParticleScheduler` (`MetaAgentParticleCoreBridge` in `MetaAgentTypeBridge.cpp`).
- UE ↔ core conversion, curve sampling, actuation compose scratch → `MetaAgentTypeBridge`.
- Niagara GPU/CPU buffer I/O only → `MetaAgentParticleRuntime.cpp` / `MetaAgentParticleControl.cpp`.

## Embed elsewhere

Add `metaagent/include` to your include path and compile via the amalgamation entry point, or link the CMake static library.

```cpp
#include <metaagent/metaagent.h>

int main() {
    metaagent::initialize_defaults();
    // ...
}
```
