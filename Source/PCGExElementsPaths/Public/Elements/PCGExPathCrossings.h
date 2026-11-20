// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOctree.h"
#include "Core/PCGExPathProcessor.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Math/PCGExMathAxis.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathIntersectionDetails.h"
#include "PCGExPathCrossings.generated.h"

class UPCGExSubPointsBlendInstancedFactory;
class FPCGExSubPointsBlendOperation;

namespace PCGExBlending
{
	class IUnionBlender;
}

namespace PCGExPaths
{
	struct FPathEdgeCrossings;
	class FPathEdgeLength;
	class FPath;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/intersect/path-path-crossings"))
class UPCGExPathCrossingsSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathCrossings, "Path × Path Crossings", "Find crossing points between & inside paths.");
#endif

#if WITH_EDITORONLY_DATA
	// UObject interface
	virtual void PostInitProperties() override;
	// End of UObject interface
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** If enabled, crossings are only computed per path, against themselves only.
	 * Note: this ignores the "bEnableSelfIntersection" from details below. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	bool bSelfIntersectionOnly = false;

	/** Filter entire dataset. If any tag is found on these paths, they are considered cut-able. Empty or None will ignore filtering. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bSelfIntersectionOnly"))
	FName CanBeCutTag = NAME_None;

	/** If enabled, the absence of the specified tag considers paths as cut-able. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Invert", PCG_NotOverridable, EditCondition="!bSelfIntersectionOnly", EditConditionHides))
	bool bInvertCanBeCutTag = false;

	/** Filter entire dataset. If any tag is found on these paths, they are considered cutters. Empty or None will ignore filtering. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bSelfIntersectionOnly"))
	FName CanCutTag = NAME_None;

	/** If enabled, the absence of the specified tag considers paths as cutters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Invert", PCG_NotOverridable, EditCondition="!bSelfIntersectionOnly", EditConditionHides))
	bool bInvertCanCutTag = false;

	/** If enabled, a point will be created at the crossing' location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bCreatePointAtCrossings = true;

	/** Configuration for how edge intersections are detected and processed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExPathEdgeIntersectionDetails IntersectionDetails;

	/** Blending applied on intersecting points along the path prev and next point. This is different from inheriting from external properties. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExSubPointsBlendInstancedFactory> Blending;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Cross Blending", meta=(PCG_Overridable))
	bool bDoCrossBlending = false;

	/** If enabled, blend in properties & attributes from external sources. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Cross Blending", meta=(PCG_Overridable, EditCondition="bDoCrossBlending"))
	FPCGExCarryOverDetails CrossingCarryOver;

	/** If enabled, blend in properties & attributes from external sources. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Cross Blending", meta=(PCG_Overridable, EditCondition="bDoCrossBlending"))
	FPCGExBlendingDetails CrossingBlending = FPCGExBlendingDetails(EPCGExBlendingType::Average, EPCGExBlendingType::None);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAlpha = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Crossing Alpha", EditCondition="bWriteAlpha"))
	FName CrossingAlphaAttributeName = "Alpha";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteAlpha", EditConditionHides, HideEditConditionToggle))
	double DefaultAlpha = -1;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOrientCrossing = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bOrientCrossing"))
	EPCGExAxis CrossingOrientAxis = EPCGExAxis::Forward;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCrossDirection = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Cross Direction", EditCondition="bWriteCrossDirection"))
	FName CrossDirectionAttributeName = "Cross";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteCrossDirection", EditConditionHides, HideEditConditionToggle))
	FVector DefaultCrossDirection = FVector::ZeroVector;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsPointCrossing = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName="Is Point Crossing", EditCondition="bWriteIsPointCrossing"))
	FName IsPointCrossingAttributeName = "IsPointCrossing";


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasCrossing = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasCrossing"))
	FString HasCrossingsTag = TEXT("HasCrossings");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoCrossings = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoCrossings"))
	FString HasNoCrossingsTag = TEXT("HasNoCrossings");

	/** If enabled, paths that are only "cutters" (paths that will cut but won't be cut). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bOmitUncuttableFromOutput = false;
};

struct FPCGExPathCrossingsContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathCrossingsElement;

	FString CanCutTag = TEXT("");
	FString CanBeCutTag = TEXT("");

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> CanCutFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> CanBeCutFilterFactories;

	UPCGExSubPointsBlendInstancedFactory* Blending = nullptr;

	FPCGExBlendingDetails CrossingBlending;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathCrossingsElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathCrossings)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathCrossings
{
	class FProcessor;
	class FBatch;

	struct FCutter
	{
		const FProcessor* Processor = nullptr;
		// Padded path bounds, tested against each edge before the cutter's octree. Invalid = always queried.
		FBox Reach = FBox(ForceInit);

		explicit FCutter(const FProcessor* InProcessor)
			: Processor(InProcessor)
		{
		}

		FCutter(const FProcessor* InProcessor, const FBox& InReach)
			: Processor(InProcessor)
			  , Reach(InReach)
		{
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathCrossingsContext, UPCGExPathCrossingsSettings>
	{
		friend class FBatch;

		bool bClosedLoop = false;
		bool bSelfIntersectionOnly = false;
		bool bCanCut = true;
		bool bCanBeCut = true;

		TSharedPtr<PCGExPaths::FPath> Path;
		TSharedPtr<PCGExPaths::FPathEdgeLength> PathLength;

		TArray<TSharedPtr<PCGExPaths::FPathEdgeCrossings>> EdgeCrossings;

		TSharedPtr<PCGExPointFilter::FManager> CanCutFilterManager;
		TSharedPtr<PCGExPointFilter::FManager> CanBeCutFilterManager;

		TBitArray<> CanCut;
		TBitArray<> CanBeCut;

		// Whether every edge bound of the path is finite. The batch only culls cutters when all paths are.
		bool bFiniteBounds = false;

		// Paths that can cut this one, in processor order. Filled by CompleteWork, read-only afterwards.
		TArray<FCutter> Cutters;
		// Padding the cutters' reach was built with; edge boxes get the same before being tested against it.
		double CutterPad = 0;

		TSet<FName> ProtectedAttributes;
		TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending;

		TSet<int32> CrossIOIndices;
		TSharedPtr<PCGExBlending::IUnionBlender> UnionBlender;

		FPCGExPathEdgeIntersectionDetails Details;

		TSharedPtr<PCGExData::TBuffer<bool>> FlagWriter;
		TSharedPtr<PCGExData::TBuffer<double>> AlphaWriter;
		TSharedPtr<PCGExData::TBuffer<FVector>> CrossWriter;
		TSharedPtr<PCGExData::TBuffer<bool>> IsPointCrossingWriter;

		int32 FoundCrossingsNum = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool IsTrivial() const override
		{
			return false;
		} // Force non-trivial because this shit is expensive

		// Whether this path has edges that other paths (or itself) can be tested against.
		bool IsCutter() const;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		void GatherCutters();
		void CollapseCrossings(const PCGExMT::FScope& Scope);
		void CrossBlend(const PCGExMT::FScope& Scope);

		virtual void Write() override;
	};

	class FBatch final : public PCGExPointsMT::TBatch<FProcessor>
	{
		friend class FProcessor;

		// Padded bounds of every cutter, keyed by processor index. Null when cutters can't be culled.
		TSharedPtr<PCGExOctree::FItemOctree> CutterOctree;
		double CutterPad = 0;

	public:
		explicit FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection);

	protected:
		virtual void OnInitialPostProcess() override;
	};
}
