// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

class AMetaAgentPlayerController;
struct FMetaAgentGUIState;

struct FMetaAgentGUIRuntime
{
	static void RunApplyHelpPanelSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentGUIState& GUI);

	static void RunToggleHelpPanelSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentGUIState& GUI);

	static void BuildRuntimeSections(
		AMetaAgentPlayerController& Controller,
		FMetaAgentGUIState& GUI);
};
