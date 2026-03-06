// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <type_traits>

#include "CoreMinimal.h"
#include "Core/PCGExContext.h"
#include "Metadata/PCGMetadataCommon.h"
#include "PCGExCoreMacros.h"
#include "PCGParamData.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Types/PCGExAttributeIdentity.h"
#include "Types/PCGExTypeOpsImpl.h"

namespace PCGExAttributeMapHelpers
{
	template <typename T_KEY, typename T_VALUE>
	static int32 BuildMap(const UPCGMetadata* Metadata, const FPCGMetadataAttributeBase* KeyAttr, const FPCGMetadataAttributeBase* ValueAttr, TMap<T_KEY, T_VALUE>& OutMap)
	{
		if (!Metadata || !KeyAttr || !ValueAttr) { return 0; }

		TUniquePtr<FPCGAttributeAccessorKeysEntries> Keys = MakeUnique<FPCGAttributeAccessorKeysEntries>(KeyAttr->GetMetadataDomain());

		const int32 NumEntries = Keys->GetNum();
		if (NumEntries == 0) { return 0; }

		TUniquePtr<const IPCGAttributeAccessor> KeysAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(KeyAttr, KeyAttr->GetMetadataDomain(), true);
		if (!KeysAccessor) { return 0; }

		TUniquePtr<const IPCGAttributeAccessor> ValuesAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(ValueAttr, ValueAttr->GetMetadataDomain(), true);
		if (!ValuesAccessor) { return 0; }

		TArray<T_KEY> KeysArray;
		TArray<T_VALUE> ValuesArray;

		PCGExArrayHelpers::InitArray(KeysArray, NumEntries);
		PCGExArrayHelpers::InitArray(ValuesArray, NumEntries);

		if (!KeysAccessor->GetRange<T_KEY>(KeysArray, 0, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcast)) { return 0; }
		if (!ValuesAccessor->GetRange<T_VALUE>(ValuesArray, 0, *Keys.Get(), EPCGAttributeAccessorFlags::AllowBroadcast)) { return 0; }

		const int32 NumBefore = OutMap.Num();
		for (int i = 0; i < NumEntries; i++) { OutMap.Add(KeysArray[i], ValuesArray[i]); }

		return OutMap.Num() - NumBefore;
	}

	template <typename T_KEY, typename T_VALUE>
	static int32 BuildMap(FPCGExContext* InContext, const UPCGParamData* ParamData, const FName KeyId, const FName ValueId, TMap<T_KEY, T_VALUE>& OutMap)
	{
		const FPCGMetadataAttributeBase* KeyAttr = ParamData->Metadata->GetConstAttribute(KeyId);

		if (!KeyAttr)
		{
			if (InContext) { PCGEX_LOG_INVALID_ATTR_C(InContext, KeyId, KeyId) }
			return 0;
		}

		const FPCGMetadataAttributeBase* ValueAttr = ParamData->Metadata->GetConstAttribute(ValueId);

		if (!ValueAttr)
		{
			if (InContext) { PCGEX_LOG_INVALID_ATTR_C(InContext, ValueId, ValueId) }
			return 0;
		}

		return BuildMap(ParamData->Metadata, KeyAttr, ValueAttr, OutMap);
	}

	template <typename T_KEY, typename T_VALUE>
	static int32 BuildMap(FPCGExContext* InContext, FName Pin, TMap<T_KEY, T_VALUE>& OutMap)
	{
		TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(Pin);

		if (Inputs.IsEmpty()) { return 0; }
		int16 KeyType = PCGExTypes::TTraits<T_KEY>::TypeId;
		int16 ValueType = PCGExTypes::TTraits<T_VALUE>::TypeId;

		int32 NumTotal = 0;

		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data);
			if (!ParamData) { continue; }

			TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(ParamData->Metadata);
			if (!Infos || Infos->Attributes.IsEmpty()) { continue; }

			FPCGMetadataAttributeBase* KeyCandidate = nullptr;
			FPCGMetadataAttributeBase* ValueCandidate = nullptr;

			for (FPCGMetadataAttributeBase* Candidate : Infos->Attributes)
			{
				if (!Candidate) { continue; }

				if (!KeyCandidate && Candidate->GetTypeId() == KeyType)
				{
					KeyCandidate = Candidate;
					if (ValueCandidate) { break; }
					continue;
				}

				if (!ValueCandidate && Candidate->GetTypeId() == ValueType)
				{
					ValueCandidate = Candidate;
					if (KeyCandidate) { break; }
				}
			}

			if (!KeyCandidate || !ValueCandidate) { continue; }

			NumTotal += BuildMap(ParamData->Metadata, KeyCandidate, ValueCandidate, OutMap);
		}

		return NumTotal;
	}
}
