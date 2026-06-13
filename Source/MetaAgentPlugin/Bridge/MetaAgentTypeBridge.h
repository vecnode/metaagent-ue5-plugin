#pragma once

#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleReturnTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"

namespace metaagent::particle {
struct PatternConfig;
struct PatternRuntime;
struct RepresentationFrame;
struct ShapeContext;
struct ShapeBuildResult;
struct ImageMaskBuildParams;
struct ImageMaskBuildOutput;
struct FormingSettings;
struct ReturnSettings;
enum class PatternState : uint8_t;
enum class PatternPreset : uint8_t;
enum class FormingMode : uint8_t;
enum class ReturnMode : uint8_t;
enum class TransitionTrigger : uint8_t;
}

namespace MetaAgentTypeBridge {

void copy_pattern_config_to_core(const FMetaAgentParticlePatternConfig& source, metaagent::particle::PatternConfig& destination);
void copy_pattern_config_from_core(const metaagent::particle::PatternConfig& source, FMetaAgentParticlePatternConfig& destination);
void copy_pattern_runtime_to_core(const FMetaAgentParticlePatternRuntime& source, metaagent::particle::PatternRuntime& destination);
void copy_pattern_runtime_from_core(const metaagent::particle::PatternRuntime& source, FMetaAgentParticlePatternRuntime& destination);
void copy_shape_context_to_core(const FMetaAgentParticleShapeContext& source, metaagent::particle::ShapeContext& destination);
void copy_shape_build_result_from_core(
	const metaagent::particle::ShapeBuildResult& source,
	FMetaAgentParticleShapeBuildResult& destination);
void copy_image_mask_params_to_core(
	const FMetaAgentImageMaskBuildParams& source,
	metaagent::particle::ImageMaskBuildParams& destination);
void copy_image_mask_output_from_core(
	const metaagent::particle::ImageMaskBuildOutput& source,
	FMetaAgentImageMaskBuildOutput& destination);
void copy_forming_settings_to_core(
	const FMetaAgentParticleFormingSettings& source,
	metaagent::particle::FormingSettings& destination);
void copy_forming_settings_from_core(
	const metaagent::particle::FormingSettings& source,
	FMetaAgentParticleFormingSettings& destination);
void copy_return_settings_to_core(
	const FMetaAgentParticleReturnSettings& source,
	metaagent::particle::ReturnSettings& destination);
void copy_return_settings_from_core(
	const metaagent::particle::ReturnSettings& source,
	FMetaAgentParticleReturnSettings& destination);
void copy_representation_frame_from_core(const metaagent::particle::RepresentationFrame& source, FMetaAgentParticleRepresentationFrame& destination);

metaagent::particle::FormingMode to_core_forming_mode(EMetaAgentParticleFormingMode mode);
EMetaAgentParticleFormingMode from_core_forming_mode(metaagent::particle::FormingMode mode);
metaagent::particle::ReturnMode to_core_return_mode(EMetaAgentParticleReturnMode mode);
EMetaAgentParticleReturnMode from_core_return_mode(metaagent::particle::ReturnMode mode);

metaagent::particle::PatternState to_core_pattern_state(EMetaAgentParticlePatternState state);
EMetaAgentParticlePatternState from_core_pattern_state(metaagent::particle::PatternState state);
metaagent::particle::PatternPreset to_core_pattern_preset(EMetaAgentParticlePatternPreset preset);
metaagent::particle::TransitionTrigger to_core_transition_trigger(EMetaAgentPatternTransitionTrigger trigger);

} // namespace MetaAgentTypeBridge
