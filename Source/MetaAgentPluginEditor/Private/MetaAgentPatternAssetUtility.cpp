// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "AssetToolsModule.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "UObject/SavePackage.h"

namespace MetaAgentPatternAssetUtility
{
	static const TCHAR* SampleAssetFolder = TEXT("/MetaAgentPlugin/MetaAgent/Patterns");

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
		Asset->PatternConfig = FMetaAgentParticlePatternConfig();
		Asset->PatternConfig.ActivePreset = Preset;
		switch (Preset)
		{
		case EMetaAgentParticlePatternPreset::Slow:
			Asset->PatternConfig.FormDurationSeconds = 3.0f;
			Asset->PatternConfig.HoldDurationSeconds = 1.5f;
			Asset->PatternConfig.ReturnDurationSeconds = 3.0f;
			break;
		case EMetaAgentParticlePatternPreset::Dramatic:
			Asset->PatternConfig.FormDurationSeconds = 4.0f;
			Asset->PatternConfig.HoldDurationSeconds = 2.0f;
			Asset->PatternConfig.ReturnDurationSeconds = 5.0f;
			break;
		case EMetaAgentParticlePatternPreset::Normal:
		default:
			Asset->PatternConfig.FormDurationSeconds = 1.5f;
			Asset->PatternConfig.HoldDurationSeconds = 0.5f;
			Asset->PatternConfig.ReturnDurationSeconds = 1.5f;
			break;
		}
		Asset->bOverrideShape = true;
		Asset->ShapeOverride.ShapeType = ShapeType;
		Asset->HoldPulseAmplitude = Preset == EMetaAgentParticlePatternPreset::Dramatic ? 0.15f : 0.0f;
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
			TEXT("Create Normal / Slow / Dramatic UMetaAgentParticlePatternAsset samples under /MetaAgentPlugin/MetaAgent/Patterns."),
			FConsoleCommandDelegate::CreateStatic(&CreateSamplePatternAssets),
			ECVF_Default);
	}
}
