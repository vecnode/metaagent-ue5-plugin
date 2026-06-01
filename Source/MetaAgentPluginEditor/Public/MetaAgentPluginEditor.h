#pragma once

#include "Modules/ModuleManager.h"
#include "Delegates/Delegate.h"

class FMetaAgentPluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();

	FDelegateHandle ToolMenusStartupCallbackHandle;
};
