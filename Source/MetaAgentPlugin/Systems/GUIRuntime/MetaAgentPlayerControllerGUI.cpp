// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"

void AMetaAgentPlayerController::HandleToggleHelpPanelPressed()
{
	FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(*this, GUI);
}

void AMetaAgentPlayerController::ApplyGUIHelpPanelState()
{
	FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(*this, GUI);
}
