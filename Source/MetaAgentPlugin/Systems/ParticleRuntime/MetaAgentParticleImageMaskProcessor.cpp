// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

#include "metaagent/particle/image_mask_processor.hpp"

namespace MetaAgentImageMask
{
namespace
{
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

metaagent::particle::RgbaImage ToCoreRgbaImage(const TArray<FColor>& Pixels, const int32 Width, const int32 Height)
{
	metaagent::particle::RgbaImage Image;
	Image.width = Width;
	Image.height = Height;
	Image.pixels.resize(static_cast<size_t>(Width * Height));
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const FColor& Pixel = Pixels[Index];
		Image.pixels[static_cast<size_t>(Index)] = {
			Pixel.R,
			Pixel.G,
			Pixel.B,
			Pixel.A};
	}
	return Image;
}
} // namespace

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

	metaagent::particle::ImageMaskBuildParams CoreParams;
	MetaAgentTypeBridge::copy_image_mask_params_to_core(Params, CoreParams);

	metaagent::particle::ImageMaskBuildOutput CoreOutput;
	const metaagent::particle::RgbaImage CoreImage = ToCoreRgbaImage(SourcePixels, SourceWidth, SourceHeight);
	if (!metaagent::particle::image_mask::build_mask_from_rgba(CoreImage, CoreParams, CoreOutput))
	{
		MetaAgentTypeBridge::copy_image_mask_output_from_core(CoreOutput, OutOutput);
		return false;
	}

	MetaAgentTypeBridge::copy_image_mask_output_from_core(CoreOutput, OutOutput);
	return OutOutput.bSuccess;
}
} // namespace MetaAgentImageMask
