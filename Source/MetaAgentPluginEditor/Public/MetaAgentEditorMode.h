#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FMetaAgentEditorMode : public FEdMode
{
public:
	static const FEditorModeID EM_MetaAgentEditorModeId;

	FMetaAgentEditorMode();

	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return true; }
};
