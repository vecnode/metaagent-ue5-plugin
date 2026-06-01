#include "MetaAgentPluginEditor.h"

#include "EditorModeRegistry.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Logging/LogMacros.h"
#include "MetaAgentEditorMode.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	void ExecuteMetaAgentHelloWorld()
	{
		UE_LOG(LogTemp, Log, TEXT("Hello World"));

		FNotificationInfo NotificationInfo(FText::FromString(TEXT("Hello World")));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 2.0f;
		NotificationInfo.bUseSuccessFailIcons = false;

		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}

	bool IsMetaAgentModeActive()
	{
		return GLevelEditorModeTools().IsModeActive(FMetaAgentEditorMode::EM_MetaAgentEditorModeId);
	}
}

#define LOCTEXT_NAMESPACE "MetaAgentPluginEditor"

void FMetaAgentPluginEditorModule::StartupModule()
{
	ToolMenusStartupCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMetaAgentPluginEditorModule::RegisterMenus));

	FEditorModeRegistry::Get().RegisterMode<FMetaAgentEditorMode>(
		FMetaAgentEditorMode::EM_MetaAgentEditorModeId,
		LOCTEXT("MetaAgentEditorModeName", "MetaAgent"),
		FSlateIcon(),
		true,
		7000);

	UE_LOG(LogTemp, Log, TEXT("MetaAgentPluginEditor module startup."));
}

void FMetaAgentPluginEditorModule::ShutdownModule()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (FModuleManager::Get().IsModuleLoaded("UnrealEd"))
	{
		FEditorModeRegistry::Get().UnregisterMode(FMetaAgentEditorMode::EM_MetaAgentEditorModeId);
	}

	UE_LOG(LogTemp, Log, TEXT("MetaAgentPluginEditor module shutdown."));
}

void FMetaAgentPluginEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* SettingsToolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.SettingsToolbar");
	FToolMenuSection& ProjectSettingsSection = SettingsToolbar->FindOrAddSection("ProjectSettings");

	FToolMenuEntry HelloWorldEntry = FToolMenuEntry::InitToolBarButton(
		"MetaAgentExecuteHelloWorld",
		FUIAction(
			FExecuteAction::CreateStatic(&ExecuteMetaAgentHelloWorld),
			FCanExecuteAction::CreateStatic(&IsMetaAgentModeActive),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&IsMetaAgentModeActive)),
		LOCTEXT("MetaAgentExecuteHelloWorldLabel", "Execute Hello World"),
		LOCTEXT("MetaAgentExecuteHelloWorldTooltip", "Execute a test action for MetaAgent mode."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Play"));

	HelloWorldEntry.StyleNameOverride = "CalloutToolbar";
	ProjectSettingsSection.AddEntry(HelloWorldEntry);
}

IMPLEMENT_MODULE(FMetaAgentPluginEditorModule, MetaAgentPluginEditor)

#undef LOCTEXT_NAMESPACE
