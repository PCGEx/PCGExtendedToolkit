// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExTensorFactoryProvider.h"


#include "Data/PCGExData.h"
#include "Helpers/PCGExPropertyHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensor"
#define PCGEX_NAMESPACE CreateTensor

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoTensor, UPCGExTensorFactoryData)

TSharedPtr<PCGExTensorOperation> UPCGExTensorFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	return nullptr; // Create shape builder operation
}

PCGExFactories::EPreparationResult UPCGExTensorFactoryData::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}
	return InitInternalData(InContext);
}

PCGExFactories::EPreparationResult UPCGExTensorFactoryData::InitInternalData(FPCGExContext* InContext)
{
	return PCGExFactories::EPreparationResult::Success;
}

void UPCGExTensorFactoryData::InheritFromOtherTensor(const UPCGExTensorFactoryData* InOtherTensor)
{
	const TSet<FString> Exclusions = {TEXT("Points"), TEXT("Splines"), TEXT("ManagedSplines")};
	PCGExPropertyHelpers::CopyProperties(this, InOtherTensor, &Exclusions);
}

TArray<FPCGPinProperties> UPCGExTensorFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORY(PCGExTensor::SourceTensorConfigSourceLabel, "A tensor that already exist which settings will be used to override the settings of this one. This is to streamline re-using params between tensors, or to 'fake' the ability to transform tensors.", Advanced, FPCGExDataTypeInfoTensor::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExTensorFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	TArray<FPCGTaggedData> Collection = InContext->InputData.GetInputsByPin(PCGExTensor::SourceTensorConfigSourceLabel);
	const UPCGExTensorFactoryData* InTensorReference = Collection.IsEmpty() ? nullptr : Cast<UPCGExTensorFactoryData>(Collection[0].Data);
	if (InTensorReference)
	{
		Cast<UPCGExTensorFactoryData>(InFactory)->InheritFromOtherTensor(InTensorReference);
	}

	return Super::CreateFactory(InContext, InFactory);
}

PCGExFactories::EPreparationResult UPCGExTensorPointFactoryData::InitInternalData(FPCGExContext* InContext)
{
	PCGExFactories::EPreparationResult Result = Super::InitInternalData(InContext);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	if (!InitInternalFacade(InContext))
	{
		return PCGExFactories::EPreparationResult::Fail;
	}

	EffectorsArray = GetEffectorsArray();

	// Bulk of the work happens here
	if (!EffectorsArray->Init(InContext, this))
	{
		return PCGExFactories::EPreparationResult::Fail;
	}

	InputDataFacade->Flush(); // Flush cached buffers
	InputDataFacade.Reset();

	return Result;
}

TSharedPtr<PCGExTensor::FEffectorsArray> UPCGExTensorPointFactoryData::GetEffectorsArray() const
{
	return MakeShared<PCGExTensor::FEffectorsArray>();
}

bool UPCGExTensorPointFactoryData::InitInternalFacade(FPCGExContext* InContext)
{
	InputDataFacade = PCGExData::TryGetSingleFacade(InContext, PCGExTensor::SourceEffectorsLabel, false, true);
	if (!InputDataFacade)
	{
		return false;
	}
	return true;
}

TArray<FPCGPinProperties> UPCGExTensorPointFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExTensor::SourceEffectorsLabel, "Single point collection that represent individual effectors within that tensor", Required)
	return PinProperties;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
