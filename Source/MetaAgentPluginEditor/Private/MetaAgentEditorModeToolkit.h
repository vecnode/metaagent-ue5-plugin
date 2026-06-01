#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

template<typename OptionType>
class SComboBox;

class STextBlock;

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
	FReply HandleScanCharactersClicked();

	void OnTargetSlotChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> MakeTargetSlotOptionWidget(TSharedPtr<FString> InOption) const;
	FText GetSelectedTargetSlotText() const;

	TSharedPtr<SWidget> ToolkitWidget;
	TArray<TSharedPtr<FString>> TargetSlotOptions;
	TSharedPtr<FString> SelectedTargetSlot;
};
