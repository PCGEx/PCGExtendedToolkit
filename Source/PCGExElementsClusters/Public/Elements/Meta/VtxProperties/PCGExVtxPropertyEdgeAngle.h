// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExVtxPropertyFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExVtxPropertyEdgeAngle.generated.h"

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

UENUM()
enum class EPCGExEdgeAngleAggregation : uint8
{
	Min     = 0 UMETA(DisplayName = "Min", ToolTip="Smallest pairwise edge angle"),
	Max     = 1 UMETA(DisplayName = "Max", ToolTip="Largest pairwise edge angle"),
	Average = 2 UMETA(DisplayName = "Average", ToolTip="Average of all pairwise edge angles"),
};

USTRUCT(BlueprintType)
struct FPCGExEdgeAngleConfig
{
	GENERATED_BODY()

	FPCGExEdgeAngleConfig();

	/** Name of the 'double' attribute to write the edge angle to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Angle", PCG_Overridable))
	FName AngleAttributeName = FName("EdgeAngle");

	/** Unit/range to output the angle to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Range"))
	EPCGExAngleRange AngleRange = EPCGExAngleRange::PIRadians;

	/** How to aggregate the pairwise edge angles of complex nodes (more than two edges). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExEdgeAngleAggregation NonBinaryAggregation = EPCGExEdgeAngleAggregation::Average;

	/** Value written as-is for leaf nodes (single edge, no pair to measure). Never mapped to the selected range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double LeavesFallback = UE_DOUBLE_PI;

	/** Winding reference Up vector. Only affects winding-aware ranges (signed or 0..360 outputs). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Up Vector"))
	FPCGExInputShorthandSelectorDirection UpVector = FPCGExInputShorthandSelectorDirection(FName("Up"), FVector::UpVector, false);

	bool Validate(FPCGExContext* InContext) const;
};

/**
 *
 */
class FPCGExVtxPropertyEdgeAngle : public FPCGExVtxPropertyOperation
{
public:
	FPCGExEdgeAngleConfig Config;

	virtual bool PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual void ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP) override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> UpCache;
	TSharedPtr<PCGExData::TBuffer<double>> AngleBuffer;
	double UpMultiplier = 1;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExVtxPropertyEdgeAngleFactory : public UPCGExVtxPropertyFactoryData
{
	GENERATED_BODY()

public:
	FPCGExEdgeAngleConfig Config;
	virtual TSharedPtr<FPCGExVtxPropertyOperation> CreateOperation(FPCGExContext* InContext) const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|VtxProperty", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-vtx-properties/vtx-edge-angle"))
class UPCGExVtxPropertyEdgeAngleSettings : public UPCGExVtxPropertyProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(VtxEdgeAngle, "Vtx : Edge Angle", "Writes the angle between a vtx' connected edges. Static fallback for leaves, min/max/average for non-binary nodes.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Edge angle settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExEdgeAngleConfig Config;
};
