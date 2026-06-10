# MetaAgent Niagara parameter actuation

Use these **User** parameters on Niagara systems driven by `UMetaAgentParticleRuntime` when actuation mode is **Parameters** or **Hybrid** (packaged builds default to Parameters).

| Parameter | Type | Description |
|-----------|------|-------------|
| `MetaAgentPatternPhase` | Float | Blend phase 0–1 (Forming 0→1, Holding 1, Returning 1→0) |
| `MetaAgentPatternCenter` | Vec3 | Pattern centroid in world space |
| `MetaAgentPatternActive` | Bool | True while a pattern run is active |
| `MetaAgentPatternHoldScale` | Float | Hold pulse scale (1 + sinusoidal amplitude from pattern asset) |

## Minimal lerp module (artist-facing)

In a Niagara **Particle Update** stack, lerp simulated position toward a target using the phase parameter:

1. Add a **User** float `MetaAgentPatternPhase` and bool `MetaAgentPatternActive`.
2. When `MetaAgentPatternActive` is true, blend `Position` toward your authored target (curve, texture sample, or module output) by `MetaAgentPatternPhase`.
3. For packaged GPU sims, keep motion on the GPU path — do not rely on C++ buffer writes.

Sample systems can be saved under `/MetaAgentPlugin/MetaAgent/Niagara/` after authoring in the editor.
