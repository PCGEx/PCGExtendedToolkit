// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Blenders/PCGExUnionOpsManager.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Utils/PCGExAttributeCopy.h"

#include "UObject/Object.h"

class FPCGExIntTracker;
struct FPCGExCarryOverDetails;
struct FPCGExNameFiltersDetails;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FPointIO;
	class FFacade;
	class IDataValue;
}

namespace PCGExPointIOMerger
{
	struct PCGEXBLENDING_API FMergeScope
	{
		PCGExMT::FScope Read;
		PCGExMT::FScope Write;
		bool bReverse = false;
		TArrayView<int32> ReadIndices;

		FMergeScope() = default;
	};
}

class PCGEXBLENDING_API FPCGExPointIOMerger final : public TSharedFromThis<FPCGExPointIOMerger>
{
	friend class FPCGExAttributeMergeTask;

public:
	TArray<PCGExData::FAttributeIdentity> UniqueIdentities;
	TSharedRef<PCGExData::FFacade> UnionDataFacade;
	TArray<TSharedPtr<PCGExData::FPointIO>> IOSources;
	TArray<PCGExPointIOMerger::FMergeScope> Scopes;

	// Per-source tag values for names converted to attributes (entry size == IOSources.Num(), null where the
	// source lacks the tag); FCopyAttributeTask uses it as the per-source fallback. See MergeAsync.
	TMap<FName, TArray<TSharedPtr<PCGExData::IDataValue>>> TagValuesByName;

	explicit FPCGExPointIOMerger(const TSharedRef<PCGExData::FFacade>& InUnionDataFacade);
	~FPCGExPointIOMerger();

	PCGExPointIOMerger::FMergeScope& Append(const TSharedPtr<PCGExData::FPointIO>& InData, const PCGExMT::FScope ReadScope, const PCGExMT::FScope WriteScope);
	PCGExPointIOMerger::FMergeScope& Append(const TSharedPtr<PCGExData::FPointIO>& InData, const PCGExMT::FScope ReadScope);
	PCGExPointIOMerger::FMergeScope& Append(const TSharedPtr<PCGExData::FPointIO>& InData);
	void Append(const TArray<TSharedPtr<PCGExData::FPointIO>>& InData);
	void MergeAsync(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const FPCGExCarryOverDetails* InCarryOverDetails, const TSet<FName>* InIgnoredAttributes = nullptr, const bool bWriteUnion = false, const FPCGExNameFiltersDetails* InTagsToAttributes = nullptr);

	bool WantsDataToElements() const
	{
		return bDataDomainToElements;
	}

	bool WantsInitDefault() const
	{
		return bInitDefault;
	}

	TSharedPtr<FPCGExIntTracker> InternalTracker;

protected:
	bool bWriteFacade = false;
	void CopyProperties(const int32 Index);
	PCGExPointIOMerger::FMergeScope NullScope;
	bool bDataDomainToElements = false;
	// Merger-wide: whether output buffers should be initialized from each attribute's default value
	// (vs. a zero-init T{}). Sourced from FPCGExCarryOverDetails::bPreserveAttributesDefaultValue.
	bool bInitDefault = false;

	// Tag keys converted to attributes this merge; stripped from the merged data-domain tags before write.
	TSet<FName> ConvertedTagNames;
	int32 NumCompositePoints = 0;
	EPCGPointNativeProperties AllocateProperties = EPCGPointNativeProperties::None;

	// Utils
	int32 MaxNumElements = 0;
	TArray<int32> ReverseIndices;
};

namespace PCGExPointIOMerger
{
	template <typename T>
	static void ScopeMerge(const FMergeScope& Scope, const PCGExData::FAttributeIdentity& Identity, const TSharedPtr<PCGExData::FPointIO>& SourceIO, const TSharedPtr<PCGExData::TBuffer<T>>& OutBuffer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPointIOMerger::ScopeMerge);

		const FPCGMetadataAttributeBase* TypedInAttribute = PCGExMetaHelpers::TryGetConstAttribute<T>(SourceIO->GetIn()->Metadata, Identity.GetIdentifier());
		if (!TypedInAttribute)
		{
			return;
		}

		if (!ensureMsgf(
			TypedInAttribute->Name == Identity.Name,
			TEXT("[PCGEx] merge read guard: attribute '%s' on '%s' reports a mismatched internal name -- memory reclaimed/overwritten?"),
			*Identity.Name.ToString(), *GetNameSafe(SourceIO->GetIn())))
		{
			return;
		}

		// The source-domain x output-domain matrix lives in CopyAttributeScoped, shared with the blenders.
		PCGExBlending::CopyAttributeScoped<T>(
			PCGExBlending::FAttributeCopyScope{Scope.Read, Scope.Write, Scope.bReverse, Scope.ReadIndices},
			TypedInAttribute, SourceIO, OutBuffer);
	}
}
