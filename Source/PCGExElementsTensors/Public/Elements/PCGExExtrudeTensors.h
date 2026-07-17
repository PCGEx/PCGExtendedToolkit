// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExTensorsTransform.h"
#include "Core/PCGExExtrusion.h"

#include "Core/PCGExPathProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExTensor.h"
#include "Data/PCGExPointElements.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Math/PCGExMathAxis.h"
#include "Paths/PCGExPathIntersectionDetails.h"
#include "Paths/PCGExPathOutputDetails.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExExtrudeTensors.generated.h"

struct FPCGExSortRuleConfig;

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

UENUM()
enum class EPCGExSelfIntersectionMode : uint8
{
	PathLength  = 0 UMETA(DisplayName = "Path Length", Tooltip="Sort extrusion by length, and resort to sorting rules in case of equality."),
	SortingOnly = 1 UMETA(DisplayName = "Sorting only", Tooltip="Only use sorting rules to sort paths."),
};

UENUM()
enum class EPCGExSelfIntersectionPriority : uint8
{
	Crossing = 0 UMETA(DisplayName = "Favor Crossing", Tooltip="Resolve crossing detection first, then merge."),
	Merge    = 1 UMETA(DisplayName = "Favor Merge", Tooltip="Resolve merge first, then crossing"),
};

//
// Import types from PCGExExtrusion namespace
//

namespace PCGExExtrudeTensors
{
	// Import core extrusion types from the reusable module
	using EExtrusionState = PCGExExtrusion::EExtrusionState;
	using EStopReason = PCGExExtrusion::EStopReason;
	using EExtrusionFlags = PCGExExtrusion::EExtrusionFlags;
	using FExtrusionConfig = PCGExExtrusion::FExtrusionConfig;
	using FCollisionResult = PCGExExtrusion::FCollisionResult;
	using FBranchPoint = PCGExExtrusion::FBranchPoint;
	using FExtrusionCallbacks = PCGExExtrusion::FExtrusionCallbacks;
	using FExtrusion = PCGExExtrusion::FExtrusion;

	// Re-export HasFlag for convenience
	using PCGExExtrusion::HasFlag;
}

// Forward declarations for config initialization
struct FPCGExExtrudeTensorsContext;
class UPCGExExtrudeTensorsSettings;

namespace PCGExExtrudeTensors
{
	/** Initialize FExtrusionConfig from ExtrudeTensors settings */
	void InitExtrusionConfigFromSettings(
		FExtrusionConfig& OutConfig,
		const FPCGExExtrudeTensorsContext* InContext,
		const UPCGExExtrudeTensorsSettings* InSettings,
		bool bHasStopFilters);
}

//
// Settings and Context
//

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="paths/generate/path-extrude-tensors"))
class UPCGExExtrudeTensorsSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(ExtrudeTensors, "Tensors : Extrude", "Extrude input points into paths along tensors.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Transform);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPathProcessorSettings
public:
	virtual FName GetMainInputPin() const override;
	virtual FName GetMainOutputPin() const override;
	//~End UPCGExPathProcessorSettings

	/** Whether to update point rotations during extrusion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Transforms", meta=(PCG_Overridable))
	bool bTransformRotation = true;

	/** How rotation transform is applied. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Transforms", meta=(PCG_Overridable, EditCondition="bTransformRotation"))
	EPCGExTensorTransformMode Rotation = EPCGExTensorTransformMode::Align;

	/** The axis to align with movement direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Transforms", meta=(PCG_Overridable, EditCondition="bTransformRotation && Rotation == EPCGExTensorTransformMode::Align"))
	EPCGExAxis AlignAxis = EPCGExAxis::Forward;

	/** Use a per-point attribute to control max iterations instead of a global value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bUsePerPointMaxIterations = false;

	/** Per-point Max Iterations. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Per-point Iterations", EditCondition="bUsePerPointMaxIterations"))
	FName IterationsAttribute = FName("Iterations");

	/** Max Iterations. If using per-point max, this will act as a clamping mechanism. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Max Iterations", ClampMin=1))
	int32 Iterations = 1;

	PCGEX_SETTING_VALUE_DECL(Iterations, int32)

	/** Whether to adjust max iteration based on max value found on points. Use at your own risks! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Use Max from Points", ClampMin=1, EditCondition="bUsePerPointMaxIterations", HideEditConditionToggle))
	bool bUseMaxFromPoints = false;

	/** Whether to limit the length of the generated path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable))
	bool bUseMaxLength = false;

	/** Max length of the generated path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable, DisplayName="Max Length", EditCondition="bUseMaxLength", EditConditionHides))
	FPCGExInputShorthandNameDoubleAbs MaxLengthValue = FPCGExInputShorthandNameDoubleAbs(FName("MaxLength"), 100, false);

	/** Whether to limit the number of points in a generated path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable))
	bool bUseMaxPointsCount = false;

	/** Max number of points in a generated path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable, DisplayName="Max Points Count", EditCondition="bUseMaxPointsCount", EditConditionHides))
	FPCGExInputShorthandNameInteger32Abs MaxPointsCountValue = FPCGExInputShorthandNameInteger32Abs(FName("MaxPointsCount"), 100, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MaxLengthInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName MaxLengthAttribute_DEPRECATED = FName("MaxLength");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double MaxLength_DEPRECATED = 100;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MaxPointsCountInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName MaxPointsCountAttribute_DEPRECATED = FName("MaxPointsCount");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 MaxPointsCount_DEPRECATED = 100;

#pragma endregion

	/** Whether to limit path length or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable, ClampMin=0.001))
	double FuseDistance = DBL_COLLOCATION_TOLERANCE;

	/** How to deal with points that are stopped */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable))
	EPCGExTensorStopConditionHandling StopConditionHandling = EPCGExTensorStopConditionHandling::Exclude;

	/** Whether to stop sampling when extrusion is stopped. While path will be cut, there's a chance that the head of the search comes back into non-stopping conditions, which would start a new extrusion. With this option disabled, new paths won't be permitted to exist. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable))
	bool bAllowChildExtrusions = false;

	/** If enabled, seeds that start stopped won't be extruded at all. Otherwise, they are transformed until they eventually reach a point that's outside stopping conditions and start an extrusion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable))
	bool bIgnoreStoppedSeeds = false;

	/** Test for intersections with external path data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Ext)", meta=(PCG_Overridable))
	bool bDoExternalPathIntersections = false;

	/** If enabled, if the origin location of the extrusion is detected as an intersection, it is not considered an intersection. This allows to have seeds perfectly located on paths used for intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Ext)", meta=(PCG_Overridable, EditCondition="bDoExternalPathIntersections"))
	bool bIgnoreIntersectionOnOrigin = true;

	/** Intersection settings  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Ext)", meta=(PCG_Overridable, EditCondition="bDoExternalPathIntersections"))
	FPCGExPathIntersectionDetails ExternalPathIntersections;

	/** Whether to test for intersection between actively extruding paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable))
	bool bDoSelfPathIntersections = false;

	/** How to order intersection checks. Sorting is using seeds input attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, EditCondition="bDoSelfPathIntersections"))
	EPCGExSelfIntersectionMode SelfIntersectionMode = EPCGExSelfIntersectionMode::PathLength;

	/** Controls the order in which paths extrusion will be stopped when intersecting, if shortest/longest path fails. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta = (PCG_Overridable, EditCondition="bDoSelfPathIntersections"))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Descending;

	/** Intersection settings for extruding path intersections */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, EditCondition="bDoSelfPathIntersections"))
	FPCGExPathIntersectionDetails SelfPathIntersections;


	/** Whether to test for intersection between actively extruding paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, EditCondition="bDoSelfPathIntersections"))
	bool bMergeOnProximity = false;

	/** Whether to resolve crossing or merge detection first. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, DisplayName=" ├─ Priority", EditCondition="bDoSelfPathIntersections && bMergeOnProximity", ClampMin = 0, ClampMax =1))
	EPCGExSelfIntersectionPriority SelfIntersectionPriority = EPCGExSelfIntersectionPriority::Crossing;

	/** Which end of the extruded segment should be favored. 0 = start, 1 = end. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, DisplayName=" ├─ Balance", EditCondition="bDoSelfPathIntersections && bMergeOnProximity", ClampMin = 0, ClampMax =1))
	double ProximitySegmentBalance = 0.5;

	/** Whether to test for intersection between actively extruding paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)", meta=(PCG_Overridable, DisplayName=" └─ Settings", EditCondition="bDoSelfPathIntersections && bMergeOnProximity"))
	FPCGExPathIntersectionDetails MergeDetails = FPCGExPathIntersectionDetails(10, 20);


	/** Whether the node should attempt to close loops based on angle and proximity */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)|Closing Loops", meta=(PCG_NotOverridable))
	bool bDetectClosedLoops = false;

	/** Range at which the first point must be located to check angle */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)|Closing Loops", meta=(PCG_Overridable, DisplayName=" ├─ Search Distance", EditCondition="bDetectClosedLoops"))
	double ClosedLoopSearchDistance = 100;

	/** Angle at which the loop will be closed, if within range */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Intersections (Self)|Closing Loops", meta=(PCG_Overridable, DisplayName=" └─ Search Angle", EditCondition="bDetectClosedLoops", Units="Degrees", ClampMin=0, ClampMax=90))
	double ClosedLoopSearchAngle = 11.25;

	/** Copy source point attributes as tags on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding")
	FPCGExAttributeToTagDetails AttributesToPathTags;

	/** Tag paths that were spawned from another extrusion (child extrusions). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfChildExtrusion = false;

	/** Tag to apply to child extrusion paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfChildExtrusion"))
	FString IsChildExtrusionTag = TEXT("Child");

	/** Tag paths that were stopped by stop filters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfIsStoppedByFilters = false;

	/** Tag to apply to paths stopped by filters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfIsStoppedByFilters"))
	FString IsStoppedByFiltersTag = TEXT("StoppedByFilters");

	/** Tag paths that were stopped by external path intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfIsStoppedByIntersection = false;

	/** Tag to apply to paths stopped by external intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfIsStoppedByIntersection"))
	FString IsStoppedByIntersectionTag = TEXT("StoppedByPath");

	/** Tag paths that were stopped by self-intersection with another extruding path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfIsStoppedBySelfIntersection = false;

	/** Tag to apply to paths stopped by self-intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfIsStoppedBySelfIntersection"))
	FString IsStoppedBySelfIntersectionTag = TEXT("SelfCrossed");

	/** Tag paths that were merged with another extruding path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfSelfMerged = false;

	/** Tag to apply to merged paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfSelfMerged"))
	FString IsSelfMergedTag = TEXT("SelfMerged");

	/** Tag paths that are continuations from a previous extrusion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(InlineEditConditionToggle))
	bool bTagIfIsFollowUp = false;

	/** Tag to apply to follow-up paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="bTagIfIsFollowUp"))
	FString IsFollowUpTag = TEXT("IsFollowUp");


	/** Tensor sampling settings. Note that these are applied on the flattened sample, e.g after & on top of individual tensors' mutations. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Tensor Sampling Settings"))
	FPCGExTensorHandlerDetails TensorHandlerDetails;


	/** Whether to give a new seed to the points. If disabled, they will inherit the original one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_NotOverridable))
	bool bRefreshSeed = true;

	/** ... */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Paths Output Settings"))
	FPCGExPathOutputDetails PathOutputDetails;

	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const;

private:
	friend class FPCGExExtrudeTensorsElement;
};

struct FPCGExExtrudeTensorsContext final : FPCGExPathProcessorContext
{
	friend class FPCGExExtrudeTensorsElement;

	TArray<TObjectPtr<const UPCGExTensorFactoryData>> TensorFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> StopFilterFactories;

	// Cached configuration (uses imported type)
	PCGExExtrudeTensors::FExtrusionConfig ExtrusionConfig;

	TArray<TSharedPtr<PCGExData::FFacade>> PathsFacades;
	TArray<TSharedPtr<PCGExPaths::FPath>> ExternalPaths;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExExtrudeTensorsElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ExtrudeTensors)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

//
// Processor Implementation
//

namespace PCGExExtrudeTensors
{
	/**
	 * FProcessor - Processes a batch of seed points into extrusions
	 *
	 * Uses callbacks to communicate with FExtrusion instances,
	 * enabling the decoupled architecture for reusability.
	 */
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExExtrudeTensorsContext, UPCGExExtrudeTensorsSettings>
	{
	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool IsTrivial() const override
		{
			return false;
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		void InitExtrusionFromSeed(const int32 InSeedIndex);
		TSharedPtr<FExtrusion> InitExtrusionFromExtrusion(const TSharedRef<FExtrusion>& InExtrusion);

		void SortQueue();

		virtual void PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;

		void ProcessSelfIntersectionsAndCutoffs();
		bool UpdateExtrusionQueue();

		virtual void CompleteWork() override;

	protected:
		TSharedPtr<PCGExSorting::FSorter> Sorter;

		FRWLock NewExtrusionLock;
		int32 RemainingIterations = 0;

		TSharedPtr<PCGExDetails::TSettingValue<int32>> PerPointIterations;
		TSharedPtr<PCGExDetails::TSettingValue<int32>> MaxPointsCount;
		TSharedPtr<PCGExDetails::TSettingValue<double>> MaxLength;

		TSharedPtr<PCGExPointFilter::FManager> StopFilters;
		TSharedPtr<PCGExTensor::FTensorsHandler> TensorsHandler;

		FPCGExAttributeToTagDetails AttributesToPathTags;
		TArray<TSharedPtr<FExtrusion>> ExtrusionQueue;
		TArray<TSharedPtr<FExtrusion>> NewExtrusions;

		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<FExtrusion>>> CompletedExtrusions;
		TSharedPtr<TArray<TSharedPtr<PCGExPaths::FPath>>> StaticPaths;

		TSharedPtr<FExtrusion> CreateExtrusion(const int32 InSeedIndex, const int32 InMaxIterations);

		/** Set up callbacks for a newly created extrusion */
		void SetupExtrusionCallbacks(const TSharedPtr<FExtrusion>& Extrusion);
	};

	/**
	 * FBatch - Batch processor for multiple point collections
	 */
	class FBatch final : public PCGExPointsMT::TBatch<FProcessor>
	{
	public:
		explicit FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection);
		virtual void Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void OnPathsPrepared();
	};
}
