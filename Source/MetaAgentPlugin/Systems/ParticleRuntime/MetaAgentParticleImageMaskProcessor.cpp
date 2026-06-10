// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"

#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace MetaAgentImageMask
{
	namespace
	{
		FVector2D ApplyCellJitter(const FVector2D& NormalizedPoint, const int32 GridSize, const float JitterNormalized);
		void FillPointsWithWeightedScatter(
			const TArray<float>& Weights,
			const int32 MaskWidth,
			const int32 MaskHeight,
			const int32 DesiredCount,
			const float JitterNormalized,
			const int32 GridSize,
			TArray<FVector2D>& InOutPoints);

		int32 ResolveEffectiveSampleResolution(
			const int32 SourceWidth,
			const int32 SourceHeight,
			const int32 RequestedResolution)
		{
			const int32 ClampedRequest = FMath::Clamp(RequestedResolution, 32, 4096);
			if (SourceWidth <= 0 || SourceHeight <= 0)
			{
				return ClampedRequest;
			}

			const int32 MaxSourceDim = FMath::Max(SourceWidth, SourceHeight);
			return FMath::Min(ClampedRequest, MaxSourceDim);
		}

		void ResolveMaskDimensions(
			const int32 SourceWidth,
			const int32 SourceHeight,
			const int32 TargetResolution,
			int32& OutWidth,
			int32& OutHeight)
		{
			OutWidth = FMath::Clamp(TargetResolution, 32, 4096);
			OutHeight = OutWidth;

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

		void BuildDownsampledGrayscale(
			const TArray<FColor>& SourcePixels,
			const int32 SourceWidth,
			const int32 SourceHeight,
			const int32 TargetResolution,
			TArray<float>& OutGray,
			int32& OutWidth,
			int32& OutHeight)
		{
			const int32 EffectiveResolution = ResolveEffectiveSampleResolution(SourceWidth, SourceHeight, TargetResolution);
			ResolveMaskDimensions(SourceWidth, SourceHeight, EffectiveResolution, OutWidth, OutHeight);
			OutGray.Reset();

			if (SourceWidth <= 0 || SourceHeight <= 0 || OutWidth <= 0 || OutHeight <= 0)
			{
				return;
			}

			if (OutWidth == SourceWidth && OutHeight == SourceHeight)
			{
				OutGray.SetNumUninitialized(SourceWidth * SourceHeight);
				for (int32 Index = 0; Index < SourcePixels.Num(); ++Index)
				{
					const FColor& Pixel = SourcePixels[Index];
					OutGray[Index] = (0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B) / 255.0f;
				}
				return;
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
			const int32 EffectiveResolution = ResolveEffectiveSampleResolution(SourceWidth, SourceHeight, TargetResolution);
			ResolveMaskDimensions(SourceWidth, SourceHeight, EffectiveResolution, OutWidth, OutHeight);

			if (SourceWidth <= 0 || SourceHeight <= 0 || OutWidth <= 0 || OutHeight <= 0)
			{
				OutPixels.Reset();
				return;
			}

			if (OutWidth == SourceWidth && OutHeight == SourceHeight)
			{
				OutPixels = SourcePixels;
				return;
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
			TArray<int32> Histogram;
			Histogram.SetNumZeroed(256);

			int32 NonZeroCount = 0;
			for (const float Magnitude : Magnitudes)
			{
				if (Magnitude <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 Bin = FMath::Clamp(FMath::FloorToInt(Magnitude * 255.0f), 0, 255);
				Histogram[Bin]++;
				NonZeroCount++;
			}

			if (NonZeroCount <= 0)
			{
				return RequestedThreshold;
			}

			const int32 TargetKeep = FMath::Clamp(DesiredCandidateCount, 1, NonZeroCount);
			int32 Accumulated = 0;
			float AdaptiveThreshold = 0.0f;
			for (int32 Bin = 255; Bin >= 0; --Bin)
			{
				Accumulated += Histogram[Bin];
				if (Accumulated >= TargetKeep)
				{
					AdaptiveThreshold = static_cast<float>(Bin) / 255.0f;
					break;
				}
			}

			return FMath::Min(RequestedThreshold, FMath::Max(AdaptiveThreshold, KINDA_SMALL_NUMBER));
		}

		void SubsampleMaskedPointsFast(
			const TArray<FVector2D>& CandidatePoints,
			const TArray<float>* CandidateWeights,
			const int32 DesiredCount,
			TArray<FVector2D>& OutPoints,
			const float DensityGridScale = 1.0f,
			const float JitterNormalized = 0.0f)
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
			}
			else
			{
			const int32 GridSize = FMath::Max(
				1,
				FMath::CeilToInt(FMath::Sqrt(static_cast<float>(DesiredCount) * FMath::Max(DensityGridScale, 1.0f))));
			const int32 CellCount = GridSize * GridSize;
			TArray<int32> BestCandidatePerCell;
			TArray<float> BestWeightPerCell;
			BestCandidatePerCell.Init(INDEX_NONE, CellCount);
			BestWeightPerCell.Init(-1.0f, CellCount);

			for (int32 CandidateIndex = 0; CandidateIndex < CandidatePoints.Num(); ++CandidateIndex)
			{
				const FVector2D& Point = CandidatePoints[CandidateIndex];
				const int32 CellX = FMath::Clamp(FMath::FloorToInt(Point.X * GridSize), 0, GridSize - 1);
				const int32 CellY = FMath::Clamp(FMath::FloorToInt(Point.Y * GridSize), 0, GridSize - 1);
				const int32 CellIndex = CellY * GridSize + CellX;

				const float Weight = CandidateWeights ? (*CandidateWeights)[CandidateIndex] : 1.0f;
				if (Weight > BestWeightPerCell[CellIndex])
				{
					BestWeightPerCell[CellIndex] = Weight;
					BestCandidatePerCell[CellIndex] = CandidateIndex;
				}
			}

			TArray<FVector2D> GridPoints;
			GridPoints.Reserve(CellCount);
			for (const int32 CandidateIndex : BestCandidatePerCell)
			{
				if (CandidateIndex != INDEX_NONE)
				{
					GridPoints.Add(CandidatePoints[CandidateIndex]);
				}
			}

			if (GridPoints.Num() <= 0)
			{
				return;
			}

			if (GridPoints.Num() >= DesiredCount)
			{
				const float Stride = static_cast<float>(GridPoints.Num()) / static_cast<float>(DesiredCount);
				for (int32 Index = 0; Index < DesiredCount; ++Index)
				{
					const int32 PickIndex = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Index) + 0.5f) * Stride), 0, GridPoints.Num() - 1);
					OutPoints.Add(GridPoints[PickIndex]);
				}
			}
			else
			{
				OutPoints = GridPoints;
				while (OutPoints.Num() < DesiredCount)
				{
					OutPoints.Add(GridPoints[OutPoints.Num() % GridPoints.Num()]);
				}
			}
			}

			if (JitterNormalized > KINDA_SMALL_NUMBER && OutPoints.Num() > 0)
			{
				const int32 JitterGridSize = FMath::Max(
					1,
					FMath::CeilToInt(FMath::Sqrt(static_cast<float>(DesiredCount) * FMath::Max(DensityGridScale, 1.0f))));
				for (FVector2D& Point : OutPoints)
				{
					Point = ApplyCellJitter(Point, JitterGridSize, JitterNormalized);
				}
			}
		}

		bool ScatterStratifiedFromMaskWeights(
			const TArray<float>& Weights,
			const int32 MaskWidth,
			const int32 MaskHeight,
			const FMetaAgentImageMaskBuildParams& Params,
			TArray<FVector2D>& OutNormalizedPoints,
			int32& OutStratGridSize)
		{
			OutNormalizedPoints.Reset();
			OutStratGridSize = 1;

			if (Weights.Num() <= 0 || MaskWidth <= 0 || MaskHeight <= 0)
			{
				return false;
			}

			struct FDensityCell
			{
				float WeightSum = 0.0f;
				FVector2D WeightedUvSum = FVector2D::ZeroVector;
			};

			const int32 GridSize = FMath::Max(
				1,
				FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Params.DesiredPointCount) * Params.DensityGridScale)));
			OutStratGridSize = GridSize;
			TArray<FDensityCell> Cells;
			Cells.SetNum(GridSize * GridSize);

			for (int32 Y = 0; Y < MaskHeight; ++Y)
			{
				for (int32 X = 0; X < MaskWidth; ++X)
				{
					const int32 Index = Y * MaskWidth + X;
					const float Weight = Weights[Index];
					if (Weight <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(MaskWidth);
					const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(MaskHeight);
					const int32 CellX = FMath::Clamp(FMath::FloorToInt(U * GridSize), 0, GridSize - 1);
					const int32 CellY = FMath::Clamp(FMath::FloorToInt(V * GridSize), 0, GridSize - 1);
					const int32 CellIndex = CellY * GridSize + CellX;

					FDensityCell& Cell = Cells[CellIndex];
					Cell.WeightSum += Weight;
					Cell.WeightedUvSum += FVector2D(U, V) * Weight;
				}
			}

			struct FWeightedCell
			{
				int32 CellIndex = INDEX_NONE;
				float WeightSum = 0.0f;
				FVector2D Uv = FVector2D::ZeroVector;
			};

			TArray<FWeightedCell> ActiveCells;
			ActiveCells.Reserve(Cells.Num());
			for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
			{
				const FDensityCell& Cell = Cells[CellIndex];
				if (Cell.WeightSum <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				FWeightedCell Entry;
				Entry.CellIndex = CellIndex;
				Entry.WeightSum = Cell.WeightSum;
				Entry.Uv = Cell.WeightedUvSum / Cell.WeightSum;
				ActiveCells.Add(Entry);
			}

			OutNormalizedPoints.Reserve(Params.DesiredPointCount);

			TArray<FWeightedCell> RemainingCells = ActiveCells;
			const int32 PrimaryCount = FMath::Min(Params.DesiredPointCount, RemainingCells.Num());
			FRandomStream PrimaryStream(Params.DesiredPointCount * 131 + GridSize * 17 + MaskWidth);
			for (int32 Index = 0; Index < PrimaryCount && RemainingCells.Num() > 0; ++Index)
			{
				float TotalCellWeight = 0.0f;
				for (const FWeightedCell& Cell : RemainingCells)
				{
					TotalCellWeight += Cell.WeightSum;
				}

				int32 ChosenIndex = RemainingCells.Num() - 1;
				if (TotalCellWeight > KINDA_SMALL_NUMBER)
				{
					float Pick = PrimaryStream.FRand() * TotalCellWeight;
					for (int32 CellIndex = 0; CellIndex < RemainingCells.Num(); ++CellIndex)
					{
						Pick -= RemainingCells[CellIndex].WeightSum;
						if (Pick <= 0.0f)
						{
							ChosenIndex = CellIndex;
							break;
						}
					}
				}

				const FVector2D Jittered = ApplyCellJitter(
					RemainingCells[ChosenIndex].Uv,
					GridSize,
					Params.TargetJitterNormalized);
				OutNormalizedPoints.Add(Jittered);
				RemainingCells.RemoveAt(ChosenIndex);
			}

			FillPointsWithWeightedScatter(
				Weights,
				MaskWidth,
				MaskHeight,
				Params.DesiredPointCount,
				Params.TargetJitterNormalized,
				GridSize,
				OutNormalizedPoints);

			return OutNormalizedPoints.Num() > 0;
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

		FVector2D ApplyCellJitter(
			const FVector2D& NormalizedPoint,
			const int32 GridSize,
			const float JitterNormalized)
		{
			if (JitterNormalized <= KINDA_SMALL_NUMBER || GridSize <= 0)
			{
				return NormalizedPoint;
			}

			const float CellSize = 1.0f / static_cast<float>(GridSize);
			const float JitterRange = CellSize * JitterNormalized;
			const FVector2D Jitter(
				(FRandomStream(HashCombine(::GetTypeHash(NormalizedPoint.X), 17)).FRand() - 0.5f) * JitterRange,
				(FRandomStream(HashCombine(::GetTypeHash(NormalizedPoint.Y), 31)).FRand() - 0.5f) * JitterRange);

			return FVector2D(
				FMath::Clamp(NormalizedPoint.X + Jitter.X, 0.0f, 1.0f),
				FMath::Clamp(NormalizedPoint.Y + Jitter.Y, 0.0f, 1.0f));
		}

		bool IsSeparatedFromSet(
			const FVector2D& Candidate,
			const TArray<FVector2D>& ExistingPoints,
			const float MinSeparationSq)
		{
			for (const FVector2D& Existing : ExistingPoints)
			{
				if (FVector2D::DistSquared(Candidate, Existing) < MinSeparationSq)
				{
					return false;
				}
			}
			return true;
		}

		void FillPointsWithWeightedScatter(
			const TArray<float>& Weights,
			const int32 MaskWidth,
			const int32 MaskHeight,
			const int32 DesiredCount,
			const float JitterNormalized,
			const int32 GridSize,
			TArray<FVector2D>& InOutPoints)
		{
			if (InOutPoints.Num() >= DesiredCount)
			{
				InOutPoints.SetNum(DesiredCount);
				return;
			}

			TArray<int32> WeightedIndices;
			TArray<float> WeightedValues;
			float TotalWeight = 0.0f;
			for (int32 Index = 0; Index < Weights.Num(); ++Index)
			{
				if (Weights[Index] <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				WeightedIndices.Add(Index);
				WeightedValues.Add(Weights[Index]);
				TotalWeight += Weights[Index];
			}

			if (WeightedIndices.Num() <= 0 || TotalWeight <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			const float MinSeparation = 0.5f / static_cast<float>(FMath::Max(MaskWidth, MaskHeight));
			const float MinSeparationSq = MinSeparation * MinSeparation;
			FRandomStream RandomStream(static_cast<int32>(DesiredCount * 977 + MaskWidth * 13 + MaskHeight));

			int32 SafetyCounter = 0;
			while (InOutPoints.Num() < DesiredCount && SafetyCounter < DesiredCount * 64)
			{
				SafetyCounter++;
				float Pick = RandomStream.FRand() * TotalWeight;
				int32 ChosenIndex = WeightedIndices.Last();
				for (int32 WeightIndex = 0; WeightIndex < WeightedIndices.Num(); ++WeightIndex)
				{
					Pick -= WeightedValues[WeightIndex];
					if (Pick <= 0.0f)
					{
						ChosenIndex = WeightedIndices[WeightIndex];
						break;
					}
				}

				const int32 X = ChosenIndex % MaskWidth;
				const int32 Y = ChosenIndex / MaskWidth;
				FVector2D Candidate(
					(static_cast<float>(X) + RandomStream.FRand()) / static_cast<float>(MaskWidth),
					(static_cast<float>(Y) + RandomStream.FRand()) / static_cast<float>(MaskHeight));
				Candidate = ApplyCellJitter(Candidate, GridSize, JitterNormalized);

				if (!IsSeparatedFromSet(Candidate, InOutPoints, MinSeparationSq))
				{
					continue;
				}

				InOutPoints.Add(Candidate);
			}

			while (InOutPoints.Num() < DesiredCount && WeightedIndices.Num() > 0)
			{
				const int32 Index = WeightedIndices[InOutPoints.Num() % WeightedIndices.Num()];
				const int32 X = Index % MaskWidth;
				const int32 Y = Index / MaskWidth;
				FVector2D Candidate(
					(static_cast<float>(X) + 0.5f) / static_cast<float>(MaskWidth),
					(static_cast<float>(Y) + 0.5f) / static_cast<float>(MaskHeight));
				Candidate = ApplyCellJitter(
					Candidate,
					GridSize,
					FMath::Max(JitterNormalized, 0.5f));
				InOutPoints.Add(Candidate);
			}
		}

		bool ExtractGrayscaleDensityPoints(
			const TArray<FColor>& SourcePixels,
			const int32 SourceWidth,
			const int32 SourceHeight,
			const FMetaAgentImageMaskBuildParams& Params,
			TArray<FVector2D>& OutNormalizedPoints,
			int32& OutMaskWidth,
			int32& OutMaskHeight,
			int32& OutCandidateCount,
			FString& OutDebugInfo)
		{
			TArray<float> Gray;
			BuildDownsampledGrayscale(
				SourcePixels,
				SourceWidth,
				SourceHeight,
				Params.SampleResolution,
				Gray,
				OutMaskWidth,
				OutMaskHeight);

			if (Gray.Num() <= 0 || OutMaskWidth <= 0 || OutMaskHeight <= 0)
			{
				OutDebugInfo = TEXT("Failed to build grayscale density field.");
				return false;
			}

			TArray<float> Weights;
			Weights.SetNum(Gray.Num());
			OutCandidateCount = 0;

			for (int32 Y = 0; Y < OutMaskHeight; ++Y)
			{
				for (int32 X = 0; X < OutMaskWidth; ++X)
				{
					const int32 Index = Y * OutMaskWidth + X;
					const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(OutMaskWidth);
					const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(OutMaskHeight);
					const float Luminance = Gray[Index];
					const float Alpha = SampleAlpha01(SourcePixels, SourceWidth, SourceHeight, U, V);

					float Concentration = Params.bUseLuminance
						? (1.0f - Luminance) * Alpha
						: Alpha;
					Concentration = FMath::Pow(FMath::Max(Concentration, 0.0f), Params.GrayscaleGamma);

					if (Concentration < Params.AlphaThreshold)
					{
						Weights[Index] = 0.0f;
						continue;
					}

					Weights[Index] = Concentration;
					OutCandidateCount++;
				}
			}

			if (OutCandidateCount <= 0)
			{
				OutDebugInfo = FString::Printf(
					TEXT("No grayscale density pixels passed threshold %.2f."),
					Params.AlphaThreshold);
				return false;
			}

			int32 StratGridSize = 1;
			if (!ScatterStratifiedFromMaskWeights(
				Weights,
				OutMaskWidth,
				OutMaskHeight,
				Params,
				OutNormalizedPoints,
				StratGridSize))
			{
				OutDebugInfo = TEXT("Grayscale density produced no scatter points.");
				return false;
			}

			return true;
		}

		bool ExtractFilledSilhouettePoints(
			const TArray<FColor>& SourcePixels,
			const int32 SourceWidth,
			const int32 SourceHeight,
			const FMetaAgentImageMaskBuildParams& Params,
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
				Params.SampleResolution,
				Params.bUseLuminance,
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
					if (MaskAlpha < Params.AlphaThreshold)
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

			SubsampleMaskedPointsFast(
				CandidateNormalizedPoints,
				nullptr,
				Params.DesiredPointCount,
				OutNormalizedPoints,
				Params.DensityGridScale,
				Params.TargetJitterNormalized);
			return OutNormalizedPoints.Num() > 0;
		}

		bool ExtractSobelEdgePoints(
			const TArray<FColor>& SourcePixels,
			const int32 SourceWidth,
			const int32 SourceHeight,
			const FMetaAgentImageMaskBuildParams& Params,
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
				Params.SampleResolution,
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

			const int32 DesiredCandidates = FMath::Max(
				FMath::RoundToInt(static_cast<float>(Params.DesiredPointCount) * Params.DensityGridScale * 3.0f),
				128);
			OutAppliedThreshold = ResolveAdaptiveEdgeThreshold(
				SobelMagnitudes,
				Params.EdgeThreshold,
				DesiredCandidates);

			TArray<float> Weights;
			Weights.SetNum(SobelMagnitudes.Num());
			OutCandidateCount = 0;

			for (int32 Index = 0; Index < SobelMagnitudes.Num(); ++Index)
			{
				const float EdgeMagnitude = SobelMagnitudes[Index];
				if (EdgeMagnitude < OutAppliedThreshold)
				{
					Weights[Index] = 0.0f;
					continue;
				}

				Weights[Index] = FMath::Pow(EdgeMagnitude, Params.GrayscaleGamma);
				OutCandidateCount++;
			}

			if (OutCandidateCount <= 0)
			{
				OutDebugInfo = FString::Printf(
					TEXT("No Sobel edge pixels passed threshold %.3f."),
					OutAppliedThreshold);
				return false;
			}

			int32 StratGridSize = 1;
			if (!ScatterStratifiedFromMaskWeights(
				Weights,
				OutMaskWidth,
				OutMaskHeight,
				Params,
				OutNormalizedPoints,
				StratGridSize))
			{
				OutDebugInfo = TEXT("Sobel edges produced no scatter points.");
				return false;
			}

			return true;
		}
	}

	bool GetImageFileIdentity(const FString& SourceImagePath, FDateTime& OutTimestamp, int64& OutFileSize)
	{
		OutTimestamp = FDateTime::MinValue();
		OutFileSize = 0;

		if (SourceImagePath.IsEmpty())
		{
			return false;
		}

		const FString FullPath = FPaths::ConvertRelativePathToFull(SourceImagePath);
		if (!FPaths::FileExists(FullPath))
		{
			return false;
		}

		OutTimestamp = IFileManager::Get().GetTimeStamp(*FullPath);
		OutFileSize = IFileManager::Get().FileSize(*FullPath);
		return OutTimestamp != FDateTime::MinValue() && OutFileSize > 0;
	}

	FMetaAgentImageMaskBuildParams MakeBuildParams(
		const FString& SourceImagePath,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount)
	{
		FMetaAgentImageMaskBuildParams Params;
		Params.SourceImagePath = SourceImagePath;
		GetImageFileIdentity(SourceImagePath, Params.SourceFileTimestamp, Params.SourceFileSize);
		Params.ImageSamplingMode = ShapeDefinition.ImageSamplingMode;
		Params.AlphaThreshold = ShapeDefinition.AlphaThreshold;
		Params.EdgeThreshold = ShapeDefinition.EdgeThreshold;
		Params.bUseLuminance = ShapeDefinition.bUseLuminance;
		Params.SampleResolution = ShapeDefinition.SampleResolution;
		Params.GrayscaleGamma = ShapeDefinition.GrayscaleGamma;
		Params.DensityGridScale = ShapeDefinition.DensityGridScale;
		Params.TargetJitterNormalized = ShapeDefinition.TargetJitterNormalized;
		Params.DesiredPointCount = FMath::Max(1, DesiredPointCount);
		return Params;
	}

	bool BuildMaskOnWorkerThread(const FMetaAgentImageMaskBuildParams& Params, FMetaAgentImageMaskBuildOutput& OutOutput)
	{
		OutOutput = FMetaAgentImageMaskBuildOutput();

		TArray<FColor> SourcePixels;
		int32 SourceWidth = 0;
		int32 SourceHeight = 0;
		if (!ReadTexturePixelsFromPngFile(Params.SourceImagePath, SourcePixels, SourceWidth, SourceHeight))
		{
			OutOutput.DebugInfo = FString::Printf(
				TEXT("Failed to read PNG '%s'."),
				Params.SourceImagePath.IsEmpty() ? TEXT("<none>") : *FPaths::GetCleanFilename(Params.SourceImagePath));
			return false;
		}

		TArray<FVector2D> SelectedNormalizedPoints;
		int32 MaskWidth = 0;
		int32 MaskHeight = 0;
		int32 CandidateCount = 0;
		float AppliedEdgeThreshold = Params.EdgeThreshold;
		bool bExtracted = false;

		if (Params.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::FilledSilhouette)
		{
			bExtracted = ExtractFilledSilhouettePoints(
				SourcePixels,
				SourceWidth,
				SourceHeight,
				Params,
				SelectedNormalizedPoints,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				OutOutput.DebugInfo);
		}
		else if (Params.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::GrayscaleDensity)
		{
			bExtracted = ExtractGrayscaleDensityPoints(
				SourcePixels,
				SourceWidth,
				SourceHeight,
				Params,
				SelectedNormalizedPoints,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				OutOutput.DebugInfo);
		}
		else
		{
			bExtracted = ExtractSobelEdgePoints(
				SourcePixels,
				SourceWidth,
				SourceHeight,
				Params,
				SelectedNormalizedPoints,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				AppliedEdgeThreshold,
				OutOutput.DebugInfo);
		}

		if (!bExtracted)
		{
			return false;
		}

		NormalizedPointsToLocalCm(SelectedNormalizedPoints, OutOutput.LocalPointsCm);
		OutOutput.bSuccess = OutOutput.LocalPointsCm.Num() > 0;

		const FString PixelSource = FString::Printf(TEXT("PNG:%s"), *FPaths::GetCleanFilename(Params.SourceImagePath));
		if (Params.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::SobelEdges)
		{
			const int32 StratGridSize = FMath::Max(
				1,
				FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Params.DesiredPointCount) * Params.DensityGridScale)));
			OutOutput.DebugInfo = FString::Printf(
				TEXT("SobelEdges async pixels=%s src=%dx%d mask=%dx%d edgeCandidates=%d points=%d stratGrid=%dx%d edgeThreshold=%.3f gridScale=%.1f jitter=%.2f"),
				*PixelSource,
				SourceWidth,
				SourceHeight,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				OutOutput.LocalPointsCm.Num(),
				StratGridSize,
				StratGridSize,
				AppliedEdgeThreshold,
				Params.DensityGridScale,
				Params.TargetJitterNormalized);
		}
		else if (Params.ImageSamplingMode == EMetaAgentParticleImageSamplingMode::GrayscaleDensity)
		{
			const int32 StratGridSize = FMath::Max(
				1,
				FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Params.DesiredPointCount) * Params.DensityGridScale)));
			OutOutput.DebugInfo = FString::Printf(
				TEXT("GrayscaleDensity async pixels=%s src=%dx%d mask=%dx%d weightedPixels=%d points=%d stratGrid=%dx%d gamma=%.2f gridScale=%.1f jitter=%.2f"),
				*PixelSource,
				SourceWidth,
				SourceHeight,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				OutOutput.LocalPointsCm.Num(),
				StratGridSize,
				StratGridSize,
				Params.GrayscaleGamma,
				Params.DensityGridScale,
				Params.TargetJitterNormalized);
		}
		else
		{
			OutOutput.DebugInfo = FString::Printf(
				TEXT("FilledSilhouette async pixels=%s src=%dx%d mask=%dx%d candidates=%d points=%d alphaThreshold=%.2f"),
				*PixelSource,
				SourceWidth,
				SourceHeight,
				MaskWidth,
				MaskHeight,
				CandidateCount,
				OutOutput.LocalPointsCm.Num(),
				Params.AlphaThreshold);
		}

		return OutOutput.bSuccess;
	}
}
