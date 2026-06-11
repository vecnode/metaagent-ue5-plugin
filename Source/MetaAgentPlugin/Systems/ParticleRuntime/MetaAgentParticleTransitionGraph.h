// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationTypes.h"

class METAAGENTPLUGIN_API FMetaAgentParticleTransitionGraph
{
public:
	static void RegisterDefaults();

	/** Append a custom transition row (evaluated after built-in rows). */
	static void RegisterTransition(
		EMetaAgentParticlePatternState From,
		EMetaAgentPatternTransitionTrigger Trigger,
		TFunction<bool(const FMetaAgentPatternTransitionContext&)> Guard,
		EMetaAgentPatternTransitionAction Action,
		EMetaAgentParticlePatternState ToState = EMetaAgentParticlePatternState::Idle,
		bool bRestoreIdleBaselineOnEnter = false,
		bool bClearPatternStartLog = false);

	static bool EvaluateTransition(
		const FMetaAgentPatternTransitionContext& Context,
		EMetaAgentPatternTransitionTrigger Trigger,
		FMetaAgentPatternTransitionResult& OutResult);

private:
	struct FTransitionRow
	{
		EMetaAgentParticlePatternState From = EMetaAgentParticlePatternState::Idle;
		EMetaAgentPatternTransitionTrigger Trigger = EMetaAgentPatternTransitionTrigger::Advance;
		TFunction<bool(const FMetaAgentPatternTransitionContext&)> Guard;
		EMetaAgentPatternTransitionAction Action = EMetaAgentPatternTransitionAction::None;
		EMetaAgentParticlePatternState ToState = EMetaAgentParticlePatternState::Idle;
		bool bRestoreIdleBaselineOnEnter = false;
		bool bClearPatternStartLog = false;
	};

	static TArray<FTransitionRow>& GetRows();
};
