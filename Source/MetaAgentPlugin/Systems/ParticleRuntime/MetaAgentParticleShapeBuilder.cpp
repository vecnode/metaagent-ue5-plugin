// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

#include "Core/MetaAgent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"

namespace
{
	struct FImageMaskCacheKey
	{
		const UTexture2D* Texture = nullptr;
		FString SourceImagePath;
		EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;
		float AlphaThreshold = 0.0f;
		float EdgeThreshold = 0.0f;
		bool bUseLuminance = false;
		int32 SampleResolution = 0;

		bool operator==(const FImageMaskCacheKey& Other) const
		{
			return Texture == Other.Texture
				&& SourceImagePath == Other.SourceImagePath
				&& ImageSamplingMode == Other.ImageSamplingMode
				&& FMath::IsNearlyEqual(AlphaThreshold, Other.AlphaThreshold)
				&& FMath::IsNearlyEqual(EdgeThreshold, Other.EdgeThreshold)
				&& bUseLuminance == Other.bUseLuminance
				&& SampleResolution == Other.SampleResolution;
		}
	};

	uint32 GetTypeHash(const FImageMaskCacheKey& Key)
	{
		uint32 Hash = PointerHash(Key.Texture);
		Hash = HashCombine(Hash, FCrc::StrCrc32(*Key.SourceImagePath));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.ImageSamplingMode));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.AlphaThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.EdgeThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.bUseLuminance));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SampleResolution));
		return Hash;
	}

	static TMap<FImageMaskCacheKey, TArray<FVector>> GImageMaskLocalPointCache;

	float SampleMaskAlpha(
		const TArray<FColor>& Pixels,
		const int32 Width,
		const int32 Height,
		const float U,
		const float V,
		const bool bUseLuminance)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(U * static_cast<float>(Width - 1)), 0, Width - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(V * static_cast<float>(Height - 1)), 0, Height - 1);
		const FColor Pixel = Pixels[Y * Width + X];

		if (bUseLuminance)
		{
			const float Luminance = (0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B) / 255.0f;
			return 1.0f - Luminance;
		}

		return static_cast<float>(Pixel.A) / 255.0f;
	}

	void DownsampleTextureMask(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const int32 TargetResolution,
		const bool bUseLuminance,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutWidth = FMath::Clamp(TargetResolution, 32, 1024);
		OutHeight = OutWidth;

		if (SourceWidth <= 0 || SourceHeight <= 0)
		{
			OutPixels.Reset();
			return;
		}

		const float Aspect = static_cast<float>(SourceWidth) / static_cast<float>(SourceHeight);
		if (Aspect >= 1.0f)
		{
			OutWidth = TargetResolution;
			OutHeight = FMath::Max(32, FMath::RoundToInt(static_cast<float>(TargetResolution) / Aspect));
		}
		else
		{
			OutHeight = TargetResolution;
			OutWidth = FMath::Max(32, FMath::RoundToInt(static_cast<float>(TargetResolution) * Aspect));
		}

		OutPixels.SetNum(OutWidth * OutHeight);
		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			for (int32 X = 0; X < OutWidth; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(OutWidth);
				const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(OutHeight);
				const float MaskAlpha = SampleMaskAlpha(SourcePixels, SourceWidth, SourceHeight, U, V, bUseLuminance);
				const uint8 AlphaByte = static_cast<uint8>(FMath::Clamp(MaskAlpha, 0.0f, 1.0f) * 255.0f);
				OutPixels[Y * OutWidth + X] = FColor(255, 255, 255, AlphaByte);
			}
		}
	}

	bool DecodeBGRA8Pixels(
		const TArray64<uint8>& MipData,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		const int32 BytesPerPixel = 4;
		const int64 ExpectedSize = static_cast<int64>(Width) * static_cast<int64>(Height) * BytesPerPixel;
		if (MipData.Num() < ExpectedSize)
		{
			return false;
		}

		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			const int64 ByteIndex = static_cast<int64>(Index) * BytesPerPixel;
			OutPixels[Index] = FColor(
				MipData[ByteIndex + 2],
				MipData[ByteIndex + 1],
				MipData[ByteIndex + 0],
				MipData[ByteIndex + 3]);
		}

		return true;
	}

	bool DecodeRGBA8Pixels(
		const TArray64<uint8>& MipData,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		const int32 BytesPerPixel = 4;
		const int64 ExpectedSize = static_cast<int64>(Width) * static_cast<int64>(Height) * BytesPerPixel;
		if (MipData.Num() < ExpectedSize)
		{
			return false;
		}

		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			const int64 ByteIndex = static_cast<int64>(Index) * BytesPerPixel;
			OutPixels[Index] = FColor(
				MipData[ByteIndex + 0],
				MipData[ByteIndex + 1],
				MipData[ByteIndex + 2],
				MipData[ByteIndex + 3]);
		}

		return true;
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

	bool ReadTexturePixelsFromSource(UTexture2D& SourceTexture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;

		FTextureSource& Source = SourceTexture.Source;
		if (!Source.IsValid())
		{
			return false;
		}

		OutWidth = Source.GetSizeX();
		OutHeight = Source.GetSizeY();
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			return false;
		}

		TArray64<uint8> MipData;
		if (!Source.GetMipData(MipData, 0))
		{
			return false;
		}

		const ETextureSourceFormat SourceFormat = Source.GetFormat();
		switch (SourceFormat)
		{
		case TSF_BGRA8:
			return DecodeBGRA8Pixels(MipData, OutWidth, OutHeight, OutPixels);
		case TSF_RGBA8_DEPRECATED:
			return DecodeRGBA8Pixels(MipData, OutWidth, OutHeight, OutPixels);
		default:
			return false;
		}
	}

	bool ReadTexturePixelsFromPlatformData(UTexture2D& SourceTexture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;

		FTexturePlatformData* PlatformData = SourceTexture.GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() <= 0)
		{
			return false;
		}

		if (PlatformData->PixelFormat != PF_B8G8R8A8)
		{
			return false;
		}

		OutWidth = PlatformData->SizeX;
		OutHeight = PlatformData->SizeY;
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			return false;
		}

		FTexture2DMipMap& Mip0 = PlatformData->Mips[0];
		const uint8* MipData = static_cast<const uint8*>(Mip0.BulkData.LockReadOnly());
		if (!MipData)
		{
			return false;
		}

		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (Mip0.BulkData.GetBulkDataSize() < ExpectedBytes)
		{
			Mip0.BulkData.Unlock();
			return false;
		}

		OutPixels.SetNum(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), MipData, ExpectedBytes);
		Mip0.BulkData.Unlock();
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

	bool ResolveImagePixels(
		UTexture2D* SourceTexture,
		const FString& SourceImagePath,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		FString& OutPixelSource)
	{
		OutPixelSource.Reset();

		if (ReadTexturePixelsFromPngFile(SourceImagePath, OutPixels, OutWidth, OutHeight))
		{
			OutPixelSource = FString::Printf(TEXT("PNG:%s"), *FPaths::GetCleanFilename(SourceImagePath));
			return true;
		}

		if (SourceTexture)
		{
			if (ReadTexturePixelsFromSource(*SourceTexture, OutPixels, OutWidth, OutHeight))
			{
				OutPixelSource = TEXT("TextureSource");
				return true;
			}

			if (ReadTexturePixelsFromPlatformData(*SourceTexture, OutPixels, OutWidth, OutHeight))
			{
				OutPixelSource = TEXT("PlatformData");
				return true;
			}
		}

		OutPixelSource = TEXT("Failed");
		return false;
	}

	void SubsampleMaskedPoints(
		const TArray<FVector2D>& CandidatePoints,
		const int32 DesiredCount,
		TArray<FVector2D>& OutPoints)
	{
		OutPoints.Reset();
		if (CandidatePoints.Num() <= 0 || DesiredCount <= 0)
		{
			return;
		}

		if (CandidatePoints.Num() <= DesiredCount)
		{
			OutPoints = CandidatePoints;
			while (OutPoints.Num() < DesiredCount)
			{
				OutPoints.Add(CandidatePoints[OutPoints.Num() % CandidatePoints.Num()]);
			}
			return;
		}

		OutPoints.Reserve(DesiredCount);
		TArray<bool> Used;
		Used.Init(false, CandidatePoints.Num());

		int32 SeedIndex = FMath::RandRange(0, CandidatePoints.Num() - 1);
		OutPoints.Add(CandidatePoints[SeedIndex]);
		Used[SeedIndex] = true;

		while (OutPoints.Num() < DesiredCount)
		{
			float BestMinDistanceSq = -1.0f;
			int32 BestCandidateIndex = INDEX_NONE;

			for (int32 CandidateIndex = 0; CandidateIndex < CandidatePoints.Num(); ++CandidateIndex)
			{
				if (Used[CandidateIndex])
				{
					continue;
				}

				float MinDistanceSqToSet = TNumericLimits<float>::Max();
				for (const FVector2D& SelectedPoint : OutPoints)
				{
					MinDistanceSqToSet = FMath::Min(
						MinDistanceSqToSet,
						FVector2D::DistSquared(CandidatePoints[CandidateIndex], SelectedPoint));
				}

				if (BestCandidateIndex == INDEX_NONE || MinDistanceSqToSet > BestMinDistanceSq)
				{
					BestMinDistanceSq = MinDistanceSqToSet;
					BestCandidateIndex = CandidateIndex;
				}
			}

			if (BestCandidateIndex == INDEX_NONE)
			{
				break;
			}

			Used[BestCandidateIndex] = true;
			OutPoints.Add(CandidatePoints[BestCandidateIndex]);
		}

		while (OutPoints.Num() < DesiredCount)
		{
			OutPoints.Add(CandidatePoints[OutPoints.Num() % CandidatePoints.Num()]);
		}
	}

	float SampleGrayscale01(
		const TArray<FColor>& Pixels,
		const int32 Width,
		const int32 Height,
		const float U,
		const float V)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(U * static_cast<float>(Width - 1)), 0, Width - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(V * static_cast<float>(Height - 1)), 0, Height - 1);
		const FColor& Pixel = Pixels[Y * Width + X];
		return (0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B) / 255.0f;
	}

	float SampleAlpha01(
		const TArray<FColor>& Pixels,
		const int32 Width,
		const int32 Height,
		const float U,
		const float V)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(U * static_cast<float>(Width - 1)), 0, Width - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(V * static_cast<float>(Height - 1)), 0, Height - 1);
		return static_cast<float>(Pixels[Y * Width + X].A) / 255.0f;
	}

	void BuildDownsampledGrayscale(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const int32 TargetResolution,
		TArray<float>& OutGray,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutWidth = FMath::Clamp(TargetResolution, 32, 1024);
		OutHeight = OutWidth;
		OutGray.Reset();

		if (SourceWidth <= 0 || SourceHeight <= 0)
		{
			return;
		}

		const float Aspect = static_cast<float>(SourceWidth) / static_cast<float>(SourceHeight);
		if (Aspect >= 1.0f)
		{
			OutWidth = TargetResolution;
			OutHeight = FMath::Max(32, FMath::RoundToInt(static_cast<float>(TargetResolution) / Aspect));
		}
		else
		{
			OutHeight = TargetResolution;
			OutWidth = FMath::Max(32, FMath::RoundToInt(static_cast<float>(TargetResolution) * Aspect));
		}

		OutGray.SetNum(OutWidth * OutHeight);
		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			for (int32 X = 0; X < OutWidth; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(OutWidth);
				const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(OutHeight);
				OutGray[Y * OutWidth + X] = SampleGrayscale01(SourcePixels, SourceWidth, SourceHeight, U, V);
			}
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

				const float Magnitude = FMath::Sqrt(Gx * Gx + Gy * Gy);
				OutMagnitudes[Y * Width + X] = FMath::Clamp(Magnitude / 4.0f, 0.0f, 1.0f);
			}
		}
	}

	float ResolveAdaptiveEdgeThreshold(
		const TArray<float>& Magnitudes,
		const float RequestedThreshold,
		const int32 DesiredCandidateCount)
	{
		TArray<float> SortedMagnitudes;
		SortedMagnitudes.Reserve(Magnitudes.Num());
		for (const float Magnitude : Magnitudes)
		{
			if (Magnitude > KINDA_SMALL_NUMBER)
			{
				SortedMagnitudes.Add(Magnitude);
			}
		}

		if (SortedMagnitudes.Num() <= 0)
		{
			return RequestedThreshold;
		}

		SortedMagnitudes.Sort();
		const int32 TargetIndex = FMath::Clamp(
			SortedMagnitudes.Num() - DesiredCandidateCount,
			0,
			SortedMagnitudes.Num() - 1);
		const float AdaptiveThreshold = SortedMagnitudes[TargetIndex];
		return FMath::Min(RequestedThreshold, AdaptiveThreshold);
	}

	void SubsampleWeightedPoints(
		const TArray<FVector2D>& CandidatePoints,
		const TArray<float>& CandidateWeights,
		const int32 DesiredCount,
		TArray<FVector2D>& OutPoints)
	{
		OutPoints.Reset();
		if (CandidatePoints.Num() <= 0 || DesiredCount <= 0)
		{
			return;
		}

		if (CandidatePoints.Num() != CandidateWeights.Num())
		{
			SubsampleMaskedPoints(CandidatePoints, DesiredCount, OutPoints);
			return;
		}

		TArray<int32> Order;
		Order.Reserve(CandidatePoints.Num());
		for (int32 Index = 0; Index < CandidatePoints.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&CandidateWeights](const int32 A, const int32 B)
		{
			return CandidateWeights[A] > CandidateWeights[B];
		});

		TArray<FVector2D> WeightedPool;
		WeightedPool.Reserve(FMath::Min(CandidatePoints.Num(), DesiredCount * 4));
		for (const int32 Index : Order)
		{
			WeightedPool.Add(CandidatePoints[Index]);
			if (WeightedPool.Num() >= DesiredCount * 4)
			{
				break;
			}
		}

		SubsampleMaskedPoints(WeightedPool, DesiredCount, OutPoints);
	}

	void NormalizedPointsToLocalCm(
		const TArray<FVector2D>& NormalizedPoints,
		TArray<FVector>& OutLocalPointsCm)
	{
		OutLocalPointsCm.Reserve(NormalizedPoints.Num());
		for (const FVector2D& NormalizedPoint : NormalizedPoints)
		{
			OutLocalPointsCm.Add(FVector(NormalizedPoint.X - 0.5f, 0.5f - NormalizedPoint.Y, 0.0f));
		}
	}

	bool ExtractFilledSilhouettePoints(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount,
		TArray<FVector2D>& OutNormalizedPoints,
		int32& OutMaskWidth,
		int32& OutMaskHeight,
		int32& OutCandidateCount,
		FString& OutDebugInfo)
	{
		TArray<FColor> MaskPixels;
		DownsampleTextureMask(
			SourcePixels,
			SourceWidth,
			SourceHeight,
			ShapeDefinition.SampleResolution,
			ShapeDefinition.bUseLuminance,
			MaskPixels,
			OutMaskWidth,
			OutMaskHeight);

		TArray<FVector2D> CandidateNormalizedPoints;
		CandidateNormalizedPoints.Reserve(OutMaskWidth * OutMaskHeight / 4);

		for (int32 Y = 0; Y < OutMaskHeight; ++Y)
		{
			for (int32 X = 0; X < OutMaskWidth; ++X)
			{
				const FColor& Pixel = MaskPixels[Y * OutMaskWidth + X];
				const float MaskAlpha = static_cast<float>(Pixel.A) / 255.0f;
				if (MaskAlpha < ShapeDefinition.AlphaThreshold)
				{
					continue;
				}

				CandidateNormalizedPoints.Add(FVector2D(
					(static_cast<float>(X) + 0.5f) / static_cast<float>(OutMaskWidth),
					(static_cast<float>(Y) + 0.5f) / static_cast<float>(OutMaskHeight)));
			}
		}

		OutCandidateCount = CandidateNormalizedPoints.Num();
		if (OutCandidateCount <= 0)
		{
			OutDebugInfo = TEXT("No filled silhouette pixels passed threshold.");
			return false;
		}

		SubsampleMaskedPoints(CandidateNormalizedPoints, DesiredPointCount, OutNormalizedPoints);
		return OutNormalizedPoints.Num() > 0;
	}

	bool ExtractSobelEdgePoints(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount,
		TArray<FVector2D>& OutNormalizedPoints,
		int32& OutMaskWidth,
		int32& OutMaskHeight,
		int32& OutCandidateCount,
		float& OutAppliedThreshold,
		FString& OutDebugInfo)
	{
		TArray<float> Gray;
		BuildDownsampledGrayscale(
			SourcePixels,
			SourceWidth,
			SourceHeight,
			ShapeDefinition.SampleResolution,
			Gray,
			OutMaskWidth,
			OutMaskHeight);

		if (Gray.Num() <= 0)
		{
			OutDebugInfo = TEXT("Failed to build grayscale field.");
			return false;
		}

		TArray<float> SobelMagnitudes;
		ComputeSobelMagnitudes(Gray, OutMaskWidth, OutMaskHeight, SobelMagnitudes);

		const int32 DesiredCandidates = FMath::Max(DesiredPointCount * 3, 128);
		OutAppliedThreshold = ResolveAdaptiveEdgeThreshold(
			SobelMagnitudes,
			ShapeDefinition.EdgeThreshold,
			DesiredCandidates);

		TArray<FVector2D> CandidateNormalizedPoints;
		TArray<float> CandidateWeights;
		CandidateNormalizedPoints.Reserve(DesiredCandidates);
		CandidateWeights.Reserve(DesiredCandidates);

		for (int32 Y = 1; Y < OutMaskHeight - 1; ++Y)
		{
			for (int32 X = 1; X < OutMaskWidth - 1; ++X)
			{
				const float EdgeMagnitude = SobelMagnitudes[Y * OutMaskWidth + X];
				if (EdgeMagnitude < OutAppliedThreshold)
				{
					continue;
				}

				CandidateNormalizedPoints.Add(FVector2D(
					(static_cast<float>(X) + 0.5f) / static_cast<float>(OutMaskWidth),
					(static_cast<float>(Y) + 0.5f) / static_cast<float>(OutMaskHeight)));
				CandidateWeights.Add(EdgeMagnitude);
			}
		}

		OutCandidateCount = CandidateNormalizedPoints.Num();
		if (OutCandidateCount <= 0)
		{
			OutDebugInfo = FString::Printf(
				TEXT("No Sobel edge pixels passed threshold %.3f."),
				OutAppliedThreshold);
			return false;
		}

		SubsampleWeightedPoints(CandidateNormalizedPoints, CandidateWeights, DesiredPointCount, OutNormalizedPoints);
		return OutNormalizedPoints.Num() > 0;
	}
}

FString FMetaAgentParticleShapeDefinition::GetShapeDisplayName() const
{
	switch (ShapeType)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette:
		return TEXT("ImageSilhouette");
	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		return TEXT("SquareGrid");
	}
}

FString FMetaAgentParticleShapeDefinition::GetImageSamplingDisplayName() const
{
	switch (ImageSamplingMode)
	{
	case EMetaAgentParticleImageSamplingMode::FilledSilhouette:
		return TEXT("FilledSilhouette");
	case EMetaAgentParticleImageSamplingMode::SobelEdges:
	default:
		return TEXT("SobelEdges");
	}
}

void FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache()
{
	GImageMaskLocalPointCache.Reset();
}

FMetaAgentParticleShapeBuildResult FMetaAgentParticleShapeBuilder::BuildPatternTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext)
{
	FMetaAgentParticleShapeBuildResult Result;
	Result.ResolvedShape = PatternConfig.Shape.ShapeType;

	switch (PatternConfig.Shape.ShapeType)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette:
		if (BuildImageSilhouetteTargets(PatternConfig, ShapeContext, Result))
		{
			return Result;
		}

		{
			const FString FailedReason = Result.DebugInfo;
			UE_LOG(LogMetaAgent, Warning,
				TEXT("ParticleShapeBuilder: image silhouette failed (%s), falling back to square grid."),
				*FailedReason);
			Result.ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;
			BuildSquareGridTargets(PatternConfig, ShapeContext, Result);
			Result.DebugInfo = FString::Printf(TEXT("Fallback SquareGrid | %s"), *FailedReason);
		}
		return Result;

	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		BuildSquareGridTargets(PatternConfig, ShapeContext, Result);
		return Result;
	}
}

bool FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	OutResult.PatternWorldTargets.Reset();

	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles.");
		return false;
	}

	const int32 PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	const int32 RowCount = FMath::DivideAndRoundUp(ParticleCount, PatternColumns);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	const float GridSpacingCm = FMath::Max(1.0f, PatternConfig.GridSpacingCm);
	const FVector GridOrigin = Centroid - FVector(
		(PatternColumns - 1) * GridSpacingCm * 0.5f,
		(RowCount - 1) * GridSpacingCm * 0.5f,
		0.0f);

	OutResult.PatternWorldTargets.SetNum(ParticleCount);
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		const int32 Column = ParticleIndex % PatternColumns;
		const int32 Row = ParticleIndex / PatternColumns;
		OutResult.PatternWorldTargets[ParticleIndex] = GridOrigin + FVector(
			Column * GridSpacingCm,
			Row * GridSpacingCm,
			0.0f);
	}

	OutResult.ShapeFrame = FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
		Baseline,
		PatternColumns * GridSpacingCm,
		RowCount * GridSpacingCm,
		PatternConfig.Shape.ZOffsetCm);
	OutResult.ShapeFrame.Origin = Centroid;
	OutResult.PatternCenter = Centroid;
	OutResult.PatternColumns = PatternColumns;
	OutResult.ShapePointCount = ParticleCount;
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("SquareGrid columns=%d spacing=%.1f"),
		PatternColumns,
		GridSpacingCm);

	return true;
}

FMetaAgentParticleShapeFrame FMetaAgentParticleShapeBuilder::ResolveShapeFrame(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	const UTexture2D* SourceTexture)
{
	const FMetaAgentParticleShapeDefinition& ShapeDef = PatternConfig.Shape;

	if (ShapeDef.bAlignToPreviewPlane && ShapeContext.PreviewPlaneMesh)
	{
		FMetaAgentParticleShapeFrame Frame = FMetaAgentImagePreviewRuntime::BuildShapeFrameFromPreviewPlane(
			ShapeContext.PreviewPlaneMesh,
			ShapeDef.ZOffsetCm);
		return Frame;
	}

	float ShapeHeightCm = ShapeDef.ShapeHeightCm;
	if (ShapeHeightCm <= 0.0f && SourceTexture)
	{
		const int32 TexWidth = SourceTexture->GetSizeX();
		const int32 TexHeight = SourceTexture->GetSizeY();
		if (TexWidth > 0 && TexHeight > 0)
		{
			ShapeHeightCm = ShapeDef.ShapeWidthCm * (static_cast<float>(TexHeight) / static_cast<float>(TexWidth));
		}
	}
	if (ShapeHeightCm <= 0.0f)
	{
		ShapeHeightCm = ShapeDef.ShapeWidthCm;
	}

	return FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
		ShapeContext.BaselineWorldPositions,
		ShapeDef.ShapeWidthCm,
		ShapeHeightCm,
		ShapeDef.ZOffsetCm);
}

bool FMetaAgentParticleShapeBuilder::ExtractSilhouetteLocalPoints(
	UTexture2D* SourceTexture,
	const FString& SourceImagePath,
	const FMetaAgentParticleShapeDefinition& ShapeDefinition,
	const int32 DesiredPointCount,
	TArray<FVector>& OutLocalPointsCm,
	FString& OutDebugInfo)
{
	OutLocalPointsCm.Reset();

	FImageMaskCacheKey CacheKey;
	CacheKey.Texture = SourceTexture;
	CacheKey.SourceImagePath = SourceImagePath;
	CacheKey.ImageSamplingMode = ShapeDefinition.ImageSamplingMode;
	CacheKey.AlphaThreshold = ShapeDefinition.AlphaThreshold;
	CacheKey.EdgeThreshold = ShapeDefinition.EdgeThreshold;
	CacheKey.bUseLuminance = ShapeDefinition.bUseLuminance;
	CacheKey.SampleResolution = ShapeDefinition.SampleResolution;

	if (TArray<FVector>* CachedPoints = GImageMaskLocalPointCache.Find(CacheKey))
	{
		OutLocalPointsCm = *CachedPoints;
		OutDebugInfo = FString::Printf(
			TEXT("%s cache hit points=%d"),
			*ShapeDefinition.GetImageSamplingDisplayName(),
			OutLocalPointsCm.Num());
		return OutLocalPointsCm.Num() > 0;
	}

	TArray<FColor> SourcePixels;
	int32 SourceWidth = 0;
	int32 SourceHeight = 0;
	FString PixelSource;
	if (!ResolveImagePixels(SourceTexture, SourceImagePath, SourcePixels, SourceWidth, SourceHeight, PixelSource))
	{
		OutDebugInfo = FString::Printf(
			TEXT("Failed to read image pixels (path='%s', texture=%s)."),
			SourceImagePath.IsEmpty() ? TEXT("<none>") : *FPaths::GetCleanFilename(SourceImagePath),
			SourceTexture ? TEXT("yes") : TEXT("no"));
		return false;
	}

	TArray<FVector2D> SelectedNormalizedPoints;
	int32 MaskWidth = 0;
	int32 MaskHeight = 0;
	int32 CandidateCount = 0;
	float AppliedEdgeThreshold = ShapeDefinition.EdgeThreshold;
	bool bExtracted = false;

	if (ShapeDefinition.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::FilledSilhouette)
	{
		bExtracted = ExtractFilledSilhouettePoints(
			SourcePixels,
			SourceWidth,
			SourceHeight,
			ShapeDefinition,
			DesiredPointCount,
			SelectedNormalizedPoints,
			MaskWidth,
			MaskHeight,
			CandidateCount,
			OutDebugInfo);
	}
	else
	{
		bExtracted = ExtractSobelEdgePoints(
			SourcePixels,
			SourceWidth,
			SourceHeight,
			ShapeDefinition,
			DesiredPointCount,
			SelectedNormalizedPoints,
			MaskWidth,
			MaskHeight,
			CandidateCount,
			AppliedEdgeThreshold,
			OutDebugInfo);
	}

	if (!bExtracted)
	{
		return false;
	}

	NormalizedPointsToLocalCm(SelectedNormalizedPoints, OutLocalPointsCm);

	GImageMaskLocalPointCache.Add(CacheKey, OutLocalPointsCm);

	if (ShapeDefinition.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::SobelEdges)
	{
		OutDebugInfo = FString::Printf(
			TEXT("SobelEdges pixels=%s src=%dx%d mask=%dx%d edgeCandidates=%d points=%d edgeThreshold=%.3f"),
			*PixelSource,
			SourceWidth,
			SourceHeight,
			MaskWidth,
			MaskHeight,
			CandidateCount,
			OutLocalPointsCm.Num(),
			AppliedEdgeThreshold);
	}
	else
	{
		OutDebugInfo = FString::Printf(
			TEXT("FilledSilhouette pixels=%s src=%dx%d mask=%dx%d candidates=%d points=%d alphaThreshold=%.2f"),
			*PixelSource,
			SourceWidth,
			SourceHeight,
			MaskWidth,
			MaskHeight,
			CandidateCount,
			OutLocalPointsCm.Num(),
			ShapeDefinition.AlphaThreshold);
	}

	return OutLocalPointsCm.Num() > 0;
}

FVector FMetaAgentParticleShapeBuilder::LocalPointToWorld(
	const FVector& LocalPointCm,
	const FMetaAgentParticleShapeFrame& ShapeFrame)
{
	const FVector ScaledLocal(
		LocalPointCm.X * ShapeFrame.ExtentsCm.X,
		LocalPointCm.Y * ShapeFrame.ExtentsCm.Y,
		ShapeFrame.ZOffsetCm);

	const FVector RotatedLocal = ShapeFrame.Orientation.RotateVector(ScaledLocal);
	return ShapeFrame.Origin + RotatedLocal;
}

void FMetaAgentParticleShapeBuilder::AssignParticlesToShapePoints(
	const TArray<FVector>& BaselineWorldPositions,
	const TArray<FVector>& LocalShapePointsCm,
	const FMetaAgentParticleShapeFrame& ShapeFrame,
	const EMetaAgentParticleShapeAssignmentMode AssignmentMode,
	TArray<FVector>& OutWorldTargets)
{
	const int32 ParticleCount = BaselineWorldPositions.Num();
	OutWorldTargets.Reset();
	if (ParticleCount <= 0 || LocalShapePointsCm.Num() <= 0)
	{
		return;
	}

	TArray<FVector> ShapeWorldPoints;
	ShapeWorldPoints.Reserve(LocalShapePointsCm.Num());
	for (const FVector& LocalPoint : LocalShapePointsCm)
	{
		ShapeWorldPoints.Add(LocalPointToWorld(LocalPoint, ShapeFrame));
	}

	OutWorldTargets.SetNum(ParticleCount);
	TArray<bool> UsedShapePoints;
	UsedShapePoints.Init(false, ShapeWorldPoints.Num());

	if (AssignmentMode == EMetaAgentParticleShapeAssignmentMode::Ordered)
	{
		for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
		{
			const int32 ShapeIndex = ParticleIndex % ShapeWorldPoints.Num();
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[ShapeIndex];
		}
		return;
	}

	TArray<int32> ParticleOrder;
	ParticleOrder.Reserve(ParticleCount);
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		ParticleOrder.Add(ParticleIndex);
	}
	ParticleOrder.Sort([&BaselineWorldPositions](const int32 A, const int32 B)
	{
		const FVector PosA = BaselineWorldPositions[A];
		const FVector PosB = BaselineWorldPositions[B];
		if (!FMath::IsNearlyEqual(PosA.Y, PosB.Y, 0.01f))
		{
			return PosA.Y < PosB.Y;
		}
		return PosA.X < PosB.X;
	});

	for (const int32 ParticleIndex : ParticleOrder)
	{
		const FVector& BaselinePosition = BaselineWorldPositions[ParticleIndex];

		float BestDistanceSq = TNumericLimits<float>::Max();
		int32 BestShapeIndex = 0;
		for (int32 ShapeIndex = 0; ShapeIndex < ShapeWorldPoints.Num(); ++ShapeIndex)
		{
			if (UsedShapePoints.IsValidIndex(ShapeIndex) && UsedShapePoints[ShapeIndex])
			{
				continue;
			}

			const float DistanceSq = FVector::DistSquared(BaselinePosition, ShapeWorldPoints[ShapeIndex]);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestShapeIndex = ShapeIndex;
			}
		}

		if (ShapeWorldPoints.IsValidIndex(BestShapeIndex))
		{
			UsedShapePoints[BestShapeIndex] = true;
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[BestShapeIndex];
		}
		else
		{
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[ParticleIndex % ShapeWorldPoints.Num()];
		}
	}
}

bool FMetaAgentParticleShapeBuilder::BuildImageSilhouetteTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles.");
		return false;
	}

	UTexture2D* SourceTexture = ShapeContext.SourceTexture;
	if (!SourceTexture && ShapeContext.SourceImagePath.IsEmpty())
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No source texture or image path for image silhouette.");
		return false;
	}

	FMetaAgentParticleShapeFrame ShapeFrame = ResolveShapeFrame(PatternConfig, ShapeContext, SourceTexture);

	TArray<FVector> LocalShapePointsCm;
	FString ExtractionDebug;
	if (!ExtractSilhouetteLocalPoints(
		SourceTexture,
		ShapeContext.SourceImagePath,
		PatternConfig.Shape,
		ParticleCount,
		LocalShapePointsCm,
		ExtractionDebug))
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = ExtractionDebug;
		return false;
	}

	AssignParticlesToShapePoints(
		Baseline,
		LocalShapePointsCm,
		ShapeFrame,
		PatternConfig.Shape.AssignmentMode,
		OutResult.PatternWorldTargets);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	OutResult.ShapeFrame = ShapeFrame;
	OutResult.PatternCenter = ShapeFrame.Origin;
	OutResult.PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	OutResult.ShapePointCount = LocalShapePointsCm.Num();
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::ImageSilhouette;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("%s | frame=%.0fx%.0fcm plane=%s"),
		*ExtractionDebug,
		ShapeFrame.ExtentsCm.X,
		ShapeFrame.ExtentsCm.Y,
		ShapeContext.PreviewPlaneMesh ? TEXT("yes") : TEXT("no"));

	return true;
}
