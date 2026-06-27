// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Descriptors/PCGExDynamicMeshDescriptor.h"
#include "Details/PCGExAttachmentRules.h"
#include "Metadata/PCGObjectPropertyOverride.h"

#include "PCGExSpawnDynamicMesh.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="topology/spawn-dynamic-mesh"))
class UPCGExSpawnDynamicMeshSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExSpawnDynamicMeshElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SpawnDynamicMesh, "Spawn Dynamic Mesh", "A more flexible alternative to the native Spawn Dynamic Mesh");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::DynamicMesh;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExDynamicMeshDescriptor TemplateDescriptor;

	UPROPERTY(BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	TSoftObjectPtr<AActor> TargetActor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGObjectPropertyOverrideDescription> PropertyOverrideDescriptions;

	/** How to attach the component to their parent actor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExAttachmentRules AttachmentRules = FPCGExAttachmentRules(EAttachmentRule::KeepRelative);

	/** Specify a list of functions to be called on the target actor after instances are spawned. Functions need to be parameter-less and with "CallInEditor" flag enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> PostProcessFunctionNames;
};

struct FPCGExSpawnDynamicMeshContext final : FPCGExContext
{
	friend class FPCGExSpawnDynamicMeshElement;
	bool bWait = true;
};

class FPCGExSpawnDynamicMeshElement final : public IPCGExElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return false;
	}

protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true)
	PCGEX_ELEMENT_CREATE_CONTEXT(SpawnDynamicMesh)

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}
};
