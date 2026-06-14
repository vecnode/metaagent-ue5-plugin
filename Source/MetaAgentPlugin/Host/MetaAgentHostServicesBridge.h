#pragma once

#include "CoreMinimal.h"
#include "metaagent/runtime/host_interfaces.hpp"

class AMetaAgentPlayerController;

class FMetaAgentHostServicesBridge
{
public:
	static metaagent::runtime::HostServiceCallbacks BuildFromPlayerController(AMetaAgentPlayerController& Controller);
};
