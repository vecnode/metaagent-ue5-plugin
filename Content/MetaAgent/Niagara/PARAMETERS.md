# MetaAgent Niagara parameter actuation

Use these **User** parameters on Niagara systems driven by `UMetaAgentParticleRuntime` when actuation mode is **Parameters** or **Hybrid** (packaged builds default to Parameters).

Assign a **`UMetaAgentNiagaraSystemProfile`** on the orchestrator/runtime to validate required parameters at bind time.

## Scalar / bool contract (always pushed)

| Parameter | Type | Description |
|-----------|------|-------------|
| `MetaAgentPatternPhase` | Float | Blend phase 0–1 (Forming 0→1, Holding 1, Returning 1→0) |
| `MetaAgentPatternCenter` | Vec3 | Pattern centroid in world space |
| `MetaAgentPatternActive` | Bool | True while a pattern run is active |
| `MetaAgentPatternHoldScale` | Float | Sustain breathe pulse (1.0 ± amplitude); animated in C++ during **Holding** |
| `MetaAgentPatternDissipateActive` | Bool | True during **Dissipating** |
| `MetaAgentPatternDissipateVisibility` | Float | Fade 1→0 while particles collapse to center |
| `MetaAgentFormingMode` | Int | 0=DirectLerp, 1=ArcLift, 2=SpiralIn, 3=StaggeredWave, 4=SpringChase |
| `MetaAgentFormingArcLift` | Float | Arc lift height (cm) when mode is ArcLift |
| `MetaAgentFormingSpiralTurns` | Float | Spiral revolutions when mode is SpiralIn |

## Target data contract (Parameters mode parity)

When the Niagara profile enables **TargetArrayUpload** and the system exposes matching User parameters, C++ also pushes (Parameters-only builds; **skipped in Hybrid** when Direct buffer writes already run):

| Parameter | Type | Description |
|-----------|------|-------------|
| `MetaAgentPatternTargetCount` | Int | Number of pattern targets |
| `MetaAgentPatternTargetData` | Object (`UMetaAgentNiagaraTargetData`) | CPU-readable baselines + targets arrays |

In Niagara CPU modules / Blueprint bindings, read:

```text
Target = MetaAgentPatternTargetData.GetPatternTargets()[ID]
Baseline = MetaAgentPatternTargetData.GetBaselines()[ID]
Position = lerp(Baseline, Target, MetaAgentPatternPhase)
```

## Representation macro phases

The runtime scheduler emits **`FMetaAgentParticleRepresentationFrame`** each tick with macro phase:

| Macro | Micro-states |
|-------|----------------|
| **Prepare** | Anticipating |
| **Express** | Forming |
| **Sustain** | Holding |
| **Release** | Returning, Dissipating |

## Minimal lerp module (artist-facing)

1. Add User parameters from the tables above.
2. When `MetaAgentPatternActive` is true, blend toward targets using `MetaAgentPatternPhase`.
3. Branch on `MetaAgentFormingMode` for arc/spiral offsets, or read pre-solved targets from the array contract.
4. For packaged GPU sims, keep motion on the GPU path — do not rely on C++ buffer writes.

## Forming mode notes

- **StaggeredWave** (3): per-particle phase offset — shape fills in as a wave (`WaveSpread` on forming settings).
- **SpringChase** (4): overshoot + settle easing (`SpringOvershoot` on forming settings).
- Mode 5 (`NiagaraForces`) remains reserved for a custom driver plugin.

**Hold breathe:** tune `HoldPulseAmplitude` / `HoldPulseFrequencyHz` on `FMetaAgentParticlePatternConfig` or pattern assets; C++ drives `MetaAgentPatternHoldScale` during **Holding**.

**Return curves:** optional per-mode curves on `FMetaAgentParticleReturnSettings` (`DirectLerpReturnCurve`, `ArcLiftReturnCurve`, `SpiralInReturnCurve`) override the asset-level `ReturnCurve` fallback.

Sample systems can be saved under `Content/MetaAgent/Niagara/` after authoring in the editor.
