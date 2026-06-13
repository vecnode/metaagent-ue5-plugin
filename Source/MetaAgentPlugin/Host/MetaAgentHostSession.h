#pragma once

#include "CoreMinimal.h"
#include "metaagent/session/types.hpp"

struct FMetaAgentHostSessionSnapshot
{
	bool bActive = true;
	bool bInputEnabled = true;
	bool bCameraEnabled = true;
	bool bAiEnabled = true;
	bool bNetworkingEnabled = true;
	bool bRecordingEnabled = true;
	bool bUiEnabled = true;
	bool bParticleEnabled = true;
	FString MapName = TEXT("unknown");
	FString BuildLabel = TEXT("unknown");
	int32 HttpPort = 30080;
	bool bHttpEnabled = true;
	bool bHttpRouterBound = false;

	metaagent::session::RuntimeSession ToCoreSession() const;
};

class AMetaAgentPlayerController;

namespace MetaAgentHostSession
{
	FString BuildLabelFromBuildConfig();

	FMetaAgentHostSessionSnapshot MakeFromWorld(
		const UWorld* World,
		bool bActive,
		bool bInputEnabled,
		bool bCameraEnabled,
		bool bAiEnabled,
		bool bNetworkingEnabled,
		bool bRecordingEnabled,
		bool bUiEnabled,
		bool bParticleEnabled,
		int32 HttpPort,
		bool bHttpEnabled,
		bool bHttpRouterBound);

	FMetaAgentHostSessionSnapshot MakeFromPlayerController(const AMetaAgentPlayerController& Controller);
}
