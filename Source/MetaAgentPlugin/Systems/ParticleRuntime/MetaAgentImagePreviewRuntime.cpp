// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/Paths.h"

namespace
{
	bool NameMatches(const FString& CandidateName, const FString& WantedName)
	{
		if (CandidateName.Equals(WantedName, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString Prefix = WantedName + TEXT("_");
		return CandidateName.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	FString ResolveDesktopDirectory()
	{
		const FString UserProfileDir = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (!UserProfileDir.IsEmpty())
		{
			const FString DesktopDir = FPaths::Combine(UserProfileDir, TEXT("Desktop"));
			if (FPaths::DirectoryExists(DesktopDir))
			{
				return DesktopDir;
			}
		}

		const FString OneDriveDir = FPlatformMisc::GetEnvironmentVariable(TEXT("OneDrive"));
		if (!OneDriveDir.IsEmpty())
		{
			const FString OneDriveDesktopDir = FPaths::Combine(OneDriveDir, TEXT("Desktop"));
			if (FPaths::DirectoryExists(OneDriveDesktopDir))
			{
				return OneDriveDesktopDir;
			}
		}

		return FPaths::ProjectDir();
	}
}

FString FMetaAgentImagePreviewRuntime::ResolveDefaultSdxlPngPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ResolveDesktopDirectory(), TEXT("github/agent-sequencer-app/output/sdxl_latest.png")));
}

UStaticMeshComponent* FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
	UWorld* World,
	const FName ActorName,
	const FName ComponentName,
	UStaticMeshComponent* CachedMesh)
{
	if (CachedMesh)
	{
		return CachedMesh;
	}

	if (!World)
	{
		return nullptr;
	}

	const FString WantedActorName = ActorName.ToString();
	const FString WantedComponentName = ComponentName.ToString();

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* CandidateActor = *It;
		if (!CandidateActor)
		{
			continue;
		}

		UStaticMeshComponent* CandidateMesh = CandidateActor->GetStaticMeshComponent();
		if (!CandidateMesh)
		{
			continue;
		}

		const bool bActorNameMatches =
			NameMatches(CandidateActor->GetActorNameOrLabel(), WantedActorName) ||
			NameMatches(CandidateActor->GetName(), WantedActorName);

		const bool bComponentNameMatches =
			NameMatches(CandidateMesh->GetName(), WantedComponentName);

		if (bActorNameMatches || bComponentNameMatches)
		{
			return CandidateMesh;
		}
	}

	return nullptr;
}

FMetaAgentParticleShapeFrame FMetaAgentImagePreviewRuntime::BuildShapeFrameFromPreviewPlane(
	const UStaticMeshComponent* PreviewMesh,
	const float ZOffsetCm)
{
	FMetaAgentParticleShapeFrame Frame;
	if (!PreviewMesh)
	{
		return Frame;
	}

	const FBoxSphereBounds WorldBounds = PreviewMesh->Bounds;
	Frame.Origin = WorldBounds.Origin;
	Frame.Orientation = PreviewMesh->GetComponentRotation();
	Frame.ZOffsetCm = ZOffsetCm;

	FVector LocalMin = FVector::ZeroVector;
	FVector LocalMax = FVector::ZeroVector;
	PreviewMesh->GetLocalBounds(LocalMin, LocalMax);
	const FVector LocalExtent = (LocalMax - LocalMin) * 0.5f;
	const FVector Scale = PreviewMesh->GetComponentScale();
	Frame.ExtentsCm = FVector2D(
		FMath::Max(10.0f, LocalExtent.X * Scale.X * 2.0f),
		FMath::Max(10.0f, LocalExtent.Y * Scale.Y * 2.0f));

	return Frame;
}

FMetaAgentParticleShapeFrame FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
	const TArray<FVector>& BaselineWorldPositions,
	const float ShapeWidthCm,
	const float ShapeHeightCm,
	const float ZOffsetCm)
{
	FMetaAgentParticleShapeFrame Frame;
	Frame.ZOffsetCm = ZOffsetCm;
	Frame.Orientation = FRotator::ZeroRotator;

	const int32 ParticleCount = BaselineWorldPositions.Num();
	if (ParticleCount <= 0)
	{
		Frame.ExtentsCm = FVector2D(ShapeWidthCm, ShapeHeightCm > 0.0f ? ShapeHeightCm : ShapeWidthCm);
		return Frame;
	}

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : BaselineWorldPositions)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	Frame.Origin = Centroid;
	Frame.ExtentsCm = FVector2D(
		ShapeWidthCm,
		ShapeHeightCm > 0.0f ? ShapeHeightCm : ShapeWidthCm);

	return Frame;
}

UTexture2D* FMetaAgentImagePreviewRuntime::ImportPngTexture(const FString& PngPath)
{
	if (!FPaths::FileExists(PngPath))
	{
		return nullptr;
	}

	UTexture2D* ImportedTexture = FImageUtils::ImportFileAsTexture2D(PngPath);
	if (!ImportedTexture)
	{
		return nullptr;
	}

	ImportedTexture->SRGB = true;
	ImportedTexture->UpdateResource();
	return ImportedTexture;
}

bool FMetaAgentImagePreviewRuntime::EnsurePreviewTextureLoaded(
	AMetaAgentPlayerController& Controller,
	FString& OutResolvedPath)
{
	OutResolvedPath.Reset();

	if (Controller.GetLatestPngPreviewTexture())
	{
		OutResolvedPath = Controller.GetLastLoadedPreviewImagePath();
		if (OutResolvedPath.IsEmpty())
		{
			OutResolvedPath = ResolveDefaultSdxlPngPath();
		}
		return true;
	}

	const FString PngPath = ResolveDefaultSdxlPngPath();
	OutResolvedPath = PngPath;

	UTexture2D* ImportedTexture = ImportPngTexture(PngPath);
	if (!ImportedTexture)
	{
		return false;
	}

	Controller.SetLatestPngPreviewTexture(ImportedTexture);
	Controller.SetLastLoadedPreviewImagePath(PngPath);
	return true;
}
