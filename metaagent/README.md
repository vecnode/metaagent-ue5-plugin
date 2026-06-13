# metaagent

Portable C++ core library for MetaAgent mechanics (particle FSM, forming solvers, shape scatter, representation scheduling).

## Layout

```
metaagent/
  include/metaagent/     Public headers (#include <metaagent/metaagent.hpp>)
  src/metaagent/         Implementations
  tests/                 Standalone unit tests (CMake)
  CMakeLists.txt
```

## Standalone build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Unreal integration

The UE plugin compiles this library via `Source/MetaAgentPlugin/Bridge/MetaAgentCoreAggregate.cpp` and links headers through `MetaAgentPlugin.Build.cs`.

Runtime particle FSM tick, transition graph, representation frame build, and status text are routed through `MetaAgentParticleCoreBridge` into `metaagent::particle::ParticleScheduler`.

## Embed in other projects

Add `metaagent/include` to your include path and compile all `.cpp` files under `metaagent/src/metaagent`, or link the static library produced by CMake.

```cpp
#include <metaagent/metaagent.hpp>

int main() {
  metaagent::initialize_defaults();
  // ...
}
```
