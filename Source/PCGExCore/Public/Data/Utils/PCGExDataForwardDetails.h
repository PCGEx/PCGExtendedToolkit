// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include <type_traits>

#include "PCGExDataFilterDetails.h"
#include "Details/PCGExAttributesDetails.h"
#include "Types/PCGExTypeOps.h"

#include "PCGExDataForwardDetails.generated.h"

class UPCGData;

namespace PCGExData
{
	struct FConstPoint;
	class IAttributeBroadcaster;
	class FDataForwardHandler;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExForwardDetails : public FPCGExNameFiltersDetails
{
	GENERATED_BODY()

	FPCGExForwardDetails()
	{
	}

	explicit FPCGExForwardDetails(const bool InEnabled)
		: bEnabled(InEnabled)
	{
	}

	/** Is forwarding enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayPriority=0))
	bool bEnabled = false;

	/** If enabled, will preserve the initial attribute default value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayPriority=0, EditCondition="bEnabled"))
	bool bPreserveAttributesDefaultValue = false;

	void Filter(TArray<PCGExData::FAttributeIdentity>& Identities) const;

	TSharedPtr<PCGExData::FDataForwardHandler> GetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, bool bForwardToDataDomain = true) const;
	TSharedPtr<PCGExData::FDataForwardHandler> GetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, const TSharedPtr<PCGExData::FFacade>& InTargetDataFacade, bool bForwardToDataDomain = true) const;
	TSharedPtr<PCGExData::FDataForwardHandler> TryGetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, bool bForwardToDataDomain = true) const;
	TSharedPtr<PCGExData::FDataForwardHandler> TryGetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, const TSharedPtr<PCGExData::FFacade>& InTargetDataFacade, bool bForwardToDataDomain = true) const;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExAttributeToTagDetails
{
	GENERATED_BODY()

	FPCGExAttributeToTagDetails()
	{
	}

	/** Use reference point index to tag output data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bAddIndexTag = false;

	/** Prefix added to the reference point index */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bAddIndexTag"))
	FString IndexTagPrefix = TEXT("IndexTag");

	/** If enabled, prefix the attribute value with the attribute name  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bPrefixWithAttributeName = true;

#pragma region DEPRECATED
	// Pre-remapping selector list. Migrated to AttributeMappings in PostSerialize (see PCGExDataForwardDetails.cpp).
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	TArray<FPCGAttributePropertyInputSelector> Attributes_DEPRECATED;
#pragma endregion

	/** Attributes whose value is promoted to tags, each with an optional source->target output-name remap.
	 *  Leave the target empty to keep the source's resolved name (the pre-remapping behavior). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(TitleProperty="{Source}"))
	TArray<FPCGExAttributeSourceToTargetDetails> AttributeMappings;

	/** A list of selectors separated by a comma, for easy overrides. Will be appended to the existing array. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString CommaSeparatedAttributeSelectors;

	TSharedPtr<PCGExData::FFacade> SourceDataFacade;
	TArray<TSharedPtr<PCGExData::IAttributeBroadcaster>> Getters;

	/** Per-getter output-name override, index-aligned with Getters. NAME_None falls back to Getter->GetName()
	 *  (the resolved source name). Populated in Init from AttributeMappings' remapped entries. */
	TArray<FName> GetterNames;

	void PostSerialize(const FArchive& Ar);

	bool Init(const FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSourceFacade, const TSet<FName>* IgnoreAttributes = nullptr);

	// Raw-data variant: resolves getters directly against a UPCGData (point data OR attribute set), skipping
	// FFacade/FPointIO. SourceDataFacade is left null; Tag(...) is unaffected as it reads getters by row index.
	bool Init(const FPCGExContext* InContext, const UPCGData* InSourceData, const TSet<FName>* IgnoreAttributes = nullptr);

	void Tag(const PCGExData::FConstPoint& TagSource, TSet<FString>& InTags) const;
	void Tag(const PCGExData::FConstPoint& TagSource, const TSharedPtr<PCGExData::FPointIO>& PointIO) const;
	void Tag(const PCGExData::FConstPoint& TagSource, UPCGMetadata* InMetadata) const;

	// Format one promoted value as a data tag, matching Tag()'s per-getter rule: bools tag by presence;
	// everything else becomes "Name:Value" (or just "Value" when not prefixed); empty strings are skipped.
	// Shared so the broadcaster path and any raw @Data path (TryReadDataValue) produce identical tags.
	template <typename T>
	static void AppendValueTag(const FName InName, const T& InValue, const bool bPrefixWithAttributeName, TSet<FString>& OutTags)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			if (InValue) { OutTags.Add(InName.ToString()); }
		}
		else
		{
			const FString StringValue = PCGExTypeOps::Convert<T, FString>(InValue);
			if (StringValue.IsEmpty()) { return; }
			OutTags.Add(bPrefixWithAttributeName ? (InName.ToString() + TEXT(":") + StringValue) : StringValue);
		}
	}
};

template <>
struct TStructOpsTypeTraits<FPCGExAttributeToTagDetails> : public TStructOpsTypeTraitsBase2<FPCGExAttributeToTagDetails>
{
	enum
	{
		WithPostSerialize = true,
	};
};
