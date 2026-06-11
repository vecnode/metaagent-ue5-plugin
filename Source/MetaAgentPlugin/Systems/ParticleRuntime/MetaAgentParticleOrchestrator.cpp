// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleOrchestrator.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MetaAgent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleGameplayTags.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"
#include "Misc/Paths.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

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
		Context.BaselineWorldPositions = ParticleRuntime->GetKnownParticlePositions();
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

bool UMetaAgentParticleOrchestrator::PrepareShapeContextForPlay()
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

	ParticleRuntime->SetPatternShapeContext(BuildShapeContext());
	RequestImageMaskBuild();
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

	const int32 ParticleCount = FMath::Max(ParticleRuntime->GetKnownParticleCount(), 128);
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
			FString::Printf(TEXT("Preset applied (%s). Press >> to step pattern."), *PatternConfig.GetPresetDisplayName()));
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
		ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing;
	LastTriggeredEffectId = EffectId;
	LastEffectSpec = Spec;
	bHasLastEffectSpec = true;

	if (Result.bAwaitingAsyncPrepare)
	{
		Result.UserMessage = FText::FromString(
			FString::Printf(TEXT("Preparing image shape (%s)."), *ParticleRuntime->BuildPatternStatusText()));
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

	if (EffectId == MetaAgentParticleEffectIds::PatternStepForward)
	{
		return StepPatternStateForward();
	}

	if (EffectId == MetaAgentParticleEffectIds::PatternStepBackward)
	{
		return StepPatternStateBackward();
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

FMetaAgentParticleEffectResult UMetaAgentParticleOrchestrator::StepPatternStateForward()
{
	FMetaAgentParticleEffectResult Result;
	Result.EffectId = MetaAgentParticleEffectIds::PatternStepForward;

	if (!ParticleRuntime)
	{
		Result.UserMessage = FText::FromString(TEXT("Particle runtime not initialized."));
		return Result;
	}

	if (ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Idle)
	{
		ParticleRuntime->ForceCaptureParticles();
		PrepareShapeContextForPlay();
		PatternConfig.Shape.ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;
		SyncConfigToRuntime();

		LastTriggeredEffectId = MetaAgentParticleEffectIds::ImageReveal;
		LastEffectSpec = FMetaAgentParticleEffectSpec();
		LastEffectSpec.PatternConfig = PatternConfig;
		LastEffectSpec.bOverridePatternConfig = true;
		LastEffectSpec.bStartPattern = false;
		bHasLastEffectSpec = true;
	}

	const bool bAdvanced = ParticleRuntime->AdvancePatternStateForward();
	Result.bSuccess = bAdvanced;
	Result.bAwaitingAsyncPrepare =
		ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing;
	Result.UserMessage = FText::FromString(
		bAdvanced
			? FString::Printf(TEXT("Pattern >> %s"), *ParticleRuntime->BuildPatternStatusText())
			: TEXT("Pattern step forward unavailable (busy or no captured particles)."));
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
	Result.bSuccess = bRetreated;
	Result.UserMessage = FText::FromString(
		bRetreated
			? FString::Printf(TEXT("Pattern << %s"), *ParticleRuntime->BuildPatternStatusText())
			: TEXT("Pattern step backward unavailable (already idle)."));
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
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
	PrepareShapeContextForPlay();

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
