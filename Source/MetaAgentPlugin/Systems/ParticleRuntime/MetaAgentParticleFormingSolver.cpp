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
}

TArray<TUniquePtr<IMetaAgentParticleFormingSolver>>& FMetaAgentParticleFormingSolverRegistry::GetSolvers()
{
	static TArray<TUniquePtr<IMetaAgentParticleFormingSolver>> Solvers;
	return Solvers;
}

void FMetaAgentParticleFormingSolverRegistry::RegisterSolver(TUniquePtr<IMetaAgentParticleFormingSolver> Solver)
{
	RegisterDefaults();
	if (Solver)
	{
		GetSolvers().Add(MoveTemp(Solver));
	}
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
}

const IMetaAgentParticleFormingSolver& FMetaAgentParticleFormingSolverRegistry::GetSolver(
	const EMetaAgentParticleFormingMode Mode)
{
	RegisterDefaults();

	const EMetaAgentParticleFormingMode SanitizedMode =
		FMetaAgentParticleFormingSettings::SanitizeMode(Mode);

	for (const TUniquePtr<IMetaAgentParticleFormingSolver>& Solver : GetSolvers())
	{
		if (Solver && Solver->GetMode() == SanitizedMode)
		{
			return *Solver;
		}
	}

	return *GetSolvers()[0];
}

FVector FMetaAgentParticleFormingSolverRegistry::SolveFormingPosition(FMetaAgentParticleFormingContext& Context)
{
	const EMetaAgentParticleFormingMode Mode = Context.Settings
		? FMetaAgentParticleFormingSettings::SanitizeMode(Context.Settings->Mode)
		: EMetaAgentParticleFormingMode::DirectLerp;
	return GetSolver(Mode).SolvePosition(Context);
}
