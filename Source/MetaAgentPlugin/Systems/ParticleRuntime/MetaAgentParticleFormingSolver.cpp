// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleFormingSolver.h"

namespace MetaAgentParticleFormingInternal
{
	static float SmoothStep01(const float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Clamped * Clamped * (3.0f - 2.0f * Clamped);
	}

	static FVector ApplySteeringOffset(
		const FVector& Position,
		const FMetaAgentParticleFormingContext& Context,
		const float BlendAlpha)
	{
		if (Context.FormingSteeringWeight <= KINDA_SMALL_NUMBER
			|| Context.FormingSteeringOffset.IsNearlyZero())
		{
			return Position;
		}

		return Position + Context.FormingSteeringOffset * Context.FormingSteeringWeight * (1.0f - BlendAlpha);
	}

	static FVector SolveDirectLerp(FMetaAgentParticleFormingContext& Context)
	{
		const float Alpha = SmoothStep01(Context.BlendAlpha);
		FVector Position = FMath::Lerp(Context.Baseline, Context.Target, Alpha);
		return ApplySteeringOffset(Position, Context, Alpha);
	}

	class FMetaAgentDirectLerpFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::DirectLerp;
		}

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			return SolveDirectLerp(Context);
		}
	};

	class FMetaAgentArcLiftFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::ArcLift;
		}

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			const float Alpha = SmoothStep01(Context.BlendAlpha);
			FVector Position = FMath::Lerp(Context.Baseline, Context.Target, Alpha);

			const float LiftHeight = Context.Settings
				? Context.Settings->ArcLiftHeightCm
				: 80.0f;
			const float Arc = FMath::Sin(Alpha * PI);
			Position.Z += Arc * LiftHeight;

			return ApplySteeringOffset(Position, Context, Alpha);
		}
	};

	class FMetaAgentSpiralInFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::SpiralIn;
		}

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			const float Alpha = SmoothStep01(Context.BlendAlpha);
			const float Turns = Context.Settings ? Context.Settings->SpiralTurns : 1.5f;
			const FVector RadialStart = Context.Baseline - Context.PatternCenter;
			const FVector RadialEnd = Context.Target - Context.PatternCenter;
			const float SpinRadians = Turns * TWO_PI * Alpha;
			const FVector SpunStart = RadialStart.RotateAngleAxis(
				FMath::RadiansToDegrees(SpinRadians),
				FVector::UpVector);
			const FVector Spiraled = Context.PatternCenter + FMath::Lerp(SpunStart, RadialEnd, Alpha);
			FVector Position = FMath::Lerp(
				FMath::Lerp(Context.Baseline, Context.Target, Alpha),
				Spiraled,
				0.65f);

			return ApplySteeringOffset(Position, Context, Alpha);
		}
	};

	class FMetaAgentStaggeredWaveFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::StaggeredWave;
		}

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			const float WaveCycles = Context.Settings ? Context.Settings->StaggerWaveCycles : 2.0f;
			const float Stagger = Context.TotalParticleCount > 0
				? static_cast<float>(Context.GlobalIndex) / static_cast<float>(Context.TotalParticleCount)
				: 0.0f;
			const float WavePhase = Context.BlendAlpha * WaveCycles * TWO_PI;
			const float WaveOffset = 0.2f * FMath::Sin(WavePhase + Stagger * TWO_PI);
			const float DelayedAlpha = SmoothStep01(
				FMath::Clamp(Context.BlendAlpha + WaveOffset - Stagger * 0.4f, 0.0f, 1.0f));
			FVector Position = FMath::Lerp(Context.Baseline, Context.Target, DelayedAlpha);
			return ApplySteeringOffset(Position, Context, DelayedAlpha);
		}
	};

	class FMetaAgentSpringChaseFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::SpringChase;
		}

		virtual bool NeedsSpringState() const override { return true; }

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			if (!Context.SpringPositions || !Context.SpringVelocities
				|| !Context.SpringPositions->IsValidIndex(Context.GlobalIndex)
				|| !Context.SpringVelocities->IsValidIndex(Context.GlobalIndex))
			{
				return SolveDirectLerp(Context);
			}

			const float Stiffness = Context.Settings ? Context.Settings->SpringStiffness : 12.0f;
			const float Damping = Context.Settings ? Context.Settings->SpringDamping : 6.0f;
			const float DeltaTime = FMath::Max(Context.DeltaTimeSeconds, 1.0f / 120.0f);

			FVector& Position = (*Context.SpringPositions)[Context.GlobalIndex];
			FVector& Velocity = (*Context.SpringVelocities)[Context.GlobalIndex];
			const FVector Desired = Context.Target;
			const FVector Acceleration = Stiffness * (Desired - Position) - Damping * Velocity;
			Velocity += Acceleration * DeltaTime;
			Position += Velocity * DeltaTime;

			const float Alpha = SmoothStep01(Context.BlendAlpha);
			Position = FMath::Lerp(Position, Desired, Alpha * 0.15f);
			return ApplySteeringOffset(Position, Context, Alpha);
		}
	};

	class FMetaAgentNiagaraForcesFormingSolver final : public IMetaAgentParticleFormingSolver
	{
	public:
		virtual EMetaAgentParticleFormingMode GetMode() const override
		{
			return EMetaAgentParticleFormingMode::NiagaraForces;
		}

		virtual bool PrefersNiagaraParameters() const override { return true; }

		virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const override
		{
			return SolveDirectLerp(Context);
		}
	};
}

TArray<TUniquePtr<IMetaAgentParticleFormingSolver>>& FMetaAgentParticleFormingSolverRegistry::GetSolvers()
{
	static TArray<TUniquePtr<IMetaAgentParticleFormingSolver>> Solvers;
	return Solvers;
}

void FMetaAgentParticleFormingSolverRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<IMetaAgentParticleFormingSolver>>& Solvers = GetSolvers();
	if (Solvers.Num() > 0)
	{
		return;
	}

	using namespace MetaAgentParticleFormingInternal;
	Solvers.Emplace(MakeUnique<FMetaAgentDirectLerpFormingSolver>());
	Solvers.Emplace(MakeUnique<FMetaAgentArcLiftFormingSolver>());
	Solvers.Emplace(MakeUnique<FMetaAgentSpiralInFormingSolver>());
	Solvers.Emplace(MakeUnique<FMetaAgentStaggeredWaveFormingSolver>());
	Solvers.Emplace(MakeUnique<FMetaAgentSpringChaseFormingSolver>());
	Solvers.Emplace(MakeUnique<FMetaAgentNiagaraForcesFormingSolver>());
}

const IMetaAgentParticleFormingSolver& FMetaAgentParticleFormingSolverRegistry::GetSolver(
	const EMetaAgentParticleFormingMode Mode)
{
	RegisterDefaults();

	for (const TUniquePtr<IMetaAgentParticleFormingSolver>& Solver : GetSolvers())
	{
		if (Solver && Solver->GetMode() == Mode)
		{
			return *Solver;
		}
	}

	return *GetSolvers()[0];
}

FVector FMetaAgentParticleFormingSolverRegistry::SolveFormingPosition(FMetaAgentParticleFormingContext& Context)
{
	const EMetaAgentParticleFormingMode Mode = Context.Settings
		? Context.Settings->Mode
		: EMetaAgentParticleFormingMode::DirectLerp;
	return GetSolver(Mode).SolvePosition(Context);
}
