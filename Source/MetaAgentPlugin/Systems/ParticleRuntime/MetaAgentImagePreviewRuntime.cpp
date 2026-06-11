// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

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

	bool CopyFImageToFColorPixels(const FImage& InImage, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		OutWidth = InImage.SizeX;
		OutHeight = InImage.SizeY;
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			return false;
		}

		if (InImage.Format == ERawImageFormat::BGRA8)
		{
			const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
			if (InImage.RawData.Num() < ExpectedBytes)
			{
				return false;
			}

			OutPixels.SetNum(OutWidth * OutHeight);
			FMemory::Memcpy(OutPixels.GetData(), InImage.RawData.GetData(), ExpectedBytes);
			return true;
		}

		FImage BGRAImage;
		BGRAImage.Init(OutWidth, OutHeight, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FImageCore::CopyImage(InImage, BGRAImage);

		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (BGRAImage.RawData.Num() < ExpectedBytes)
		{
			return false;
		}

		OutPixels.SetNum(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), BGRAImage.RawData.GetData(), ExpectedBytes);
		return true;
	}

	bool ReadTexturePixelsFromPngFile(
		const FString& SourceImagePath,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;

		if (SourceImagePath.IsEmpty() || !FPaths::FileExists(SourceImagePath))
		{
			return false;
		}

		FImage LoadedImage;
		if (!FImageUtils::LoadImage(*SourceImagePath, LoadedImage))
		{
			return false;
		}

		return CopyFImageToFColorPixels(LoadedImage, OutPixels, OutWidth, OutHeight);
	}

	void DownsampleToSize(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const int32 TargetWidth,
		const int32 TargetHeight,
		TArray<FColor>& OutPixels)
	{
		OutPixels.SetNum(TargetWidth * TargetHeight);
		for (int32 Y = 0; Y < TargetHeight; ++Y)
		{
			for (int32 X = 0; X < TargetWidth; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(TargetWidth);
				const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(TargetHeight);
				const int32 SrcX = FMath::Clamp(
					FMath::FloorToInt(U * static_cast<float>(SourceWidth - 1)),
					0,
					SourceWidth - 1);
				const int32 SrcY = FMath::Clamp(
					FMath::FloorToInt(V * static_cast<float>(SourceHeight - 1)),
					0,
					SourceHeight - 1);
				OutPixels[Y * TargetWidth + X] = SourcePixels[SrcY * SourceWidth + SrcX];
			}
		}
	}

	void BuildGrayscalePixels(
		const TArray<FColor>& SourcePixels,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < SourcePixels.Num(); ++Index)
		{
			const FColor& Pixel = SourcePixels[Index];
			const uint8 Gray = static_cast<uint8>(
				FMath::Clamp(
					(0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B),
					0.0f,
					255.0f));
			OutPixels[Index] = FColor(Gray, Gray, Gray, 255);
		}
	}

	float SampleGrayAt(const TArray<float>& Gray, const int32 Width, const int32 Height, const int32 X, const int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
		return Gray[ClampedY * Width + ClampedX];
	}

	void ComputeSobelMagnitudes(
		const TArray<float>& Gray,
		const int32 Width,
		const int32 Height,
		TArray<float>& OutMagnitudes)
	{
		OutMagnitudes.Init(0.0f, Width * Height);
		if (Width < 3 || Height < 3)
		{
			return;
		}

		for (int32 Y = 1; Y < Height - 1; ++Y)
		{
			for (int32 X = 1; X < Width - 1; ++X)
			{
				const float Gx =
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y - 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y - 1) +
					-2.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y) +
					2.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y) +
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y + 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y + 1);
				const float Gy =
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y - 1) +
					-2.0f * SampleGrayAt(Gray, Width, Height, X, Y - 1) +
					-1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y - 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y + 1) +
					2.0f * SampleGrayAt(Gray, Width, Height, X, Y + 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y + 1);
				OutMagnitudes[Y * Width + X] = FMath::Sqrt(Gx * Gx + Gy * Gy);
			}
		}
	}

	void MagnitudesToPixels(
		const TArray<float>& Magnitudes,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		float MaxMagnitude = 0.0f;
		for (const float Magnitude : Magnitudes)
		{
			MaxMagnitude = FMath::Max(MaxMagnitude, Magnitude);
		}

		const float InvMax = MaxMagnitude > KINDA_SMALL_NUMBER ? (1.0f / MaxMagnitude) : 1.0f;
		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < Magnitudes.Num(); ++Index)
		{
			const uint8 Value = static_cast<uint8>(FMath::Clamp(Magnitudes[Index] * InvMax, 0.0f, 1.0f) * 255.0f);
			OutPixels[Index] = FColor(Value, Value, Value, 255);
		}
	}

	UTexture2D* CreateTextureFromPixels(const TArray<FColor>& Pixels, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->SRGB = true;
		Texture->NeverStream = true;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
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
	const FShapeFrameBuildParams& Params)
{
	FMetaAgentParticleShapeFrame Frame;
	Frame.ZOffsetCm = Params.ZOffsetCm;

	const int32 ParticleCount = BaselineWorldPositions.Num();
	if (ParticleCount <= 0)
	{
		Frame.ExtentsCm = FVector2D(
			Params.ShapeWidthCm,
			Params.ShapeHeightCm > 0.0f ? Params.ShapeHeightCm : Params.ShapeWidthCm);
		return Frame;
	}

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : BaselineWorldPositions)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	float BoundingRadiusCm = 0.0f;
	for (const FVector& Position : BaselineWorldPositions)
	{
		BoundingRadiusCm = FMath::Max(BoundingRadiusCm, FVector::Dist(Position, Centroid));
	}

	float ShapeWidthCm = Params.ShapeWidthCm;
	float ShapeHeightCm = Params.ShapeHeightCm;
	if (Params.bAutoFitToParticleSphere && BoundingRadiusCm > KINDA_SMALL_NUMBER)
	{
		const float DiameterCm = FMath::Max(10.0f, BoundingRadiusCm * 2.0f * 0.92f);
		if (Params.ShapeHeightCm > 0.0f && Params.ShapeWidthCm > 0.0f)
		{
			const float Aspect = Params.ShapeHeightCm / Params.ShapeWidthCm;
			ShapeWidthCm = DiameterCm;
			ShapeHeightCm = DiameterCm * Aspect;
		}
		else
		{
			ShapeWidthCm = DiameterCm;
			ShapeHeightCm = DiameterCm;
		}
	}
	if (ShapeHeightCm <= 0.0f)
	{
		ShapeHeightCm = ShapeWidthCm;
	}

	Frame.Origin = Centroid;
	Frame.ExtentsCm = FVector2D(ShapeWidthCm, ShapeHeightCm);
	Frame.Orientation = FRotator::ZeroRotator;

	if (Params.bOrientShapeToView && Params.bHasViewOrigin)
	{
		FVector PlaneNormal = Params.ViewOrigin - Centroid;
		if (!PlaneNormal.Normalize())
		{
			return Frame;
		}

		FVector UpReference = FVector::UpVector;
		if (FMath::Abs(FVector::DotProduct(PlaneNormal, UpReference)) > 0.95f)
		{
			UpReference = FVector::ForwardVector;
		}

		const FVector TangentX = FVector::CrossProduct(UpReference, PlaneNormal).GetSafeNormal();
		const FVector TangentY = FVector::CrossProduct(PlaneNormal, TangentX);
		Frame.Orientation = FRotationMatrix::MakeFromXY(TangentX, TangentY).Rotator();
	}

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

bool FMetaAgentImagePreviewRuntime::BuildPanelPreviewThumbnails(
	const FString& PngPath,
	const int32 PreviewSize,
	TArray<FPanelPreviewThumbnail>& OutThumbnails)
{
	OutThumbnails.Reset();

	const int32 ClampedSize = FMath::Clamp(PreviewSize, 16, 256);
	TArray<FColor> SourcePixels;
	int32 SourceWidth = 0;
	int32 SourceHeight = 0;
	if (!ReadTexturePixelsFromPngFile(PngPath, SourcePixels, SourceWidth, SourceHeight))
	{
		return false;
	}

	TArray<FColor> SourceThumb;
	DownsampleToSize(SourcePixels, SourceWidth, SourceHeight, ClampedSize, ClampedSize, SourceThumb);

	TArray<FColor> GrayscaleThumb;
	BuildGrayscalePixels(SourceThumb, ClampedSize, ClampedSize, GrayscaleThumb);

	TArray<float> GrayValues;
	GrayValues.SetNum(ClampedSize * ClampedSize);
	for (int32 Index = 0; Index < SourceThumb.Num(); ++Index)
	{
		const FColor& Pixel = SourceThumb[Index];
		GrayValues[Index] = (0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B) / 255.0f;
	}

	TArray<float> SobelMagnitudes;
	ComputeSobelMagnitudes(GrayValues, ClampedSize, ClampedSize, SobelMagnitudes);
	TArray<FColor> SobelThumb;
	MagnitudesToPixels(SobelMagnitudes, ClampedSize, ClampedSize, SobelThumb);

	auto AddThumbnail = [&OutThumbnails](UTexture2D* Texture, const FString& Label)
	{
		if (!Texture)
		{
			return;
		}

		FPanelPreviewThumbnail Thumbnail;
		Thumbnail.Texture = Texture;
		Thumbnail.Label = Label;
		OutThumbnails.Add(Thumbnail);
	};

	AddThumbnail(CreateTextureFromPixels(SourceThumb, ClampedSize, ClampedSize), TEXT("Source"));
	AddThumbnail(CreateTextureFromPixels(GrayscaleThumb, ClampedSize, ClampedSize), TEXT("Gray"));
	AddThumbnail(CreateTextureFromPixels(SobelThumb, ClampedSize, ClampedSize), TEXT("Sobel"));
	return OutThumbnails.Num() > 0;
}
