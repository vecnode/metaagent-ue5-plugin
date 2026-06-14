#include "MetaAgentParticleControl.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MetaAgentTypeBridge.h"
#include "app/gui_catalog.hpp"
#include "particle/effect_catalog.hpp"
#include "particle/representation_actuation.hpp"
#include "particle/state_effects.hpp"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentHUD.h"
#include "MetaAgentParticleRuntime.h"
#include "MetaAgentPlayerController.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"

// ===== MetaAgentParticleOrchestrator.cpp =====
namespace
{
	bool AreTagsBlocked(const FGameplayTagContainer& BlockedTags, const FGameplayTagContainer& PatternTags)
	{
		if (BlockedTags.IsEmpty())
		{
			return false;
		}

		if (BlockedTags.HasTag(MetaAgentParticleTags::Pattern_Blocked))
		{
			return true;
		}

		return !PatternTags.IsEmpty() && BlockedTags.HasAny(PatternTags);
	}

}

void UMetaAgentParticleOrchestrator::InitializeOrchestrator(APlayerController* InHostController, UWorld* InWorld)
{
	HostController = InHostController;
	CachedWorld = InWorld;

	if (!ParticleRuntime)
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
	}

	if (ParticleRuntime && InHostController)
	{
		ParticleRuntime->InitializeRuntime(InHostController);
		SyncConfigToRuntime();
		UE_LOG(LogMetaAgent, Log, TEXT("ParticleOrchestrator: %s"), *ParticleRuntime->BuildStatusText());
	}
}

void UMetaAgentParticleOrchestrator::TickOrchestrator(const float DeltaTimeSeconds)
{
	if (ParticleRuntime)
	{
		ParticleRuntime->TickRuntime(DeltaTimeSeconds);
	}
}

void UMetaAgentParticleOrchestrator::SyncConfigToRuntime()
{
	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->ApplyPatternConfig(PatternConfig);
	ParticleRuntime->SetActuationMode(ActuationMode);
	ParticleRuntime->SetNiagaraSystemProfile(NiagaraSystemProfile);
}

void UMetaAgentParticleOrchestrator::SetNiagaraSystemProfile(UMetaAgentNiagaraSystemProfile* Profile)
{
	NiagaraSystemProfile = Profile;
	SyncConfigToRuntime();
}

void UMetaAgentParticleOrchestrator::ApplyPatternConfig(const FMetaAgentParticlePatternConfig& Config)
{
	PatternConfig = Config;
	SyncConfigToRuntime();
}

void UMetaAgentParticleOrchestrator::SetActuationMode(const EMetaAgentParticleActuationMode NewMode)
{
	ActuationMode = NewMode;
	SyncConfigToRuntime();
}

bool UMetaAgentParticleOrchestrator::ArePatternTagsBlocked(const FGameplayTagContainer& PatternTags) const
{
	return AreTagsBlocked(BlockedPatternTags, PatternTags);
}

FMetaAgentParticleShapeContext UMetaAgentParticleOrchestrator::BuildShapeContext() const
{
	FMetaAgentParticleShapeContext Context;

	if (ParticleRuntime)
	{
		if (ParticleRuntime->IsPatternActive()
			&& ParticleRuntime->GetPatternBaselineWorldPositions().Num() > 0)
		{
			Context.BaselineWorldPositions = ParticleRuntime->GetPatternBaselineWorldPositions();
		}
		else
		{
			Context.BaselineWorldPositions = ParticleRuntime->GetKnownParticlePositions();
		}
	}

	if (UWorld* World = CachedWorld.Get())
	{
		Context.PreviewPlaneMesh = FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
			World,
			PreviewPlaneActorName,
			PreviewPlaneComponentName,
			CachedPreviewPlaneMesh.Get());
		Context.World = World;
	}

	Context.SourceTexture = LatestPngPreviewTexture;
	Context.SourceImagePath = LastLoadedPreviewImagePath;
	Context.bHasResolvedImage = Context.SourceTexture != nullptr;

	if (APlayerController* PC = HostController.Get())
	{
		if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			Context.ViewOrigin = CameraManager->GetCameraLocation();
			Context.bHasViewOrigin = true;
		}
	}

	return Context;
}

bool UMetaAgentParticleOrchestrator::PrepareShapeContextForPlay(const bool bRequestMaskBuild)
{
	if (!ParticleRuntime)
	{
		return false;
	}

	if (PatternConfig.Shape.ShapeType == EMetaAgentParticlePatternShape::ImageSilhouette
		&& !LatestPngPreviewTexture)
	{
		const FString PngPath = FMetaAgentImagePreviewRuntime::ResolveDefaultSdxlPngPath();
		if (UTexture2D* ImportedTexture = FMetaAgentImagePreviewRuntime::ImportPngTexture(PngPath))
		{
			LatestPngPreviewTexture = ImportedTexture;
			LastLoadedPreviewImagePath = PngPath;
			RefreshPanelPreviewThumbnails();
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("ParticleOrchestrator: image silhouette requested but no preview texture is available."));
		}
	}

	ParticleRuntime->ForceCaptureParticles();
	ParticleRuntime->SetPatternShapeContext(BuildShapeContext());
	if (bRequestMaskBuild)
	{
		RequestImageMaskBuild();
	}
	return true;
}

void UMetaAgentParticleOrchestrator::RequestImageMaskBuild()
{
	if (!ParticleRuntime
		|| PatternConfig.Shape.ShapeType != EMetaAgentParticlePatternShape::ImageSilhouette)
	{
		return;
	}

	if (LastLoadedPreviewImagePath.IsEmpty())
	{
		return;
	}

	ParticleRuntime->ForceCaptureParticles();
	const int32 ParticleCount = FMath::Max(1, ParticleRuntime->GetKnownParticleCount());
	if (ParticleCount <= 0)
	{
		return;
	}

	FMetaAgentParticleShapeBuilder::RequestImageMaskBuild(
		LastLoadedPreviewImagePath,
		PatternConfig.Shape,
		ParticleCount);
}

bool UMetaAgentParticleOrchestrator::PopulateEffectSpec(
	const FName EffectId,
	FMetaAgentParticleEffectSpec& OutSpec) const
{
	OutSpec = FMetaAgentParticleEffectSpec();
	OutSpec.PatternConfig = PatternConfig;
	OutSpec.bOverridePatternConfig = true;
	OutSpec.bStartPattern = true;

	if (EffectId == MetaAgentParticleEffectIds::ImageReveal)
	{
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::SplinePath)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Normal);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::SplinePath;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::MeshSilhouette)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Normal);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::MeshSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::GridSquare)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Normal);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::SquareGrid;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PresetSlow)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Slow);
		OutSpec.bStartPattern = false;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PresetDramatic)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Dramatic);
		OutSpec.bStartPattern = false;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PresetSnappy)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Snappy);
		OutSpec.bStartPattern = false;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PresetDreamy)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Dreamy);
		OutSpec.bStartPattern = false;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PlayNormal)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Normal);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PlaySlow)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Slow);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PlayDramatic)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Dramatic);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PlaySnappy)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Snappy);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::PlayDreamy)
	{
		OutSpec.PatternConfig.ApplyPreset(EMetaAgentParticlePatternPreset::Dreamy);
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		return true;
	}

	if (EffectId == MetaAgentParticleEffectIds::AttractToView)
	{
		OutSpec.PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		OutSpec.bSteerTowardViewOnForm = true;
		return true;
	}

	return false;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::ApplyEffectSpec(
	const FMetaAgentParticleEffectSpec& Spec,
	const FName EffectId)
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = EffectId;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	if (Spec.bOverridePatternConfig)
	{
		PatternConfig = Spec.PatternConfig;
		SyncConfigToRuntime();
	}

	if (Spec.bSteerTowardViewOnForm)
	{
		const FMetaAgentParticleShapeContext Context = BuildShapeContext();
		if (Context.bHasViewOrigin)
		{
			SetSteeringTarget(Context.ViewOrigin, Spec.SteeringStrength);
		}
	}
	else
	{
		ClearSteeringTarget();
	}

	if (!Spec.bStartPattern)
	{
		Result.bSuccess = true;
		Result.UserMessage = FText::FromString(
			FString::Printf(TEXT("Preset applied (%s). Press >> or Play Full Cycle."), *PatternConfig.GetPresetDisplayName()));
		LastTriggeredEffectId = EffectId;
		LastEffectSpec = Spec;
		bHasLastEffectSpec = true;
		return Result;
	}

	const FGameplayTagContainer PatternTags = Spec.PatternAsset
		? Spec.PatternAsset->PatternTags
		: (DefaultPatternAsset ? DefaultPatternAsset->PatternTags : FGameplayTagContainer());

	if (ArePatternTagsBlocked(PatternTags))
	{
		Result.UserMessage = FText::FromString(TEXT("Pattern blocked by gameplay tags."));
		return Result;
	}

	ParticleRuntime->ForceCaptureParticles();
	PrepareShapeContextForPlay();
	ParticleRuntime->SetManualPatternStateAdvance(true);

	bool bStarted = false;
	if (Spec.PatternAsset)
	{
		bStarted = ParticleRuntime->RequestPatternStart(Spec.PatternAsset);
	}
	else if (DefaultPatternAsset && EffectId == MetaAgentParticleEffectIds::ImageReveal)
	{
		bStarted = ParticleRuntime->RequestPatternStart(DefaultPatternAsset);
	}
	else
	{
		bStarted = ParticleRuntime->StartPattern();
	}

	if (!bStarted)
	{
		Result.UserMessage = FText::FromString(TEXT("Pattern unavailable (busy or no captured particles)."));
		return Result;
	}

	Result.bSuccess = true;
	Result.bAwaitingAsyncPrepare =
		ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Anticipating
		&& ParticleRuntime->IsAwaitingAsyncMask();
	LastTriggeredEffectId = EffectId;
	LastEffectSpec = Spec;
	bHasLastEffectSpec = true;

	if (Result.bAwaitingAsyncPrepare)
	{
		Result.UserMessage = FText::FromString(
			FString::Printf(TEXT("Anticipating (loading mask) â€” %s"), *ParticleRuntime->BuildPatternStatusText()));
	}
	else
	{
		Result.UserMessage = FText::FromString(
			FString::Printf(
				TEXT("Effect '%s' started (%s | %s)."),
				*EffectId.ToString(),
				*ParticleRuntime->BuildPatternShapeText(),
				*ParticleRuntime->BuildPatternStatusText()));
	}

	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::TriggerEffect(const FName EffectId)
{
	if (EffectId == MetaAgentParticleEffectIds::ReplayLast)
	{
		return ReplayLastEffect();
	}

	if (EffectId == MetaAgentParticleEffectIds::CycleSampling)
	{
		return CycleImageSamplingMode();
	}

	if (EffectId == MetaAgentParticleEffectIds::CycleForming)
	{
		return CycleFormingMode();
	}

	if (EffectId == MetaAgentParticleEffectIds::CycleReturning)
	{
		return CycleReturningMode();
	}

	if (EffectId == MetaAgentParticleEffectIds::CyclePreset)
	{
		return CyclePatternPreset();
	}

	if (EffectId == MetaAgentParticleEffectIds::CycleOverlay)
	{
		return CycleOverlayEffects();
	}

	if (EffectId == MetaAgentParticleEffectIds::PatternStepForward)
	{
		return StepPatternStateForward();
	}

	if (EffectId == MetaAgentParticleEffectIds::PatternStepBackward)
	{
		return StepPatternStateBackward();
	}

	if (EffectId == MetaAgentParticleEffectIds::DissipateToCenter)
	{
		return DissipateToCenterEffect();
	}

	if (EffectId == MetaAgentParticleEffectIds::PatternMorph)
	{
		return MorphPatternEffect();
	}

	FMetaAgentParticleEffectSpec Spec;
	if (!PopulateEffectSpec(EffectId, Spec))
	{
		FMetaAgentParticleEffectResult Result;
		Result.EffectId = EffectId;
		Result.UserMessage = FText::FromString(FString::Printf(TEXT("Unknown particle effect '%s'."), *EffectId.ToString()));
		return Result;
	}

	return ApplyEffectSpec(Spec, EffectId);
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::ReplayLastEffect()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::ReplayLast;

	if (!bHasLastEffectSpec)
	{
		Result.UserMessage = FText::FromString(TEXT("No previous effect to replay."));
		return Result;
	}

	FMetaAgentParticleEffectSpec ReplaySpec = LastEffectSpec;
	ReplaySpec.bStartPattern = true;
	return ApplyEffectSpec(ReplaySpec, LastTriggeredEffectId);
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::CycleImageSamplingMode()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::CycleSampling;

	switch (FMetaAgentParticleShapeDefinition::SanitizeImageSamplingMode(PatternConfig.Shape.ImageSamplingMode))
	{
	case EMetaAgentParticleImageSamplingMode::GrayscaleDensity:
		PatternConfig.Shape.ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;
		break;
	case EMetaAgentParticleImageSamplingMode::SobelEdges:
	default:
		PatternConfig.Shape.ImageSamplingMode = EMetaAgentParticleImageSamplingMode::GrayscaleDensity;
		break;
	}

	SyncConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();

	if (ParticleRuntime && ParticleRuntime->IsPatternActive())
	{
		ParticleRuntime->RefreshPatternTargetsAfterConfigChange();
	}

	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Image sampling: %s"), *PatternConfig.Shape.GetImageSamplingDisplayName()));
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::CycleFormingMode()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::CycleForming;

	PatternConfig.Forming.CycleMode();
	SyncConfigToRuntime();
	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Forming mode: %s"), *PatternConfig.Forming.GetModeDisplayName()));
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::CycleReturningMode()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::CycleReturning;

	PatternConfig.Return.CycleMode();
	SyncConfigToRuntime();
	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Returning mode: %s"), *PatternConfig.Return.GetModeDisplayName()));
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::CyclePatternPreset()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::CyclePreset;

	PatternConfig.CyclePreset();
	SyncConfigToRuntime();
	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Timing preset: %s"), *PatternConfig.GetPresetDisplayName()));
	return Result;
}

namespace MetaAgentParticleOrchestratorInternal
{
	void EnsureOverlayEffectState(
		UMetaAgentParticleRuntime& Runtime,
		const bool bWantCohesion,
		const bool bWantTurbulence)
	{
		auto EnsureEffect = [&Runtime](const metaagent::core::String& EffectId, const bool bWantActive)
		{
			const bool bIsActive = MetaAgentParticleCoreBridge::is_state_effect_active(Runtime, EffectId);
			if (bIsActive != bWantActive)
			{
				MetaAgentParticleCoreBridge::toggle_state_effect(Runtime, EffectId);
			}
		};

		EnsureEffect(metaagent::particle::state_effect_ids::Cohesion, bWantCohesion);
		EnsureEffect(metaagent::particle::state_effect_ids::Turbulence, bWantTurbulence);
	}
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::CycleOverlayEffects()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::CycleOverlay;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	OverlayCycleIndex = (OverlayCycleIndex + 1) % 4;
	switch (OverlayCycleIndex)
	{
	case 0:
		MetaAgentParticleOrchestratorInternal::EnsureOverlayEffectState(*ParticleRuntime, true, false);
		Result.UserMessage = FText::FromString(TEXT("Overlay: Cohesion"));
		break;
	case 1:
		MetaAgentParticleOrchestratorInternal::EnsureOverlayEffectState(*ParticleRuntime, false, true);
		Result.UserMessage = FText::FromString(TEXT("Overlay: Turbulence"));
		break;
	case 2:
		MetaAgentParticleOrchestratorInternal::EnsureOverlayEffectState(*ParticleRuntime, true, true);
		Result.UserMessage = FText::FromString(TEXT("Overlay: Cohesion + Turbulence"));
		break;
	default:
		MetaAgentParticleOrchestratorInternal::EnsureOverlayEffectState(*ParticleRuntime, false, false);
		Result.UserMessage = FText::FromString(TEXT("Overlay: Off"));
		break;
	}

	ParticleRuntime->ApplyPatternRepresentation();
	Result.bSuccess = true;
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::StepPatternStateForward()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::PatternStepForward;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	ParticleRuntime->SetManualPatternStateAdvance(true);

	if (ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Idle)
	{
		PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		SyncConfigToRuntime();
		PrepareShapeContextForPlay(/*bRequestMaskBuild=*/true);

		LastTriggeredEffectId = MetaAgentParticleEffectIds::ImageReveal;
		LastEffectSpec = FMetaAgentParticleEffectSpec();
		LastEffectSpec.PatternConfig = PatternConfig;
		LastEffectSpec.bOverridePatternConfig = true;
		LastEffectSpec.bStartPattern = false;
		bHasLastEffectSpec = true;
	}
	else if (ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing
		&& ParticleRuntime->IsAwaitingAsyncMask())
	{
		FMetaAgentParticleShapeContext Context = ParticleRuntime->GetPatternShapeContext();
		Context.SourceTexture = LatestPngPreviewTexture;
		Context.SourceImagePath = LastLoadedPreviewImagePath;
		Context.bHasResolvedImage = LatestPngPreviewTexture != nullptr;
		if (Context.BaselineWorldPositions.Num() <= 0
			&& ParticleRuntime->GetPatternBaselineWorldPositions().Num() > 0)
		{
			Context.BaselineWorldPositions = ParticleRuntime->GetPatternBaselineWorldPositions();
		}
		ParticleRuntime->SetPatternShapeContext(Context);
		ParticleRuntime->RebuildPatternTargets();
	}

	const bool bAdvanced = ParticleRuntime->AdvancePatternStateForward();
	ParticleRuntime->ApplyPatternRepresentation();

	Result.bSuccess = bAdvanced;
	Result.bAwaitingAsyncPrepare =
		ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing
		&& ParticleRuntime->IsAwaitingAsyncMask();
	if (bAdvanced)
	{
		Result.UserMessage = FText::FromString(
			FString::Printf(TEXT("Pattern >> %s"), *ParticleRuntime->BuildPatternStatusText()));
	}
	else if (Result.bAwaitingAsyncPrepare)
	{
		Result.UserMessage = FText::FromString(
			FString::Printf(
				TEXT("Loading image mask — press . again when ready (%s)"),
				*ParticleRuntime->BuildPatternStatusText()));
	}
	else if (ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing
		&& !ParticleRuntime->IsAwaitingAsyncMask())
	{
		Result.bSuccess = true;
		Result.UserMessage = FText::FromString(
			FString::Printf(
				TEXT("Mask ready — press . to enter Forming (%s)"),
				*ParticleRuntime->BuildPatternStatusText()));
	}
	else
	{
		Result.UserMessage = FText::FromString(
			TEXT("Pattern step forward unavailable (busy, mask loading, or no captured particles)."));
	}
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::StepPatternStateBackward()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::PatternStepBackward;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	const bool bRetreated = ParticleRuntime->RetreatPatternStateBackward();
	if (bRetreated)
	{
		ParticleRuntime->ApplyPatternRepresentation();
	}
	Result.bSuccess = bRetreated;
	Result.UserMessage = FText::FromString(
		bRetreated
			? FString::Printf(TEXT("Pattern << %s"), *ParticleRuntime->BuildPatternStatusText())
			: TEXT("Pattern step backward unavailable (already idle)."));
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::PlayFullImageRevealCycle()
{
	if (!ParticleRuntime)
	{
		FMetaAgentParticleEffectResult Result;
		Result.EffectId = MetaAgentParticleEffectIds::ImageReveal;
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	if (ParticleRuntime->IsPatternActive())
	{
		ParticleRuntime->RequestPatternCancel(true);
	}

	ParticleRuntime->SetManualPatternStateAdvance(false);

	FMetaAgentParticleEffectSpec Spec;
	if (!PopulateEffectSpec(MetaAgentParticleEffectIds::ImageReveal, Spec))
	{
		FMetaAgentParticleEffectResult Result;
		Result.EffectId = MetaAgentParticleEffectIds::ImageReveal;
		Result.UserMessage = FText::FromString(TEXT("Image reveal effect unavailable."));
		return Result;
	}

	return ApplyEffectSpec(Spec, MetaAgentParticleEffectIds::ImageReveal);
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::DissipateToCenterEffect()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::DissipateToCenter;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	if (!ParticleRuntime->RequestDissipateToCenter())
	{
		Result.UserMessage = FText::FromString(
			TEXT("Dissipate unavailable (pattern must be Forming, Holding, or Returning)."));
		return Result;
	}

	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Dissipating toward center â€” %s"), *ParticleRuntime->BuildPatternStatusText()));
	return Result;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::MorphPatternEffect()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::PatternMorph;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	if (!ParticleRuntime->RequestPatternMorph())
	{
		Result.UserMessage = FText::FromString(
			TEXT("Morph unavailable (must be Holding with valid targets). Press F first if the image changed."));
		return Result;
	}

	Result.bSuccess = true;
	Result.UserMessage = FText::FromString(
		FString::Printf(TEXT("Morphing shape â€” %s"), *ParticleRuntime->BuildPatternStatusText()));
	return Result;
}

bool UMetaAgentParticleOrchestrator::LoadDefaultPreviewPng(FString& OutUserMessage)
{
	const FString PngPath = FMetaAgentImagePreviewRuntime::ResolveDefaultSdxlPngPath();
	if (!FPaths::FileExists(PngPath))
	{
		OutUserMessage = TEXT("Preview PNG not found: sdxl_latest.png");
		return false;
	}

	UTexture2D* ImportedTexture = FMetaAgentImagePreviewRuntime::ImportPngTexture(PngPath);
	if (!ImportedTexture)
	{
		OutUserMessage = TEXT("Failed to import sdxl_latest.png");
		return false;
	}

	LatestPngPreviewTexture = ImportedTexture;
	LastLoadedPreviewImagePath = PngPath;
	RefreshPanelPreviewThumbnails();
	if (ParticleRuntime && ParticleRuntime->IsPatternActive())
	{
		PrepareShapeContextForPlay(/*bRequestMaskBuild=*/true);
		if (ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing)
		{
			ParticleRuntime->RebuildPatternTargets();
		}
	}
	else
	{
		FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
		PrepareShapeContextForPlay();
	}

	if (UWorld* World = CachedWorld.Get())
	{
		UStaticMeshComponent* PreviewMesh = FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
			World,
			PreviewPlaneActorName,
			PreviewPlaneComponentName,
			CachedPreviewPlaneMesh.Get());
		if (PreviewMesh)
		{
			CachedPreviewPlaneMesh = PreviewMesh;
			UMaterialInterface* BasePreviewMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough"));
			if (BasePreviewMaterial)
			{
				UMaterialInstanceDynamic* PreviewMID = Cast<UMaterialInstanceDynamic>(PreviewMesh->GetMaterial(0));
				if (!PreviewMID)
				{
					PreviewMID = UMaterialInstanceDynamic::Create(BasePreviewMaterial, this);
					if (PreviewMID)
					{
						for (int32 SlotIndex = 0; SlotIndex < PreviewMesh->GetNumMaterials(); ++SlotIndex)
						{
							PreviewMesh->SetMaterial(SlotIndex, PreviewMID);
						}
					}
				}

				if (PreviewMID)
				{
					PreviewMID->SetTextureParameterValue(TEXT("SlateUI"), ImportedTexture);
					PreviewMID->SetTextureParameterValue(TEXT("SpriteTexture"), ImportedTexture);
					PreviewMID->SetTextureParameterValue(TEXT("Texture"), ImportedTexture);
					const FLinearColor PreviewTint(PreviewPlaneBrightness, PreviewPlaneBrightness, PreviewPlaneBrightness, 1.0f);
					PreviewMID->SetVectorParameterValue(TEXT("TintColorAndOpacity"), PreviewTint);
					PreviewMID->SetVectorParameterValue(TEXT("ColorAndOpacity"), PreviewTint);
					PreviewMID->SetVectorParameterValue(TEXT("TintColor"), PreviewTint);
					PreviewMID->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
					PreviewMID->SetScalarParameterValue(TEXT("EmissiveScale"), PreviewPlaneBrightness);
					PreviewMID->SetScalarParameterValue(TEXT("Brightness"), PreviewPlaneBrightness);
					PreviewMesh->SetHiddenInGame(false);
					PreviewMesh->SetCastShadow(false);
					PreviewMesh->MarkRenderStateDirty();
				}
			}
		}
	}

	OutUserMessage = FString::Printf(TEXT("Loaded sdxl_latest.png (%s)."), *BuildPatternShapeText());
	return true;
}

void UMetaAgentParticleOrchestrator::SubmitExportedParticlePositions(
	const TArray<FVector>& ParticlePositions,
	const FName SourceActorName,
	const FName SourceComponentName)
{
	if (ParticleRuntime)
	{
		ParticleRuntime->SubmitExportedParticlePositions(ParticlePositions, SourceActorName, SourceComponentName);
	}
}

void UMetaAgentParticleOrchestrator::DiscoverNiagaraComponents(const bool bLogSummary)
{
	if (ParticleRuntime)
	{
		ParticleRuntime->DiscoverNiagaraComponents(bLogSummary);
	}
}

void UMetaAgentParticleOrchestrator::SetSteeringTarget(const FVector TargetLocation, const float Strength)
{
	if (ParticleRuntime)
	{
		ParticleRuntime->SetSteeringTarget(TargetLocation, Strength);
	}
}

void UMetaAgentParticleOrchestrator::ClearSteeringTarget()
{
	if (ParticleRuntime)
	{
		ParticleRuntime->ClearSteeringTarget();
	}
}

bool UMetaAgentParticleOrchestrator::RequestPatternCancel(const bool bSkipReturn)
{
	return ParticleRuntime ? ParticleRuntime->RequestPatternCancel(bSkipReturn) : false;
}

bool UMetaAgentParticleOrchestrator::RequestSkipHold()
{
	return ParticleRuntime ? ParticleRuntime->RequestSkipHold() : false;
}

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::StartPatternWithAsset(
	UMetaAgentParticlePatternAsset* PatternAsset)
{
	FMetaAgentParticleEffectSpec Spec;
	Spec.PatternAsset = PatternAsset;
	Spec.PatternConfig = PatternConfig;
	Spec.bOverridePatternConfig = false;
	Spec.bStartPattern = true;
	return ApplyEffectSpec(Spec, MetaAgentParticleEffectIds::ImageReveal);
}

void UMetaAgentParticleOrchestrator::SetPreviewSource(UTexture2D* Texture, const FString& ImagePath)
{
	LatestPngPreviewTexture = Texture;
	LastLoadedPreviewImagePath = ImagePath;
	RefreshPanelPreviewThumbnails();
}

void UMetaAgentParticleOrchestrator::RefreshPanelPreviewThumbnails()
{
	PanelPreviewThumbnails.Reset();

	if (LastLoadedPreviewImagePath.IsEmpty() || !FPaths::FileExists(LastLoadedPreviewImagePath))
	{
		return;
	}

	TArray<FMetaAgentImagePreviewRuntime::FPanelPreviewThumbnail> BuiltThumbnails;
	if (!FMetaAgentImagePreviewRuntime::BuildPanelPreviewThumbnails(LastLoadedPreviewImagePath, 64, BuiltThumbnails))
	{
		return;
	}

	PanelPreviewThumbnails.Reserve(BuiltThumbnails.Num());
	for (const FMetaAgentImagePreviewRuntime::FPanelPreviewThumbnail& BuiltThumbnail : BuiltThumbnails)
	{
		FMetaAgentGUIPreviewThumbnail Thumbnail;
		Thumbnail.Texture = BuiltThumbnail.Texture;
		Thumbnail.Label = BuiltThumbnail.Label;
		PanelPreviewThumbnails.Add(Thumbnail);
	}
}

void UMetaAgentParticleOrchestrator::SetCachedPreviewPlaneMesh(UStaticMeshComponent* Mesh)
{
	CachedPreviewPlaneMesh = Mesh;
}

bool UMetaAgentParticleOrchestrator::RequestPatternQueue(UMetaAgentParticlePatternAsset* PatternAsset)
{
	if (!PatternAsset || !ParticleRuntime)
	{
		return false;
	}

	if (ArePatternTagsBlocked(PatternAsset->PatternTags))
	{
		return false;
	}

	return ParticleRuntime->RequestPatternQueue(PatternAsset);
}

FString UMetaAgentParticleOrchestrator::BuildPatternStatusText() const
{
	return ParticleRuntime ? ParticleRuntime->BuildPatternStatusText() : FString();
}

FString UMetaAgentParticleOrchestrator::BuildPatternTimingsText() const
{
	return ParticleRuntime ? ParticleRuntime->BuildPatternTimingsText() : FString();
}

FString UMetaAgentParticleOrchestrator::BuildPatternShapeText() const
{
	if (ParticleRuntime && ParticleRuntime->IsPatternActive())
	{
		return ParticleRuntime->BuildPatternShapeText();
	}

	return FString::Printf(
		TEXT("Pattern Shape: %s | Sampling=%s | ScatterGrid=%.1f Jitter=%.2f | Forming=%s | ImageLoaded=%s"),
		*PatternConfig.Shape.GetShapeDisplayName(),
		*PatternConfig.Shape.GetImageSamplingDisplayName(),
		PatternConfig.Shape.DensityGridScale,
		PatternConfig.Shape.TargetJitterNormalized,
		*PatternConfig.Forming.GetModeDisplayName(),
		LatestPngPreviewTexture ? TEXT("TRUE") : TEXT("FALSE"));
}

FString UMetaAgentParticleOrchestrator::BuildRuntimeStatusText() const
{
	return ParticleRuntime ? ParticleRuntime->BuildStatusText() : TEXT("ParticleRuntime: unavailable");
}

// ===== MetaAgentParticleInputRouter.cpp =====
namespace
{
	FMetaAgentGUIActionRow MakeParticleRow(const FString& KeyLabel, const FString& Description, const FName ActionId)
	{
		FMetaAgentGUIActionRow Row;
		Row.KeyLabel = KeyLabel;
		Row.Description = Description;
		Row.ActionId = ActionId;
		return Row;
	}
}

void FMetaAgentParticleInputRouter::BindKeyboardInput(
	AMetaAgentPlayerController* Controller,
	UInputComponent* InputComponent,
	UMetaAgentParticleOrchestrator* Orchestrator)
{
	if (!Controller || !InputComponent || !Orchestrator)
	{
		return;
	}

	InputComponent->BindKey(EKeys::F, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleLoadPreviewPressed);
	InputComponent->BindKey(EKeys::Comma, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleStepPatternBackwardPressed);
	InputComponent->BindKey(EKeys::Period, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleStepPatternForwardPressed);
	InputComponent->BindKey(EKeys::B, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCyclePresetPressed);
	InputComponent->BindKey(EKeys::T, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleSamplingPressed);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleFormingPressed);
	InputComponent->BindKey(EKeys::K, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleReturningPressed);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleOverlayPressed);
}

TArray<FString> FMetaAgentParticleInputRouter::GetParticleKeyHelpLines()
{
	TArray<FString> Lines;
	for (const FMetaAgentGUIActionRow& Row : GetParticleGUIActionRows())
	{
		Lines.Add(FString::Printf(TEXT("%-7s : %s"), *Row.KeyLabel, *Row.Description));
	}
	Lines.Add(TEXT("Console: MetaAgent.Pattern.ScatterGrid / .ScatterJitter / .Forming / .Returning"));
	Lines.Add(TEXT("Console: MetaAgent.Pattern.Cancel / .SkipHold / .Dissipate / .Ready"));
	return Lines;
}

TArray<FMetaAgentGUIActionRow> FMetaAgentParticleInputRouter::GetParticleGUIActionRows()
{
	TArray<FMetaAgentGUIActionRow> Rows;
	for (const metaagent::app::GuiPanelRow& RowSpec : metaagent::particle::particle_gui_panel_rows())
	{
		FMetaAgentGUIActionRow Row;
		Row.KeyLabel = FString(UTF8_TO_TCHAR(RowSpec.key_label.c_str()));
		Row.Description = FString(UTF8_TO_TCHAR(RowSpec.description.c_str()));
		Row.ActionId = FName(*FString(UTF8_TO_TCHAR(RowSpec.action_id.c_str())));
		Rows.Add(Row);
	}
	return Rows;
}

// ===== MetaAgentParticleRepresentationDriver.cpp =====
namespace MetaAgentRepresentationDriverInternal
{
	static const FName DirectDriverId(TEXT("DirectPosition"));
	static const FName ParameterDriverId(TEXT("NiagaraParameters"));

	bool ComponentHasLiveNiagaraInstance(const UNiagaraComponent& NiagaraComponent)
	{
		if (!IsValid(&NiagaraComponent) || !NiagaraComponent.IsActive() || !IsValid(NiagaraComponent.GetOwner()))
		{
			return false;
		}

		if (UWorld* World = NiagaraComponent.GetWorld())
		{
			if (!IsValid(World) || World->bIsTearingDown)
			{
				return false;
			}
		}

		const FNiagaraSystemInstanceControllerConstPtr InstanceController =
			NiagaraComponent.GetSystemInstanceController();
		if (!InstanceController.IsValid() || !InstanceController->IsValid())
		{
			return false;
		}

		FNiagaraSystemInstance* SystemInstance = InstanceController->GetSoloSystemInstance();
		if (!SystemInstance)
		{
			SystemInstance = InstanceController->GetSystemInstance_Unsafe();
		}

		return SystemInstance != nullptr && !SystemInstance->IsComplete();
	}

	bool CanPushTargetPayload(
		const UNiagaraComponent& NiagaraComponent,
		const UMetaAgentNiagaraSystemProfile* Profile)
	{
		if (!Profile || !IsValid(Profile) || !Profile->HasCapability(EMetaAgentNiagaraDriverCapability::TargetArrayUpload))
		{
			return false;
		}

		if (!ComponentHasLiveNiagaraInstance(NiagaraComponent))
		{
			return false;
		}

		return !Profile->TargetDataParameterName.IsNone() || !Profile->TargetCountParameterName.IsNone();
	}

	void PushTargetArrays(
		UNiagaraComponent& NiagaraComponent,
		const UMetaAgentNiagaraSystemProfile* Profile,
		const FMetaAgentParticleRepresentationFrame& Frame,
		UMetaAgentNiagaraTargetData* SharedTargetData)
	{
		if (!CanPushTargetPayload(NiagaraComponent, Profile))
		{
			return;
		}

		const int32 TargetCount = Frame.PatternWorldTargets.Num();
		if (!Profile->TargetCountParameterName.IsNone()
			&& UMetaAgentNiagaraSystemProfile::ComponentExposesUserParameter(
				NiagaraComponent,
				Profile->TargetCountParameterName))
		{
			NiagaraComponent.SetVariableInt(Profile->TargetCountParameterName, TargetCount);
		}

		if (!Profile->TargetDataParameterName.IsNone()
			&& IsValid(SharedTargetData)
			&& TargetCount > 0
			&& UMetaAgentNiagaraSystemProfile::ComponentExposesUserParameter(
				NiagaraComponent,
				Profile->TargetDataParameterName))
		{
			NiagaraComponent.SetVariableObject(Profile->TargetDataParameterName, SharedTargetData);
		}
	}

	class FMetaAgentDirectPositionRepresentationDriver final : public IMetaAgentParticleRepresentationDriver
	{
	public:
		virtual FName GetDriverId() const override { return DirectDriverId; }
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Direct;
		}

		virtual bool SupportsComponent(
			const UNiagaraComponent& NiagaraComponent,
			const UMetaAgentNiagaraSystemProfile* Profile) const override
		{
			return NiagaraComponent.IsActive()
				&& (!Profile || Profile->HasCapability(EMetaAgentNiagaraDriverCapability::DirectPositionWrite));
		}

		virtual int32 ApplyFrame(
			const FMetaAgentParticleRepresentationFrame& Frame,
			FMetaAgentParticleActuationRequest& Request,
			const UMetaAgentNiagaraSystemProfile* Profile,
			TArray<FVector>& OutAppliedWorldPositions) const override
		{
			return FMetaAgentParticleActuation::ApplyDirect(Request, OutAppliedWorldPositions);
		}
	};

	class FMetaAgentParameterRepresentationDriver final : public IMetaAgentParticleRepresentationDriver
	{
	public:
		virtual FName GetDriverId() const override { return ParameterDriverId; }
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Parameters;
		}

		virtual bool SupportsComponent(
			const UNiagaraComponent& NiagaraComponent,
			const UMetaAgentNiagaraSystemProfile* Profile) const override
		{
			if (!NiagaraComponent.IsActive())
			{
				return false;
			}

			if (!Profile)
			{
				return true;
			}

			FString MissingParameter;
			return Profile->ValidateComponent(const_cast<UNiagaraComponent*>(&NiagaraComponent), MissingParameter);
		}

		virtual int32 ApplyFrame(
			const FMetaAgentParticleRepresentationFrame& Frame,
			FMetaAgentParticleActuationRequest& Request,
			const UMetaAgentNiagaraSystemProfile* Profile,
			TArray<FVector>& OutAppliedWorldPositions) const override
		{
			(void)Frame;
			(void)Profile;
			FMetaAgentParticleActuation::ApplyParameters(Request);
			return FMetaAgentParticleActuation::ComposeWorldPositionsFromRequest(Request, OutAppliedWorldPositions);
		}
	};

	void PushTargetPayloadToComponents(
		const FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		const FMetaAgentParticleRepresentationFrame& Frame,
		UMetaAgentNiagaraTargetData* SharedTargetData)
	{
		if (!Profile || !IsValid(Profile))
		{
			return;
		}

		for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : Request.TrackedComponents)
		{
			UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
			if (!IsValid(NiagaraComponent)
				|| !NiagaraComponent->IsActive()
				|| !IsValid(NiagaraComponent->GetOwner()))
			{
				continue;
			}

			if (!ComponentHasLiveNiagaraInstance(*NiagaraComponent))
			{
				continue;
			}

			PushTargetArrays(*NiagaraComponent, Profile, Frame, SharedTargetData);
		}
	}
}

TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>&
FMetaAgentParticleRepresentationDriverRegistry::GetDrivers()
{
	static TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>> Drivers;
	return Drivers;
}

void FMetaAgentParticleRepresentationDriverRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>& Drivers = GetDrivers();
	if (Drivers.Num() > 0)
	{
		return;
	}

	using namespace MetaAgentRepresentationDriverInternal;
	Drivers.Add(MakeUnique<FMetaAgentDirectPositionRepresentationDriver>());
	Drivers.Add(MakeUnique<FMetaAgentParameterRepresentationDriver>());
}

void FMetaAgentParticleRepresentationDriverRegistry::RegisterDriver(
	TUniquePtr<IMetaAgentParticleRepresentationDriver> Driver)
{
	RegisterDefaults();
	if (Driver)
	{
		GetDrivers().Add(MoveTemp(Driver));
	}
}

const IMetaAgentParticleRepresentationDriver& FMetaAgentParticleRepresentationDriverRegistry::ResolveDriver(
	const EMetaAgentParticleActuationMode EffectiveMode)
{
	RegisterDefaults();

	const EMetaAgentParticleActuationMode SanitizedMode =
		EffectiveMode == EMetaAgentParticleActuationMode::Direct
			? EMetaAgentParticleActuationMode::Direct
			: EMetaAgentParticleActuationMode::Parameters;

	for (const TUniquePtr<IMetaAgentParticleRepresentationDriver>& Driver : GetDrivers())
	{
		if (Driver && Driver->GetActuationMode() == SanitizedMode)
		{
			return *Driver;
		}
	}

	checkf(GetDrivers().Num() > 0 && GetDrivers()[0], TEXT("MetaAgent representation drivers were not registered."));
	return *GetDrivers()[0];
}

void FMetaAgentParticleRepresentationDriverRegistry::BuildActuationRequestFromFrame(
	const FMetaAgentParticleRepresentationFrame& Frame,
	FMetaAgentParticleActuationRequest& OutRequest)
{
	OutRequest.BlendAlpha = Frame.Phase.BlendAlpha;
	OutRequest.HoldPulseScale = Frame.Phase.Emphasis;
	OutRequest.PatternCenter = Frame.PatternCenter;
	OutRequest.bPatternActive = Frame.bPatternActive;
	OutRequest.bUseReturnHoldBlend = Frame.bUseReturnHoldBlend;
	OutRequest.PatternState = Frame.PatternState;
	OutRequest.bAnticipatingMotion = Frame.bAnticipatingMotion;
	OutRequest.bDissipatingMotion = Frame.bDissipatingMotion;
	OutRequest.AnticipationElapsedSeconds = Frame.AnticipationElapsedSeconds;
	OutRequest.AnticipationAmplitudeCm = Frame.AnticipationAmplitudeCm;
	OutRequest.AnticipationFrequencyHz = Frame.AnticipationFrequencyHz;
	OutRequest.AnticipationIdleBlendDurationSeconds = Frame.AnticipationIdleBlendDurationSeconds;
	OutRequest.AnticipationHandoffElapsedSeconds = Frame.AnticipationHandoffElapsedSeconds;
	OutRequest.FormingAnticipationCarryoverDurationSeconds = Frame.FormingAnticipationCarryoverDurationSeconds;
	OutRequest.DissipateVisibility = Frame.DissipateVisibility;
	OutRequest.FormingStateElapsedSeconds = Frame.FormingStateElapsedSeconds;
	OutRequest.FormingDurationSeconds = Frame.FormingDurationSeconds;
	OutRequest.FormingDeltaTimeSeconds = Frame.FormingDeltaTimeSeconds;
	OutRequest.FormingSteeringWeight = Frame.FormingSteeringWeight;

	OutRequest.BaselineWorldPositions = &Frame.BaselineWorldPositions;
	OutRequest.PatternWorldTargets = &Frame.PatternWorldTargets;

	if (Frame.bUseReturnHoldBlend)
	{
		OutRequest.ReturnHoldPositions = &Frame.ReturnHoldPositions;
		OutRequest.ReturnRestPositions = &Frame.ReturnRestPositions;
	}

	if (Frame.bDissipatingMotion)
	{
		OutRequest.DissipateStartPositions = &Frame.DissipateStartPositions;
	}

	if (Frame.IdleBaselineWorldPositions.Num() > 0)
	{
		OutRequest.IdleBaselineWorldPositions = &Frame.IdleBaselineWorldPositions;
	}

	if (Frame.FormingSteeringOffsets.Num() > 0)
	{
		OutRequest.FormingSteeringOffsets = &Frame.FormingSteeringOffsets;
	}

	if (Frame.StateEffectOffsets.Num() > 0)
	{
		OutRequest.StateEffectOffsets = &Frame.StateEffectOffsets;
	}

	const bool bNeedsFormingSettings = Frame.bReturnUsesMotionSolver
		|| Frame.PatternState == EMetaAgentParticlePatternState::Forming
		|| Frame.PatternState == EMetaAgentParticlePatternState::Holding
		|| Frame.PatternState == EMetaAgentParticlePatternState::Returning;

	if (bNeedsFormingSettings)
	{
		OutRequest.FormingSettings = Frame.bReturnUsesMotionSolver
			? &Frame.ReturnMotionSettings
			: &Frame.FormingSettings;
	}
}

int32 FMetaAgentParticleRepresentationDriverRegistry::ApplyRepresentationFrame(
	const FMetaAgentParticleRepresentationFrame& Frame,
	FMetaAgentParticleActuationRequest& Request,
	const UMetaAgentNiagaraSystemProfile* Profile,
	const EMetaAgentParticleActuationMode ConfiguredMode,
	const float ReturnReleaseAuthorityThreshold,
	UMetaAgentNiagaraTargetData* SharedTargetData,
	TArray<FVector>& OutAppliedWorldPositions)
{
	using namespace MetaAgentRepresentationDriverInternal;

	const EMetaAgentParticleActuationMode PreferredMode = Profile
		? Profile->PreferredActuationMode
		: ConfiguredMode;

#if WITH_EDITOR
	constexpr bool bHybridUseDirectPath = true;
#else
	constexpr bool bHybridUseDirectPath = false;
#endif

	metaagent::particle::ActuationPolicyInput PolicyInput;
	PolicyInput.configured_mode = MetaAgentTypeBridge::to_core_actuation_mode(ConfiguredMode);
	PolicyInput.preferred_mode = MetaAgentTypeBridge::to_core_actuation_mode(PreferredMode);
	PolicyInput.hybrid_use_direct_path = bHybridUseDirectPath;
	PolicyInput.use_return_hold_blend = Frame.bUseReturnHoldBlend;
	PolicyInput.blend_alpha = Frame.Phase.BlendAlpha;
	PolicyInput.return_release_authority_threshold = ReturnReleaseAuthorityThreshold;

	const metaagent::particle::ActuationPolicyResult PolicyResult =
		metaagent::particle::RepresentationActuationPolicy::resolve(PolicyInput);

	const IMetaAgentParticleRepresentationDriver& ParameterDriver =
		ResolveDriver(EMetaAgentParticleActuationMode::Parameters);
	const IMetaAgentParticleRepresentationDriver& DirectDriver =
		ResolveDriver(EMetaAgentParticleActuationMode::Direct);

	auto ApplyParametersPath = [&](const bool bPushTargetPayload)
	{
		ParameterDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);
		const bool bShouldUploadTargets = bPushTargetPayload
			&& Profile
			&& IsValid(Profile)
			&& Profile->HasCapability(EMetaAgentNiagaraDriverCapability::TargetArrayUpload)
			&& PolicyResult.delivery != metaagent::particle::ActuationDelivery::DirectWrite
			&& PolicyResult.delivery != metaagent::particle::ActuationDelivery::HybridDirectWithScalars;
		if (bShouldUploadTargets)
		{
			PushTargetPayloadToComponents(Request, Profile, Frame, SharedTargetData);
		}
	};

	if (PolicyResult.force_pattern_inactive)
	{
		Request.bPatternActive = false;
		Request.BlendAlpha = PolicyResult.override_blend_alpha;
		ApplyParametersPath(PolicyResult.push_target_payload);
		if (Request.StateEffectOffsets
			&& Request.StateEffectOffsets->Num() > 0
			&& Request.BaselineWorldPositions
			&& Request.BaselineWorldPositions->Num() > 0)
		{
			return DirectDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);
		}
		return 0;
	}

	switch (PolicyResult.delivery)
	{
	case metaagent::particle::ActuationDelivery::ParametersOnly:
	case metaagent::particle::ActuationDelivery::ParametersWithTargets:
		ApplyParametersPath(PolicyResult.push_target_payload);
		return FMetaAgentParticleActuation::ComposeWorldPositionsFromRequest(Request, OutAppliedWorldPositions);
	case metaagent::particle::ActuationDelivery::DirectWrite:
		return DirectDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);
	case metaagent::particle::ActuationDelivery::HybridDirectWithScalars:
	default:
	{
		const int32 AppliedCount = DirectDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);
		ApplyParametersPath(false);
		return AppliedCount;
	}
	}
}

// ===== MetaAgentNiagaraExportHandler.cpp =====
void UMetaAgentNiagaraExportHandler::Initialize(AMetaAgentPlayerController* InOwnerController)
{
	OwnerController = InOwnerController;
}

void UMetaAgentNiagaraExportHandler::ReceiveParticleData_Implementation(
	const TArray<FBasicParticleData>& Data,
	UNiagaraSystem* NiagaraSystem,
	const FVector& SimulationPositionOffset)
{
	AMetaAgentPlayerController* Controller = OwnerController.Get();
	if (!Controller)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: export handler has no owner controller."));
		return;
	}

	TArray<FVector> ParticlePositions;
	ParticlePositions.Reserve(Data.Num());

	for (const FBasicParticleData& ParticleData : Data)
	{
		ParticlePositions.Add(ParticleData.Position + SimulationPositionOffset);
	}

	const FName SourceSystemName = NiagaraSystem ? NiagaraSystem->GetFName() : NAME_None;
	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleRuntime: ReceiveParticleData from system '%s' with %d particle(s)."),
		*SourceSystemName.ToString(),
		ParticlePositions.Num());

	Controller->SubmitNiagaraParticlePositions(ParticlePositions, SourceSystemName, NAME_None);
}

// ===== MetaAgentNiagaraSystemProfile.cpp =====
namespace MetaAgentNiagaraProfileInternal
{
	static const FName PatternPhaseParameterName(TEXT("MetaAgentPatternPhase"));
	static const FName PatternCenterParameterName(TEXT("MetaAgentPatternCenter"));
	static const FName PatternActiveParameterName(TEXT("MetaAgentPatternActive"));
	static const FName PatternHoldScaleParameterName(TEXT("MetaAgentPatternHoldScale"));
	static const FName PatternDissipateActiveParameterName(TEXT("MetaAgentPatternDissipateActive"));
	static const FName PatternDissipateVisibilityParameterName(TEXT("MetaAgentPatternDissipateVisibility"));
	static const FName FormingModeParameterName(TEXT("MetaAgentFormingMode"));
	static const FName FormingArcLiftParameterName(TEXT("MetaAgentFormingArcLift"));
	static const FName FormingSpiralTurnsParameterName(TEXT("MetaAgentFormingSpiralTurns"));

	bool ParameterNameMatchesExposed(const FName ParameterName, const FName ExposedName)
	{
		if (ParameterName.IsNone() || ExposedName.IsNone())
		{
			return false;
		}

		if (ExposedName == ParameterName)
		{
			return true;
		}

		const FString ParameterString = ParameterName.ToString();
		const FString ExposedString = ExposedName.ToString();
		const FString ParameterSuffix = FString::Printf(TEXT(".%s"), *ParameterString);
		return ExposedString.Equals(ParameterString, ESearchCase::CaseSensitive)
			|| ExposedString.EndsWith(ParameterSuffix, ESearchCase::CaseSensitive);
	}

	bool StoreContainsParameterName(const FNiagaraParameterStore& Store, const FName ParameterName)
	{
		TArray<FNiagaraVariable> Parameters;
		Store.GetParameters(Parameters);
		for (const FNiagaraVariable& Parameter : Parameters)
		{
			if (ParameterNameMatchesExposed(ParameterName, Parameter.GetName()))
			{
				return true;
			}
		}
		return false;
	}

	bool ComponentHasLiveInstance(const UNiagaraComponent& NiagaraComponent)
	{
		if (!IsValid(&NiagaraComponent) || !NiagaraComponent.IsActive() || !IsValid(NiagaraComponent.GetOwner()))
		{
			return false;
		}

		const FNiagaraSystemInstanceControllerConstPtr InstanceController =
			NiagaraComponent.GetSystemInstanceController();
		return InstanceController.IsValid() && InstanceController->IsValid();
	}

	FNiagaraSystemInstance* ResolveLiveSystemInstance(const UNiagaraComponent& NiagaraComponent)
	{
		const FNiagaraSystemInstanceControllerConstPtr InstanceController =
			NiagaraComponent.GetSystemInstanceController();
		if (!InstanceController.IsValid() || !InstanceController->IsValid())
		{
			return nullptr;
		}

		if (FNiagaraSystemInstance* SoloInstance = InstanceController->GetSoloSystemInstance())
		{
			return SoloInstance;
		}

		return InstanceController->GetSystemInstance_Unsafe();
	}
}

bool UMetaAgentNiagaraSystemProfile::HasCapability(const EMetaAgentNiagaraDriverCapability Capability) const
{
	return (Capabilities & static_cast<int32>(Capability)) != 0;
}

bool UMetaAgentNiagaraSystemProfile::ComponentExposesUserParameter(
	const UNiagaraComponent& NiagaraComponent,
	const FName ParameterName)
{
	using namespace MetaAgentNiagaraProfileInternal;

	if (ParameterName.IsNone() || !ComponentHasLiveInstance(NiagaraComponent))
	{
		return false;
	}

	// Never query UNiagaraSystem::GetExposedParameters() here. The asset pointer can be stale
	// mid-reload and crash inside the exposed-parameter store (seen as AV at ~0x138).
	if (StoreContainsParameterName(NiagaraComponent.GetOverrideParameters(), ParameterName))
	{
		return true;
	}

	if (FNiagaraSystemInstance* SystemInstance = ResolveLiveSystemInstance(NiagaraComponent))
	{
		return StoreContainsParameterName(SystemInstance->GetInstanceParameters(), ParameterName);
	}

	return false;
}

bool UMetaAgentNiagaraSystemProfile::ValidateComponent(
	UNiagaraComponent* NiagaraComponent,
	FString& OutMissingParameter) const
{
	if (!NiagaraComponent)
	{
		OutMissingParameter = TEXT("<null component>");
		return false;
	}

	using namespace MetaAgentNiagaraProfileInternal;

	TArray<FName> ParametersToCheck = RequiredUserParameters;
	if (ParametersToCheck.Num() <= 0)
	{
		ParametersToCheck = {
			PatternPhaseParameterName,
			PatternCenterParameterName,
			PatternActiveParameterName,
			PatternHoldScaleParameterName,
			FormingModeParameterName
		};
	}

	for (const FName ParameterName : ParametersToCheck)
	{
		if (!ComponentExposesUserParameter(*NiagaraComponent, ParameterName))
		{
			OutMissingParameter = ParameterName.ToString();
			return false;
		}
	}

	if (HasCapability(EMetaAgentNiagaraDriverCapability::TargetArrayUpload)
		&& !TargetCountParameterName.IsNone()
		&& !ComponentExposesUserParameter(*NiagaraComponent, TargetCountParameterName))
	{
		OutMissingParameter = TargetCountParameterName.ToString();
		return false;
	}

	OutMissingParameter.Reset();
	return true;
}

const UMetaAgentNiagaraSystemProfile* UMetaAgentNiagaraSystemProfile::GetDefaultProfile()
{
	static TObjectPtr<UMetaAgentNiagaraSystemProfile> DefaultProfile;
	if (!DefaultProfile)
	{
		DefaultProfile = NewObject<UMetaAgentNiagaraSystemProfile>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient | RF_Public);
		DefaultProfile->DisplayName = FText::FromString(TEXT("MetaAgent Default Niagara Profile"));
		DefaultProfile->Capabilities = static_cast<int32>(
			EMetaAgentNiagaraDriverCapability::ParameterPhase
			| EMetaAgentNiagaraDriverCapability::DissipateVisibility);
		DefaultProfile->RequiredUserParameters = {
			MetaAgentNiagaraProfileInternal::PatternPhaseParameterName,
			MetaAgentNiagaraProfileInternal::PatternCenterParameterName,
			MetaAgentNiagaraProfileInternal::PatternActiveParameterName,
			MetaAgentNiagaraProfileInternal::PatternHoldScaleParameterName,
			MetaAgentNiagaraProfileInternal::FormingModeParameterName
		};
	}
	return DefaultProfile;
}
