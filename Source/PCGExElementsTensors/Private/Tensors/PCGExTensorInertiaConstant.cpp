// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorInertiaConstant.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"


#define LOCTEXT_NAMESPACE "PCGExCreateTensorInertiaConstant"
#define PCGEX_NAMESPACE CreateTensorInertiaConstant

bool FPCGExTensorInertiaConstant::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}
	Offset = Config.Offset.Quaternion();
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorInertiaConstant::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	if (Config.bSetInertiaOnce)
	{
		Samples.Emplace_GetRef(PCGExMath::GetDirection(PrimaryDataFacade->GetIn()->GetTransform(InSeedIndex).GetRotation() * Offset, Config.Axis), Config.PotencyValue.Constant, Config.WeightValue.Constant);
	}
	else
	{
		Samples.Emplace_GetRef(PCGExMath::GetDirection(InProbe.GetRotation() * Offset, Config.Axis), Config.PotencyValue.Constant, Config.WeightValue.Constant);
	}


	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGEX_TENSOR_BOILERPLATE(
	InertiaConstant,
	{
	NewFactory->Config.Axis = Axis;
	NewFactory->Config.Offset = Offset;
	NewFactory->Config.PotencyValue.Constant = Potency;
	NewFactory->Config.PotencyValue.Input = EPCGExInputValueType::Constant;
	NewFactory->Config.WeightValue.Constant = 1;
	NewFactory->Config.TensorWeight = TensorWeight;
	NewFactory->Config.WeightValue.Input = EPCGExInputValueType::Constant;
	NewFactory->Config.bSetInertiaOnce = bSetInertiaOnce;
	},
	{})

PCGExFactories::EPreparationResult UPCGExTensorInertiaConstantFactory::InitInternalData(FPCGExContext* InContext)
{
	if (Config.PotencyValue.Input == EPCGExInputValueType::Attribute)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attribute-driven Potency is not supported on Constant Tensor."));
		return PCGExFactories::EPreparationResult::Fail;
	}

	if (Config.WeightValue.Input == EPCGExInputValueType::Attribute)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attribute-driven Weight is not supported on Constant Tensor."));
		return PCGExFactories::EPreparationResult::Fail;
	}

	return Super::InitInternalData(InContext);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
