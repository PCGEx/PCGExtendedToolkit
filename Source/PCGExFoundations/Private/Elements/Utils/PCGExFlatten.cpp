// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExFlatten.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "FlattenElement"

TArray<FPCGPinProperties> UPCGExFlattenSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFlattenSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultOutputLabel, "", Required)
	return PinProperties;
}

FPCGElementPtr UPCGExFlattenSettings::CreateElement() const
{
	return MakeShared<FPCGExFlattenElement>();
}

bool FPCGExFlattenElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<UPCGData*> Copies;
	TArray<int32> Sources; // index into Inputs, for tag/pin propagation later
	Copies.Reserve(Inputs.Num());
	Sources.Reserve(Inputs.Num());

	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		const UPCGData* InData = Inputs[i].Data;
		if (!InData)
		{
			continue;
		}

		UPCGData* Copy = InData->DuplicateData(Context, /*bInitializeMetadata=*/true);
		if (!Copy)
		{
			continue;
		}

		Copies.Add(Copy);
		Sources.Add(i);
	}

	PCGExMT::ParallelOrSequential(Copies.Num(), [&](int32 i)
	{
		UPCGData* Copy = Copies[i];
		UPCGMetadata* Metadata = Copy->MutableMetadata();
		if (Metadata)
		{
			if (FPCGMetadataDomain* Domain = Metadata->GetDefaultMetadataDomain())
			{
				// Engine Flatten no-ops on a domain with 0 *local* attributes (FlattenAndCompress
				// bails on Attributes.IsEmpty()), leaving it parented and unsafe to duplicate from
				// another component. Giving it one owned attribute is the only thing that flips that.
				Domain->FindOrCreateAttribute<double>(FName("____FIXUP____"), 0.0, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/false);
				if (Domain->GetItemCountForChild() == 0)
				{
					Domain->AddEntry();
				}
			}
		}

		Copy->Flatten();

		if (Metadata)
		{
			Metadata->DeleteAttribute(FName("____FIXUP____"));
		}

		// The fixup attribute's removal can leave valid keys with no point attribute behind them, and the owning
		// component flattens again on graph output. See PCGExMetaHelpers::SanitizeMetadataEntries.
		PCGExMetaHelpers::SanitizeMetadataEntries(Cast<UPCGBasePointData>(Copy));
	}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	Context->OutputData.TaggedData.Reserve(Copies.Num());
	for (int32 i = 0; i < Copies.Num(); ++i)
	{
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Inputs[Sources[i]]);
		Output.Data = Copies[i];
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
