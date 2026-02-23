// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "PCGExOctree.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Utils/PCGPointOctree.h"

class UPCGData;
class UPCGExMatchRuleFactoryData;
struct FPCGExMatchingDetails;
enum class EPCGExDistance : uint8;
struct FPCGExDistanceDetails;
struct FPCGExContext;

namespace PCGExMatching
{
	struct FScope;
	class FDataMatcher;
}

namespace PCGExMath
{
	class IDistances;
}

namespace PCGExData
{
	class FMultiFacadePreloader;
	struct FConstPoint;
	struct FPoint;
	class FPointIO;
	class FFacade;
}

namespace PCGExMatching
{
	class PCGEXMATCHING_API FTargetsHandler : public TSharedFromThis<FTargetsHandler>
	{
	protected:
		TSharedPtr<PCGExOctree::FItemOctree> TargetsOctree;
		TArray<TSharedRef<PCGExData::FFacade>> TargetFacades;
		TArray<const PCGPointOctree::FPointOctree*> TargetOctrees;
		int32 MaxNumTargets = 0;

		const PCGExMath::IDistances* Distances = nullptr;

	public:
		using FInitData = std::function<FBox(const TSharedPtr<PCGExData::FPointIO>&, const int32)>;
		using FFacadeRefIterator = std::function<void(const TSharedRef<PCGExData::FFacade>&, const int32)>;
		using FFacadeRefIteratorWithBreak = std::function<void(const TSharedRef<PCGExData::FFacade>&, const int32, bool&)>;
		using FPointIterator = std::function<void(const PCGExData::FPoint&)>;
		using FPointIteratorWithData = std::function<void(const PCGExData::FConstPoint&)>;
		using FTargetQuery = std::function<void(const PCGExOctree::FItem&)>;

		TSharedPtr<PCGExData::FMultiFacadePreloader> TargetsPreloader;
		TSharedPtr<FDataMatcher> DataMatcher;

		FTargetsHandler() = default;
		virtual ~FTargetsHandler() = default;

		const TArray<TSharedRef<PCGExData::FFacade>>& GetFacades() const { return TargetFacades; }
		int32 Num() const { return TargetFacades.Num(); }
		bool IsEmpty() const { return TargetFacades.IsEmpty(); }
		int32 GetMaxNumTargets() const { return MaxNumTargets; }

		int32 Init(FPCGExContext* InContext, const FName InPinLabel, FInitData&& InitFn);
		int32 Init(FPCGExContext* InContext, const FName InPinLabel);

		void SetDistances(const FPCGExDistanceDetails& InDetails);
		void SetDistances(const EPCGExDistance Source, const EPCGExDistance Target, const bool bOverlapIsZero);
		FORCEINLINE const PCGExMath::IDistances* GetDistances() const { return Distances; }

		void SetMatchingDetails(FPCGExContext* InContext, const FPCGExMatchingDetails* InDetails);
		bool PopulateIgnoreList(const TSharedPtr<PCGExData::FPointIO>& InDataCandidate, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const;
		/** Static matching: uses Test(UPCGData*, ...) which reads the first point only. For per-point attribute
		 *  matching, create an FDataMatcher directly and call Test(FConstPoint, ...) per-point instead. */
		bool PopulateIgnoreListInverse(const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& InMatchRuleFactories, const TSharedPtr<PCGExData::FFacade>& InSourceFacade, const FPCGExMatchingDetails* InDetails, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const;
		bool HandleUnmatchedOutput(const TSharedPtr<PCGExData::FFacade>& InFacade, const bool bForward = true) const;

		void ForEachPreloader(PCGExData::FMultiFacadePreloader::FPreloaderItCallback&& It) const;

		void ForEachTarget(FFacadeRefIterator&& It, const TSet<const UPCGData*>* Exclude = nullptr) const;
		bool ForEachTarget(FFacadeRefIteratorWithBreak&& It, const TSet<const UPCGData*>* Exclude = nullptr) const;
		void ForEachTargetPoint(FPointIterator&& It, const TSet<const UPCGData*>* Exclude = nullptr) const;
		void ForEachTargetPoint(FPointIteratorWithData&& It, const TSet<const UPCGData*>* Exclude = nullptr) const;

		void FindTargetsWithBoundsTest(const FBoxCenterAndExtent& QueryBounds, FTargetQuery&& Func, const TSet<const UPCGData*>* Exclude = nullptr) const;
		void FindElementsWithBoundsTest(const FBoxCenterAndExtent& QueryBounds, FPointIteratorWithData&& Func, const TSet<const UPCGData*>* Exclude = nullptr) const;

		bool FindClosestTarget(const PCGExData::FConstPoint& Probe, const FBoxCenterAndExtent& QueryBounds, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude = nullptr) const;
		void FindClosestTarget(const PCGExData::FConstPoint& Probe, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude = nullptr) const;
		void FindClosestTarget(const FVector& Probe, PCGExData::FConstPoint& OutResult, double& OutDistSquared, const TSet<const UPCGData*>* Exclude = nullptr) const;

		PCGExData::FConstPoint GetPoint(const int32 IO, const int32 Index) const;
		PCGExData::FConstPoint GetPoint(const PCGExData::FPoint& Point) const;

		// Parallel iteration support
		int32 GetTotalTargetPointCount(const TSet<const UPCGData*>* Exclude = nullptr) const;
		void BuildFlatTargetMap(TArray<int32>& OutPrefixSums, const TSet<const UPCGData*>* Exclude = nullptr) const;
		PCGExData::FConstPoint GetPointByFlatIndex(int32 FlatIndex, const TArray<int32>& PrefixSums, const TSet<const UPCGData*>* Exclude = nullptr) const;

		double GetDistSquared(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint) const;
		FVector GetSourceCenter(const PCGExData::FPoint& OriginPoint, const FVector& OriginLocation, const FVector& ToCenter) const;

		void StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle = nullptr) const;
	};
}
