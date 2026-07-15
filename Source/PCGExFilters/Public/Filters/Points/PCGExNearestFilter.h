// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExTaggedData.h"
#include "Details/PCGExDistancesDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"
#include "UObject/Object.h"

#include "PCGExNearestFilter.generated.h"


namespace PCGExMatching
{
	class FTargetsHandler;
}

namespace PCGExData
{
	struct FConstPoint;
	class FFacadePreloader;
}

/** Config slice shared by every "nearest target" point filter. Used as a const slice via GetNearestConfig(). */
USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExNearestFilterConfigBase
{
	GENERATED_BODY()

	FPCGExNearestFilterConfigBase() = default;

	/** Distance method to be used for source & target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExDistanceDetails DistanceDetails;

	/** Maximum search distance, read on the tested (source) point -- not the targets. Multiplied by Distance Scale. When the resulting value is 0 or less, the search range is unlimited (default). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble MaxDistance = FPCGExInputShorthandSelectorDouble(FName("MaxDistance"), 0, false);

	/** Constant scale applied to the Max Distance value. A non-positive result (e.g. a negative scale) means an unbounded search. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Scale"))
	double DistanceScale = 1;

	/** Exclude the point's own data from the nearest search. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Data matching settings. When enabled, only targets whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExFilterMatchingDetails DataMatching;
};


/** Abstract base for "nearest target" filter factories. Owns the TargetsHandler and drives shared prep; concrete factories set NearestConfig in Init and customize via the RegisterTargetDependencies / BuildTargetCaches hooks. */
UCLASS(Abstract, MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNearestFilterFactoryData : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> MatchRuleFactories;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;

	const FPCGExNearestFilterConfigBase& GetNearestConfig() const { return *NearestConfig; }

	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override { return true; }

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual bool SupportsCollectionEvaluation() const override { return false; }

	virtual void BeginDestroy() override;

	// Registers MaxDistance (source-side) for preload + consumption. Overrides MUST call Super.
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

protected:
	// Concrete factory's own Config slice. MUST be set at the top of Init, before Super::Init.
	const FPCGExNearestFilterConfigBase* NearestConfig = nullptr;

	// Hook: register per-target buffer dependencies (called once during Prepare).
	virtual void RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
	{
	}

	// Hook: build per-target caches once targets finish loading (off game thread). Return false to fail prep.
	virtual bool BuildTargetCaches(FPCGExContext* InContext)
	{
		return true;
	}
};

namespace PCGExPointFilter
{
	/** Base for "nearest target" point filters. Concrete filters implement Test() using ResolveExclude / FindNearestInRange / GetMaxDistance plus their own per-target read + comparison. */
	class FNearestFilter : public ISimpleFilter
	{
	public:
		explicit FNearestFilter(const TObjectPtr<const UPCGExNearestFilterFactoryData>& InFactory)
			: ISimpleFilter(InFactory)
			  , NearestFactory(InFactory)
		{
			TargetsHandler = InFactory->TargetsHandler;
		}

		const TObjectPtr<const UPCGExNearestFilterFactoryData> NearestFactory;

		TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
		TSet<const UPCGData*> IgnoreList; // Self-ignore + collection-level non-matching targets, built once in Init()
		bool bMatchingFailed = false;

		// Fallback result (NoMatchFallback == Pass) returned when a result can't be determined -- collection match
		// failure (via bCollectionTestResult) and, in derived filters, a missing target buffer.
		bool bNoMatchResult = false;

		// Per-point distance threshold = MaxDistance (read on the tested point) * DistanceScale.
		TSharedPtr<PCGExDetails::TSettingValue<double>> MaxDistance;
		double DistanceScale = 1;

		// Resolved once in Init: when MaxDistance is constant, GetMaxDistance returns the precomputed scalar
		// instead of a per-point virtual Read. bInflateQueryBounds is set only for bounds-based source modes
		// (Center needs no query-box padding).
		double ConstantMaxDistance = 0;
		bool bConstantMaxDistance = false;
		bool bInflateQueryBounds = false;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

	protected:
		// Per-filter setup hook, called after the shared nearest/matching machinery is ready.
		virtual bool InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
		{
			return true;
		}

		// Effective per-point search radius (<= 0 means unbounded).
		FORCEINLINE double GetMaxDistance(const int32 PointIndex) const
		{
			return bConstantMaxDistance ? ConstantMaxDistance : MaxDistance->Read(PointIndex) * DistanceScale;
		}

		// Returns the per-point exclude set, or nullptr when the point short-circuits (then return
		// bShortCircuitResult). Scratch is a caller-owned set the result may alias.
		const TSet<const UPCGData*>* ResolveExclude(const int32 PointIndex, TSet<const UPCGData*>& Scratch, bool& bShortCircuit, bool& bShortCircuitResult) const;

		// Find the nearest qualifying target within MaxDist (<= 0 == unbounded). Returns an invalid
		// FConstPoint when none qualifies (no target found, or the nearest is beyond the radius).
		PCGExData::FConstPoint FindNearestInRange(const PCGExData::FConstPoint& SourcePt, const double MaxDist, const TSet<const UPCGData*>* ExcludePtr) const;
	};
}
