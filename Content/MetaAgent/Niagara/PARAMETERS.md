# MetaAgent Niagara parameter actuation

Use these **User** parameters on Niagara systems driven by `UMetaAgentParticleRuntime` when actuation mode is **Parameters** or **Hybrid** (packaged builds default to Parameters).

| Parameter | Type | Description |
|-----------|------|-------------|
| `MetaAgentPatternPhase` | Float | Blend phase 0–1 (Forming 0→1, Holding 1, Returning 1→0) |
| `MetaAgentPatternCenter` | Vec3 | Pattern centroid in world space |
| `MetaAgentPatternActive` | Bool | True while a pattern run is active |
| `MetaAgentPatternHoldScale` | Float | Hold pulse scale (1 + sinusoidal amplitude from pattern asset) |
| `MetaAgentFormingMode` | Int | Forming solver id (0=DirectLerp, 1=ArcLift, 2=SpiralIn, 3=StaggeredWave, 4=SpringChase, 5=NiagaraForces) |
| `MetaAgentFormingArcLift` | Float | Arc lift height (cm) when mode is ArcLift |
| `MetaAgentFormingSpiralTurns` | Float | Spiral revolutions when mode is SpiralIn |
| `MetaAgentFormingStaggerCycles` | Float | Wave cycles when mode is StaggeredWave |
| `MetaAgentFormingForceStrength` | Float | Attraction/force scale when mode is NiagaraForces |
| `MetaAgentFormingSpringStiffness` | Float | Spring stiffness hint for GPU modules |
| `MetaAgentFormingSpringDamping` | Float | Spring damping hint for GPU modules |

## Minimal lerp module (artist-facing)

In a Niagara **Particle Update** stack, lerp simulated position toward a target using the phase parameter:

1. Add **User** floats/ints: `MetaAgentPatternPhase`, `MetaAgentPatternActive`, `MetaAgentFormingMode`.
2. When `MetaAgentPatternActive` is true, blend `Position` toward your authored target by `MetaAgentPatternPhase`.
3. Branch on `MetaAgentFormingMode` for alternate motion (arc offset, spiral, staggered phase, force field).
4. For packaged GPU sims, keep motion on the GPU path — do not rely on C++ buffer writes.

## Niagara Forces recipe (mode 5)

When forming mode is **NiagaraForces**, C++ still pushes phase/center but leaves detailed motion to Niagara:

1. Read `MetaAgentFormingForceStrength` and `MetaAgentPatternCenter`.
2. Apply a **Curl Noise Force** or **Point Attraction** toward pattern targets sampled from your authored module.
3. Gate forces with `MetaAgentPatternPhase` so particles settle by phase 1.0.

Sample systems can be saved under `/MetaAgentPlugin/MetaAgent/Niagara/` after authoring in the editor.
