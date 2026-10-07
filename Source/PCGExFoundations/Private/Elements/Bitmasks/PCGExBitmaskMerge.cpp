// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bitmasks/PCGExBitmaskMerge.h"

#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE BitmaskMerge

#pragma region UPCGSettings interface

TArray<FPCGPinProperties> UPCGExBitmaskMergeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAMS(FName("Bitmasks"), TEXT("Bitmask."), Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBitmaskMergeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(FName("Bitmask"), TEXT("Bitmask."), Required)
	return PinProperties;
}

FPCGElementPtr UPCGExBitmaskMergeSettings::CreateElement() const
{
	return MakeShared<FPCGExBitmaskMergeElement>();
}

#pragma endregion

bool FPCGExBitmaskMergeElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_SETTINGS_C(InContext, BitmaskMerge)

	TArray<FPCGTaggedData> InputParams = InContext->InputData.GetInputsByPin(FName("Bitmasks"));

	bool bInitialized = false;
	int64 OutputMask = 0;

	for (FPCGTaggedData& TaggedData : InputParams)
	{
		const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data);
		if (!ParamData)
		{
			continue;
		}

		const UPCGMetadata* Metadata = ParamData->Metadata;
		if (!Metadata)
		{
			continue;
		}

		const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(Metadata);

		for (int i = 0; i < Infos->Identities.Num(); i++)
		{
			const PCGExData::FAttributeIdentity& Identity = Infos->Identities[i];
			if (Identity.ValueType != EPCGMetadataTypes::Integer64 || !Identity.Attribute)
			{
				continue;
			}

			const int64 InputMask = PCGExData::Helpers::ReadDataValue<int64>(Identity.Attribute);

			if (!bInitialized)
			{
				bInitialized = true;
				OutputMask = InputMask;
				continue;
			}

			PCGExBitmask::Do(Settings->Operation, OutputMask, InputMask);
		}
	}

	UPCGParamData* Bitmask = FPCGContext::NewObject_AnyThread<UPCGParamData>(InContext);
	Bitmask->Metadata->CreateAttribute<int64>(FName("Bitmask"), OutputMask, false, true);
	Bitmask->Metadata->AddEntry();
	InContext->StageOutput(Bitmask, FName("Bitmask"), PCGExData::EStaging::Managed);

	InContext->Done();
	return InContext->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
