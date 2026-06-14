#pragma once

#include "MetaAgentParticleTypes.h"
#include "MetaAgentParticleShapes.h"

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
struct StateEffectTriggerResult;
}

#include "metaagent/particle/actuation_math.hpp"
#include "metaagent/camera/types.hpp"

#include "metaagent/particle/actuation_math.hpp"
#include "metaagent/camera/types.hpp"
#include "metaagent/media/image.hpp"

namespace metaagent::camera {
class CameraController;
struct FocusTarget;
}

class UTexture2D;
struct FMetaAgentCinematicCameraState;
struct FMetaAgentCameraZoomState;
class UMetaAgentParticleRuntime;
struct FMetaAgentParticleActuationRequest;

namespace MetaAgentParticleCoreBridge {

struct FCoreSchedulerState;

FCoreSchedulerState* get_or_create_state(UMetaAgentParticleRuntime& runtime);
void sync_runtime_to_core(UMetaAgentParticleRuntime& runtime);
void sync_core_to_runtime(UMetaAgentParticleRuntime& runtime);

void tick_pattern_runtime(UMetaAgentParticleRuntime& runtime, float delta_time_seconds);
void build_representation_frame(const UMetaAgentParticleRuntime& runtime, FMetaAgentParticleRepresentationFrame& out_frame);
bool dispatch_pattern_transition(UMetaAgentParticleRuntime& runtime, EMetaAgentPatternTransitionTrigger trigger, bool b_skip_return = false);

float get_active_state_duration_seconds(const UMetaAgentParticleRuntime& runtime);
float get_active_state_time_remaining_seconds(const UMetaAgentParticleRuntime& runtime);
FString build_pattern_status_text(const UMetaAgentParticleRuntime& runtime);
FString build_pattern_timings_text(const UMetaAgentParticleRuntime& runtime);
metaagent::particle::StateEffectTriggerResult toggle_state_effect(
	UMetaAgentParticleRuntime& runtime,
	const metaagent::core::String& effect_id);
bool is_state_effect_active(
	const UMetaAgentParticleRuntime& runtime,
	const metaagent::core::String& effect_id);

} // namespace MetaAgentParticleCoreBridge

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

metaagent::particle::ActuationMode to_core_actuation_mode(EMetaAgentParticleActuationMode mode);
EMetaAgentParticleActuationMode from_core_actuation_mode(metaagent::particle::ActuationMode mode);

struct FMetaAgentActuationComposeScratch {
	metaagent::core::Array<metaagent::core::Vec3> baseline_world_positions;
	metaagent::core::Array<metaagent::core::Vec3> pattern_world_targets;
	metaagent::core::Array<metaagent::core::Vec3> return_hold_positions;
	metaagent::core::Array<metaagent::core::Vec3> return_rest_positions;
	metaagent::core::Array<metaagent::core::Vec3> dissipate_start_positions;
	metaagent::core::Array<metaagent::core::Vec3> idle_baseline_world_positions;
	metaagent::core::Array<metaagent::core::Vec3> forming_steering_offsets;
	metaagent::particle::FormingSettings forming_settings;
	metaagent::particle::ActuationComposeInput input;
};

void build_actuation_compose_input(
	const FMetaAgentParticleActuationRequest& request,
	FMetaAgentActuationComposeScratch& scratch);

FMetaAgentParticleShapeFrame build_shape_frame_from_centroid(
	const TArray<FVector>& baseline_world_positions,
	float shape_width_cm,
	float shape_height_cm,
	float z_offset_cm,
	bool auto_fit_to_particle_sphere,
	bool orient_shape_to_view,
	bool has_view_origin,
	const FVector& view_origin);

::UTexture2D* create_texture2d_from_rgba(const metaagent::media::RgbaImage& image);

metaagent::camera::FocusTarget make_focus_target_from_world_points(
	const TArray<FVector>& world_positions,
	float padding_scale = 1.15f);

metaagent::camera::CameraController& get_camera_controller(class AMetaAgentPlayerController& controller);

void sync_cinematic_settings_to_core(
	const ::FMetaAgentCinematicCameraState& source,
	metaagent::camera::CinematicSettings& destination);

void sync_zoom_settings_to_core(
	const ::FMetaAgentCameraZoomState& source,
	metaagent::camera::ZoomSettings& destination);

void sync_zoom_settings_from_core(
	const metaagent::camera::ZoomSettings& source,
	::FMetaAgentCameraZoomState& destination);

void sync_cinematic_runtime_to_core(
	const ::FMetaAgentCinematicCameraState& source,
	metaagent::camera::CinematicRuntimeState& destination);

void sync_cinematic_runtime_from_core(
	const metaagent::camera::CinematicRuntimeState& source,
	::FMetaAgentCinematicCameraState& destination);

metaagent::core::Vec3 to_core_vec3(const FVector& value);
FVector from_core_vec3(const metaagent::core::Vec3& value);
FRotator from_core_rotator(const metaagent::core::Rotator& value);

metaagent::camera::FocusTarget make_focus_target_from_world_location(
	const FVector& world_location,
	float look_at_z_offset = 100.0f);

} // namespace MetaAgentTypeBridge
