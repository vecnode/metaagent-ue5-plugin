// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

class AMetaAgentPlayerController;
struct FMetaAgentONNXState;

struct FMetaAgentONNXRuntime
{
	static void RunLoadPipelineSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentONNXState& ONNXState);

	static void RunGenerateImageSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentONNXState& ONNXState);
};
