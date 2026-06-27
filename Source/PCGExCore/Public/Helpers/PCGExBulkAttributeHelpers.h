// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Data/PCGBasePointData.h"
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
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			return MakeShared<FPCGAttributeAccessorKeysPointIndices>(PointData);
		}
		if (InData->ConstMetadata())
		{
			return MakeShared<FPCGAttributeAccessorKeysEntries>(InData->ConstMetadata());
		}
		return nullptr;
	}

	/** TAttributeBroadcaster isn't an option here -- its FAttributeProcessingInfos::Init gates
	 *  attribute discovery on Cast<UPCGSpatialData>(InData), silently rejecting UPCGParamData. */
	template <typename T>
	void BulkReadRows(const UPCGData* InData, const FName AttributeName, TArray<T>& OutValues,
	                  const TSharedPtr<IPCGAttributeAccessorKeys>& InKeys = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExData::Helpers::BulkReadRows);

		OutValues.Reset();
		if (!InData) { return; }

		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(AttributeName.ToString());
		Selector = Selector.CopyAndFixLast(InData);

		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, Selector);
		if (!Accessor) { return; }

		const TSharedPtr<IPCGAttributeAccessorKeys> Keys = InKeys ? InKeys : GetKeys(InData);
		if (!Keys) { return; }

		const int32 NumValues = Keys->GetNum();
		if (NumValues <= 0) { return; }

		OutValues.SetNum(NumValues);
		if (!Accessor->GetRange<T>(OutValues, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			OutValues.Reset();
		}
	}

	inline void BulkReadSoftPaths(const UPCGData* InData, const FName AttributeName, TArray<FSoftObjectPath>& OutPaths)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExData::Helpers::BulkReadSoftPaths);

		const TSharedPtr<IPCGAttributeAccessorKeys> Keys = GetKeys(InData);
		if (!Keys)
		{
			OutPaths.Reset();
			return;
		}

		BulkReadRows<FSoftObjectPath>(InData, AttributeName, OutPaths, Keys);
		if (!OutPaths.IsEmpty()) { return; }

		// FString fallback for authored-as-string paths.
		TArray<FSoftObjectPath> Paths;
		BulkReadRows<FSoftObjectPath>(InData, AttributeName, Paths, Keys);
		if (Paths.IsEmpty()) { return; }

		OutPaths.SetNum(Paths.Num());
		for (int32 i = 0; i < Paths.Num(); i++)
		{
			OutPaths[i] = FSoftObjectPath(Paths[i]);
		}
	}
}
