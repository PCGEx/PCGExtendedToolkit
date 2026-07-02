// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"

#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Details/PCGExBoxIntersectionDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Math/OBB/PCGExOBBIntersections.h"

#include "PCGExBoundsPathIntersection.generated.h"

class UPCGExSubPointsBlendInstancedFactory;
class FPCGExSubPointsBlendOperation;
class UPCGExBlendOpFactory;

namespace PCGExSampling
{
	class FTargetsHandler;
}

namespace PCGExMath::OBB
{
	class FCollection;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/intersect/path-bounds-intersection"))
class UPCGExBoundsPathIntersectionSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BoundsPathIntersection, "Path × Bounds Intersection", "Find intersection with target input points.");
#endif

#if WITH_EDITORONLY_DATA
	// UObject interface
	virtual void PostInitProperties() override;
	// End of UObject interface
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	//PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** If enabled, allows you to filter out which targets get sampled by which data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Sampling);

	/** Blending applied on intersecting points along the path prev and next point. This is different from inheriting from external properties. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExSubPointsBlendInstancedFactory> Blending;

	/** Output settings for intersection points and attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Output"))
	FPCGExBoxIntersectionDetails OutputSettings;

	/** Tag paths that were cut by bounds intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasCuts = true;

	/** Tag to apply to paths with intersection cuts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasCuts"))
	FString HasCutsTag = TEXT("HasCuts");

	/** Tag paths that had no bounds intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfUncut = false;

	/** Tag to apply to uncut paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfUncut"))
	FString UncutTag = TEXT("Uncut");

	void AddTags(const TSharedPtr<PCGExData::FPointIO>& IO, bool bIsCut = false) const;
};

struct FPCGExBoundsPathIntersectionContext final : FPCGExPathProcessorContext
{
	friend class FPCGExBoundsPathIntersectionElement;

	UPCGExSubPointsBlendInstancedFactory* Blending = nullptr;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	int32 NumMaxTargets = 0;

	TArray<TSharedPtr<PCGExMath::OBB::FCollection>> Collections;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBoundsPathIntersectionElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BoundsPathIntersection)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBoundsPathIntersection
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBoundsPathIntersectionContext, UPCGExBoundsPathIntersectionSettings>
	{
	protected:
		TSet<const UPCGData*> IgnoreList;

		bool bClosedLoop = false;
		int32 LastIndex = 0;
		TArray<TSharedPtr<PCGExMath::OBB::FIntersections>> Intersections;

		TArray<int32> StartIndices;
		FPCGExBoxIntersectionDetails Details;

		TSet<FName> ProtectedAttributes;
		TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending;

		int32 NewPointsNum = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};
}
