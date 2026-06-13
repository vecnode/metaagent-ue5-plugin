#include "Host/MetaAgentInputBridge.h"

namespace
{
FMetaAgentInputBridgeResult FromCoreResult(
	const metaagent::app::CommandResult& CoreResult,
	const metaagent::app::CommandId Command)
{
	FMetaAgentInputBridgeResult Result;
	Result.bHandled = CoreResult.handled;
	Result.bSuccess = CoreResult.success;
	Result.Command = Command;
	Result.UserMessage = UTF8_TO_TCHAR(CoreResult.user_message.c_str());
	return Result;
}

std::string ToCoreString(const FString& Value)
{
	const FTCHARToUTF8 Converter(*Value);
	return std::string(Converter.Get(), static_cast<size_t>(Converter.Length()));
}
}

FMetaAgentInputBridgeResult FMetaAgentInputBridge::ValidateCommandByName(
	const FString& CommandName,
	const FMetaAgentHostSessionSnapshot& SessionSnapshot)
{
	const metaagent::app::CommandId Command = metaagent::app::parse_command_name(ToCoreString(CommandName));
	return ValidateCommand(Command, SessionSnapshot);
}

FMetaAgentInputBridgeResult FMetaAgentInputBridge::ValidateCommand(
	const metaagent::app::CommandId Command,
	const FMetaAgentHostSessionSnapshot& SessionSnapshot)
{
	return FromCoreResult(
		metaagent::app::validate_command(Command, SessionSnapshot.ToCoreSession()),
		Command);
}

FString FMetaAgentInputBridge::GetCommandDisplayName(const metaagent::app::CommandId Command)
{
	return UTF8_TO_TCHAR(metaagent::app::command_display_name(Command).c_str());
}

metaagent::app::CommandId FMetaAgentInputBridge::CommandForGuiAction(const FString& ActionId)
{
	return metaagent::app::command_for_gui_action(ToCoreString(ActionId));
}

FMetaAgentInputBridgeResult FMetaAgentInputBridge::ValidateGuiAction(
	const FString& ActionId,
	const FMetaAgentHostSessionSnapshot& SessionSnapshot)
{
	const metaagent::app::CommandId Command = CommandForGuiAction(ActionId);
	if (Command != metaagent::app::CommandId::Unknown)
	{
		return ValidateCommand(Command, SessionSnapshot);
	}

	return FromCoreResult(
		metaagent::app::validate_gui_action(ToCoreString(ActionId), SessionSnapshot.ToCoreSession()),
		Command);
}
