#include "MetaAgentEditorMode.h"

#include "MetaAgentEditorModeToolkit.h"
#include "EditorModeManager.h"
#include "Toolkits/ToolkitManager.h"

#define LOCTEXT_NAMESPACE "MetaAgentEditorMode"

const FEditorModeID FMetaAgentEditorMode::EM_MetaAgentEditorModeId = TEXT("EM_MetaAgentEditorMode");

FMetaAgentEditorMode::FMetaAgentEditorMode()
{
	Info = FEditorModeInfo(
		EM_MetaAgentEditorModeId,
		LOCTEXT("MetaAgentEditorModeName", "MetaAgent"),
		FSlateIcon(),
		true,
		7000);
}

void FMetaAgentEditorMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FMetaAgentEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FMetaAgentEditorMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

#undef LOCTEXT_NAMESPACE
