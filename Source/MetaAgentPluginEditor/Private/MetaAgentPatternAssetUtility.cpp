// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "AssetToolsModule.h"
#include "Curves/CurveFloat.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "UObject/SavePackage.h"

namespace MetaAgentPatternAssetUtility
{
	static const TCHAR* SampleAssetFolder = TEXT("/MetaAgentPlugin/MetaAgent/Patterns");

	static UCurveFloat* CreateOrUpdateCurveAsset(
		UPackage* Package,
		const TCHAR* AssetName,
		const TArray<TPair<float, float>>& Keys)
	{
		UCurveFloat* Curve = FindObject<UCurveFloat>(Package, AssetName);
		if (!Curve)
		{
			Curve = NewObject<UCurveFloat>(Package, AssetName, RF_Public | RF_Standalone);
		}

		if (!Curve)
		{
			return nullptr;
		}

		FRichCurve& RichCurve = Curve->FloatCurve;
		RichCurve.Reset();
		for (const TPair<float, float>& Key : Keys)
		{
			const FKeyHandle KeyHandle = RichCurve.AddKey(Key.Key, Key.Value);
			RichCurve.SetKeyInterpMode(KeyHandle, RCIM_Cubic, RCTM_Auto);
		}

		Curve->MarkPackageDirty();
		return Curve;
	}

	static void ApplyPresetMotionProfile(
		UMetaAgentParticlePatternAsset& Asset,
		const EMetaAgentParticlePatternPreset Preset,
		UPackage* Package,
		const FString& AssetNamePrefix)
	{
		Asset.PatternConfig = FMetaAgentParticlePatternConfig();
		Asset.PatternConfig.ApplyPreset(Preset);

		switch (Preset)
		{
		case EMetaAgentParticlePatternPreset::Snappy:
			Asset.HoldPulseAmplitude = 0.035f;
			Asset.HoldPulseFrequencyHz = 1.1f;
			Asset.FormCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_FormCurve")),
				{ {0.0f, 0.0f}, {0.15f, 0.55f}, {0.45f, 0.9f}, {1.0f, 1.0f} });
			Asset.ReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnCurve")),
				{ {0.0f, 0.0f}, {0.25f, 0.75f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.DirectLerpReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnDirectCurve")),
				{ {0.0f, 0.0f}, {0.2f, 0.8f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.ArcLiftReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnArcCurve")),
				{ {0.0f, 0.0f}, {0.35f, 0.55f}, {0.7f, 0.95f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.SpiralInReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnSpiralCurve")),
				{ {0.0f, 0.0f}, {0.5f, 0.35f}, {1.0f, 1.0f} });
			break;

		case EMetaAgentParticlePatternPreset::Dreamy:
			Asset.HoldPulseAmplitude = 0.06f;
			Asset.HoldPulseFrequencyHz = 0.45f;
			Asset.FormCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_FormCurve")),
				{ {0.0f, 0.0f}, {0.35f, 0.08f}, {0.7f, 0.45f}, {1.0f, 1.0f} });
			Asset.ReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnCurve")),
				{ {0.0f, 0.0f}, {0.4f, 0.12f}, {0.8f, 0.65f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.DirectLerpReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnDirectCurve")),
				{ {0.0f, 0.0f}, {0.5f, 0.15f}, {0.85f, 0.7f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.ArcLiftReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnArcCurve")),
				{ {0.0f, 0.0f}, {0.55f, 0.2f}, {0.9f, 0.85f}, {1.0f, 1.0f} });
			Asset.PatternConfig.Return.SpiralInReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnSpiralCurve")),
				{ {0.0f, 0.0f}, {0.6f, 0.18f}, {1.0f, 1.0f} });
			break;

		case EMetaAgentParticlePatternPreset::Dramatic:
			Asset.HoldPulseAmplitude = 0.12f;
			Asset.HoldPulseFrequencyHz = 0.9f;
			Asset.FormCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_FormCurve")),
				{ {0.0f, 0.0f}, {0.25f, 0.15f}, {0.75f, 0.85f}, {1.0f, 1.0f} });
			Asset.ReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnCurve")),
				{ {0.0f, 0.0f}, {0.3f, 0.2f}, {0.85f, 0.9f}, {1.0f, 1.0f} });
			break;

		case EMetaAgentParticlePatternPreset::Slow:
			Asset.HoldPulseAmplitude = 0.05f;
			Asset.HoldPulseFrequencyHz = 0.6f;
			Asset.FormCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_FormCurve")),
				{ {0.0f, 0.0f}, {0.5f, 0.35f}, {1.0f, 1.0f} });
			Asset.ReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnCurve")),
				{ {0.0f, 0.0f}, {0.55f, 0.4f}, {1.0f, 1.0f} });
			break;

		case EMetaAgentParticlePatternPreset::Normal:
		default:
			Asset.HoldPulseAmplitude = 0.04f;
			Asset.HoldPulseFrequencyHz = 0.75f;
			Asset.FormCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_FormCurve")),
				{ {0.0f, 0.0f}, {0.4f, 0.55f}, {1.0f, 1.0f} });
			Asset.ReturnCurve = CreateOrUpdateCurveAsset(
				Package,
				*(AssetNamePrefix + TEXT("_ReturnCurve")),
				{ {0.0f, 0.0f}, {0.45f, 0.5f}, {1.0f, 1.0f} });
			break;
		}
	}

	UMetaAgentParticlePatternAsset* CreateOrUpdateSampleAsset(
		const FString& AssetName,
		const FText& DisplayName,
		const EMetaAgentParticlePatternPreset Preset,
		const EMetaAgentParticlePatternShape ShapeType)
	{
		const FString PackageName = FString::Printf(TEXT("%s/%s"), SampleAssetFolder, *AssetName);
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UMetaAgentParticlePatternAsset* Asset = FindObject<UMetaAgentParticlePatternAsset>(Package, *AssetName);
		if (!Asset)
		{
			Asset = NewObject<UMetaAgentParticlePatternAsset>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone);
		}

		if (!Asset)
		{
			return nullptr;
		}

		Asset->DisplayName = DisplayName;
		ApplyPresetMotionProfile(*Asset, Preset, Package, AssetName);
		Asset->bOverrideShape = true;
		Asset->ShapeOverride.ShapeType = ShapeType;
		Asset->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
		return Asset;
	}

	void CreateSamplePatternAssets()
	{
		CreateOrUpdateSampleAsset(
			TEXT("DA_Pattern_Normal"),
			FText::FromString(TEXT("Normal")),
			EMetaAgentParticlePatternPreset::Normal,
			EMetaAgentParticlePatternShape::ImageSilhouette);

		CreateOrUpdateSampleAsset(
			TEXT("DA_Pattern_Slow"),
			FText::FromString(TEXT("Slow")),
			EMetaAgentParticlePatternPreset::Slow,
			EMetaAgentParticlePatternShape::ImageSilhouette);

		CreateOrUpdateSampleAsset(
			TEXT("DA_Pattern_Dramatic"),
			FText::FromString(TEXT("Dramatic")),
			EMetaAgentParticlePatternPreset::Dramatic,
			EMetaAgentParticlePatternShape::ImageSilhouette);

		CreateOrUpdateSampleAsset(
			TEXT("DA_Pattern_Snappy"),
			FText::FromString(TEXT("Snappy")),
			EMetaAgentParticlePatternPreset::Snappy,
			EMetaAgentParticlePatternShape::ImageSilhouette);

		CreateOrUpdateSampleAsset(
			TEXT("DA_Pattern_Dreamy"),
			FText::FromString(TEXT("Dreamy")),
			EMetaAgentParticlePatternPreset::Dreamy,
			EMetaAgentParticlePatternShape::ImageSilhouette);

		UE_LOG(LogTemp, Log, TEXT("MetaAgent: created sample pattern assets under %s."), SampleAssetFolder);
	}

	void RegisterConsoleCommands()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}

		bRegistered = true;
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("MetaAgent.CreateSamplePatternAssets"),
			TEXT("Create Normal / Slow / Dramatic / Snappy / Dreamy UMetaAgentParticlePatternAsset samples under /MetaAgentPlugin/MetaAgent/Patterns."),
			FConsoleCommandDelegate::CreateStatic(&CreateSamplePatternAssets),
			ECVF_Default);
	}
}
