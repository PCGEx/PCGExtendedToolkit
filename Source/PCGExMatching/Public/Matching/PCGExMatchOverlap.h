// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchAttrToAttr.h"
#include "PCGExOctree.h"
#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExMatchOverlap.generated.h"

struct FPCGExTaggedData;

namespace PCGExData
{
	template <typename T>
	class TAttributeBroadcaster;
}

UENUM()
enum class EPCGExMatchOverlapExpansionMode : uint8
{
	None  = 0 UMETA(DisplayName = "None", Tooltip="Don't alter extents"),
	Add   = 1 UMETA(DisplayName = "Add", Tooltip="Add the value to the extents"),
	Scale = 2 UMETA(DisplayName = "Scale", Tooltip="Scale the data bounds"),
};

USTRUCT(BlueprintType)
struct FPCGExMatchOverlapConfig : public FPCGExMatchRuleConfigBase
{
	GENERATED_BODY()

	FPCGExMatchOverlapConfig()
		: FPCGExMatchRuleConfigBase()
	{
	}

	/** Amount by which the bounds should be expanded or scaled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExMatchOverlapExpansionMode ExpansionMode = EPCGExMatchOverlapExpansionMode::None;

	/** Expansion value - either added to extents or used as scale factor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="ExpansionMode != EPCGExMatchOverlapExpansionMode::None", EditConditionHides))
	FPCGExInputShorthandNameVector Expansion = FPCGExInputShorthandNameVector(FName("@Data.Expansion"), FVector(1, 1, 1));

	/** If enabled, require a minimum overlap ratio to match. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMinOverlapRatio = false;

	/** Minimum overlap ratio (0-1) required for a match. Ratio is computed as overlap volume / smallest box volume. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMinOverlapRatio"))
	FPCGExInputShorthandNameDouble01 MinOverlapRatio = FPCGExInputShorthandNameDouble01(FName("@Data.MinOverlapRatio"), 0.5, false);

	/** Enable recursive/transitive matching through overlapping chains. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bRecursive = false;

	/** If enabled, matches are expanded transitively - if A overlaps B and B overlaps C, then A, B, and C are all considered matching.
	 * Maximum number of hops for recursive matching. -1 means unlimited. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bRecursive", ClampMin=-1))
	int32 MaxRecursionDepth = -1;
};

/**
 *
 */
class FPCGExMatchOverlap : public FPCGExMatchRuleOperation
{
public:
	FPCGExMatchOverlapConfig Config;

	virtual bool PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources) override;

	virtual bool Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const override;

	virtual bool SupportsRecursion() const override
	{
		return true;
	}

	virtual bool WantsRecursion() const override
	{
		return Config.bRecursive;
	}

	virtual int32 GetMaxRecursionDepth() const override
	{
		return Config.MaxRecursionDepth;
	}

protected:
	// Pre-computed source bounds (already expanded during preparation)
	TArray<FBox> SourceBounds;

	// Pre-computed min overlap ratios per source (when using attribute input)
	TArray<double> MinOverlapRatios;

	// Octree for spatial queries - used to quickly find overlapping candidates
	TUniquePtr<PCGExOctree::FItemOctree> Octree;

	// Get source index from octree query for a given candidate bounds
	void GetOverlappingSourceIndices(const FBox& CandidateBounds, TArray<int32>& OutIndices) const;

	// Compute overlap ratio between two boxes (overlap volume / smallest volume)
	static double ComputeOverlapRatio(const FBox& BoxA, const FBox& BoxB);
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExMatchOverlapFactory : public UPCGExMatchRuleFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExMatchOverlapConfig Config;

	virtual bool WantsPoints() const override;

	virtual TSharedPtr<FPCGExMatchRuleOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="DataMatch", meta=(PCGExNodeLibraryDoc="utilities/data-matching/match-overlap"))
class UPCGExCreateMatchOverlapSettings : public UPCGExMatchRuleFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(MatchOverlap, "Match : Overlap", "Check if there is an overlap with the data bounds (AABB)", FName(GetDisplayName()))

#endif
	//~End UPCGSettings

	/** Rules properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExMatchOverlapConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
