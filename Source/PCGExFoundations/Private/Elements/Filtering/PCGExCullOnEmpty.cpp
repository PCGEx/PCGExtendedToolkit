// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExCullOnEmpty.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPolyLineData.h"
#include "Factories/PCGExFactories.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "CullOnEmptyElement"

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExCullOnEmptySettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Configs;
	Configs.Emplace(0, GetDefaultNodeTitle());
	Configs.Emplace(1, FTEXT("PCGEx | Is Data Empty"));
	Configs.Emplace(2, FTEXT("PCGEx | Is Data NOT Empty"));
	return Configs;
}

bool UPCGExCullOnEmptySettings::ShouldDrawNodeCompact() const
{
	return bCheckOnly;
}
#endif

void UPCGExCullOnEmptySettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	if (PreconfigureInfo.PreconfiguredIndex != 0)
	{
		bCheckOnly = true;
		bInvert = PreconfigureInfo.PreconfiguredIndex == 2;
	}
}

FPCGDataTypeIdentifier UPCGExCullOnEmptySettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (!InPin->IsOutputPin() || InPin->Properties.Label == PCGPinConstants::DefaultOutputLabel)
	{
		return Super::GetCurrentPinTypesID(InPin);
	}

	FPCGDataTypeIdentifier Id = FPCGDataTypeInfoParam::AsId();
	Id.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::Boolean);
	return Id;
}

TArray<FPCGPinProperties> UPCGExCullOnEmptySettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, FPCGDataTypeInfo::AsId());
	Pin.SetRequiredPin();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCullOnEmptySettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (!bCheckOnly)
	{
		(void)PinProperties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, FPCGDataTypeInfo::AsId());
	}
	if (bCheckOnly || bOutputIsEmpty)
	{
		PCGEX_PIN_PARAM(PCGExCullOnEmpty::IsEmptyName, "", Normal)
	}
	return PinProperties;
}

FPCGElementPtr UPCGExCullOnEmptySettings::CreateElement() const
{
	return MakeShared<FPCGExCullOnEmptyElement>();
}

namespace PCGExCullOnEmpty
{
	static bool IsDataNonEmpty(const UPCGData* Data)
	{
		if (!Data)
		{
			return false;
		}

		// Point data: check point count
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Data))
		{
			return PointData->GetNumPoints() > 0;
		}

		// Spline/polyline data: check segment count
		if (const UPCGPolyLineData* PolyLineData = Cast<UPCGPolyLineData>(Data))
		{
			return PolyLineData->GetNumSegments() > 0;
		}

		// Attribute set: check metadata entry count
		if (const UPCGParamData* ParamData = Cast<UPCGParamData>(Data))
		{
			const UPCGMetadata* Metadata = ParamData->ConstMetadata();
			return Metadata && Metadata->GetLocalItemCount() > 0;
		}

		// Other data types: valid if they exist
		return true;
	}
}

bool FPCGExCullOnEmptyElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	PCGEX_SETTINGS_C(Context, CullOnEmpty)

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	bool bHasValidData = false;

	if (Settings->bCheckOnly)
	{
		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			if (PCGExCullOnEmpty::IsDataNonEmpty(TaggedData.Data))
			{
				bHasValidData = true;
				break;
			}
		}
	}
	else
	{
		Context->OutputData.TaggedData.Reserve(Inputs.Num());
		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			if (PCGExCullOnEmpty::IsDataNonEmpty(TaggedData.Data))
			{
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(TaggedData);
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
				bHasValidData = true;
			}
		}

		if (!bHasValidData)
		{
			Context->OutputData.InactiveOutputPinBitmask |= 1ULL << 0;
		}
	}
	
	if (Settings->bInvert)
	{
		bHasValidData = !bHasValidData;
	}

	{
		if (Settings->bCheckOnly || Settings->bOutputIsEmpty)
		{
			UPCGParamData* ParamData = Context->NewObject_AnyThread<UPCGParamData>(Context);
			FPCGMetadataAttribute<bool>* Attr = ParamData->Metadata->CreateAttribute<bool>(Settings->OutputIsEmpty, !bHasValidData, true, true);
			Attr->SetValue(ParamData->Metadata->AddEntry(), !bHasValidData);

			FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
			OutData.Data = ParamData;
			OutData.Pin = PCGExCullOnEmpty::IsEmptyName;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
