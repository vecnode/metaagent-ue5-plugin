#pragma once

#include "CoreMinimal.h"
#include "Host/MetaAgentHostSession.h"
#include "app/commands.hpp"
#include "app/gui_actions.hpp"

struct FMetaAgentInputBridgeResult
{
	bool bHandled = false;
	bool bSuccess = false;
	FString UserMessage;
	metaagent::app::CommandId Command = metaagent::app::CommandId::Unknown;
};

class FMetaAgentInputBridge
{
public:
	static FMetaAgentInputBridgeResult ValidateCommandByName(
		const FString& CommandName,
		const FMetaAgentHostSessionSnapshot& SessionSnapshot);

	static FMetaAgentInputBridgeResult ValidateCommand(
		metaagent::app::CommandId Command,
		const FMetaAgentHostSessionSnapshot& SessionSnapshot);

	static FMetaAgentInputBridgeResult ValidateGuiAction(
		const FString& ActionId,
		const FMetaAgentHostSessionSnapshot& SessionSnapshot);

	static metaagent::app::CommandId CommandForGuiAction(const FString& ActionId);

	static FString GetCommandDisplayName(metaagent::app::CommandId Command);
};
