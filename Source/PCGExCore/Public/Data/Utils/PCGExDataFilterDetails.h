// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"
#include "Metadata/PCGMetadataCommon.h"

#include "PCGExDataFilterDetails.generated.h"

namespace PCGExData
{
	class FPointIO;
}

class FPCGMetadataAttributeBase;

namespace PCGExData
{
	struct FAttributeIdentity;
	class FAttributesInfos;
}

class UPCGMetadata;
enum class EPCGMetadataTypes : uint8;

UENUM()
enum class EPCGExAttributeFilter : uint8
{
	All     = 0 UMETA(DisplayName = "All", ToolTip="All elements", ActionIcon="All"),
	Exclude = 1 UMETA(DisplayName = "Exclude", ToolTip="Discard listed elements, keep the others", ActionIcon="Exclude"),
	Include = 2 UMETA(DisplayName = "Include", ToolTip="Keep listed elements, discard the others", ActionIcon="Include"),
};

namespace PCGExDataFilter
{
	namespace Helpers
	{
		void PCGEXCORE_API GetAttributes(const UPCGMetadata* InMetadata, EPCGExAttributeDomainScope InScope, TArray<FPCGAttributeIdentifier>& Identifiers);
	}
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExNameFiltersDetails
{
	GENERATED_BODY()

	FPCGExNameFiltersDetails() = default;

	explicit FPCGExNameFiltersDetails(const bool FilterToRemove)
		: bFilterToRemove(FilterToRemove)
	{
	}

	bool bFilterToRemove = false;

	/** How the names are processed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExAttributeFilter FilterMode = EPCGExAttributeFilter::All;

	/** List of matches that will be checked. Any success is a pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="FilterMode != EPCGExAttributeFilter::All", EditConditionHides))
	TMap<FString, EPCGExStringMatchMode> Matches;

	/** A list of names separated by a comma, for easy overrides. The limitation is that they all use the same shared filter mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString CommaSeparatedNames;

	/** Unique filter mode applied to comma separated names */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="FilterMode != EPCGExAttributeFilter::All", EditConditionHides))
	EPCGExStringMatchMode CommaSeparatedNameFilter = EPCGExStringMatchMode::Equals;

	/** If enabled, PCGEx attributes & tags won't be affected.  Cluster-related nodes rely on these to work! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bPreservePCGExData = true;

	void Init();

	bool Test(const FString& Name) const;
	bool Test(const FPCGMetadataAttributeBase* InAttribute) const;

	void Prune(TArray<FString>& Names, bool bInvert = false) const;
	void Prune(TSet<FName>& Names, bool bInvert = false) const;
	void Prune(PCGExData::FAttributesInfos& InAttributeInfos, const bool bInvert = false) const;
};

namespace PCGExDataFilter
{
	// Centralizes the 'bEnabled ? &Filters : nullptr' idiom for opt-in name filters, repeated at every
	// merge call site that forwards a TagsToAttributes filter to FPCGExPointIOMerger / FMergeList.
	FORCEINLINE const FPCGExNameFiltersDetails* ResolveOptional(const bool bEnabled, const FPCGExNameFiltersDetails& Filters)
	{
		return bEnabled ? &Filters : nullptr;
	}
}

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/data-utils/attribute-gather-details"))
struct PCGEXCORE_API FPCGExAttributeGatherDetails : public FPCGExNameFiltersDetails
{
	GENERATED_BODY()

	FPCGExAttributeGatherDetails();

	// TODO : Expose how to handle overlaps
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExCarryOverDetails
{
	GENERATED_BODY()

	FPCGExCarryOverDetails()
	{
	}

	explicit FPCGExCarryOverDetails(const bool InUsedForCleanup)
		:bUsedForCleanup(InUsedForCleanup)
	{
	}
	
	UPROPERTY()
	bool bUsedForCleanup = false;
		
	/** If enabled, will preserve the initial attribute default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="!bUsedForCleanup", EditConditionHides, HideEditConditionToggle))
	bool bPreserveAttributesDefaultValue = false;

	/** Attributes to carry over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails Attributes = FPCGExNameFiltersDetails(false);

	/** If enabled, data domain attributes are promoted to the elements domain (the single @Data value is
	 * broadcast to every point). Off by default; Merge Points is the node that enables it. Not used by all nodes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName=" ├─ Data domain to Elements", EditCondition="!bUsedForCleanup", EditConditionHides, HideEditConditionToggle))
	bool bDataDomainToElements = false;

	/** Scope */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName=" └─ Scope", EditCondition="bUsedForCleanup", EditConditionHides, HideEditConditionToggle))
	EPCGExAttributeDomainScope Scope = EPCGExAttributeDomainScope::Any;

	/** Tags to carry over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails Tags = FPCGExNameFiltersDetails(false);

	/** If enabled, will test full tag with its value ('Tag:Value'), otherwise only test the left part ignoring the right `:Value` ('Tag'). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName=" └─ Flatten tag value", EditCondition="!bUsedForCleanup", EditConditionHides, HideEditConditionToggle))
	bool bTestTagsWithValues = false;

	void Init();

	void Prune(TSet<FString>& InValues) const;
	void Prune(TArray<FString>& InValues) const;
	void Prune(const PCGExData::FPointIO* PointIO) const;
	void Prune(TArray<PCGExData::FAttributeIdentity>& Identities) const;
	void Prune(PCGExData::FTags* InTags) const;

	bool Test(const PCGExData::FPointIO* PointIO) const;
	bool Test(PCGExData::FTags* InTags) const;

	bool GetPrunableIdentifiers(const UPCGMetadata* Metadata, TArray<FPCGAttributeIdentifier>& Identifiers) const;
	void Prune(UPCGMetadata* Metadata) const;
	

	bool Test(const UPCGMetadata* Metadata) const;
};
