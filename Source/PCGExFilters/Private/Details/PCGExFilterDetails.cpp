// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExFilterDetails.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGEx
{
	// Converts key:value tag strings into typed metadata attributes.
	// ToData writes to the @Data domain, ToElements writes to per-element metadata.
	// Uses ExecuteWithRightType to dispatch the correct attribute type at runtime.
	void TagsToData(UPCGData* Data, const TSharedPtr<PCGExData::FTags>& Tags, const EPCGExTagsToDataAction Action)
	{
		if (Action == EPCGExTagsToDataAction::Ignore)
		{
			return;
		}

		if (Action == EPCGExTagsToDataAction::ToData)
		{
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& ValueTag : Tags->ValueTags)
			{
				PCGExMetaHelpers::ExecuteWithRightType(ValueTag.Value->GetTypeId(), [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<PCGExData::TDataValue<T>> TypedValue = StaticCastSharedPtr<PCGExData::TDataValue<T>>(ValueTag.Value);
					PCGExData::Helpers::SetDataValue<T>(Data, FName(ValueTag.Key), TypedValue->Value);
				});
			}
		}
		else if (Action == EPCGExTagsToDataAction::ToElements)
		{
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& ValueTag : Tags->ValueTags)
			{
				PCGExMetaHelpers::ExecuteWithRightType(ValueTag.Value->GetTypeId(), [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<PCGExData::TDataValue<T>> TypedValue = StaticCastSharedPtr<PCGExData::TDataValue<T>>(ValueTag.Value);
					Data->MutableMetadata()->FindOrCreateAttribute<T>(FName(ValueTag.Key), TypedValue->Value);
				});
			}
		}
	}

	void TagsToData(const TSharedPtr<PCGExData::FPointIO>& Data, const EPCGExTagsToDataAction Action)
	{
		if (Action == EPCGExTagsToDataAction::Ignore)
		{
			return;
		}
		TagsToData(Data->GetOut(), Data->Tags, Action);
	}
}

FPCGExFilterResultDetails::FPCGExFilterResultDetails(bool bTogglable, bool InEnabled)
	: bOptional(bTogglable)
	  , bEnabled(InEnabled)
{
}

bool FPCGExFilterResultDetails::Validate(FPCGExContext* InContext) const
{
	if (!bEnabled)
	{
		return true;
	}
	PCGEX_VALIDATE_NAME_C(InContext, ResultAttributeName);
	return true;
}

void FPCGExFilterResultDetails::Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	switch (Action)
	{
	case EPCGExResultWriteAction::Bool:
		BoolBuffer = InDataFacade->GetWritable<bool>(ResultAttributeName, false, true, PCGExData::EBufferInit::New);
		break;
	case EPCGExResultWriteAction::Counter:
		IncrementBuffer = InDataFacade->GetWritable<double>(ResultAttributeName, 0, true, PCGExData::EBufferInit::Inherit);
		break;
	case EPCGExResultWriteAction::Bitmask:
		BitmaskBuffer = InDataFacade->GetWritable<int64>(ResultAttributeName, 0, true, PCGExData::EBufferInit::Inherit);
		break;
	}
}

bool FPCGExFilterResultDetails::InitForData(UPCGData* InData)
{
	UPCGMetadata* Metadata = InData ? InData->MutableMetadata() : nullptr;
	if (!Metadata)
	{
		return false;
	}

	switch (Action)
	{
	case EPCGExResultWriteAction::Bool:
		BoolAttribute = Metadata->FindOrCreateAttribute<bool>(ResultAttributeName, false);
		return BoolAttribute != nullptr;
	case EPCGExResultWriteAction::Counter:
		IncrementAttribute = Metadata->FindOrCreateAttribute<double>(ResultAttributeName, 0);
		return IncrementAttribute != nullptr;
	case EPCGExResultWriteAction::Bitmask:
		BitmaskAttribute = Metadata->FindOrCreateAttribute<int64>(ResultAttributeName, 0);
		return BitmaskAttribute != nullptr;
	default:
		checkNoEntry();
		return false;
	}
}

void FPCGExFilterResultDetails::WriteToData(const TArray<int8>& Results) const
{
	const int32 NumRows = Results.Num();

	TArray<PCGMetadataEntryKey> Keys;
	Keys.SetNumUninitialized(NumRows);
	for (int i = 0; i < NumRows; i++)
	{
		Keys[i] = i;
	}

	if (Action == EPCGExResultWriteAction::Bool)
	{
		check(BoolAttribute)

		TArray<bool> Values;
		Values.SetNumUninitialized(NumRows);
		for (int i = 0; i < NumRows; i++)
		{
			Values[i] = static_cast<bool>(Results[i]);
		}

		BoolAttribute->SetValues(Keys, Values);
	}
	else if (Action == EPCGExResultWriteAction::Counter)
	{
		check(IncrementAttribute)

		TArray<double> Values;
		Values.SetNumUninitialized(NumRows);
		for (int i = 0; i < NumRows; i++)
		{
			Values[i] = IncrementAttribute->GetValueFromItemKey(Keys[i]) + (Results[i] ? PassIncrement : FailIncrement);
		}

		IncrementAttribute->SetValues(Keys, Values);
	}
	else if (Action == EPCGExResultWriteAction::Bitmask)
	{
		check(BitmaskAttribute)

		TArray<int64> Values;
		Values.SetNumUninitialized(NumRows);
		for (int i = 0; i < NumRows; i++)
		{
			int64 Flags = BitmaskAttribute->GetValueFromItemKey(Keys[i]);
			if (Results[i])
			{
				if (bDoBitmaskOpOnPass)
				{
					PassBitmask.Mutate(Flags);
				}
			}
			else if (bDoBitmaskOpOnFail)
			{
				FailBitmask.Mutate(Flags);
			}
			Values[i] = Flags;
		}

		BitmaskAttribute->SetValues(Keys, Values);
	}
}

void FPCGExFilterResultDetails::Write(const int32 Index, bool bPass) const
{
	switch (Action)
	{
	case EPCGExResultWriteAction::Bool:
		BoolBuffer->SetValue(Index, bPass);
		break;
	case EPCGExResultWriteAction::Counter:
		IncrementBuffer->SetValue(Index, IncrementBuffer->GetValue(Index) + (bPass ? PassIncrement : FailIncrement));
		break;
	case EPCGExResultWriteAction::Bitmask:
		if (bDoBitmaskOpOnFail && bDoBitmaskOpOnPass)
		{
			int64 Flags = BitmaskBuffer->GetValue(Index);
			if (bPass)
			{
				PassBitmask.Mutate(Flags);
			}
			else
			{
				FailBitmask.Mutate(Flags);
			}
			BitmaskBuffer->SetValue(Index, Flags);
		}
		else if (bDoBitmaskOpOnPass)
		{
			int64 Flags = BitmaskBuffer->GetValue(Index);
			if (bPass)
			{
				PassBitmask.Mutate(Flags);
				BitmaskBuffer->SetValue(Index, Flags);
			}
		}
		else if (bDoBitmaskOpOnFail)
		{
			int64 Flags = BitmaskBuffer->GetValue(Index);
			if (!bPass)
			{
				FailBitmask.Mutate(Flags);
				BitmaskBuffer->SetValue(Index, Flags);
			}
		}
		break;
	}
}

void FPCGExFilterResultDetails::Write(const PCGExMT::FScope& Scope, const TArray<int8>& Results) const
{
	if (Action == EPCGExResultWriteAction::Bool)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			BoolBuffer->SetValue(Index, static_cast<bool>(Results[Index]));
		}
	}
	else if (Action == EPCGExResultWriteAction::Counter)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			IncrementBuffer->SetValue(Index, IncrementBuffer->GetValue(Index) + (Results[Index] ? PassIncrement : FailIncrement));
		}
	}
	else if (Action == EPCGExResultWriteAction::Bitmask)
	{
		if (bDoBitmaskOpOnFail && bDoBitmaskOpOnPass)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);

				if (Results[Index])
				{
					PassBitmask.Mutate(Flags);
				}
				else
				{
					FailBitmask.Mutate(Flags);
				}

				BitmaskBuffer->SetValue(Index, Flags);
			}
		}
		else if (bDoBitmaskOpOnPass)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);
				if (Results[Index])
				{
					PassBitmask.Mutate(Flags);
					BitmaskBuffer->SetValue(Index, Flags);
				}
			}
		}
		else if (bDoBitmaskOpOnFail)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);
				if (!Results[Index])
				{
					FailBitmask.Mutate(Flags);
					BitmaskBuffer->SetValue(Index, Flags);
				}
			}
		}
	}
}

void FPCGExFilterResultDetails::Write(const PCGExMT::FScope& Scope, const TBitArray<>& Results) const
{
	if (Action == EPCGExResultWriteAction::Bool)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			BoolBuffer->SetValue(Index, Results[Index]);
		}
	}
	else if (Action == EPCGExResultWriteAction::Counter)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			IncrementBuffer->SetValue(Index, IncrementBuffer->GetValue(Index) + (Results[Index] ? PassIncrement : FailIncrement));
		}
	}
	else if (Action == EPCGExResultWriteAction::Bitmask)
	{
		if (bDoBitmaskOpOnFail && bDoBitmaskOpOnPass)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);

				if (Results[Index])
				{
					PassBitmask.Mutate(Flags);
				}
				else
				{
					FailBitmask.Mutate(Flags);
				}

				BitmaskBuffer->SetValue(Index, Flags);
			}
		}
		else if (bDoBitmaskOpOnPass)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);
				if (Results[Index])
				{
					PassBitmask.Mutate(Flags);
					BitmaskBuffer->SetValue(Index, Flags);
				}
			}
		}
		else if (bDoBitmaskOpOnFail)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				int64 Flags = BitmaskBuffer->GetValue(Index);
				if (!Results[Index])
				{
					FailBitmask.Mutate(Flags);
					BitmaskBuffer->SetValue(Index, Flags);
				}
			}
		}
	}
}
