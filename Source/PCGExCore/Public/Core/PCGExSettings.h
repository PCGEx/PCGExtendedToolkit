// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGSettings.h"
#include "Data/PCGExDataCommon.h"
#include "PCGExSettings.generated.h"

class UPCGExInstancedFactory;
struct FPCGExContext;

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class PCGEXCORE_API UPCGExSettings : public UPCGSettings
{
	GENERATED_BODY()

	friend struct FPCGExContext;
	friend class IPCGExElement;

public:
	//~Begin UPCGSettings	
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins);
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void ApplyDeprecation(UPCGNode* InOutNode) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode);
	virtual bool GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual FGuid GetUserCustomVersionGuid() override;

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const;

	virtual bool OnlyPassThroughOneEdgeWhenDisabled() const override
	{
		return false;
	}

	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	/** If enabled, will pre-allocate all data on a single thread to avoid contention. Not all nodes support this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	EPCGExOptionState BulkInitData = EPCGExOptionState::Default;

	/** Cache the results of this node. Cached data persists until inputs change, avoiding re-computation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	EPCGExOptionState CacheData = EPCGExOptionState::Default;

	/** Cache the resources loaded by this node. Only accounted for if the node actually relies on external resources. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	EPCGExOptionState CacheLoadedResources = EPCGExOptionState::Default;

	/** Whether scoped attribute read is enabled or not. Disabling this on small dataset may greatly improve performance. It's enabled by default for legacy reasons. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	EPCGExOptionState ScopedAttributeGet = EPCGExOptionState::Default;

	/** This node will not make any copy of the data and instead modify the inputs directly.
	 * When enabling this you must make absolutely sure the data plugged into this node is not plugged in any other node.
	 * Only supported by nodes set-up in a way that makes in-place mutations possible. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	EPCGExOptionState StealData = EPCGExOptionState::Disabled;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	bool bForceOffThreadPrepare = false;

	bool GetForceOffThreadPrepare(const FPCGExContext* InContext) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable))
	bool bForceOffThreadExecute = false;

	bool GetForceOffThreadExecute(const FPCGExContext* InContext) const;

	virtual bool WantsDataStealing() const
	{
		return SupportsDataStealing() && StealData == EPCGExOptionState::Enabled;
	}

	bool WantsResourcesCached() const;

	/** Flatten the output of this node. Merges hierarchical data into a single flat collection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cleanup", meta=(PCG_NotOverridable))
	bool bFlattenOutput = false;

	/** If the node registers consumable attributes, these will be deleted from the output data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cleanup", meta=(PCG_NotOverridable))
	bool bCleanupConsumableAttributes = false;

	/** If the node registers consumable attributes, this a list of comma separated names that won't be deleted if they were registered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cleanup", meta=(PCG_Overridable, DisplayName="Protected Attributes", EditCondition="bCleanupConsumableAttributes"))
	FString CommaSeparatedProtectedAttributesName;

	/** Attribute names to exclude from cleanup. Works alongside the comma-separated string list above. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cleanup", meta=(PCG_NotOverridable, DisplayName="Protected Attributes", EditCondition="bCleanupConsumableAttributes"))
	TArray<FName> ProtectedAttributes;

	/** Whether the execution of the graph should be cancelled if this node execution is cancelled internally */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bPropagateAbortedExecution = false;

	/** Suppress warnings about invalid input data (e.g., missing attributes, wrong data types). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietInvalidInputWarning = false;

	/** Suppress errors about missing required input connections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietMissingInputError = false;

	/** Suppress error messages when node execution is cancelled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable))
	bool bQuietCancellationError = false;

	//~End UPCGExPointsProcessorSettings

#if WITH_EDITOR
	/** Open a browser and navigate to that node' documentation page. */
	UFUNCTION(CallInEditor, Category = Tools, meta=(DisplayName="Node Documentation", ShortToolTip="Open a browser and navigate to that node' documentation page", DisplayOrder=-1))
	void EDITOR_OpenNodeDocumentation() const;
#endif

protected:
	/**
	 * Legacy per-object deprecation version. No longer the serialized source of truth -- the package
	 * custom version (FPCGExCustomVersion, surfaced by the engine as UserDataVersion) is. Kept as a
	 * UPROPERTY for one release so ResolveDataVersion() can bridge assets saved before the custom
	 * version existed; the PCGEX_IF_VERSION_LOWER gates still read this field.
	 */
	UPROPERTY()
	int64 PCGExDataVersion = INDEX_NONE;

#if WITH_EDITOR
	/** Resolve PCGExDataVersion from the package custom version (UserDataVersion), bridging legacy assets. Called from Serialize on load. */
	void ResolveDataVersion();
#endif

	virtual bool SupportsDataStealing() const;
	virtual bool ShouldCache() const;
	virtual bool WantsScopedAttributeGet() const;
	virtual bool WantsBulkInitData() const;
};
