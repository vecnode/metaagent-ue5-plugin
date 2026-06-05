#include "MetaAgentEditorModeToolkit.h"

#include "EditorModes.h"
#include "EditorModeManager.h"
#include "MetaAgentEditorMode.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/PackageName.h"
#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Factories/WorldFactory.h"
#include "GameFramework/Pawn.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaAgentEditorModeToolkit"

void FMetaAgentEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
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
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateEmptyLevelButtonLabel", "New Empty Level"))
			.OnClicked_Raw(this, &FMetaAgentEditorModeToolkit::HandleCreateNewEmptyLevelClicked)
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

FReply FMetaAgentEditorModeToolkit::HandleCreateNewEmptyLevelClicked()
{
	const FString PluginMountPoint = TEXT("/MetaAgentPlugin");

	FString NextLevelAssetName;
	FString NextLevelPackagePath;

	for (int32 Index = 1; Index < 10000; ++Index)
	{
		const FString CandidateName = FString::Printf(TEXT("LEVEL%04d"), Index);
		const FString CandidatePackagePath = PluginMountPoint / CandidateName;

		if (!FPackageName::DoesPackageExist(CandidatePackagePath))
		{
			NextLevelAssetName = CandidateName;
			NextLevelPackagePath = CandidatePackagePath;
			break;
		}
	}

	if (NextLevelAssetName.IsEmpty())
	{
		FNotificationInfo NotificationInfo(LOCTEXT("CreateEmptyLevelNoSlots", "No available LEVEL#### slot found."));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	UWorldFactory* WorldFactory = NewObject<UWorldFactory>();
	if (!WorldFactory)
	{
		FNotificationInfo NotificationInfo(LOCTEXT("CreateEmptyLevelFactoryFail", "Failed to create world factory."));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewLevelAsset = AssetToolsModule.Get().CreateAsset(NextLevelAssetName, PluginMountPoint, UWorld::StaticClass(), WorldFactory);

	if (!NewLevelAsset)
	{
		FNotificationInfo NotificationInfo(LOCTEXT("CreateEmptyLevelAssetFail", "Failed to create new level asset."));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	UPackage* Package = NewLevelAsset->GetOutermost();
	FString PackageFilename;
	const bool bHasPackageFilename = Package
		&& FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetMapPackageExtension());

	if (!bHasPackageFilename)
	{
		FNotificationInfo NotificationInfo(LOCTEXT("CreateEmptyLevelPathFail", "Failed to resolve level package filename."));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	const bool bSaved = UPackage::SavePackage(Package, NewLevelAsset, *PackageFilename, SaveArgs);

	if (!bSaved)
	{
		FNotificationInfo NotificationInfo(LOCTEXT("CreateEmptyLevelSaveFail", "Failed to save the new level asset."));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	const bool bOpened = FEditorFileUtils::LoadMap(PackageFilename, false, true);
	if (!bOpened)
	{
		const FText OpenFailText = FText::Format(
			LOCTEXT("CreateEmptyLevelOpenFail", "Created {0}.umap but failed to open it."),
			FText::FromString(NextLevelAssetName));

		FNotificationInfo NotificationInfo(OpenFailText);
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 3.0f;
		NotificationInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);

		UE_LOG(LogTemp, Warning, TEXT("[MetaAgent] Created level asset but failed to open: %s"), *NextLevelPackagePath);
		return FReply::Handled();
	}

	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	bool bSpawnedStarterScene = false;

	if (EditorWorld)
	{
		if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transactional;

			AStaticMeshActor* PlaneActor = EditorWorld->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(),
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParams);

			if (PlaneActor && PlaneActor->GetStaticMeshComponent())
			{
				PlaneActor->SetActorLabel(TEXT("MA_GroundPlane"));
				PlaneActor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
				PlaneActor->SetActorScale3D(FVector(20.0f, 20.0f, 1.0f));
				PlaneActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
				bSpawnedStarterScene = true;
			}
		}

		if (UClass* PlayerPawnClass = LoadClass<APawn>(nullptr, TEXT("/MetaAgentPlugin/BP_MH_PlayerChar.BP_MH_PlayerChar_C")))
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transactional;

			APawn* PlayerPawn = EditorWorld->SpawnActor<APawn>(
				PlayerPawnClass,
				FVector(0.0f, 0.0f, 120.0f),
				FRotator::ZeroRotator,
				SpawnParams);

			if (PlayerPawn)
			{
				PlayerPawn->SetActorLabel(TEXT("MA_PlayerStartPawn"));
				bSpawnedStarterScene = true;
			}
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transactional;

		ADirectionalLight* DirectionalLight = EditorWorld->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			FVector(0.0f, 0.0f, 500.0f),
			FRotator(-45.0f, -45.0f, 0.0f),
			SpawnParams);

		if (DirectionalLight && DirectionalLight->GetLightComponent())
		{
			DirectionalLight->SetActorLabel(TEXT("MA_KeyLight"));
				DirectionalLight->GetLightComponent()->SetIntensity(10000.0f);
			bSpawnedStarterScene = true;
		}

			const FVector CameraLocation(-1200.0f, 0.0f, 700.0f);
			const FRotator CameraRotation = (FVector::ZeroVector - CameraLocation).Rotation();
			ACameraActor* OverviewCamera = EditorWorld->SpawnActor<ACameraActor>(
				ACameraActor::StaticClass(),
				CameraLocation,
				CameraRotation,
				SpawnParams);

			if (OverviewCamera)
			{
				OverviewCamera->SetActorLabel(TEXT("MA_OverviewCamera"));
				bSpawnedStarterScene = true;

				if (GEditor)
				{
					GEditor->MoveViewportCamerasToActor(*OverviewCamera, false);
				}
			}

		if (bSpawnedStarterScene)
		{
			EditorWorld->MarkPackageDirty();
			FEditorFileUtils::SaveLevel(EditorWorld->PersistentLevel);
		}
	}

	const FText SuccessText = FText::Format(
		LOCTEXT("CreateEmptyLevelSuccess", "Created and opened {0}.umap with starter plane and light"),
		FText::FromString(NextLevelAssetName));

	FNotificationInfo NotificationInfo(SuccessText);
	NotificationInfo.bFireAndForget = true;
	NotificationInfo.ExpireDuration = 3.0f;
	NotificationInfo.bUseSuccessFailIcons = true;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	UE_LOG(LogTemp, Log, TEXT("[MetaAgent] Created level asset: %s"), *NextLevelPackagePath);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
