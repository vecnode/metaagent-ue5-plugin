// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "MetaAgentNiagaraSystemProfile.generated.h"

class UNiagaraComponent;

UENUM(BlueprintType)
enum class EMetaAgentNiagaraDriverCapability : uint8
{
	None = 0,
	ParameterPhase = 1 << 0,
	DirectPositionWrite = 1 << 1,
	TargetArrayUpload = 1 << 2,
	DissipateVisibility = 1 << 3
};
ENUM_CLASS_FLAGS(EMetaAgentNiagaraDriverCapability)

/**
 * Content contract for a Niagara system driven by MetaAgent representation frames.
 * Assign on orchestrator/runtime or resolve from the Niagara system asset in a subclass.
 */
UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentNiagaraSystemProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	EMetaAgentParticleActuationMode PreferredActuationMode = EMetaAgentParticleActuationMode::Hybrid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara", meta = (Bitmask, BitmaskEnum = "/Script/MetaAgentPlugin.EMetaAgentNiagaraDriverCapability"))
	int32 Capabilities = static_cast<int32>(
		EMetaAgentNiagaraDriverCapability::ParameterPhase
		| EMetaAgentNiagaraDriverCapability::TargetArrayUpload
		| EMetaAgentNiagaraDriverCapability::DissipateVisibility);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	TArray<FName> RequiredUserParameters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FName TargetDataParameterName = TEXT("MetaAgentPatternTargetData");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FName TargetCountParameterName = TEXT("MetaAgentPatternTargetCount");

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	bool HasCapability(EMetaAgentNiagaraDriverCapability Capability) const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Niagara")
	bool ValidateComponent(UNiagaraComponent* NiagaraComponent, FString& OutMissingParameter) const;

	static bool ComponentExposesUserParameter(const UNiagaraComponent& NiagaraComponent, FName ParameterName);

	static const UMetaAgentNiagaraSystemProfile* GetDefaultProfile();
};
