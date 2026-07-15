// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeHubSpoke.generated.h"

UENUM()
enum class EPCGExHubSelectionMode : uint8
{
	ByDensity       = 0 UMETA(DisplayName = "By Local Density", ToolTip="Points in dense regions become hubs"),
	ByAttribute     = 1 UMETA(DisplayName = "By Attribute", ToolTip="Points with highest attribute values become hubs"),
	ByCentrality    = 2 UMETA(DisplayName = "By Centrality", ToolTip="Points closest to centroid of local region become hubs"),
	KMeansCentroids = 3 UMETA(DisplayName = "K-Means Centroids", ToolTip="Run k-means and use cluster centers as hubs"),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigHubSpoke : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigHubSpoke()
		: FPCGExProbeConfigBase(true)
	{
		HubAttribute.Update(TEXT("$Density"));
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGExHubSelectionMode HubSelectionMode = EPCGExHubSelectionMode::ByDensity;

	/** Number of hubs to create (for KMeans mode, this is K) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 NumHubs = 10;

	/** Attribute for hub selection (for ByAttribute mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, EditCondition="HubSelectionMode == EPCGExHubSelectionMode::ByAttribute"))
	FPCGAttributePropertyInputSelector HubAttribute;

	/** If true, also connect hubs to each other */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bConnectHubs = true;

	/** If true, each spoke connects only to nearest hub. If false, connects to all hubs within radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bNearestHubOnly = true;

	/** K-Means iterations (for KMeansCentroids mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1", ClampMax="100", EditCondition="HubSelectionMode == EPCGExHubSelectionMode::KMeansCentroids"))
	int32 KMeansIterations = 10;
};

class FPCGExProbeHubSpoke : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigHubSpoke Config;
	TSharedPtr<PCGExData::TBuffer<double>> HubAttributeBuffer;

protected:
	void SelectHubsByDensity(TArray<int32>& OutHubs) const;
	void SelectHubsByAttribute(TArray<int32>& OutHubs) const;
	void SelectHubsByCentrality(TArray<int32>& OutHubs) const;
	void SelectHubsByKMeans(TArray<int32>& OutHubs) const;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryHubSpoke : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigHubSpoke Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-hub-and-spoke"))
class UPCGExProbeHubSpokeProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeHubSpoke, "G-Probe : Hub & Spoke", "Creates hierarchical hub-and-spoke network topology")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigHubSpoke Config;
};
