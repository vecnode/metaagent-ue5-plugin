// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleTransitionGraph.h"

namespace MetaAgentParticleTransitionInternal
{
	static bool GuardAlways(const FMetaAgentPatternTransitionContext&)
	{
		return true;
	}

	static bool GuardNotManual(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bManualStateAdvance;
	}

	static bool GuardManual(const FMetaAgentPatternTransitionContext& Context)
	{
		return Context.bManualStateAdvance;
	}

	static bool GuardMaskReady(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bAwaitingAsyncMask && Context.PatternTargetCount > 0;
	}

	static bool GuardAwaitingMask(const FMetaAgentPatternTransitionContext& Context)
	{
		return Context.bAwaitingAsyncMask;
	}

	static bool GuardFormTimeout(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bManualStateAdvance
			&& Context.StateElapsedSeconds >= FMath::Max(0.1f, Context.FormDurationSeconds);
	}

	static bool GuardHoldTimeout(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bManualStateAdvance
			&& Context.HoldDurationSeconds > 0.0f
			&& Context.StateElapsedSeconds >= Context.HoldDurationSeconds;
	}

	static bool GuardReturnTimeout(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bManualStateAdvance
			&& Context.StateElapsedSeconds >= FMath::Max(0.1f, Context.ReturnDurationSeconds);
	}

	static bool GuardDissipateTimeout(const FMetaAgentPatternTransitionContext& Context)
	{
		return !Context.bManualStateAdvance
			&& Context.StateElapsedSeconds >= FMath::Max(0.1f, Context.DissipateDurationSeconds);
	}

	static bool GuardSkipReturnCancel(const FMetaAgentPatternTransitionContext& Context)
	{
		return Context.bSkipReturnOnCancel
			|| Context.State == EMetaAgentParticlePatternState::Anticipating;
	}

	static bool GuardNeedsReturnCancel(const FMetaAgentPatternTransitionContext& Context)
	{
		return !GuardSkipReturnCancel(Context);
	}

	static bool GuardMorphTargetsReady(const FMetaAgentPatternTransitionContext& Context)
	{
		return Context.PatternTargetCount > 0;
	}
}

TArray<FMetaAgentParticleTransitionGraph::FTransitionRow>& FMetaAgentParticleTransitionGraph::GetRows()
{
	static TArray<FTransitionRow> Rows;
	return Rows;
}

void FMetaAgentParticleTransitionGraph::RegisterDefaults()
{
	TArray<FTransitionRow>& Rows = GetRows();
	if (Rows.Num() > 0)
	{
		return;
	}

	using namespace MetaAgentParticleTransitionInternal;

	auto Add = [&Rows](FTransitionRow Row)
	{
		Rows.Add(MoveTemp(Row));
	};

	Add({ EMetaAgentParticlePatternState::Idle, EMetaAgentPatternTransitionTrigger::Start, GuardAlways,
		EMetaAgentPatternTransitionAction::BeginPatternStart, EMetaAgentParticlePatternState::Anticipating });

	Add({ EMetaAgentParticlePatternState::Idle, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::BeginPatternStart, EMetaAgentParticlePatternState::Anticipating });

	Add({ EMetaAgentParticlePatternState::Preparing, EMetaAgentPatternTransitionTrigger::Timeout, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Anticipating });

	Add({ EMetaAgentParticlePatternState::Preparing, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Anticipating });

	Add({ EMetaAgentParticlePatternState::Anticipating, EMetaAgentPatternTransitionTrigger::Advance, GuardAwaitingMask,
		EMetaAgentPatternTransitionAction::None, EMetaAgentParticlePatternState::Anticipating });

	Add({ EMetaAgentParticlePatternState::Anticipating, EMetaAgentPatternTransitionTrigger::Advance, GuardMaskReady,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Forming, false, true });

	Add({ EMetaAgentParticlePatternState::Anticipating, EMetaAgentPatternTransitionTrigger::Ready,
		[](const FMetaAgentPatternTransitionContext& Context)
		{
			return MetaAgentParticleTransitionInternal::GuardNotManual(Context)
				&& MetaAgentParticleTransitionInternal::GuardMaskReady(Context);
		},
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Forming, false, true });

	Add({ EMetaAgentParticlePatternState::Anticipating, EMetaAgentPatternTransitionTrigger::Cancel, GuardSkipReturnCancel,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Anticipating, EMetaAgentPatternTransitionTrigger::Retreat, GuardAlways,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Forming, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Holding });

	Add({ EMetaAgentParticlePatternState::Forming, EMetaAgentPatternTransitionTrigger::Timeout, GuardFormTimeout,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Holding });

	Add({ EMetaAgentParticlePatternState::Forming, EMetaAgentPatternTransitionTrigger::Retreat, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Anticipating, true });

	Add({ EMetaAgentParticlePatternState::Holding, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::BeginConfiguredReturn, EMetaAgentParticlePatternState::Returning });

	Add({ EMetaAgentParticlePatternState::Holding, EMetaAgentPatternTransitionTrigger::Timeout, GuardHoldTimeout,
		EMetaAgentPatternTransitionAction::BeginConfiguredReturn, EMetaAgentParticlePatternState::Returning });

	Add({ EMetaAgentParticlePatternState::Holding, EMetaAgentPatternTransitionTrigger::Morph, GuardMorphTargetsReady,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Forming, false, true });

	Add({ EMetaAgentParticlePatternState::Holding, EMetaAgentPatternTransitionTrigger::Retreat, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Forming });

	Add({ EMetaAgentParticlePatternState::Holding, EMetaAgentPatternTransitionTrigger::Cancel, GuardNeedsReturnCancel,
		EMetaAgentPatternTransitionAction::BeginConfiguredReturn, EMetaAgentParticlePatternState::Returning });

	Add({ EMetaAgentParticlePatternState::Returning, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Returning, EMetaAgentPatternTransitionTrigger::Timeout, GuardReturnTimeout,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Returning, EMetaAgentPatternTransitionTrigger::Retreat, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Holding });

	Add({ EMetaAgentParticlePatternState::Returning, EMetaAgentPatternTransitionTrigger::Dissipate, GuardAlways,
		EMetaAgentPatternTransitionAction::RequestDissipate, EMetaAgentParticlePatternState::Dissipating });

	Add({ EMetaAgentParticlePatternState::Dissipating, EMetaAgentPatternTransitionTrigger::Advance, GuardAlways,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Dissipating, EMetaAgentPatternTransitionTrigger::Timeout, GuardDissipateTimeout,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });

	Add({ EMetaAgentParticlePatternState::Dissipating, EMetaAgentPatternTransitionTrigger::Retreat, GuardAlways,
		EMetaAgentPatternTransitionAction::EnterState, EMetaAgentParticlePatternState::Holding });

	auto AddCancelSkip = [&Add](const EMetaAgentParticlePatternState FromState)
	{
		Add({ FromState, EMetaAgentPatternTransitionTrigger::Cancel,
			[](const FMetaAgentPatternTransitionContext& Context)
			{
				return Context.bSkipReturnOnCancel;
			},
			EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });
	};

	AddCancelSkip(EMetaAgentParticlePatternState::Forming);
	AddCancelSkip(EMetaAgentParticlePatternState::Holding);
	AddCancelSkip(EMetaAgentParticlePatternState::Returning);
	AddCancelSkip(EMetaAgentParticlePatternState::Dissipating);

	Add({ EMetaAgentParticlePatternState::Forming, EMetaAgentPatternTransitionTrigger::Cancel, GuardNeedsReturnCancel,
		EMetaAgentPatternTransitionAction::BeginConfiguredReturn, EMetaAgentParticlePatternState::Returning });

	Add({ EMetaAgentParticlePatternState::Returning, EMetaAgentPatternTransitionTrigger::Cancel, GuardNeedsReturnCancel,
		EMetaAgentPatternTransitionAction::BeginConfiguredReturn, EMetaAgentParticlePatternState::Returning });

	Add({ EMetaAgentParticlePatternState::Dissipating, EMetaAgentPatternTransitionTrigger::Cancel, GuardNeedsReturnCancel,
		EMetaAgentPatternTransitionAction::CompleteRun, EMetaAgentParticlePatternState::Idle });
}

void FMetaAgentParticleTransitionGraph::RegisterTransition(
	const EMetaAgentParticlePatternState From,
	const EMetaAgentPatternTransitionTrigger Trigger,
	TFunction<bool(const FMetaAgentPatternTransitionContext&)> Guard,
	const EMetaAgentPatternTransitionAction Action,
	const EMetaAgentParticlePatternState ToState,
	const bool bRestoreIdleBaselineOnEnter,
	const bool bClearPatternStartLog)
{
	RegisterDefaults();

	FTransitionRow Row;
	Row.From = From;
	Row.Trigger = Trigger;
	Row.Guard = MoveTemp(Guard);
	Row.Action = Action;
	Row.ToState = ToState;
	Row.bRestoreIdleBaselineOnEnter = bRestoreIdleBaselineOnEnter;
	Row.bClearPatternStartLog = bClearPatternStartLog;
	GetRows().Add(MoveTemp(Row));
}

bool FMetaAgentParticleTransitionGraph::EvaluateTransition(
	const FMetaAgentPatternTransitionContext& Context,
	const EMetaAgentPatternTransitionTrigger Trigger,
	FMetaAgentPatternTransitionResult& OutResult)
{
	RegisterDefaults();
	OutResult = FMetaAgentPatternTransitionResult();

	for (const FTransitionRow& Row : GetRows())
	{
		if (Row.From != Context.State || Row.Trigger != Trigger)
		{
			continue;
		}

		if (Row.Guard && !Row.Guard(Context))
		{
			continue;
		}

		OutResult.bHandled = true;
		OutResult.Action = Row.Action;
		OutResult.NewState = Row.ToState;
		OutResult.bRestoreIdleBaselineOnEnter = Row.bRestoreIdleBaselineOnEnter;
		OutResult.bClearPatternStartLog = Row.bClearPatternStartLog;
		return true;
	}

	return false;
}
