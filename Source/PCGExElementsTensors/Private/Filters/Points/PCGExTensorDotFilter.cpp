// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExTensorDotFilter.h"


#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"


#define LOCTEXT_NAMESPACE "PCGExTensorDotFilterDefinition"
#define PCGEX_NAMESPACE PCGExTensorDotFilterDefinition

bool UPCGExTensorDotFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	return PCGExFactories::GetInputFactories(InContext, PCGExTensor::SourceTensorsLabel, TensorFactories, {FPCGExDataTypeInfoTensor::AsId()});
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExTensorDotFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FTensorDotFilter>(this);
}

void UPCGExTensorDotFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<FVector>(InContext, Config.OperandA);
	Config.DotComparisonDetails.RegisterBuffersDependencies(InContext, FacadePreloader);
}

bool UPCGExTensorDotFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)
	PCGEX_CONSUMABLE_CONDITIONAL(Config.DotComparisonDetails.ThresholdInput == EPCGExInputValueType::Attribute, Config.DotComparisonDetails.ThresholdAttribute, Consumable)

	return true;
}

bool PCGExPointFilter::FTensorDotFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	TensorsHandler = MakeShared<PCGExTensor::FTensorsHandler>(TypedFilterFactory->Config.TensorHandlerDetails);
	if (!TensorsHandler->Init(InContext, TypedFilterFactory->TensorFactories, InPointDataFacade))
	{
		return false;
	}

	OperandA = PointDataFacade->GetBroadcaster<FVector>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);
	if (!OperandA)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	// TODO : Validate tensor factories

	InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

bool PCGExPointFilter::FTensorDotFilter::Test(const int32 PointIndex) const
{
	bool bSuccess = false;
	const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(PointIndex, InTransforms[PointIndex], bSuccess);

	if (!bSuccess)
	{
		return false;
	}

	return DotComparison.Test(FVector::DotProduct(TypedFilterFactory->Config.bTransformOperandA ? OperandA->Read(PointIndex) : InTransforms[PointIndex].TransformVectorNoScale(OperandA->Read(PointIndex)), Sample.DirectionAndSize.GetSafeNormal()), DotComparison.GetComparisonThreshold(PointIndex));
}

PCGEX_CREATE_FILTER_FACTORY(TensorDot)

TArray<FPCGPinProperties> UPCGExTensorDotFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExTensor::SourceTensorsLabel, "Tensors", Required, FPCGExDataTypeInfoTensor::AsId())
	return PinProperties;
}

#if WITH_EDITOR
FString UPCGExTensorDotFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + TEXT(" ⋅ Tensor");
	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
