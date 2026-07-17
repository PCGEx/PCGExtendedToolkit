// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Heuristics/PCGExHeuristicTensor.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExTensor.h"
#include "Core/PCGExTensorFactoryProvider.h"
#include "Math/PCGExMath.h"


void FPCGExHeuristicTensor::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);
	TensorsHandler = MakeShared<PCGExTensor::FTensorsHandler>(TensorHandlerDetails);
	TensorsHandler->Init(Context, *TensorFactories, PrimaryDataFacade);
}

double FPCGExHeuristicTensor::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(GetDot(From.PointIndex, Cluster->GetPos(From), Cluster->GetPos(Goal)));
}

double FPCGExHeuristicTensor::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	return GetScoreInternal(GetDot(From.PointIndex, Cluster->GetPos(From), Cluster->GetPos(To)));
}

double FPCGExHeuristicTensor::GetDot(const int32 InSeedIndex, const FVector& From, const FVector& To) const
{
	bool bSuccess = false;
	const PCGExTensor::FTensorSample Sample = TensorsHandler->Sample(InSeedIndex, FTransform(FRotationMatrix::MakeFromX((To - From).GetSafeNormal()).ToQuat(), From), bSuccess);
	if (!bSuccess)
	{
		return 0;
	}
	const double Dot = FVector::DotProduct((To - From).GetSafeNormal(), Sample.DirectionAndSize.GetSafeNormal());
	return bAbsoluteTensor ? 1 - FMath::Abs(Dot) : 1 - PCGExMath::Remap(Dot, -1, 1);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryTensor::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicTensor)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->bAbsoluteTensor = Config.bAbsolute;
	NewOperation->TensorHandlerDetails = Config.TensorHandlerDetails;
	NewOperation->TensorFactories = &TensorFactories;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Tensor, {})

PCGExFactories::EPreparationResult UPCGExHeuristicsFactoryTensor::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	if (!PCGExFactories::GetInputFactories(InContext, PCGExTensor::SourceTensorsLabel, TensorFactories, {FPCGExDataTypeInfoTensor::AsId()}))
	{
		return PCGExFactories::EPreparationResult::Fail;
	}
	if (TensorFactories.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing tensors."))
		return PCGExFactories::EPreparationResult::Fail;
	}
	return Result;
}

TArray<FPCGPinProperties> UPCGExHeuristicsTensorProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExTensor::SourceTensorsLabel, "Tensors fields to influence search", Required, FPCGExDataTypeInfoTensor::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExHeuristicsTensorProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryTensor* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryTensor>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
void UPCGExHeuristicsTensorProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.TensorHandlerDetails.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExHeuristicsTensorProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.TensorHandlerDetails.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExHeuristicsTensorProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
}
#endif
