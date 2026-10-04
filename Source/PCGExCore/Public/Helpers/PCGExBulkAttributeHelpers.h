// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadataAttributeTraits.h" // EPCGMetadataTypes
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

class UPCGData;

namespace PCGExData::Helpers
{
	/** Cache the result when issuing multiple bulk reads on the same data --
	 *  FPCGAttributeAccessorKeysEntries walks every metadata entry up front. */
	inline TSharedPtr<IPCGAttributeAccessorKeys> GetKeys(const UPCGData* InData)
	{
		if (!InData) { return nullptr; }
		if (InData->IsA<UPCGBasePointData>())
		{
			return PCGExMetaHelpers::MakeConstKeys(InData);
		}
		if (InData->ConstMetadata())
		{
			return MakeShared<FPCGAttributeAccessorKeysEntries>(InData->ConstMetadata());
		}
		return nullptr;
	}

	/** Null when AttributeName resolves to nothing on InData. */
	inline TUniquePtr<const IPCGAttributeAccessor> MakeConstAccessor(const UPCGData* InData, const FName AttributeName)
	{
		if (!InData) { return nullptr; }

		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(AttributeName.ToString());
		Selector = Selector.CopyAndFixLast(InData);

		return PCGAttributeAccessorHelpers::CreateConstAccessor(InData, Selector);
	}

	/** The path a string names; null at NAME_SIZE characters or more, where FSoftObjectPath's package FName would assert. */
	template <typename T = FSoftObjectPath>
	T MakePathChecked(const FString& InString)
	{
		return InString.Len() < NAME_SIZE ? T(InString) : T();
	}

	/** TAttributeBroadcaster isn't an option here -- its FAttributeProcessingInfos::Init gates
	 *  attribute discovery on Cast<UPCGSpatialData>(InData), silently rejecting UPCGParamData. */
	template <typename T>
	void BulkReadRows(const UPCGData* InData, const FName AttributeName, TArray<T>& OutValues,
	                  const TSharedPtr<const IPCGAttributeAccessorKeys>& InKeys = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExData::Helpers::BulkReadRows);

		OutValues.Reset();

		const TUniquePtr<const IPCGAttributeAccessor> Accessor = MakeConstAccessor(InData, AttributeName);
		if (!Accessor) { return; }

		// Caller-supplied keys cover data whose element count isn't its metadata-entry count (spline control points).
		TSharedPtr<const IPCGAttributeAccessorKeys> Keys = InKeys;
		if (!Keys) { Keys = GetKeys(InData); }
		if (!Keys) { return; }

		const int32 NumValues = Keys->GetNum();
		if (NumValues <= 0) { return; }

		OutValues.SetNum(NumValues);

		if constexpr (std::is_base_of_v<FSoftObjectPath, T>)
		{
			// String rows become paths here, never in the accessor: its String -> path construction is unchecked.
			if (Accessor->GetUnderlyingType() == static_cast<int16>(EPCGMetadataTypes::String))
			{
				TArray<FString> Strings;
				Strings.SetNum(NumValues);
				if (!Accessor->GetRange<FString>(Strings, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					OutValues.Reset();
					return;
				}

				for (int32 i = 0; i < NumValues; i++) { OutValues[i] = MakePathChecked<T>(Strings[i]); }
				return;
			}
		}

		if (!Accessor->GetRange<T>(OutValues, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			OutValues.Reset();
		}
	}

	inline void BulkReadSoftPaths(const UPCGData* InData, const FName AttributeName, TArray<FSoftObjectPath>& OutPaths,
	                              const TSharedPtr<const IPCGAttributeAccessorKeys>& InKeys = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExData::Helpers::BulkReadSoftPaths);

		// Keys first: data with no metadata has none, and asking it for an accessor logs an engine error.
		TSharedPtr<const IPCGAttributeAccessorKeys> Keys = InKeys;
		if (!Keys) { Keys = GetKeys(InData); }
		if (!Keys)
		{
			OutPaths.Reset();
			return;
		}

		BulkReadRows<FSoftObjectPath>(InData, AttributeName, OutPaths, Keys);
		if (!OutPaths.IsEmpty()) { return; }

		// What only broadcasts to a string (names).
		TArray<FString> Strings;
		BulkReadRows<FString>(InData, AttributeName, Strings, Keys);
		if (Strings.IsEmpty()) { return; }

		OutPaths.SetNum(Strings.Num());
		for (int32 i = 0; i < Strings.Num(); i++) { OutPaths[i] = MakePathChecked(Strings[i]); }
	}

	/**
	 * Unique non-null soft paths read from AttributeName on every data of InPin, in first-seen order.
	 * With OutCompanions, the string read from CompanionAttribute on the same row comes along (empty when the data has
	 * no such attribute): OutCompanions then pairs with OutPaths, and a path is kept once per distinct companion.
	 * False when some data has rows but no readable attribute; an input with no rows is not an error.
	 */
	inline bool BulkReadUniqueSoftPaths(
		const FPCGDataCollection& InCollection, const FName InPin, const FName AttributeName, TArray<FSoftObjectPath>& OutPaths,
		const FName CompanionAttribute = NAME_None, TArray<FString>* OutCompanions = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExData::Helpers::BulkReadUniqueSoftPaths);

		OutPaths.Reset();
		if (OutCompanions) { OutCompanions->Reset(); }

		const bool bReadCompanions = OutCompanions && !CompanionAttribute.IsNone();

		bool bAllReadable = true;
		TArray<FSoftObjectPath> Paths;
		TArray<FString> Companions;
		TSet<TPair<FSoftObjectPath, FString>> Seen;

		for (const FPCGTaggedData& TaggedData : InCollection.TaggedData)
		{
			if (TaggedData.Pin != InPin || !TaggedData.Data) { continue; }

			const TSharedPtr<IPCGAttributeAccessorKeys> Keys = GetKeys(TaggedData.Data);
			if (!Keys || Keys->GetNum() <= 0) { continue; }

			BulkReadSoftPaths(TaggedData.Data, AttributeName, Paths, Keys);
			if (Paths.IsEmpty())
			{
				bAllReadable = false;
				continue;
			}

			Companions.Reset();
			if (bReadCompanions) { BulkReadRows<FString>(TaggedData.Data, CompanionAttribute, Companions, Keys); }
			const bool bHasCompanions = Companions.Num() == Paths.Num();

			for (int32 i = 0; i < Paths.Num(); i++)
			{
				if (Paths[i].IsNull()) { continue; }

				TPair<FSoftObjectPath, FString> Entry(Paths[i], bHasCompanions ? Companions[i] : FString());

				bool bAlreadySeen = false;
				Seen.Add(Entry, &bAlreadySeen);
				if (bAlreadySeen) { continue; }

				OutPaths.Add(Paths[i]);
				if (OutCompanions) { OutCompanions->Add(MoveTemp(Entry.Value)); }
			}
		}

		return bAllReadable;
	}
}
