// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWriteIndex.h"

#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Types/PCGExTypes.h"

#define LOCTEXT_NAMESPACE "PCGExWriteIndexElement"
#define PCGEX_NAMESPACE WriteIndex

void UPCGExWriteIndexSettings::TagPointIO(const TSharedPtr<PCGExData::FPointIO>& InPointIO, const double MaxNumEntries) const
{
	if (bOutputCollectionIndex && bOutputCollectionIndexToTags)
	{
		InPointIO->Tags->Set<int32>(CollectionIndexAttributeName.ToString(), InPointIO->IOIndex);
	}

	if (bOutputCollectionNumEntries && bOutputNumEntriesToTags)
	{
		if (bNormalizeNumEntries)
		{
			InPointIO->Tags->Set<double>(NumEntriesAttributeName.ToString(), static_cast<double>(InPointIO->GetNum()) / MaxNumEntries);
		}
		else
		{
			InPointIO->Tags->Set<int32>(NumEntriesAttributeName.ToString(), InPointIO->GetNum());
		}
	}
}

void UPCGExWriteIndexSettings::TagData(const int32 Index, FPCGTaggedData& InTaggedData, double NumEntries, double MaxNumEntries) const
{
	if (bOutputCollectionIndex && bOutputCollectionIndexToTags)
	{
		const TSharedPtr<PCGExData::TDataValue<int32>> Tag = MakeShared<PCGExData::TDataValue<int32>>(Index);
		InTaggedData.Tags.Add(Tag->Flatten(CollectionIndexAttributeName.ToString()));
	}

	if (bOutputCollectionNumEntries && bOutputNumEntriesToTags)
	{
		if (bNormalizeNumEntries)
		{
			const TSharedPtr<PCGExData::TDataValue<double>> Tag = MakeShared<PCGExData::TDataValue<double>>(NumEntries / MaxNumEntries);
			InTaggedData.Tags.Add(Tag->Flatten(NumEntriesAttributeName.ToString()));
		}
		else
		{
			const TSharedPtr<PCGExData::TDataValue<int32>> Tag = MakeShared<PCGExData::TDataValue<int32>>(NumEntries);
			InTaggedData.Tags.Add(Tag->Flatten(NumEntriesAttributeName.ToString()));
		}
	}
}

bool UPCGExWriteIndexSettings::CollectionLevelOutputOnly() const
{
	return (!bOutputPointIndex) && (!bOutputCollectionNumEntries || PCGExMetaHelpers::IsDataDomainAttribute(NumEntriesAttributeName)) && (!bOutputCollectionIndex || PCGExMetaHelpers::IsDataDomainAttribute(CollectionIndexAttributeName));
}

bool UPCGExWriteIndexSettings::HasDynamicPins() const
{
	return IsInputless();
}

TArray<FPCGPinProperties> UPCGExWriteIndexSettings::InputPinProperties() const
{
	if (!IsInputless())
	{
		return Super::InputPinProperties();
	}
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainInputPin(), "Inputs", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExWriteIndexSettings::OutputPinProperties() const
{
	if (!IsInputless())
	{
		return Super::OutputPinProperties();
	}
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainOutputPin(), "Output", Required)
	return PinProperties;
}

#if WITH_EDITOR
FString UPCGExWriteIndexSettings::GetDisplayName() const
{
	return bOutputPointIndex ? OutputAttributeName.ToString() : bOutputCollectionIndex ? CollectionIndexAttributeName.ToString() : bOutputCollectionNumEntries ? NumEntriesAttributeName.ToString() : TEXT("...");
}
#endif

PCGEX_INITIALIZE_ELEMENT(WriteIndex)

PCGExData::EIOInit UPCGExWriteIndexSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(WriteIndex)

bool FPCGExWriteIndexElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(WriteIndex)

	Context->bCollectionLevelOutputOnly = Settings->CollectionLevelOutputOnly();

	bool bTagOnly = true;

	if (Settings->bOutputPointIndex)
	{
		PCGEX_VALIDATE_NAME(Settings->OutputAttributeName)
		Context->EntryIndexIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->OutputAttributeName);
	}

	if (Settings->bOutputCollectionIndex && !Settings->bOutputCollectionIndexToTags)
	{
		PCGEX_VALIDATE_NAME(Settings->CollectionIndexAttributeName)
		Context->CollectionIndexIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->CollectionIndexAttributeName);
		bTagOnly = false;
	}

	if (Settings->bOutputCollectionNumEntries)
	{
		if (!Settings->bNormalizeNumEntries)
		{
			PCGEX_VALIDATE_NAME(Settings->NumEntriesAttributeName)
		}
		Context->NumEntriesIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->NumEntriesAttributeName);
		bTagOnly = false;
	}

	if (Context->bCollectionLevelOutputOnly)
	{
		Context->WorkingData = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
		Context->NumEntries.Reserve(Context->WorkingData.Num());

		for (FPCGTaggedData& TaggedData : Context->WorkingData)
		{
			if (!bTagOnly)
			{
				TaggedData.Data = TaggedData.Data->DuplicateData(Context);
			}

			if (const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data))
			{
				TUniquePtr<FPCGAttributeAccessorKeysEntries> Keys = MakeUnique<FPCGAttributeAccessorKeysEntries>(ParamData->Metadata);
				Context->MaxNumEntries = FMath::Max(Context->MaxNumEntries, Context->NumEntries.Emplace_GetRef(Keys->GetNum()));
				continue;
			}

			if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(TaggedData.Data))
			{
				Context->MaxNumEntries = FMath::Max(Context->MaxNumEntries, Context->NumEntries.Emplace_GetRef(SplineData->GetNumSegments()));
				continue;
			}

			if (const UPCGBasePointData* BasePointData = Cast<UPCGBasePointData>(TaggedData.Data))
			{
				Context->MaxNumEntries = FMath::Max(Context->MaxNumEntries, Context->NumEntries.Emplace_GetRef(BasePointData->GetNumPoints()));
				continue;
			}

			Context->NumEntries.Emplace(0);
		}
	}
	else
	{
		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
		{
			Context->MaxNumEntries = FMath::Max(Context->MaxNumEntries, IO->GetNum());
		}
	}


	return true;
}

bool FPCGExWriteIndexElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteIndexElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WriteIndex)
	PCGEX_EXECUTION_CHECK

	if (!Context->bCollectionLevelOutputOnly)
	{
		PCGEX_ON_INITIAL_EXECUTION
		{
			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
					NewBatch->bSkipCompletion = true;
				}))
			{
				return Context->CancelExecution(TEXT("Could not find any points to process."));
			}
		}

		PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

		Context->MainPoints->StageOutputs();
		Context->Done();
	}
	else
	{
		if (Settings->bOutputCollectionIndex && !Settings->bOutputCollectionIndexToTags)
		{
			PCGExMetaHelpers::ExecuteWithRightType(PCGExData::Helpers::GetNumericType(Settings->CollectionIndexOutputType), [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				for (int i = 0; i < Context->WorkingData.Num(); i++)
				{
					PCGExData::WriteMark<T>(
						const_cast<UPCGData*>(Context->WorkingData[i].Data.Get()),
						Context->CollectionIndexIdentifier,
						PCGExTypeOps::FTypeOps<T>::template ConvertFrom<int32>(i));
				}
			});
		}

		if (Settings->bOutputCollectionNumEntries && !Settings->bOutputNumEntriesToTags)
		{
			PCGExMetaHelpers::ExecuteWithRightType(PCGExData::Helpers::GetNumericType(Settings->NumEntriesOutputType), [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				const PCGExTypeOps::ITypeOpsBase* TypeOps = PCGExTypeOps::FTypeOpsRegistry::Get<T>();

				for (int i = 0; i < Context->WorkingData.Num(); i++)
				{
					double NumEntries = Settings->bNormalizeNumEntries ? Context->NumEntries[i] / Context->MaxNumEntries : Context->NumEntries[i];
					PCGExData::WriteMark<T>(
						const_cast<UPCGData*>(Context->WorkingData[i].Data.Get()),
						Context->NumEntriesIdentifier,
						PCGExTypeOps::FTypeOps<T>::template ConvertFrom<double>(NumEntries));
				}
			});
		}

		for (int i = 0; i < Context->WorkingData.Num(); i++)
		{
			FPCGTaggedData& TaggedData = Context->WorkingData[i];
			Settings->TagData(i, TaggedData, Context->NumEntries[i], Context->MaxNumEntries);
			Context->StageOutput(
				const_cast<UPCGData*>(TaggedData.Data.Get()), Settings->GetMainOutputPin(),
				PCGExData::EStaging::None, TaggedData.Tags);
		}

		Context->Done();
	}


	return Context->TryComplete();
}

namespace PCGExWriteIndex
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteIndex::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		NumPoints = PointDataFacade->GetNum();
		MaxIndex = NumPoints - 1;

		Settings->TagPointIO(PointDataFacade->Source, Context->MaxNumEntries);

		if (Settings->bOutputCollectionIndex && !Settings->bOutputCollectionIndexToTags)
		{
			PCGExMetaHelpers::ExecuteWithRightType(PCGExData::Helpers::GetNumericType(Settings->CollectionIndexOutputType), [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				PCGExData::WriteMark<T>(PointDataFacade->GetOut(), Context->CollectionIndexIdentifier, PCGExTypeOps::Convert<int32, T>(BatchIndex));
			});
		}

		if (Settings->bOutputCollectionNumEntries && !Settings->bOutputNumEntriesToTags)
		{
			PCGExMetaHelpers::ExecuteWithRightType(PCGExData::Helpers::GetNumericType(Settings->NumEntriesOutputType), [&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				if (Settings->bNormalizeNumEntries)
				{
					PCGExData::WriteMark<T>(PointDataFacade->GetOut(), Context->NumEntriesIdentifier, PCGExTypeOps::Convert<double, T>(static_cast<double>(PointDataFacade->GetNum()) / Context->MaxNumEntries));
				}
				else
				{
					PCGExData::WriteMark<T>(PointDataFacade->GetOut(), Context->NumEntriesIdentifier, PCGExTypeOps::Convert<int32, T>(PointDataFacade->GetNum()));
				}
			});
		}

		if (Settings->bOutputPointIndex)
		{
			if (Settings->bNormalizedEntryIndex)
			{
				DoubleWriter = PointDataFacade->GetWritable<double>(Context->EntryIndexIdentifier, -1, Settings->bAllowInterpolation, PCGExData::EBufferInit::Inherit);
				if (Settings->bOneMinus)
				{
					PCGEX_PARALLEL_FOR(PointDataFacade->GetNum(), DoubleWriter->SetValue(i, 1 - (static_cast<double>(i) / MaxIndex));)
				}
				else
				{
					PCGEX_PARALLEL_FOR(PointDataFacade->GetNum(), DoubleWriter->SetValue(i, static_cast<double>(i) / MaxIndex);)
				}
			}
			else
			{
				IntWriter = PointDataFacade->GetWritable<int32>(Context->EntryIndexIdentifier, -1, Settings->bAllowInterpolation, PCGExData::EBufferInit::Inherit);
				if (Settings->bOneMinus)
				{
					PCGEX_PARALLEL_FOR(PointDataFacade->GetNum(), IntWriter->SetValue(i, MaxIndex - i);)
				}
				else
				{
					PCGEX_PARALLEL_FOR(PointDataFacade->GetNum(), IntWriter->SetValue(i, i);)
				}
			}
		}

		PointDataFacade->WriteFastest(TaskManager);

		return true;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
