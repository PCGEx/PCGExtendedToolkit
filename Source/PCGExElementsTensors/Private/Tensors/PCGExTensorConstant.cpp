// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Tensors/PCGExTensorConstant.h"

#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExTensorOperation.h"

#define LOCTEXT_NAMESPACE "PCGExCreateTensorConstant"
#define PCGEX_NAMESPACE CreateTensorConstant

bool FPCGExTensorConstant::Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory)
{
	if (!PCGExTensorOperation::Init(InContext, InFactory))
	{
		return false;
	}
	return true;
}

PCGExTensor::FTensorSample FPCGExTensorConstant::Sample(const int32 InSeedIndex, const FTransform& InProbe) const
{
	PCGExTensor::FEffectorSamples Samples = PCGExTensor::FEffectorSamples();

	Samples.Emplace_GetRef(Config.Direction, Config.PotencyValue.Constant, Config.WeightValue.Constant);

	return Config.Mutations.Mutate(InProbe, Samples.Flatten(Config.TensorWeight));
}

PCGEX_TENSOR_BOILERPLATE(
	Constant,
	{
	NewFactory->Config.Mutations = Mutations;
	NewFactory->Config.Direction = Direction;
	NewFactory->Config.PotencyValue.Constant = Potency;
	NewFactory->Config.PotencyValue.Input = EPCGExInputValueType::Constant;
	NewFactory->Config.WeightValue.Constant = 1;
	NewFactory->Config.TensorWeight = TensorWeight;
	NewFactory->Config.WeightValue.Input = EPCGExInputValueType::Constant;
	},
	{})

PCGExFactories::EPreparationResult UPCGExTensorConstantFactory::InitInternalData(FPCGExContext* InContext)
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
