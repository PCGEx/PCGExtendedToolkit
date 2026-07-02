// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExMetaCleanup.h"

#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExMetaCleanupElement"
#define PCGEX_NAMESPACE MetaCleanup

TArray<FPCGPinProperties> UPCGExMetaCleanupSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExMetaCleanupSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(MetaCleanup)


PCGExData::EIOInit UPCGExMetaCleanupSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

bool FPCGExMetaCleanupElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(MetaCleanup)

	PCGEX_FWD(Filters)
	Context->Filters.Init();

	return true;
}

bool FPCGExMetaCleanupElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExMetaCleanupElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(MetaCleanup)

	if (!Boot(Context))
	{
		return true;
	}

	const int32 NumInputs = Context->InputData.TaggedData.Num();
	Context->OutputData.TaggedData.Reserve(NumInputs);

	if (Context->Filters.Attributes.FilterMode == EPCGExAttributeFilter::All)
	{
		for (int i = 0; i < NumInputs; i++)
		{
			const FPCGTaggedData& InData = Context->InputData.TaggedData[i];
			FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
			OutData.Data = InData.Data;
			OutData.Pin = PCGPinConstants::DefaultOutputLabel;
			OutData.Tags = InData.Tags;

			Context->Filters.Prune(OutData.Tags);
		}
	}
	else
	{

		TArray<FPCGAttributeIdentifier> Identifiers;
		Identifiers.Reserve(12);

		switch (Settings->GetMainDataInitializationPolicy())
		{
		case PCGExData::EIOInit::NoInit:
		case PCGExData::EIOInit::New:
		case PCGExData::EIOInit::Duplicate:
			for (int i = 0; i < NumInputs; i++)
			{
				const FPCGTaggedData& InData = Context->InputData.TaggedData[i];

				if (Context->Filters.GetPrunableIdentifiers(InData.Data->ConstMetadata(), Identifiers))
				{
					UPCGData* NewOutData = InData.Data->DuplicateData(Context, true);
					UPCGMetadata* Metadata = NewOutData->MutableMetadata();

					for (const FPCGAttributeIdentifier& Identifier : Identifiers)
					{
						Metadata->DeleteAttribute(Identifier);
					}

					FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
					OutData.Data = NewOutData;
					OutData.Pin = PCGPinConstants::DefaultOutputLabel;
					OutData.Tags = InData.Tags;
					Context->Filters.Prune(OutData.Tags);
				}
				else
				{
					FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
					OutData.Data = InData.Data;
					OutData.Pin = PCGPinConstants::DefaultOutputLabel;
					OutData.Tags = InData.Tags;
					Context->Filters.Prune(OutData.Tags);
				}
			}
			break;
		case PCGExData::EIOInit::Forward:
			for (int i = 0; i < NumInputs; i++)
			{
				const FPCGTaggedData& InData = Context->InputData.TaggedData[i];
				UPCGData* NewOutData = const_cast<UPCGData*>(InData.Data.Get());

				FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
				OutData.Data = NewOutData;

				OutData.Pin = PCGPinConstants::DefaultOutputLabel;
				OutData.Tags = InData.Tags;
				Context->Filters.Prune(OutData.Tags);

				if (Context->Filters.GetPrunableIdentifiers(NewOutData->MutableMetadata(), Identifiers))
				{
					for (const FPCGAttributeIdentifier& Identifier : Identifiers)
					{
						NewOutData->Metadata->DeleteAttribute(Identifier);
					}
				}

			}
			break;
		}
	}

	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
