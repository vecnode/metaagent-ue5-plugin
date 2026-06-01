#include "MetaAgentEditorModeToolkit.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorModes.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "MetaAgentEditorMode.h"
#include "Misc/PackageName.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaAgentEditorModeToolkit"

void FMetaAgentEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	TargetSlotOptions.Reset();
	TargetSlotOptions.Add(MakeShared<FString>(TEXT("MAIN_CHARACTER")));
	TargetSlotOptions.Add(MakeShared<FString>(TEXT("BP_METAHUMAN_CHARACTER2")));
	TargetSlotOptions.Add(MakeShared<FString>(TEXT("BP_MetaAgentMain")));
	SelectedTargetSlot = TargetSlotOptions.Num() > 0 ? TargetSlotOptions[0] : nullptr;

	ToolkitWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MetaAgentPanelTitle", "MetaAgent"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("HelloWorldButtonLabel", "Hello World"))
			.OnClicked_Raw(this, &FMetaAgentEditorModeToolkit::HandleHelloWorldClicked)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 10.0f, 8.0f, 4.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CharacterSetupLabel", "Character Setup"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ScanCharactersButtonLabel", "Scan Characters"))
			.OnClicked_Raw(this, &FMetaAgentEditorModeToolkit::HandleScanCharactersClicked)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TargetSlotLabel", "Target Slot"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 8.0f, 8.0f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&TargetSlotOptions)
			.OnSelectionChanged_Raw(this, &FMetaAgentEditorModeToolkit::OnTargetSlotChanged)
			.OnGenerateWidget_Raw(this, &FMetaAgentEditorModeToolkit::MakeTargetSlotOptionWidget)
			.InitiallySelectedItem(SelectedTargetSlot)
			[
				SNew(STextBlock)
				.Text_Raw(this, &FMetaAgentEditorModeToolkit::GetSelectedTargetSlotText)
			]
		];

	FModeToolkit::Init(InitToolkitHost);
}

FName FMetaAgentEditorModeToolkit::GetToolkitFName() const
{
	return FName("MetaAgentEditorMode");
}

FText FMetaAgentEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitDisplayName", "MetaAgent");
}

FEdMode* FMetaAgentEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FMetaAgentEditorMode::EM_MetaAgentEditorModeId);
}

TSharedPtr<SWidget> FMetaAgentEditorModeToolkit::GetInlineContent() const
{
	return ToolkitWidget;
}

FReply FMetaAgentEditorModeToolkit::HandleHelloWorldClicked()
{
	UE_LOG(LogTemp, Log, TEXT("Hello World"));

	FNotificationInfo NotificationInfo(LOCTEXT("HelloWorldNotification", "Hello World"));
	NotificationInfo.bFireAndForget = true;
	NotificationInfo.ExpireDuration = 2.0f;
	NotificationInfo.bUseSuccessFailIcons = false;

	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	return FReply::Handled();
}

FReply FMetaAgentEditorModeToolkit::HandleScanCharactersClicked()
{
	TArray<FString> FoundLevelPaths;
	TArray<FString> FoundAssetPaths;
	TArray<FString> AllBlueprintPaths;
	TSet<FString> LevelBlueprintPaths;
	TSet<FString> LevelActorBlueprintPaths;
	bool bHasMainCharacterActor = false;

	auto ContainsMetaHumanHint = [](const FString& InValue) -> bool
	{
		return InValue.Contains(TEXT("METAHUMAN"), ESearchCase::IgnoreCase)
			|| InValue.Contains(TEXT("MH_"), ESearchCase::IgnoreCase)
			|| InValue.Contains(TEXT("/METAHUMANS/"), ESearchCase::IgnoreCase)
			|| InValue.Contains(TEXT("METAAGENTMHPLAYER"), ESearchCase::IgnoreCase)
			|| InValue.Contains(TEXT("METAAGENTCHARACTER"), ESearchCase::IgnoreCase);
	};

	auto IsMetaHumanLikeRuntimeCharacter = [&](const ACharacter* Character) -> bool
	{
		if (!Character)
		{
			return false;
		}

		if (ContainsMetaHumanHint(Character->GetName()) || ContainsMetaHumanHint(Character->GetClass()->GetName()))
		{
			return true;
		}

		TInlineComponentArray<UMeshComponent*> MeshComponents;
		Character->GetComponents(MeshComponents);

		bool bHasFaceLikeMesh = false;
		bool bHasGroomLikeComponent = false;

		for (const UMeshComponent* MeshComp : MeshComponents)
		{
			if (!MeshComp)
			{
				continue;
			}

			const FString CompName = MeshComp->GetName();
			const FString ClassName = MeshComp->GetClass() ? MeshComp->GetClass()->GetName() : FString();

			if (CompName.Contains(TEXT("Face"), ESearchCase::IgnoreCase))
			{
				bHasFaceLikeMesh = true;
			}

			if (CompName.Contains(TEXT("Hair"), ESearchCase::IgnoreCase)
				|| CompName.Contains(TEXT("Brow"), ESearchCase::IgnoreCase)
				|| CompName.Contains(TEXT("Lash"), ESearchCase::IgnoreCase)
				|| CompName.Contains(TEXT("Beard"), ESearchCase::IgnoreCase)
				|| CompName.Contains(TEXT("Mustache"), ESearchCase::IgnoreCase)
				|| ClassName.Contains(TEXT("Groom"), ESearchCase::IgnoreCase))
			{
				bHasGroomLikeComponent = true;
			}
		}

		return bHasFaceLikeMesh || bHasGroomLikeComponent;
	};

	// Scan currently loaded editor world for Character instances.
	if (GEditor)
	{
		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<AActor> ActorIt(EditorWorld); ActorIt; ++ActorIt)
			{
				if (AActor* Actor = *ActorIt)
				{
					const UClass* ActorClass = Actor->GetClass();
					FString ActorBlueprintPath = ActorClass ? ActorClass->GetPathName() : FString();
					if (ActorBlueprintPath.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
					{
						ActorBlueprintPath.LeftChopInline(2, EAllowShrinking::No);
					}

					if (!ActorBlueprintPath.IsEmpty())
					{
						LevelActorBlueprintPaths.Add(ActorBlueprintPath);
					}

					if (Actor->GetName().Contains(TEXT("MAIN_CHARACTER"), ESearchCase::IgnoreCase)
						|| ActorBlueprintPath.Contains(TEXT("MAIN_CHARACTER"), ESearchCase::IgnoreCase)
						|| ActorBlueprintPath.Contains(TEXT("BP_MH_PlayerChar"), ESearchCase::IgnoreCase))
					{
						bHasMainCharacterActor = true;
					}
				}
			}

			for (TActorIterator<ACharacter> It(EditorWorld); It; ++It)
			{
				if (ACharacter* Character = *It)
				{
					if (!IsMetaHumanLikeRuntimeCharacter(Character))
					{
						continue;
					}

					const FVector Location = Character->GetActorLocation();
					const UClass* CharacterClass = Character->GetClass();
					const FString ClassPath = CharacterClass ? CharacterClass->GetPathName() : FString();

					FString SourceBlueprintPath = ClassPath;
					if (SourceBlueprintPath.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
					{
						SourceBlueprintPath.LeftChopInline(2, EAllowShrinking::No);
					}
					if (!SourceBlueprintPath.IsEmpty())
					{
						LevelBlueprintPaths.Add(SourceBlueprintPath);
					}
					const FString Placement = FString::Printf(
						TEXT("%s | Location(X=%.2f Y=%.2f Z=%.2f) | Blueprint=%s"),
						*Character->GetPathName(),
						Location.X,
						Location.Y,
						Location.Z,
						*SourceBlueprintPath);

					FoundLevelPaths.Add(Placement);
				}
			}
		}
	}

	// Scan project content for Blueprint assets deriving from ACharacter.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SetTemporaryCachingMode(true);

	TSet<FTopLevelAssetPath> DerivedCharacterClasses;
	{
		TArray<FTopLevelAssetPath> BaseClasses;
		BaseClasses.Add(ACharacter::StaticClass()->GetClassPathName());
		TSet<FTopLevelAssetPath> ExcludedClasses;
		AssetRegistry.GetDerivedClassNames(BaseClasses, ExcludedClasses, DerivedCharacterClasses);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.PackagePaths.Add(FName(TEXT("/MetaAgentPlugin")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> GameAssets;
	AssetRegistry.GetAssets(Filter, GameAssets);

	for (const FAssetData& AssetData : GameAssets)
	{
		const FString AssetObjectPath = AssetData.GetObjectPathString();
		const FString AssetClassPath = AssetData.AssetClassPath.ToString();

		const bool bLooksLikeBlueprintAsset = AssetClassPath.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase);

		if (!bLooksLikeBlueprintAsset)
		{
			continue;
		}

		AllBlueprintPaths.Add(AssetObjectPath);

		FString GeneratedClassPath;
		if ((!AssetData.GetTagValue(FName(TEXT("GeneratedClassPath")), GeneratedClassPath) || GeneratedClassPath.IsEmpty())
			&& (!AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassPath) || GeneratedClassPath.IsEmpty()))
		{
			GeneratedClassPath = AssetObjectPath + TEXT("_C");
		}

		FString ParentClassPath;
		if (!AssetData.GetTagValue(FName(TEXT("ParentClassPath")), ParentClassPath) || ParentClassPath.IsEmpty())
		{
			AssetData.GetTagValue(FName(TEXT("ParentClass")), ParentClassPath);
		}

		const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPath);
		FString NormalizedGeneratedClassObjectPath = ObjectPath;
		if (NormalizedGeneratedClassObjectPath.IsEmpty())
		{
			NormalizedGeneratedClassObjectPath = GeneratedClassPath;
		}

		if (NormalizedGeneratedClassObjectPath.IsEmpty())
		{
			continue;
		}

		const FTopLevelAssetPath GeneratedClassTopLevelPath(NormalizedGeneratedClassObjectPath);
		const bool bCharacterDerived = DerivedCharacterClasses.Contains(GeneratedClassTopLevelPath);
		const bool bReferencedByLevelInstance = LevelBlueprintPaths.Contains(AssetObjectPath);
		const bool bRuntimeParentHint =
			ContainsMetaHumanHint(ParentClassPath)
			|| ParentClassPath.Contains(TEXT("ACharacter"), ESearchCase::IgnoreCase)
			|| ParentClassPath.Contains(TEXT("APawn"), ESearchCase::IgnoreCase);

		if (bCharacterDerived || bRuntimeParentHint || bReferencedByLevelInstance)
		{
			const bool bMetaHumanLikeAsset =
				ContainsMetaHumanHint(AssetObjectPath)
				|| ContainsMetaHumanHint(GeneratedClassPath)
				|| ContainsMetaHumanHint(ParentClassPath)
				|| AssetObjectPath.StartsWith(TEXT("/Game/MetaHumans/"), ESearchCase::IgnoreCase)
				|| bReferencedByLevelInstance;

			if (bMetaHumanLikeAsset)
			{
				FoundAssetPaths.Add(AssetObjectPath);
			}
		}
	}

	AssetRegistry.SetTemporaryCachingMode(false);

	FoundLevelPaths.Sort();
	FoundAssetPaths.Sort();
	AllBlueprintPaths.Sort();

	struct FTargetBlueprintSlot
	{
		FString SlotName;
		TArray<FString> Tokens;
		bool bPresentInLevel = false;
		TArray<FString> CandidatePaths;
	};

	TArray<FTargetBlueprintSlot> TargetSlots;
	TargetSlots.Add({ TEXT("MAIN_CHARACTER"), { TEXT("MAIN_CHARACTER"), TEXT("BP_MH_PlayerChar") }, bHasMainCharacterActor, {} });
	TargetSlots.Add({ TEXT("BP_METAHUMAN_CHARACTER2"), { TEXT("BP_METAHUMAN_CHARACTER2") }, false, {} });
	TargetSlots.Add({ TEXT("BP_MetaAgentMain"), { TEXT("BP_MetaAgentMain") }, false, {} });

	auto MatchesAnyToken = [](const FString& Source, const TArray<FString>& Tokens) -> bool
	{
		for (const FString& Token : Tokens)
		{
			if (Source.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	};

	for (FTargetBlueprintSlot& Slot : TargetSlots)
	{
		if (!Slot.bPresentInLevel)
		{
			for (const FString& LevelPath : LevelActorBlueprintPaths)
			{
				if (MatchesAnyToken(LevelPath, Slot.Tokens))
				{
					Slot.bPresentInLevel = true;
					break;
				}
			}
		}

		for (const FString& BlueprintPath : AllBlueprintPaths)
		{
			if (MatchesAnyToken(BlueprintPath, Slot.Tokens))
			{
				Slot.CandidatePaths.Add(BlueprintPath);
			}
		}

		Slot.CandidatePaths.Sort();
	}

	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] MetaHuman-runtime scan started."));
	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] Type-based Blueprint scan roots: /Game and /MetaAgentPlugin (subfolders included)."));
	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] Current level MetaHuman-like character instances: %d"), FoundLevelPaths.Num());
	for (const FString& Path : FoundLevelPaths)
	{
		UE_LOG(LogTemp, Log, TEXT("[MetaAgent][Level] %s"), *Path);
	}

	for (const FString& LevelBlueprintPath : LevelBlueprintPaths)
	{
		UE_LOG(LogTemp, Log, TEXT("[MetaAgent][LevelBlueprint] %s"), *LevelBlueprintPath);
	}

	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] MetaHuman-like Character Blueprints by type: %d"), FoundAssetPaths.Num());
	for (const FString& Path : FoundAssetPaths)
	{
		UE_LOG(LogTemp, Log, TEXT("[MetaAgent][Asset] %s"), *Path);
	}

	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] Required slot coverage report:"));
	for (const FTargetBlueprintSlot& Slot : TargetSlots)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MetaAgent][Slot] %s | InLevel=%s | CandidateFiles=%d"),
			*Slot.SlotName,
			Slot.bPresentInLevel ? TEXT("Yes") : TEXT("No"),
			Slot.CandidatePaths.Num());

		for (const FString& Candidate : Slot.CandidatePaths)
		{
			UE_LOG(LogTemp, Log, TEXT("[MetaAgent][SlotCandidate][%s] %s"), *Slot.SlotName, *Candidate);
		}
	}

	if (SelectedTargetSlot.IsValid())
	{
		for (const FTargetBlueprintSlot& Slot : TargetSlots)
		{
			if (Slot.SlotName.Equals(*SelectedTargetSlot, ESearchCase::CaseSensitive))
			{
				UE_LOG(LogTemp, Log,
					TEXT("[MetaAgent][SelectedSlot] %s | InLevel=%s | Candidates=%d"),
					*Slot.SlotName,
					Slot.bPresentInLevel ? TEXT("Yes") : TEXT("No"),
					Slot.CandidatePaths.Num());
				break;
			}
		}
	}

	for (const FString& LevelBlueprintPath : LevelBlueprintPaths)
	{
		if (!FoundAssetPaths.Contains(LevelBlueprintPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("[MetaAgent] Level blueprint not resolved by scan: %s"), *LevelBlueprintPath);
		}
	}

	int32 MissingRequiredSlots = 0;
	for (const FTargetBlueprintSlot& Slot : TargetSlots)
	{
		if (!Slot.bPresentInLevel)
		{
			++MissingRequiredSlots;
		}
	}

	const FString Summary = FString::Printf(
		TEXT("Scan complete. LevelChars: %d, CharacterBPs: %d, MissingRequiredSlots: %d"),
		FoundLevelPaths.Num(),
		FoundAssetPaths.Num(),
		MissingRequiredSlots);

	FNotificationInfo NotificationInfo(FText::FromString(Summary));
	NotificationInfo.bFireAndForget = true;
	NotificationInfo.ExpireDuration = 3.0f;
	NotificationInfo.bUseSuccessFailIcons = false;

	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	return FReply::Handled();
}

void FMetaAgentEditorModeToolkit::OnTargetSlotChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type /*SelectInfo*/)
{
	if (NewValue.IsValid())
	{
		SelectedTargetSlot = NewValue;
	}
}

TSharedRef<SWidget> FMetaAgentEditorModeToolkit::MakeTargetSlotOptionWidget(TSharedPtr<FString> InOption) const
{
	const FString OptionText = InOption.IsValid() ? *InOption : FString(TEXT("Unknown"));
	return SNew(STextBlock).Text(FText::FromString(OptionText));
}

FText FMetaAgentEditorModeToolkit::GetSelectedTargetSlotText() const
{
	if (SelectedTargetSlot.IsValid())
	{
		return FText::FromString(*SelectedTargetSlot);
	}

	return LOCTEXT("NoTargetSlotSelected", "Select target slot...");
}

#undef LOCTEXT_NAMESPACE
