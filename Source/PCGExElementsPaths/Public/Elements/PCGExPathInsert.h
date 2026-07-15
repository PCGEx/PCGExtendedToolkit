// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExH.h"

#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExPathProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Math/PCGExMath.h"
#include "PCGExPathInsert.generated.h"

namespace PCGExPathInsert
{
	struct FTargetClaimMap;
}

class UPCGExSubPointsBlendInstancedFactory;
class FPCGExSubPointsBlendOperation;

namespace PCGExMatching
{
	class FTargetsHandler;
}

namespace PCGExPaths
{
	class FPathEdgeLength;
	class FPath;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;

	class FDataForwardHandler;
}

UENUM()
enum class EPCGExInsertLimitMode : uint8
{
	Discrete = 0 UMETA(DisplayName = "Count", ToolTip="Limit value is the maximum number of inserts per edge"),
	Distance = 1 UMETA(DisplayName = "Spacing", ToolTip="Limit value is the minimum spacing; max inserts = EdgeLength / Spacing"),
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-insert"))
class UPCGExPathInsertSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PathInsert, "Path : Insert", "Insert target points into paths at their nearest location.");
#endif

#if WITH_EDITORONLY_DATA
	virtual void PostInitProperties() override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** If enabled, allows you to filter which targets get inserted into which paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Sampling);

	/** If enabled, each target can only be inserted into one path (the closest one). Otherwise, a target may be inserted into multiple paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bExclusiveTargets = false;

	/** If enabled, inserted points will be snapped to the path. Otherwise, they retain their original position. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSnapToPath = false;

	/** If enabled, only insert targets that project to edge interiors (not endpoints). Targets at alpha 0 or 1 are skipped. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bEdgeInteriorOnly = false;

	/** If enabled, targets beyond path endpoints can extend the path (open paths only). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bEdgeInteriorOnly", EditConditionHides))
	bool bAllowPathExtension = true;

	/** Only insert points that are within a specified range of the path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bWithinRange = false;

	/** Maximum distance from path for a point to be inserted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWithinRange", EditConditionHides))
	FPCGExInputShorthandNameDoubleAbs Range = FPCGExInputShorthandNameDoubleAbs(FName("Range"), 100, false);

	/** Limit how many points can be inserted per edge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bLimitInsertsPerEdge = false;

	/** How to interpret the limit value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable, EditCondition="bLimitInsertsPerEdge", EditConditionHides))
	EPCGExInsertLimitMode LimitMode = EPCGExInsertLimitMode::Discrete;

	/** The limit value. For Count mode: max inserts. For Spacing mode: minimum distance between inserts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable, EditCondition="bLimitInsertsPerEdge", EditConditionHides))
	FPCGExInputShorthandNameDoubleAbs InsertLimit = FPCGExInputShorthandNameDoubleAbs(FName("InsertLimit"), 5, false);

	/** How to round fractional insert counts when using Spacing mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable, EditCondition="bLimitInsertsPerEdge && LimitMode == EPCGExInsertLimitMode::Distance", EditConditionHides))
	EPCGExTruncateMode LimitTruncate = EPCGExTruncateMode::Round;

	/** Skip insertions that would create collocated points (within tolerance of path vertices or other inserts). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_NotOverridable))
	bool bPreventCollocation = false;

	/** Minimum distance between inserted points and path vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Limits", meta=(PCG_Overridable, EditCondition="bPreventCollocation", EditConditionHides, ClampMin=0))
	double CollocationTolerance = 1.0;


	/** Blending applied on inserted points using path's prev and next point. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExSubPointsBlendInstancedFactory> Blending;


	/** Forward attributes from target points to inserted points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExForwardDetails TargetForwarding;

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFlagInsertedPoints = false;

	/** Attribute name to mark inserted points (true) vs original path points (false). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bFlagInsertedPoints"))
	FName InsertedFlagName = FName("IsInserted");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAlpha = false;

	/** Attribute name for the alpha value (0-1 position along edge where inserted). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bWriteAlpha"))
	FName AlphaAttributeName = FName("InsertAlpha");

	/** Alpha value for non-inserted (original) points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteAlpha", EditConditionHides, HideEditConditionToggle))
	double DefaultAlpha = -1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDistance = false;

	/** Attribute name for the distance from target point to path location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bWriteDistance"))
	FName DistanceAttributeName = FName("InsertDistance");

	/** Distance value for non-inserted (original) points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteDistance", EditConditionHides, HideEditConditionToggle))
	double DefaultDistance = -1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTargetIndex = false;

	/** Attribute name for the source target collection index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bWriteTargetIndex"))
	FName TargetIndexAttributeName = FName("TargetIndex");

	/** Target index value for non-inserted (original) points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteTargetIndex", EditConditionHides, HideEditConditionToggle))
	int32 DefaultTargetIndex = -1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDirection = false;

	/** Attribute name for the direction from target point to path location (or inverted). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bWriteDirection"))
	FName DirectionAttributeName = FName("InsertDirection");

	/** If enabled, writes direction from path to target instead of target to path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Invert Direction", EditCondition="bWriteDirection", EditConditionHides))
	bool bInvertDirection = false;

	/** Direction value for non-inserted (original) points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Default Value", EditCondition="bWriteDirection", EditConditionHides))
	FVector DefaultDirection = FVector::ZeroVector;

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasInserts = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasInserts"))
	FString HasInsertsTag = TEXT("HasInserts");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfNoInserts = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfNoInserts"))
	FString NoInsertsTag = TEXT("NoInserts");
};

struct FPCGExPathInsertContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathInsertElement;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	int32 NumMaxTargets = 0;

	UPCGExSubPointsBlendInstancedFactory* Blending = nullptr;

	// Shared state for exclusive target resolution
	TSharedPtr<PCGExPathInsert::FTargetClaimMap> TargetClaimMap;

	// Shared target map (when data matching is disabled, all processors share the same target set)
	TArray<int32> SharedTargetPrefixSums;
	int32 SharedTotalTargets = 0;
	bool bUseSharedTargetMap = false;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathInsertElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathInsert)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathInsert
{
	// Shared state for exclusive target resolution using sharded map
	struct FTargetClaimMap
	{
		struct FClaim
		{
			int32 ProcessorIdx = -1;
			double Distance = TNumericLimits<double>::Max();
		};

		PCGExMT::TH64MapShards<FClaim> Claims;

		void Reserve(const int32 TotalReserve)
		{
			Claims.Reserve(TotalReserve);
		}

		void RegisterCandidate(const uint64 TargetHash, const int32 ProcessorIdx, const double Distance)
		{
			Claims.FindOrAddAndUpdate(TargetHash, FClaim{ProcessorIdx, Distance}, [&](FClaim& Claim, bool bIsNew)
			{
				if (bIsNew || Distance < Claim.Distance)
				{
					Claim.ProcessorIdx = ProcessorIdx;
					Claim.Distance = Distance;
				}
			});
		}

		bool IsClaimedBy(const uint64 TargetHash, const int32 ProcessorIdx) const
		{
			if (const FClaim* Claim = Claims.Find(TargetHash))
			{
				return Claim->ProcessorIdx == ProcessorIdx;
			}
			return false;
		}
	};

	// Compact candidate for parallel gathering (16 bytes vs 76 bytes for full candidate)
	struct FCompactCandidate
	{
		int32 TargetFlatIndex = -1; // Reconstruct IO/PointIndex from prefix sums
		int32 EdgeIndex = -1;       // -1 for pre-path, NumEdges for post-path
		float Alpha = 0;
		float Distance = 0;
	};

	struct FInsertCandidate
	{
		int32 TargetIOIndex = -1;
		int32 TargetPointIndex = -1;
		int32 EdgeIndex = -1;
		double Alpha = 0;
		double Distance = 0;
		FVector PathLocation = FVector::ZeroVector;
		FVector OriginalLocation = FVector::ZeroVector;

		FORCEINLINE uint64 GetTargetHash() const
		{
			return PCGEx::H64(TargetPointIndex, TargetIOIndex);
		}

		bool operator<(const FInsertCandidate& Other) const
		{
			return Alpha < Other.Alpha;
		}
	};

	struct FEdgeInserts
	{
		TArray<FInsertCandidate> Inserts;

		void Add(const FInsertCandidate& Candidate)
		{
			Inserts.Add(Candidate);
		}

		void SortByAlpha()
		{
			Inserts.Sort();
		}

		int32 Num() const
		{
			return Inserts.Num();
		}

		bool IsEmpty() const
		{
			return Inserts.IsEmpty();
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathInsertContext, UPCGExPathInsertSettings>
	{
		TSet<const UPCGData*> IgnoreList;

		bool bClosedLoop = false;
		int32 LastIndex = 0;

		TSharedPtr<PCGExPaths::FPath> Path;
		TSharedPtr<PCGExPaths::FPathEdgeLength> PathLength;

		TSharedPtr<PCGExDetails::TSettingValue<double>> RangeGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> LimitGetter;

		// Stage 1: Candidates per edge
		TArray<FEdgeInserts> EdgeInserts;

		// Path extension inserts (open paths only)
		TArray<FInsertCandidate> PrePathInserts;
		TArray<FInsertCandidate> PostPathInserts;

		// Stage 3: Output indices
		TArray<int32> StartIndices;
		int32 TotalInserts = 0;

		// Blending
		TSet<FName> ProtectedAttributes;
		TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending;

		// Target attribute forwarding
		TArray<TSharedPtr<PCGExData::FDataForwardHandler>> ForwardHandlers;

		// Output writers
		TSharedPtr<PCGExData::TBuffer<bool>> FlagWriter;
		TSharedPtr<PCGExData::TBuffer<double>> AlphaWriter;
		TSharedPtr<PCGExData::TBuffer<double>> DistanceWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> TargetIndexWriter;
		TSharedPtr<PCGExData::TBuffer<FVector>> DirectionWriter;

		void GatherCandidates();

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool IsTrivial() const override
		{
			return false;
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
	};
}
