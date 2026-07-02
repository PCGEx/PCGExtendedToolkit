// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDiscardByOverlap.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointsProcessor.h"

#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExSampleOverlapStats.generated.h"

#define PCGEX_FOREACH_FIELD_SAMPLEOVERLAPSTATS(MACRO)\
MACRO(OverlapCount, int32, 0)\
MACRO(OverlapSubCount, int32, 0)\
MACRO(RelativeOverlapCount, double, 0)\
MACRO(RelativeOverlapSubCount, double, 0)

namespace PCGExSampleOverlapStats
{
	struct FOverlapStats;
	struct FOverlap;
	class FProcessor;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/sample-overlap-stats"))
class UPCGExSampleOverlapStatsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SampleOverlapStats, "Sample : Overlap Stats", "Sample & write per-point overlap stats between entire point data.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Misc);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters used to know whether a point should be considered for overlap or not.", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** Overlap test mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExOverlapTestMode TestMode = EPCGExOverlapTestMode::Sphere;

	/** Point bounds to be used to compute overlaps */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** Expand bounds by that amount to account for a margin of error due to multiple layers of transformation and lack of OBB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Expansion = 10;

	/** The minimum amount two sub-points must overlap to be added to the comparison.  The higher, the more "overlap" there must be. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double MinThreshold = 0.1;

	/** How to interpret the min overlap value.  Discrete means distance in world space  Relative means uses percentage (0-1) of the averaged radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExMeanMeasure ThresholdMeasure = EPCGExMeanMeasure::Relative;

	// Output

	/** Write the unique overlap count to an int32 attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOverlapCount = false;

	/** Name of the 'int32' attribute to write unique overlap count to. Unique overlap count is the number of time a different point data set overlapped this point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Overlap Count", PCG_Overridable, EditCondition="bWriteOverlapCount"))
	FName OverlapCountAttributeName = FName("OverlapCount");


	/** Write the total number of overlaps. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOverlapSubCount = false;

	/** Name of the 'int32' attribute to write total overlap sub-count to. Total overlap count is the number of time another point overlapped this point. This count can get really high, really fast. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Overlap Sub Count", PCG_Overridable, EditCondition="bWriteOverlapSubCount"))
	FName OverlapSubCountAttributeName = FName("OverlapSubCount");


	/** Write the relative unique overlap count to an int32 attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRelativeOverlapCount = false;

	/** Name of the 'int32' attribute to write relative unique overlap count to. Relative Unique overlap count is this collection' OverlapSubCount divided by the max of all collections combined */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Relative Overlap Count", PCG_Overridable, EditCondition="bWriteRelativeOverlapCount"))
	FName RelativeOverlapCountAttributeName = FName("RelOverlapCount");


	/** Write the total number of overlaps. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRelativeOverlapSubCount = false;

	/** Name of the 'int32' attribute to write relative total overlap sub-count to. Relative Total overlap count is is this collection' OverlapCount divided by the max of all collections combined */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Relative Overlap Sub Count", PCG_Overridable, EditCondition="bWriteRelativeOverlapSubCount"))
	FName RelativeOverlapSubCountAttributeName = FName("RelOverlapSubCount");

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasAnyOverlap = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasAnyOverlap"))
	FString HasAnyOverlapTag = TEXT("HasAnyOverlap");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoOverlap = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoOverlap"))
	FString HasNoOverlapTag = TEXT("HasNoOverlap");

private:
	friend class FPCGExSampleOverlapStatsElement;
};

struct FPCGExSampleOverlapStatsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleOverlapStatsElement;

	mutable FRWLock OverlapLock;
	TMap<uint64, TSharedPtr<PCGExSampleOverlapStats::FOverlap>> OverlapMap;

	TSharedPtr<PCGExSampleOverlapStats::FOverlap> RegisterOverlap(PCGExSampleOverlapStats::FProcessor* InA, PCGExSampleOverlapStats::FProcessor* InB, const FBox& InIntersection);

	virtual void BatchProcessing_WorkComplete() override;

	PCGEX_FOREACH_FIELD_SAMPLEOVERLAPSTATS(PCGEX_OUTPUT_DECL_TOGGLE)

	double SharedOverlapSubCountMax = 0;
	double SharedOverlapCountMax = 0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleOverlapStatsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleOverlapStats)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleOverlapStats
{
	class FProcessor;

	struct PCGEXELEMENTSSAMPLING_API FOverlapStats
	{
		int32 OverlapCount = 0;
		double OverlapVolume = 0;
		double OverlapVolumeAvg = 0;
		double RelativeOverlapCount = 0;
		double RelativeOverlapVolume = 0;

		FORCEINLINE void Add(const FOverlapStats& Other)
		{
			OverlapCount += Other.OverlapCount;
			OverlapVolume += Other.OverlapVolume;
		}

		FORCEINLINE void Remove(const FOverlapStats& Other)
		{
			OverlapCount -= Other.OverlapCount;
			OverlapVolume -= Other.OverlapVolume;
		}

		FORCEINLINE void Add(const FOverlapStats& Other, const int32 MaxCount, const double MaxVolume)
		{
			Add(Other);
			UpdateRelative(MaxCount, MaxVolume);
		}

		FORCEINLINE void Remove(const FOverlapStats& Other, const int32 MaxCount, const double MaxVolume)
		{
			Remove(Other);
			UpdateRelative(MaxCount, MaxVolume);
		}

		FORCEINLINE void UpdateRelative(const int32 MaxCount, const double MaxVolume)
		{
			OverlapVolumeAvg = OverlapVolume / OverlapCount;
			RelativeOverlapCount = OverlapCount / MaxCount;
			RelativeOverlapVolume = OverlapVolume / MaxVolume;
		}
	};

	struct PCGEXELEMENTSSAMPLING_API FOverlap
	{
		uint64 HashID = 0;
		FBox Intersection = FBox(NoInit);
		bool IsValid = true;

		FProcessor* Primary = nullptr;
		FProcessor* Secondary = nullptr;

		FOverlapStats Stats;

		FOverlap(FProcessor* InPrimary, FProcessor* InSecondary, const FBox& InIntersection);
		FORCEINLINE FProcessor* GetOther(const FProcessor* InCandidate) const
		{
			return Primary == InCandidate ? Secondary : Primary;
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleOverlapStatsContext, UPCGExSampleOverlapStatsSettings>
	{
		friend struct FPCGExSampleOverlapStatsContext;

		const UPCGBasePointData* InPoints = nullptr;
		FBox Bounds = FBox(ForceInit);

		TSharedPtr<PCGExDiscardByOverlap::FPointBoundsOctree> Octree;

		TArray<TSharedPtr<PCGExDiscardByOverlap::FPointBounds>> LocalPointBounds;

		mutable FRWLock RegistrationLock;
		TArray<TSharedRef<FOverlap>> Overlaps;
		TArray<TSharedRef<FOverlap>> ManagedOverlaps;

		int32 NumPoints = 0;

		TArray<int32> OverlapSubCount;
		TArray<int32> OverlapCount;

		int8 bAnyOverlap = 0;

		PCGEX_FOREACH_FIELD_SAMPLEOVERLAPSTATS(PCGEX_OUTPUT_DECL)

	public:
		FOverlapStats Stats;

		double LocalOverlapSubCountMax = 0;
		double LocalOverlapCountMax = 0;

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		FORCEINLINE const FBox& GetBounds() const
		{
			return Bounds;
		}

		FORCEINLINE const TArray<TSharedPtr<PCGExDiscardByOverlap::FPointBounds>>& GetPointBounds() const
		{
			return LocalPointBounds;
		}

		FORCEINLINE const PCGExDiscardByOverlap::FPointBoundsOctree* GetOctree() const
		{
			return Octree.Get();
		}

		//virtual bool IsTrivial() const override { return false; } // Force non-trivial because this shit is expensive

		virtual ~FProcessor() override;

		FORCEINLINE void RegisterPointBounds(const int32 Index, const TSharedPtr<PCGExDiscardByOverlap::FPointBounds>& InPointBounds)
		{
			Bounds += InPointBounds->Bounds.GetBox();
			LocalPointBounds[Index] = InPointBounds;
		}

		void RegisterOverlap(FProcessor* InOtherProcessor, const FBox& Intersection);

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

		void ResolveOverlap(const int32 Index);
		void WriteSingleData(const int32 Index);

		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		virtual void Write() override;
	};
}
