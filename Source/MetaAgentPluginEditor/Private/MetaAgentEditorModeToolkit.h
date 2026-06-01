#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class FMetaAgentEditorModeToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

private:
	FReply HandleHelloWorldClicked();

	TSharedPtr<SWidget> ToolkitWidget;
};
