#include "MetaAgentTypeBridge.h"

#include "MetaAgentPlugin.h"
#include "MetaAgentParticleControl.h"
#include "MetaAgentParticleRuntime.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "metaagent/particle/scheduler.hpp"

#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "Containers/StringConv.h"

#include "metaagent/particle/image_mask_processor.hpp"
#include "metaagent/particle/pattern_types.hpp"
#include "metaagent/particle/representation_types.hpp"
#include "metaagent/particle/shape_builder.hpp"
#include "metaagent/particle/shape_types.hpp"

namespace
{
metaagent::core::Vec3 ToCoreVec3(const FVector& Value)
{
	return metaagent::core::Vec3(Value.X, Value.Y, Value.Z);
}

FVector FromCoreVec3(const metaagent::core::Vec3& Value)
{
	return FVector(Value.x, Value.y, Value.z);
}

metaagent::core::Rotator ToCoreRotator(const FRotator& Value)
{
	metaagent::core::Rotator Rotator;
	Rotator.pitch_deg = Value.Pitch;
	Rotator.yaw_deg = Value.Yaw;
	Rotator.roll_deg = Value.Roll;
	return Rotator;
}

FRotator FromCoreRotator(const metaagent::core::Rotator& Value)
{
	return FRotator(Value.pitch_deg, Value.yaw_deg, Value.roll_deg);
}

metaagent::core::String ToCoreString(const FString& Value)
{
	FTCHARToUTF8 Utf8(*Value);
	return metaagent::core::String(Utf8.Get(), static_cast<size_t>(Utf8.Length()));
}

FString FromCoreString(const metaagent::core::String& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}

void CopyVec3ArrayToCore(const TArray<FVector>& Source, metaagent::core::Array<metaagent::core::Vec3>& Destination)
{
	Destination.clear();
	Destination.reserve(static_cast<size_t>(Source.Num()));
	for (const FVector& Value : Source)
	{
		Destination.push_back(ToCoreVec3(Value));
	}
}

void CopyVec3ArrayFromCore(const metaagent::core::Array<metaagent::core::Vec3>& Source, TArray<FVector>& Destination)
{
	Destination.Reset(static_cast<int32>(Source.size()));
	Destination.Reserve(static_cast<int32>(Source.size()));
	for (const metaagent::core::Vec3& Value : Source)
	{
		Destination.Add(FromCoreVec3(Value));
	}
}

metaagent::particle::FormingMode ToCoreFormingMode(const EMetaAgentParticleFormingMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleFormingMode::ArcLift: return metaagent::particle::FormingMode::ArcLift;
	case EMetaAgentParticleFormingMode::SpiralIn: return metaagent::particle::FormingMode::SpiralIn;
	case EMetaAgentParticleFormingMode::StaggeredWave: return metaagent::particle::FormingMode::StaggeredWave;
	case EMetaAgentParticleFormingMode::SpringChase: return metaagent::particle::FormingMode::SpringChase;
	case EMetaAgentParticleFormingMode::DirectLerp:
	default:
		return metaagent::particle::FormingMode::DirectLerp;
	}
}

EMetaAgentParticleFormingMode FromCoreFormingMode(const metaagent::particle::FormingMode Mode)
{
	switch (Mode)
	{
	case metaagent::particle::FormingMode::ArcLift: return EMetaAgentParticleFormingMode::ArcLift;
	case metaagent::particle::FormingMode::SpiralIn: return EMetaAgentParticleFormingMode::SpiralIn;
	case metaagent::particle::FormingMode::StaggeredWave: return EMetaAgentParticleFormingMode::StaggeredWave;
	case metaagent::particle::FormingMode::SpringChase: return EMetaAgentParticleFormingMode::SpringChase;
	case metaagent::particle::FormingMode::DirectLerp:
	default:
		return EMetaAgentParticleFormingMode::DirectLerp;
	}
}

metaagent::particle::ReturnMode ToCoreReturnMode(const EMetaAgentParticleReturnMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleReturnMode::ArcLift: return metaagent::particle::ReturnMode::ArcLift;
	case EMetaAgentParticleReturnMode::SpiralIn: return metaagent::particle::ReturnMode::SpiralIn;
	case EMetaAgentParticleReturnMode::DissipateToCenter: return metaagent::particle::ReturnMode::DissipateToCenter;
	case EMetaAgentParticleReturnMode::DirectLerp:
	default:
		return metaagent::particle::ReturnMode::DirectLerp;
	}
}

EMetaAgentParticleReturnMode FromCoreReturnMode(const metaagent::particle::ReturnMode Mode)
{
	switch (Mode)
	{
	case metaagent::particle::ReturnMode::ArcLift: return EMetaAgentParticleReturnMode::ArcLift;
	case metaagent::particle::ReturnMode::SpiralIn: return EMetaAgentParticleReturnMode::SpiralIn;
	case metaagent::particle::ReturnMode::DissipateToCenter: return EMetaAgentParticleReturnMode::DissipateToCenter;
	case metaagent::particle::ReturnMode::DirectLerp:
	default:
		return EMetaAgentParticleReturnMode::DirectLerp;
	}
}

void SampleCurveToCore(const UCurveFloat* Curve, metaagent::core::Array<float>& Destination)
{
	Destination.clear();
	if (!Curve)
	{
		return;
	}

	constexpr int32 SampleCount = 64;
	Destination.reserve(SampleCount);
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Alpha = SampleCount > 1 ? static_cast<float>(Index) / static_cast<float>(SampleCount - 1) : 0.0f;
		Destination.push_back(Curve->GetFloatValue(Alpha));
	}
}

void CopyFormingSettingsToCore(
	const FMetaAgentParticleFormingSettings& Source,
	metaagent::particle::FormingSettings& Destination)
{
	Destination.mode = ToCoreFormingMode(Source.Mode);
	Destination.arc_lift_height_cm = Source.ArcLiftHeightCm;
	Destination.spiral_turns = Source.SpiralTurns;
	Destination.wave_spread = Source.WaveSpread;
	Destination.spring_overshoot = Source.SpringOvershoot;
}

void CopyFormingSettingsFromCore(
	const metaagent::particle::FormingSettings& Source,
	FMetaAgentParticleFormingSettings& Destination)
{
	Destination.Mode = FromCoreFormingMode(Source.mode);
	Destination.ArcLiftHeightCm = Source.arc_lift_height_cm;
	Destination.SpiralTurns = Source.spiral_turns;
	Destination.WaveSpread = Source.wave_spread;
	Destination.SpringOvershoot = Source.spring_overshoot;
}

metaagent::particle::PatternPreset ToCorePatternPreset(const EMetaAgentParticlePatternPreset Preset)
{
	switch (Preset)
	{
	case EMetaAgentParticlePatternPreset::Slow: return metaagent::particle::PatternPreset::Slow;
	case EMetaAgentParticlePatternPreset::Dramatic: return metaagent::particle::PatternPreset::Dramatic;
	case EMetaAgentParticlePatternPreset::Snappy: return metaagent::particle::PatternPreset::Snappy;
	case EMetaAgentParticlePatternPreset::Dreamy: return metaagent::particle::PatternPreset::Dreamy;
	case EMetaAgentParticlePatternPreset::Custom: return metaagent::particle::PatternPreset::Custom;
	case EMetaAgentParticlePatternPreset::Normal:
	default:
		return metaagent::particle::PatternPreset::Normal;
	}
}

EMetaAgentParticlePatternPreset FromCorePatternPreset(const metaagent::particle::PatternPreset Preset)
{
	switch (Preset)
	{
	case metaagent::particle::PatternPreset::Slow: return EMetaAgentParticlePatternPreset::Slow;
	case metaagent::particle::PatternPreset::Dramatic: return EMetaAgentParticlePatternPreset::Dramatic;
	case metaagent::particle::PatternPreset::Snappy: return EMetaAgentParticlePatternPreset::Snappy;
	case metaagent::particle::PatternPreset::Dreamy: return EMetaAgentParticlePatternPreset::Dreamy;
	case metaagent::particle::PatternPreset::Custom: return EMetaAgentParticlePatternPreset::Custom;
	case metaagent::particle::PatternPreset::Normal:
	default:
		return EMetaAgentParticlePatternPreset::Normal;
	}
}

metaagent::particle::ShapeType ToCoreShapeType(const EMetaAgentParticlePatternShape Shape)
{
	switch (Shape)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette: return metaagent::particle::ShapeType::ImageSilhouette;
	case EMetaAgentParticlePatternShape::SplinePath: return metaagent::particle::ShapeType::SplinePath;
	case EMetaAgentParticlePatternShape::MeshSilhouette: return metaagent::particle::ShapeType::MeshSilhouette;
	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		return metaagent::particle::ShapeType::SquareGrid;
	}
}

EMetaAgentParticlePatternShape FromCoreShapeType(const metaagent::particle::ShapeType Shape)
{
	switch (Shape)
	{
	case metaagent::particle::ShapeType::ImageSilhouette: return EMetaAgentParticlePatternShape::ImageSilhouette;
	case metaagent::particle::ShapeType::SplinePath: return EMetaAgentParticlePatternShape::SplinePath;
	case metaagent::particle::ShapeType::MeshSilhouette: return EMetaAgentParticlePatternShape::MeshSilhouette;
	case metaagent::particle::ShapeType::SquareGrid:
	default:
		return EMetaAgentParticlePatternShape::SquareGrid;
	}
}

metaagent::particle::ImageSamplingMode ToCoreImageSamplingMode(const EMetaAgentParticleImageSamplingMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleImageSamplingMode::SobelEdges: return metaagent::particle::ImageSamplingMode::SobelEdges;
	case EMetaAgentParticleImageSamplingMode::FilledSilhouette: return metaagent::particle::ImageSamplingMode::FilledSilhouette;
	case EMetaAgentParticleImageSamplingMode::GrayscaleDensity:
	default:
		return metaagent::particle::ImageSamplingMode::GrayscaleDensity;
	}
}

EMetaAgentParticleImageSamplingMode FromCoreImageSamplingMode(const metaagent::particle::ImageSamplingMode Mode)
{
	switch (Mode)
	{
	case metaagent::particle::ImageSamplingMode::SobelEdges: return EMetaAgentParticleImageSamplingMode::SobelEdges;
	case metaagent::particle::ImageSamplingMode::FilledSilhouette: return EMetaAgentParticleImageSamplingMode::FilledSilhouette;
	case metaagent::particle::ImageSamplingMode::GrayscaleDensity:
	default:
		return EMetaAgentParticleImageSamplingMode::GrayscaleDensity;
	}
}

metaagent::particle::ShapeAssignmentMode ToCoreAssignmentMode(const EMetaAgentParticleShapeAssignmentMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleShapeAssignmentMode::Ordered: return metaagent::particle::ShapeAssignmentMode::Ordered;
	case EMetaAgentParticleShapeAssignmentMode::PolarMatched: return metaagent::particle::ShapeAssignmentMode::PolarMatched;
	case EMetaAgentParticleShapeAssignmentMode::NearestNeighbor:
	default:
		return metaagent::particle::ShapeAssignmentMode::NearestNeighbor;
	}
}

EMetaAgentParticleShapeAssignmentMode FromCoreAssignmentMode(const metaagent::particle::ShapeAssignmentMode Mode)
{
	switch (Mode)
	{
	case metaagent::particle::ShapeAssignmentMode::Ordered: return EMetaAgentParticleShapeAssignmentMode::Ordered;
	case metaagent::particle::ShapeAssignmentMode::PolarMatched: return EMetaAgentParticleShapeAssignmentMode::PolarMatched;
	case metaagent::particle::ShapeAssignmentMode::NearestNeighbor:
	default:
		return EMetaAgentParticleShapeAssignmentMode::NearestNeighbor;
	}
}

metaagent::particle::ShapeAnchor ToCoreShapeAnchor(const EMetaAgentParticleShapeAnchor Anchor)
{
	switch (Anchor)
	{
	case EMetaAgentParticleShapeAnchor::PreviewPlane: return metaagent::particle::ShapeAnchor::PreviewPlane;
	case EMetaAgentParticleShapeAnchor::ParticleCentroid:
	default:
		return metaagent::particle::ShapeAnchor::ParticleCentroid;
	}
}

EMetaAgentParticleShapeAnchor FromCoreShapeAnchor(const metaagent::particle::ShapeAnchor Anchor)
{
	switch (Anchor)
	{
	case metaagent::particle::ShapeAnchor::PreviewPlane: return EMetaAgentParticleShapeAnchor::PreviewPlane;
	case metaagent::particle::ShapeAnchor::ParticleCentroid:
	default:
		return EMetaAgentParticleShapeAnchor::ParticleCentroid;
	}
}

void CopyShapeDefinitionToCore(
	const FMetaAgentParticleShapeDefinition& Source,
	metaagent::particle::ShapeDefinition& Destination)
{
	Destination.shape_type = ToCoreShapeType(Source.ShapeType);
	Destination.image_sampling_mode = ToCoreImageSamplingMode(Source.ImageSamplingMode);
	Destination.alpha_threshold = Source.AlphaThreshold;
	Destination.edge_threshold = Source.EdgeThreshold;
	Destination.use_luminance = Source.bUseLuminance;
	Destination.sample_resolution = Source.SampleResolution;
	Destination.grayscale_gamma = Source.GrayscaleGamma;
	Destination.density_grid_scale = Source.DensityGridScale;
	Destination.target_jitter_normalized = Source.TargetJitterNormalized;
	Destination.use_loaded_preview_texture = Source.bUseLoadedPreviewTexture;
	Destination.shape_anchor = ToCoreShapeAnchor(Source.ShapeAnchor);
	Destination.auto_fit_shape_to_particle_sphere = Source.bAutoFitShapeToParticleSphere;
	Destination.orient_shape_to_view = Source.bOrientShapeToView;
	Destination.shape_width_cm = Source.ShapeWidthCm;
	Destination.shape_height_cm = Source.ShapeHeightCm;
	Destination.z_offset_cm = Source.ZOffsetCm;
	Destination.assignment_mode = ToCoreAssignmentMode(Source.AssignmentMode);
	Destination.shape_source_actor_tag = ToCoreString(Source.ShapeSourceActorTag.ToString());
	Destination.procedural_sample_count = Source.ProceduralSampleCount;
}

void CopyShapeDefinitionFromCore(
	const metaagent::particle::ShapeDefinition& Source,
	FMetaAgentParticleShapeDefinition& Destination)
{
	Destination.ShapeType = FromCoreShapeType(Source.shape_type);
	Destination.ImageSamplingMode = FromCoreImageSamplingMode(Source.image_sampling_mode);
	Destination.AlphaThreshold = Source.alpha_threshold;
	Destination.EdgeThreshold = Source.edge_threshold;
	Destination.bUseLuminance = Source.use_luminance;
	Destination.SampleResolution = Source.sample_resolution;
	Destination.GrayscaleGamma = Source.grayscale_gamma;
	Destination.DensityGridScale = Source.density_grid_scale;
	Destination.TargetJitterNormalized = Source.target_jitter_normalized;
	Destination.bUseLoadedPreviewTexture = Source.use_loaded_preview_texture;
	Destination.ShapeAnchor = FromCoreShapeAnchor(Source.shape_anchor);
	Destination.bAutoFitShapeToParticleSphere = Source.auto_fit_shape_to_particle_sphere;
	Destination.bOrientShapeToView = Source.orient_shape_to_view;
	Destination.ShapeWidthCm = Source.shape_width_cm;
	Destination.ShapeHeightCm = Source.shape_height_cm;
	Destination.ZOffsetCm = Source.z_offset_cm;
	Destination.AssignmentMode = FromCoreAssignmentMode(Source.assignment_mode);
	Destination.ShapeSourceActorTag = FName(*FromCoreString(Source.shape_source_actor_tag));
	Destination.ProceduralSampleCount = Source.procedural_sample_count;
}

void CopyShapeFrameToCore(const FMetaAgentParticleShapeFrame& Source, metaagent::particle::ShapeFrame& Destination)
{
	Destination.origin = ToCoreVec3(Source.Origin);
	Destination.orientation = ToCoreRotator(Source.Orientation);
	Destination.extents_cm = metaagent::core::Vec2(Source.ExtentsCm.X, Source.ExtentsCm.Y);
	Destination.z_offset_cm = Source.ZOffsetCm;
}

void CopyShapeFrameFromCore(const metaagent::particle::ShapeFrame& Source, FMetaAgentParticleShapeFrame& Destination)
{
	Destination.Origin = FromCoreVec3(Source.origin);
	Destination.Orientation = FromCoreRotator(Source.orientation);
	Destination.ExtentsCm = FVector2D(Source.extents_cm.x, Source.extents_cm.y);
	Destination.ZOffsetCm = Source.z_offset_cm;
}

void CopyReturnSettingsToCore(const FMetaAgentParticleReturnSettings& Source, metaagent::particle::ReturnSettings& Destination)
{
	Destination.mode = ToCoreReturnMode(Source.Mode);
	Destination.arc_lift_height_cm = Source.ArcLiftHeightCm;
	Destination.spiral_turns = Source.SpiralTurns;
	SampleCurveToCore(Source.DirectLerpReturnCurve, Destination.direct_lerp_curve_samples);
	SampleCurveToCore(Source.ArcLiftReturnCurve, Destination.arc_lift_curve_samples);
	SampleCurveToCore(Source.SpiralInReturnCurve, Destination.spiral_in_curve_samples);
}

void CopyReturnSettingsFromCore(const metaagent::particle::ReturnSettings& Source, FMetaAgentParticleReturnSettings& Destination)
{
	Destination.Mode = FromCoreReturnMode(Source.mode);
	Destination.ArcLiftHeightCm = Source.arc_lift_height_cm;
	Destination.SpiralTurns = Source.spiral_turns;

	// Core settings carry sampled curves, while UE uses curve assets.
	Destination.DirectLerpReturnCurve = nullptr;
	Destination.ArcLiftReturnCurve = nullptr;
	Destination.SpiralInReturnCurve = nullptr;
}

metaagent::particle::RepresentationMacroPhase ToCoreMacroPhase(const EMetaAgentRepresentationMacroPhase Value)
{
	switch (Value)
	{
	case EMetaAgentRepresentationMacroPhase::Prepare: return metaagent::particle::RepresentationMacroPhase::Prepare;
	case EMetaAgentRepresentationMacroPhase::Express: return metaagent::particle::RepresentationMacroPhase::Express;
	case EMetaAgentRepresentationMacroPhase::Sustain: return metaagent::particle::RepresentationMacroPhase::Sustain;
	case EMetaAgentRepresentationMacroPhase::Release: return metaagent::particle::RepresentationMacroPhase::Release;
	case EMetaAgentRepresentationMacroPhase::Idle:
	default:
		return metaagent::particle::RepresentationMacroPhase::Idle;
	}
}

EMetaAgentRepresentationMacroPhase FromCoreMacroPhase(const metaagent::particle::RepresentationMacroPhase Value)
{
	switch (Value)
	{
	case metaagent::particle::RepresentationMacroPhase::Prepare: return EMetaAgentRepresentationMacroPhase::Prepare;
	case metaagent::particle::RepresentationMacroPhase::Express: return EMetaAgentRepresentationMacroPhase::Express;
	case metaagent::particle::RepresentationMacroPhase::Sustain: return EMetaAgentRepresentationMacroPhase::Sustain;
	case metaagent::particle::RepresentationMacroPhase::Release: return EMetaAgentRepresentationMacroPhase::Release;
	case metaagent::particle::RepresentationMacroPhase::Idle:
	default:
		return EMetaAgentRepresentationMacroPhase::Idle;
	}
}
} // namespace

namespace MetaAgentTypeBridge
{
void copy_pattern_config_to_core(const FMetaAgentParticlePatternConfig& Source, metaagent::particle::PatternConfig& Destination)
{
	Destination.form_duration_seconds = Source.FormDurationSeconds;
	Destination.hold_duration_seconds = Source.HoldDurationSeconds;
	Destination.return_duration_seconds = Source.ReturnDurationSeconds;
	Destination.dissipate_duration_seconds = Source.DissipateDurationSeconds;
	Destination.anticipation_amplitude_cm = Source.AnticipationAmplitudeCm;
	Destination.anticipation_frequency_hz = Source.AnticipationFrequencyHz;
	Destination.anticipation_idle_blend_duration_seconds = Source.AnticipationIdleBlendDurationSeconds;
	Destination.forming_anticipation_carryover_duration_seconds = Source.FormingAnticipationCarryoverDurationSeconds;
	Destination.grid_spacing_cm = Source.GridSpacingCm;
	CopyShapeDefinitionToCore(Source.Shape, Destination.shape);
	CopyFormingSettingsToCore(Source.Forming, Destination.forming);
	CopyReturnSettingsToCore(Source.Return, Destination.return_settings);
	Destination.hold_pulse_amplitude = Source.HoldPulseAmplitude;
	Destination.hold_pulse_frequency_hz = Source.HoldPulseFrequencyHz;
	Destination.active_preset = ToCorePatternPreset(Source.ActivePreset);
}

void copy_pattern_config_from_core(const metaagent::particle::PatternConfig& Source, FMetaAgentParticlePatternConfig& Destination)
{
	Destination.FormDurationSeconds = Source.form_duration_seconds;
	Destination.HoldDurationSeconds = Source.hold_duration_seconds;
	Destination.ReturnDurationSeconds = Source.return_duration_seconds;
	Destination.DissipateDurationSeconds = Source.dissipate_duration_seconds;
	Destination.AnticipationAmplitudeCm = Source.anticipation_amplitude_cm;
	Destination.AnticipationFrequencyHz = Source.anticipation_frequency_hz;
	Destination.AnticipationIdleBlendDurationSeconds = Source.anticipation_idle_blend_duration_seconds;
	Destination.FormingAnticipationCarryoverDurationSeconds = Source.forming_anticipation_carryover_duration_seconds;
	Destination.GridSpacingCm = Source.grid_spacing_cm;
	CopyShapeDefinitionFromCore(Source.shape, Destination.Shape);
	CopyFormingSettingsFromCore(Source.forming, Destination.Forming);
	CopyReturnSettingsFromCore(Source.return_settings, Destination.Return);
	Destination.HoldPulseAmplitude = Source.hold_pulse_amplitude;
	Destination.HoldPulseFrequencyHz = Source.hold_pulse_frequency_hz;
	Destination.ActivePreset = FromCorePatternPreset(Source.active_preset);
}

void copy_pattern_runtime_to_core(const FMetaAgentParticlePatternRuntime& Source, metaagent::particle::PatternRuntime& Destination)
{
	Destination.state = to_core_pattern_state(Source.State);
	Destination.phase = Source.Phase;
	Destination.state_elapsed_seconds = Source.StateElapsedSeconds;
	Destination.pattern_columns = Source.PatternColumns;
	Destination.pattern_center = ToCoreVec3(Source.PatternCenter);
	Destination.active_shape = ToCoreShapeType(Source.ActiveShape);
	CopyShapeFrameToCore(Source.ActiveShapeFrame, Destination.active_shape_frame);
	Destination.shape_debug_info = ToCoreString(Source.ShapeDebugInfo);
	copy_pattern_config_to_core(Source.ActiveConfig, Destination.active_config);
	CopyVec3ArrayToCore(Source.BaselineWorldPositions, Destination.baseline_world_positions);
	CopyVec3ArrayToCore(Source.IdleBaselineWorldPositions, Destination.idle_baseline_world_positions);
	CopyVec3ArrayToCore(Source.PatternWorldTargets, Destination.pattern_world_targets);
	CopyVec3ArrayToCore(Source.ReturnHoldPositions, Destination.return_hold_positions);
	CopyVec3ArrayToCore(Source.ReturnRestPositions, Destination.return_rest_positions);
	CopyVec3ArrayToCore(Source.TrajectoryWorldPositions, Destination.trajectory_world_positions);
	CopyVec3ArrayToCore(Source.DissipateStartPositions, Destination.dissipate_start_positions);
	Destination.awaiting_async_mask = Source.bAwaitingAsyncMask;
	Destination.anticipation_handoff_elapsed_seconds = Source.AnticipationHandoffElapsedSeconds;
	Destination.active_pattern_tags.clear();
	Destination.active_pattern_tags.reserve(static_cast<size_t>(Source.ActivePatternTags.Num()));
	TArray<FGameplayTag> GameplayTags;
	Source.ActivePatternTags.GetGameplayTagArray(GameplayTags);
	for (const FGameplayTag& Tag : GameplayTags)
	{
		Destination.active_pattern_tags.push_back(ToCoreString(Tag.ToString()));
	}
}

void copy_pattern_runtime_from_core(const metaagent::particle::PatternRuntime& Source, FMetaAgentParticlePatternRuntime& Destination)
{
	Destination.State = from_core_pattern_state(Source.state);
	Destination.Phase = Source.phase;
	Destination.StateElapsedSeconds = Source.state_elapsed_seconds;
	Destination.PatternColumns = Source.pattern_columns;
	Destination.PatternCenter = FromCoreVec3(Source.pattern_center);
	Destination.ActiveShape = FromCoreShapeType(Source.active_shape);
	CopyShapeFrameFromCore(Source.active_shape_frame, Destination.ActiveShapeFrame);
	Destination.ShapeDebugInfo = FromCoreString(Source.shape_debug_info);
	copy_pattern_config_from_core(Source.active_config, Destination.ActiveConfig);
	CopyVec3ArrayFromCore(Source.baseline_world_positions, Destination.BaselineWorldPositions);
	CopyVec3ArrayFromCore(Source.idle_baseline_world_positions, Destination.IdleBaselineWorldPositions);
	CopyVec3ArrayFromCore(Source.pattern_world_targets, Destination.PatternWorldTargets);
	CopyVec3ArrayFromCore(Source.return_hold_positions, Destination.ReturnHoldPositions);
	CopyVec3ArrayFromCore(Source.return_rest_positions, Destination.ReturnRestPositions);
	CopyVec3ArrayFromCore(Source.trajectory_world_positions, Destination.TrajectoryWorldPositions);
	CopyVec3ArrayFromCore(Source.dissipate_start_positions, Destination.DissipateStartPositions);
	Destination.bAwaitingAsyncMask = Source.awaiting_async_mask;
	Destination.AnticipationHandoffElapsedSeconds = Source.anticipation_handoff_elapsed_seconds;

	Destination.ActivePatternTags.Reset();
	for (const metaagent::core::String& TagString : Source.active_pattern_tags)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*FromCoreString(TagString)), false);
		if (Tag.IsValid())
		{
			Destination.ActivePatternTags.AddTag(Tag);
		}
	}
}

void copy_shape_context_to_core(const FMetaAgentParticleShapeContext& Source, metaagent::particle::ShapeContext& Destination)
{
	CopyVec3ArrayToCore(Source.BaselineWorldPositions, Destination.baseline_world_positions);
	Destination.source_image_path = ToCoreString(Source.SourceImagePath);
	Destination.has_resolved_image = Source.bHasResolvedImage;
	Destination.has_view_origin = Source.bHasViewOrigin;
	Destination.view_origin = ToCoreVec3(Source.ViewOrigin);
	Destination.has_preview_plane = false;
	Destination.source_texture_width = Source.SourceTexture ? Source.SourceTexture->GetSizeX() : 0;
	Destination.source_texture_height = Source.SourceTexture ? Source.SourceTexture->GetSizeY() : 0;

	if (const UStaticMeshComponent* PreviewPlane = Source.PreviewPlaneMesh.Get())
	{
		Destination.has_preview_plane = true;
		Destination.preview_plane_origin = ToCoreVec3(PreviewPlane->GetComponentLocation());
		Destination.preview_plane_orientation = ToCoreRotator(PreviewPlane->GetComponentRotation());
		const FVector Extent = PreviewPlane->Bounds.BoxExtent;
		Destination.preview_plane_extents_cm = metaagent::core::Vec2(Extent.X, Extent.Y);
	}
}

void copy_shape_build_result_from_core(
	const metaagent::particle::ShapeBuildResult& Source,
	FMetaAgentParticleShapeBuildResult& Destination)
{
	Destination.bSuccess = Source.success;
	Destination.bAwaitingAsyncMask = Source.awaiting_async_mask;
	CopyVec3ArrayFromCore(Source.pattern_world_targets, Destination.PatternWorldTargets);
	Destination.PatternColumns = Source.pattern_columns;
	Destination.PatternCenter = FromCoreVec3(Source.pattern_center);
	Destination.ResolvedShape = FromCoreShapeType(Source.resolved_shape);
	CopyShapeFrameFromCore(Source.shape_frame, Destination.ShapeFrame);
	Destination.ShapePointCount = Source.shape_point_count;
	Destination.DebugInfo = FromCoreString(Source.debug_info);
}

void copy_image_mask_params_to_core(
	const FMetaAgentImageMaskBuildParams& Source,
	metaagent::particle::ImageMaskBuildParams& Destination)
{
	Destination.source_image_path = ToCoreString(Source.SourceImagePath);
	Destination.source_file_timestamp = Source.SourceFileTimestamp.GetTicks();
	Destination.source_file_size = Source.SourceFileSize;
	Destination.image_sampling_mode = ToCoreImageSamplingMode(Source.ImageSamplingMode);
	Destination.alpha_threshold = Source.AlphaThreshold;
	Destination.edge_threshold = Source.EdgeThreshold;
	Destination.use_luminance = Source.bUseLuminance;
	Destination.sample_resolution = Source.SampleResolution;
	Destination.grayscale_gamma = Source.GrayscaleGamma;
	Destination.density_grid_scale = Source.DensityGridScale;
	Destination.target_jitter_normalized = Source.TargetJitterNormalized;
	Destination.desired_point_count = Source.DesiredPointCount;
}

void copy_image_mask_output_from_core(
	const metaagent::particle::ImageMaskBuildOutput& Source,
	FMetaAgentImageMaskBuildOutput& Destination)
{
	Destination.bSuccess = Source.success;
	Destination.LocalPointsCm.Reset(static_cast<int32>(Source.local_points_cm.size()));
	Destination.LocalPointsCm.Reserve(static_cast<int32>(Source.local_points_cm.size()));
	for (const metaagent::core::Vec3& Point : Source.local_points_cm)
	{
		Destination.LocalPointsCm.Add(FromCoreVec3(Point));
	}
	Destination.DebugInfo = FromCoreString(Source.debug_info);
}

void copy_representation_frame_from_core(
	const metaagent::particle::RepresentationFrame& Source,
	FMetaAgentParticleRepresentationFrame& Destination)
{
	Destination.MacroPhase = FromCoreMacroPhase(Source.macro_phase);
	Destination.PatternState = from_core_pattern_state(Source.pattern_state);
	Destination.Phase.NormalizedTime = Source.phase.normalized_time;
	Destination.Phase.BlendAlpha = Source.phase.blend_alpha;
	Destination.Phase.AuthorityWeight = Source.phase.authority_weight;
	Destination.Phase.Visibility = Source.phase.visibility;
	Destination.Phase.Emphasis = Source.phase.emphasis;
	Destination.PatternCenter = FromCoreVec3(Source.pattern_center);
	Destination.bPatternActive = Source.pattern_active;
	Destination.bAnticipatingMotion = Source.anticipating_motion;
	Destination.bDissipatingMotion = Source.dissipating_motion;
	Destination.bUseReturnHoldBlend = Source.use_return_hold_blend;
	Destination.AnticipationElapsedSeconds = Source.anticipation_elapsed_seconds;
	Destination.AnticipationAmplitudeCm = Source.anticipation_amplitude_cm;
	Destination.AnticipationFrequencyHz = Source.anticipation_frequency_hz;
	Destination.AnticipationIdleBlendDurationSeconds = Source.anticipation_idle_blend_duration_seconds;
	Destination.AnticipationHandoffElapsedSeconds = Source.anticipation_handoff_elapsed_seconds;
	Destination.FormingAnticipationCarryoverDurationSeconds = Source.forming_anticipation_carryover_duration_seconds;
	Destination.FormingStateElapsedSeconds = Source.forming_state_elapsed_seconds;
	Destination.FormingDurationSeconds = Source.forming_duration_seconds;
	Destination.FormingDeltaTimeSeconds = Source.forming_delta_time_seconds;
	Destination.FormingSteeringWeight = Source.forming_steering_weight;
	Destination.DissipateVisibility = Source.dissipate_visibility;
	CopyFormingSettingsFromCore(Source.forming_settings, Destination.FormingSettings);
	Destination.bReturnUsesMotionSolver = Source.return_uses_motion_solver;
	CopyFormingSettingsFromCore(Source.return_motion_settings, Destination.ReturnMotionSettings);
	CopyVec3ArrayFromCore(Source.baseline_world_positions, Destination.BaselineWorldPositions);
	CopyVec3ArrayFromCore(Source.pattern_world_targets, Destination.PatternWorldTargets);
	CopyVec3ArrayFromCore(Source.idle_baseline_world_positions, Destination.IdleBaselineWorldPositions);
	CopyVec3ArrayFromCore(Source.return_hold_positions, Destination.ReturnHoldPositions);
	CopyVec3ArrayFromCore(Source.return_rest_positions, Destination.ReturnRestPositions);
	CopyVec3ArrayFromCore(Source.dissipate_start_positions, Destination.DissipateStartPositions);
	CopyVec3ArrayFromCore(Source.forming_steering_offsets, Destination.FormingSteeringOffsets);
}

metaagent::particle::PatternPreset to_core_pattern_preset(const EMetaAgentParticlePatternPreset Preset)
{
	return ToCorePatternPreset(Preset);
}

metaagent::particle::PatternState to_core_pattern_state(const EMetaAgentParticlePatternState State)
{
	switch (State)
	{
	case EMetaAgentParticlePatternState::Preparing: return metaagent::particle::PatternState::Preparing;
	case EMetaAgentParticlePatternState::Anticipating: return metaagent::particle::PatternState::Anticipating;
	case EMetaAgentParticlePatternState::Forming: return metaagent::particle::PatternState::Forming;
	case EMetaAgentParticlePatternState::Holding: return metaagent::particle::PatternState::Holding;
	case EMetaAgentParticlePatternState::Returning: return metaagent::particle::PatternState::Returning;
	case EMetaAgentParticlePatternState::Dissipating: return metaagent::particle::PatternState::Dissipating;
	case EMetaAgentParticlePatternState::Releasing: return metaagent::particle::PatternState::Releasing;
	case EMetaAgentParticlePatternState::Idle:
	default:
		return metaagent::particle::PatternState::Idle;
	}
}

EMetaAgentParticlePatternState from_core_pattern_state(const metaagent::particle::PatternState State)
{
	switch (State)
	{
	case metaagent::particle::PatternState::Preparing: return EMetaAgentParticlePatternState::Preparing;
	case metaagent::particle::PatternState::Anticipating: return EMetaAgentParticlePatternState::Anticipating;
	case metaagent::particle::PatternState::Forming: return EMetaAgentParticlePatternState::Forming;
	case metaagent::particle::PatternState::Holding: return EMetaAgentParticlePatternState::Holding;
	case metaagent::particle::PatternState::Returning: return EMetaAgentParticlePatternState::Returning;
	case metaagent::particle::PatternState::Dissipating: return EMetaAgentParticlePatternState::Dissipating;
	case metaagent::particle::PatternState::Releasing: return EMetaAgentParticlePatternState::Releasing;
	case metaagent::particle::PatternState::Idle:
	default:
		return EMetaAgentParticlePatternState::Idle;
	}
}

metaagent::particle::ActuationMode to_core_actuation_mode(const EMetaAgentParticleActuationMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleActuationMode::Direct: return metaagent::particle::ActuationMode::Direct;
	case EMetaAgentParticleActuationMode::Parameters: return metaagent::particle::ActuationMode::Parameters;
	case EMetaAgentParticleActuationMode::Hybrid:
	default:
		return metaagent::particle::ActuationMode::Hybrid;
	}
}

EMetaAgentParticleActuationMode from_core_actuation_mode(const metaagent::particle::ActuationMode Mode)
{
	switch (Mode)
	{
	case metaagent::particle::ActuationMode::Direct: return EMetaAgentParticleActuationMode::Direct;
	case metaagent::particle::ActuationMode::Parameters: return EMetaAgentParticleActuationMode::Parameters;
	case metaagent::particle::ActuationMode::Hybrid:
	default:
		return EMetaAgentParticleActuationMode::Hybrid;
	}
}

void build_actuation_compose_input(
	const FMetaAgentParticleActuationRequest& Request,
	FMetaAgentActuationComposeScratch& Scratch)
{
	if (Request.BaselineWorldPositions)
	{
		CopyVec3ArrayToCore(*Request.BaselineWorldPositions, Scratch.baseline_world_positions);
	}
	else
	{
		Scratch.baseline_world_positions.clear();
	}

	if (Request.PatternWorldTargets)
	{
		CopyVec3ArrayToCore(*Request.PatternWorldTargets, Scratch.pattern_world_targets);
	}
	else
	{
		Scratch.pattern_world_targets.clear();
	}

	if (Request.ReturnHoldPositions)
	{
		CopyVec3ArrayToCore(*Request.ReturnHoldPositions, Scratch.return_hold_positions);
	}
	else
	{
		Scratch.return_hold_positions.clear();
	}

	if (Request.ReturnRestPositions)
	{
		CopyVec3ArrayToCore(*Request.ReturnRestPositions, Scratch.return_rest_positions);
	}
	else
	{
		Scratch.return_rest_positions.clear();
	}

	if (Request.DissipateStartPositions)
	{
		CopyVec3ArrayToCore(*Request.DissipateStartPositions, Scratch.dissipate_start_positions);
	}
	else
	{
		Scratch.dissipate_start_positions.clear();
	}

	if (Request.IdleBaselineWorldPositions)
	{
		CopyVec3ArrayToCore(*Request.IdleBaselineWorldPositions, Scratch.idle_baseline_world_positions);
	}
	else
	{
		Scratch.idle_baseline_world_positions.clear();
	}

	if (Request.FormingSteeringOffsets)
	{
		CopyVec3ArrayToCore(*Request.FormingSteeringOffsets, Scratch.forming_steering_offsets);
	}
	else
	{
		Scratch.forming_steering_offsets.clear();
	}

	if (Request.FormingSettings)
	{
		copy_forming_settings_to_core(*Request.FormingSettings, Scratch.forming_settings);
	}
	else
	{
		Scratch.forming_settings = {};
	}

	Scratch.input = {};
	Scratch.input.pattern_state = to_core_pattern_state(Request.PatternState);
	Scratch.input.blend_alpha = Request.BlendAlpha;
	Scratch.input.pattern_center = ToCoreVec3(Request.PatternCenter);
	Scratch.input.use_return_hold_blend = Request.bUseReturnHoldBlend;
	Scratch.input.anticipating_motion = Request.bAnticipatingMotion;
	Scratch.input.dissipating_motion = Request.bDissipatingMotion;
	Scratch.input.anticipation_elapsed_seconds = Request.AnticipationElapsedSeconds;
	Scratch.input.anticipation_amplitude_cm = Request.AnticipationAmplitudeCm;
	Scratch.input.anticipation_frequency_hz = Request.AnticipationFrequencyHz;
	Scratch.input.anticipation_idle_blend_duration_seconds = Request.AnticipationIdleBlendDurationSeconds;
	Scratch.input.anticipation_handoff_elapsed_seconds = Request.AnticipationHandoffElapsedSeconds;
	Scratch.input.forming_anticipation_carryover_duration_seconds =
		Request.FormingAnticipationCarryoverDurationSeconds;
	Scratch.input.forming_state_elapsed_seconds = Request.FormingStateElapsedSeconds;
	Scratch.input.forming_duration_seconds = Request.FormingDurationSeconds;
	Scratch.input.forming_delta_time_seconds = Request.FormingDeltaTimeSeconds;
	Scratch.input.forming_steering_weight = Request.FormingSteeringWeight;
	Scratch.input.forming_settings = Request.FormingSettings ? &Scratch.forming_settings : nullptr;
	Scratch.input.baseline_world_positions = Scratch.baseline_world_positions.empty()
		? nullptr
		: &Scratch.baseline_world_positions;
	Scratch.input.pattern_world_targets = Scratch.pattern_world_targets.empty()
		? nullptr
		: &Scratch.pattern_world_targets;
	Scratch.input.return_hold_positions = Scratch.return_hold_positions.empty()
		? nullptr
		: &Scratch.return_hold_positions;
	Scratch.input.return_rest_positions = Scratch.return_rest_positions.empty()
		? nullptr
		: &Scratch.return_rest_positions;
	Scratch.input.dissipate_start_positions = Scratch.dissipate_start_positions.empty()
		? nullptr
		: &Scratch.dissipate_start_positions;
	Scratch.input.idle_baseline_world_positions = Scratch.idle_baseline_world_positions.empty()
		? nullptr
		: &Scratch.idle_baseline_world_positions;
	Scratch.input.forming_steering_offsets = Scratch.forming_steering_offsets.empty()
		? nullptr
		: &Scratch.forming_steering_offsets;
}

metaagent::particle::TransitionTrigger to_core_transition_trigger(const EMetaAgentPatternTransitionTrigger Trigger)
{
	switch (Trigger)
	{
	case EMetaAgentPatternTransitionTrigger::Advance: return metaagent::particle::TransitionTrigger::Advance;
	case EMetaAgentPatternTransitionTrigger::Retreat: return metaagent::particle::TransitionTrigger::Retreat;
	case EMetaAgentPatternTransitionTrigger::Timeout: return metaagent::particle::TransitionTrigger::Timeout;
	case EMetaAgentPatternTransitionTrigger::Cancel: return metaagent::particle::TransitionTrigger::Cancel;
	case EMetaAgentPatternTransitionTrigger::Morph: return metaagent::particle::TransitionTrigger::Morph;
	case EMetaAgentPatternTransitionTrigger::Dissipate: return metaagent::particle::TransitionTrigger::Dissipate;
	case EMetaAgentPatternTransitionTrigger::Ready: return metaagent::particle::TransitionTrigger::Ready;
	case EMetaAgentPatternTransitionTrigger::Start:
	default:
		return metaagent::particle::TransitionTrigger::Start;
	}
}

metaagent::particle::FormingMode to_core_forming_mode(const EMetaAgentParticleFormingMode Mode)
{
	return ToCoreFormingMode(Mode);
}

EMetaAgentParticleFormingMode from_core_forming_mode(const metaagent::particle::FormingMode Mode)
{
	return FromCoreFormingMode(Mode);
}

metaagent::particle::ReturnMode to_core_return_mode(const EMetaAgentParticleReturnMode Mode)
{
	return ToCoreReturnMode(Mode);
}

EMetaAgentParticleReturnMode from_core_return_mode(const metaagent::particle::ReturnMode Mode)
{
	return FromCoreReturnMode(Mode);
}

void copy_forming_settings_to_core(
	const FMetaAgentParticleFormingSettings& Source,
	metaagent::particle::FormingSettings& Destination)
{
	CopyFormingSettingsToCore(Source, Destination);
}

void copy_forming_settings_from_core(
	const metaagent::particle::FormingSettings& Source,
	FMetaAgentParticleFormingSettings& Destination)
{
	CopyFormingSettingsFromCore(Source, Destination);
}

void copy_return_settings_to_core(
	const FMetaAgentParticleReturnSettings& Source,
	metaagent::particle::ReturnSettings& Destination)
{
	CopyReturnSettingsToCore(Source, Destination);
}

void copy_return_settings_from_core(
	const metaagent::particle::ReturnSettings& Source,
	FMetaAgentParticleReturnSettings& Destination)
{
	CopyReturnSettingsFromCore(Source, Destination);
}

FMetaAgentParticleShapeFrame build_shape_frame_from_centroid(
	const TArray<FVector>& BaselineWorldPositions,
	const float ShapeWidthCm,
	const float ShapeHeightCm,
	const float ZOffsetCm,
	const bool bAutoFitToParticleSphere,
	const bool bOrientShapeToView,
	const bool bHasViewOrigin,
	const FVector& ViewOrigin)
{
	metaagent::core::Array<metaagent::core::Vec3> CoreBaseline;
	CopyVec3ArrayToCore(BaselineWorldPositions, CoreBaseline);
	const metaagent::particle::ShapeFrame CoreFrame = metaagent::particle::ShapeBuilder::build_shape_frame_from_centroid(
		CoreBaseline,
		ShapeWidthCm,
		ShapeHeightCm,
		ZOffsetCm,
		bAutoFitToParticleSphere,
		bOrientShapeToView,
		bHasViewOrigin,
		ToCoreVec3(ViewOrigin));

	FMetaAgentParticleShapeFrame Frame;
	CopyShapeFrameFromCore(CoreFrame, Frame);
	return Frame;
}

} // namespace MetaAgentTypeBridge

#include "MetaAgentParticleTypes.h"
#include "MetaAgentParticleTypes.h"
#include "MetaAgentParticleTypes.h"
#include "Curves/CurveFloat.h"

EMetaAgentParticleFormingMode FMetaAgentParticleFormingSettings::SanitizeMode(const EMetaAgentParticleFormingMode Mode)
{
	return MetaAgentTypeBridge::from_core_forming_mode(
		metaagent::particle::FormingSettings::sanitize_mode(MetaAgentTypeBridge::to_core_forming_mode(Mode)));
}

FString FMetaAgentParticleFormingSettings::GetModeDisplayName() const
{
	metaagent::particle::FormingSettings CoreSettings;
	MetaAgentTypeBridge::copy_forming_settings_to_core(*this, CoreSettings);
	return FString(UTF8_TO_TCHAR(CoreSettings.get_mode_display_name().c_str()));
}

void FMetaAgentParticleFormingSettings::CycleMode()
{
	metaagent::particle::FormingSettings CoreSettings;
	MetaAgentTypeBridge::copy_forming_settings_to_core(*this, CoreSettings);
	CoreSettings.cycle_mode();
	MetaAgentTypeBridge::copy_forming_settings_from_core(CoreSettings, *this);
}

EMetaAgentParticleReturnMode FMetaAgentParticleReturnSettings::SanitizeMode(const EMetaAgentParticleReturnMode Mode)
{
	return MetaAgentTypeBridge::from_core_return_mode(
		metaagent::particle::ReturnSettings::sanitize_mode(MetaAgentTypeBridge::to_core_return_mode(Mode)));
}

bool FMetaAgentParticleReturnSettings::UsesMotionSolver() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	return CoreSettings.uses_motion_solver();
}

FMetaAgentParticleFormingSettings FMetaAgentParticleReturnSettings::AsFormingSettings() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	const metaagent::particle::FormingSettings CoreForming = CoreSettings.as_forming_settings();

	FMetaAgentParticleFormingSettings Out;
	MetaAgentTypeBridge::copy_forming_settings_from_core(CoreForming, Out);
	return Out;
}

FString FMetaAgentParticleReturnSettings::GetModeDisplayName() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	return FString(UTF8_TO_TCHAR(CoreSettings.get_mode_display_name().c_str()));
}

const UCurveFloat* FMetaAgentParticleReturnSettings::GetReturnCurveForMode() const
{
	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::ArcLift:
		return ArcLiftReturnCurve;
	case EMetaAgentParticleReturnMode::SpiralIn:
		return SpiralInReturnCurve;
	case EMetaAgentParticleReturnMode::DirectLerp:
	default:
		return DirectLerpReturnCurve;
	}
}

void FMetaAgentParticleReturnSettings::CycleMode()
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	CoreSettings.cycle_mode();
	MetaAgentTypeBridge::copy_return_settings_from_core(CoreSettings, *this);
}

FMetaAgentParticlePatternConfig FMetaAgentParticlePatternConfig::MakeFromPreset(const EMetaAgentParticlePatternPreset Preset)
{
	FMetaAgentParticlePatternConfig Config;
	Config.ApplyPreset(Preset);
	return Config;
}

void FMetaAgentParticlePatternConfig::ApplyPreset(const EMetaAgentParticlePatternPreset Preset)
{
	metaagent::particle::PatternConfig CoreConfig;
	CoreConfig.apply_preset(MetaAgentTypeBridge::to_core_pattern_preset(Preset));
	MetaAgentTypeBridge::copy_pattern_config_from_core(CoreConfig, *this);
}

FString FMetaAgentParticlePatternConfig::GetPresetDisplayName() const
{
	metaagent::particle::PatternConfig CoreConfig;
	CoreConfig.active_preset = MetaAgentTypeBridge::to_core_pattern_preset(ActivePreset);
	return FString(UTF8_TO_TCHAR(CoreConfig.get_preset_display_name().c_str()));
}

namespace MetaAgentParticleEffectIds
{
	METAAGENTPLUGIN_API const FName ImageReveal(TEXT("ImageReveal"));
	METAAGENTPLUGIN_API const FName SplinePath(TEXT("SplinePath"));
	METAAGENTPLUGIN_API const FName MeshSilhouette(TEXT("MeshSilhouette"));
	METAAGENTPLUGIN_API const FName GridSquare(TEXT("GridSquare"));
	METAAGENTPLUGIN_API const FName PresetSlow(TEXT("PresetSlow"));
	METAAGENTPLUGIN_API const FName PresetDramatic(TEXT("PresetDramatic"));
	METAAGENTPLUGIN_API const FName PresetSnappy(TEXT("PresetSnappy"));
	METAAGENTPLUGIN_API const FName PresetDreamy(TEXT("PresetDreamy"));
	METAAGENTPLUGIN_API const FName PlayNormal(TEXT("PlayNormal"));
	METAAGENTPLUGIN_API const FName PlaySlow(TEXT("PlaySlow"));
	METAAGENTPLUGIN_API const FName PlayDramatic(TEXT("PlayDramatic"));
	METAAGENTPLUGIN_API const FName PlaySnappy(TEXT("PlaySnappy"));
	METAAGENTPLUGIN_API const FName PlayDreamy(TEXT("PlayDreamy"));
	METAAGENTPLUGIN_API const FName PatternMorph(TEXT("PatternMorph"));
	METAAGENTPLUGIN_API const FName ReplayLast(TEXT("ReplayLast"));
	METAAGENTPLUGIN_API const FName CycleSampling(TEXT("CycleSampling"));
	METAAGENTPLUGIN_API const FName CycleForming(TEXT("CycleForming"));
	METAAGENTPLUGIN_API const FName CycleReturning(TEXT("CycleReturning"));
	METAAGENTPLUGIN_API const FName PatternStepForward(TEXT("PatternStepForward"));
	METAAGENTPLUGIN_API const FName PatternStepBackward(TEXT("PatternStepBackward"));
	METAAGENTPLUGIN_API const FName DissipateToCenter(TEXT("DissipateToCenter"));
	METAAGENTPLUGIN_API const FName AttractToView(TEXT("AttractToView"));
}

#include "MetaAgentParticleTypes.h"

EMetaAgentRepresentationMacroPhase FMetaAgentParticleRepresentationMapping::MacroPhaseFromPatternState(
	const EMetaAgentParticlePatternState State)
{
	switch (metaagent::particle::RepresentationMapping::macro_phase_from_pattern_state(
		MetaAgentTypeBridge::to_core_pattern_state(State)))
	{
	case metaagent::particle::RepresentationMacroPhase::Prepare:
		return EMetaAgentRepresentationMacroPhase::Prepare;
	case metaagent::particle::RepresentationMacroPhase::Express:
		return EMetaAgentRepresentationMacroPhase::Express;
	case metaagent::particle::RepresentationMacroPhase::Sustain:
		return EMetaAgentRepresentationMacroPhase::Sustain;
	case metaagent::particle::RepresentationMacroPhase::Release:
		return EMetaAgentRepresentationMacroPhase::Release;
	case metaagent::particle::RepresentationMacroPhase::Idle:
	default:
		return EMetaAgentRepresentationMacroPhase::Idle;
	}
}

FString FMetaAgentParticleRepresentationMapping::GetMacroPhaseDisplayName(
	const EMetaAgentRepresentationMacroPhase MacroPhase)
{
	metaagent::particle::RepresentationMacroPhase CorePhase;
	switch (MacroPhase)
	{
	case EMetaAgentRepresentationMacroPhase::Prepare: CorePhase = metaagent::particle::RepresentationMacroPhase::Prepare; break;
	case EMetaAgentRepresentationMacroPhase::Express: CorePhase = metaagent::particle::RepresentationMacroPhase::Express; break;
	case EMetaAgentRepresentationMacroPhase::Sustain: CorePhase = metaagent::particle::RepresentationMacroPhase::Sustain; break;
	case EMetaAgentRepresentationMacroPhase::Release: CorePhase = metaagent::particle::RepresentationMacroPhase::Release; break;
	case EMetaAgentRepresentationMacroPhase::Idle:
	default: CorePhase = metaagent::particle::RepresentationMacroPhase::Idle; break;
	}

	return FString(UTF8_TO_TCHAR(
		metaagent::particle::RepresentationMapping::get_macro_phase_display_name(CorePhase).c_str()));
}

namespace MetaAgentParticleCoreBridge
{
struct FCoreSchedulerState
{
	metaagent::particle::ParticleScheduler Scheduler;
};
} // namespace MetaAgentParticleCoreBridge
struct FMetaAgentCoreBridgeFriend
{
	static void SyncRuntimeToCore(UMetaAgentParticleRuntime& Runtime, metaagent::particle::ParticleScheduler& Scheduler)
	{
		MetaAgentTypeBridge::copy_pattern_config_to_core(Runtime.PatternConfig, Scheduler.pattern_config);
		MetaAgentTypeBridge::copy_pattern_runtime_to_core(Runtime.PatternRuntime, Scheduler.pattern_runtime);
		Scheduler.settings.manual_pattern_state_advance = Runtime.bManualPatternStateAdvance;
		Scheduler.settings.return_release_authority_threshold = Runtime.ReturnReleaseAuthorityThreshold;
		Scheduler.last_pattern_tick_delta_seconds = Runtime.LastPatternTickDeltaSeconds;
		Scheduler.forming_steering_blend_elapsed_seconds = Runtime.FormingSteeringBlendElapsedSeconds;
		Scheduler.steering_target_enabled = Runtime.LatestSnapshot.bSteeringTargetEnabled;
		Scheduler.forming_steering_offsets.clear();
		Scheduler.forming_steering_offsets.reserve(
			static_cast<size_t>(Runtime.LatestSnapshot.SuggestedSteeringDirections.Num()));
		for (const FVector& Offset : Runtime.LatestSnapshot.SuggestedSteeringDirections)
		{
			Scheduler.forming_steering_offsets.push_back(metaagent::core::Vec3(Offset.X, Offset.Y, Offset.Z));
		}
		Scheduler.forming_steering_blend_duration_seconds = Runtime.FormingSteeringBlendDurationSeconds;
		SampleCurveToCore(Runtime.ActiveFormCurve, Scheduler.pattern_runtime.form_curve_samples);
		SampleCurveToCore(Runtime.ActiveReturnCurve.Get(), Scheduler.pattern_runtime.asset_return_curve_samples);
	}
	static void SyncCoreToRuntime(UMetaAgentParticleRuntime& Runtime, const metaagent::particle::ParticleScheduler& Scheduler)
	{
		MetaAgentTypeBridge::copy_pattern_config_from_core(Scheduler.pattern_config, Runtime.PatternConfig);
		MetaAgentTypeBridge::copy_pattern_runtime_from_core(Scheduler.pattern_runtime, Runtime.PatternRuntime);
		Runtime.bManualPatternStateAdvance = Scheduler.settings.manual_pattern_state_advance;
		Runtime.ReturnReleaseAuthorityThreshold = Scheduler.settings.return_release_authority_threshold;
		Runtime.LastPatternTickDeltaSeconds = Scheduler.last_pattern_tick_delta_seconds;
		Runtime.FormingSteeringBlendElapsedSeconds = Scheduler.forming_steering_blend_elapsed_seconds;
		Runtime.LatestSnapshot.bSteeringTargetEnabled = Scheduler.steering_target_enabled;
		Runtime.FormingSteeringBlendDurationSeconds = Scheduler.forming_steering_blend_duration_seconds;
		Runtime.LatestSnapshot.SuggestedSteeringDirections.Reset(
			static_cast<int32>(Scheduler.forming_steering_offsets.size()));
		Runtime.LatestSnapshot.SuggestedSteeringDirections.Reserve(
			static_cast<int32>(Scheduler.forming_steering_offsets.size()));
		for (const metaagent::core::Vec3& Offset : Scheduler.forming_steering_offsets)
		{
			Runtime.LatestSnapshot.SuggestedSteeringDirections.Add(FVector(Offset.x, Offset.y, Offset.z));
		}
	}
	static metaagent::particle::SchedulerCallbacks BuildCallbacks(
		UMetaAgentParticleRuntime& Runtime,
		MetaAgentParticleCoreBridge::FCoreSchedulerState& State)
	{
		metaagent::particle::SchedulerCallbacks Callbacks;
		auto SyncFromRuntime = [&Runtime, &State]()
		{
			FMetaAgentCoreBridgeFriend::SyncRuntimeToCore(Runtime, State.Scheduler);
		};
		Callbacks.build_pattern_targets = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BuildPatternTargets();
			SyncFromRuntime();
			return bResult;
		};
		Callbacks.begin_pattern_start = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BeginPatternStart();
			SyncFromRuntime();
			return bResult;
		};
		Callbacks.begin_configured_return = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BeginConfiguredReturn();
			SyncFromRuntime();
			return bResult;
		};
		Callbacks.request_dissipate_to_center = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.RequestDissipateToCenter();
			SyncFromRuntime();
			return bResult;
		};
		Callbacks.complete_pattern_run = [&Runtime, SyncFromRuntime]()
		{
			Runtime.CompletePatternRun();
			SyncFromRuntime();
		};
		Callbacks.enter_pattern_state =
			[&Runtime, SyncFromRuntime](
				const metaagent::particle::PatternState NewState,
				const metaagent::particle::PatternState /*PreviousState*/)
		{
			Runtime.EnterPatternState(MetaAgentTypeBridge::from_core_pattern_state(NewState));
			SyncFromRuntime();
		};
		Callbacks.commit_anticipation_baseline_for_forming = [&Runtime, SyncFromRuntime]()
		{
			Runtime.CommitAnticipationBaselineForForming();
			SyncFromRuntime();
		};
		Callbacks.on_transition_side_effects = [&Runtime, SyncFromRuntime](
			const metaagent::particle::PatternState /*NewState*/)
		{
			SyncFromRuntime();
		};
		Callbacks.log_info = [](const metaagent::core::String& Message)
		{
			UE_LOG(LogMetaAgent, Log, TEXT("%s"), *FString(UTF8_TO_TCHAR(Message.c_str())));
		};
		Callbacks.log_warning = [](const metaagent::core::String& Message)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("%s"), *FString(UTF8_TO_TCHAR(Message.c_str())));
		};
		return Callbacks;
	}
};
namespace MetaAgentParticleCoreBridge
{
namespace
{
FString FromCoreString(const metaagent::core::String& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}
TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& GetStateMap()
{
	static TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>> StateMap;
	return StateMap;
}
void PruneStaleStates(TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& StateMap)
{
	for (auto It = StateMap.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
} // namespace
FCoreSchedulerState* get_or_create_state(UMetaAgentParticleRuntime& Runtime)
{
	TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& StateMap = GetStateMap();
	PruneStaleStates(StateMap);
	TWeakObjectPtr<UMetaAgentParticleRuntime> RuntimeKey(&Runtime);
	if (TUniquePtr<FCoreSchedulerState>* ExistingState = StateMap.Find(RuntimeKey))
	{
		return ExistingState->Get();
	}
	TUniquePtr<FCoreSchedulerState> NewState = MakeUnique<FCoreSchedulerState>();
	FCoreSchedulerState* NewStatePtr = NewState.Get();
	StateMap.Add(RuntimeKey, MoveTemp(NewState));
	return NewStatePtr;
}
void sync_runtime_to_core(UMetaAgentParticleRuntime& Runtime)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}
	FMetaAgentCoreBridgeFriend::SyncRuntimeToCore(Runtime, State->Scheduler);
}
void sync_core_to_runtime(UMetaAgentParticleRuntime& Runtime)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}
	FMetaAgentCoreBridgeFriend::SyncCoreToRuntime(Runtime, State->Scheduler);
}
void tick_pattern_runtime(UMetaAgentParticleRuntime& Runtime, const float DeltaTimeSeconds)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}
	sync_runtime_to_core(Runtime);
	metaagent::particle::SchedulerCallbacks Callbacks =
		FMetaAgentCoreBridgeFriend::BuildCallbacks(Runtime, *State);
	State->Scheduler.tick_pattern_runtime(DeltaTimeSeconds, Callbacks);
	sync_core_to_runtime(Runtime);
}
void build_representation_frame(const UMetaAgentParticleRuntime& Runtime, FMetaAgentParticleRepresentationFrame& OutFrame)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		OutFrame = FMetaAgentParticleRepresentationFrame();
		return;
	}
	metaagent::particle::RepresentationFrame CoreFrame = State->Scheduler.build_representation_frame();
	MetaAgentTypeBridge::copy_representation_frame_from_core(CoreFrame, OutFrame);
}
bool dispatch_pattern_transition(
	UMetaAgentParticleRuntime& Runtime,
	const EMetaAgentPatternTransitionTrigger Trigger,
	const bool bSkipReturn)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return false;
	}
	sync_runtime_to_core(Runtime);
	metaagent::particle::SchedulerCallbacks Callbacks =
		FMetaAgentCoreBridgeFriend::BuildCallbacks(Runtime, *State);
	const bool bHandled = State->Scheduler.dispatch_pattern_transition(
		MetaAgentTypeBridge::to_core_transition_trigger(Trigger),
		Callbacks,
		bSkipReturn);
	sync_core_to_runtime(Runtime);
	return bHandled;
}
float get_active_state_duration_seconds(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	return State ? State->Scheduler.get_active_state_duration_seconds() : 0.0f;
}
float get_active_state_time_remaining_seconds(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	return State ? State->Scheduler.get_active_state_time_remaining_seconds() : 0.0f;
}
FString build_pattern_status_text(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		return TEXT("Pattern State: unavailable");
	}
	const metaagent::core::String Status = State->Scheduler.build_pattern_status_text(
		Runtime.GetLatestSnapshot().ExportedParticleCount,
		Runtime.GetPatternQueueDepth());
	return FromCoreString(Status);
}
FString build_pattern_timings_text(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		return TEXT("Pattern Timings: unavailable");
	}
	return FromCoreString(State->Scheduler.build_pattern_timings_text());
}
} // namespace MetaAgentParticleCoreBridge
