// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertyEdgeAngle.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Sampling/PCGExSamplingHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExVtxPropertyEdgeAngle"
#define PCGEX_NAMESPACE PCGExVtxPropertyEdgeAngle

FPCGExEdgeAngleConfig::FPCGExEdgeAngleConfig()
{
	UpVector.Constant = PCGEX_CORE_SETTINGS.WorldUp;
}

bool FPCGExEdgeAngleConfig::Validate(FPCGExContext* InContext) const
{
	PCGEX_VALIDATE_NAME_C(InContext, AngleAttributeName)
	return true;
}

bool FPCGExVtxPropertyEdgeAngle::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!FPCGExVtxPropertyOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!Config.Validate(InContext))
	{
		bIsValidOperation = false;
		return false;
	}

	UpCache = Config.UpVector.GetValueSetting();
	if (!UpCache->Init(InVtxDataFacade, false))
	{
		bIsValidOperation = false;
		return false;
	}

	UpMultiplier = Config.UpVector.bFlip ? -1 : 1;

	AngleBuffer = InVtxDataFacade->GetWritable<double>(Config.AngleAttributeName, 0, true, PCGExData::EBufferInit::New);

	return bIsValidOperation;
}

void FPCGExVtxPropertyEdgeAngle::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
	const int32 NumAdjacency = Adjacency.Num();

	if (NumAdjacency <= 1)
	{
		AngleBuffer->SetValue(Node.PointIndex, Config.LeavesFallback);
		return;
	}

	const FVector Up = UpCache->Read(Node.PointIndex) * UpMultiplier;

	// Adjacency directions are neighbor->node, but dot & cross are invariant when both operands are
	// negated, so pairwise angles match the outgoing-edge convention: PI = straight-through, 0 = folded.
	auto PairAngle = [&](const int32 A, const int32 B)
	{
		const FVector& DirA = Adjacency[A].Direction;
		const FVector& DirB = Adjacency[B].Direction;
		const FVector Cross = FVector::CrossProduct(DirA, DirB);
		return PCGExSampling::Helpers::MapAngle(Config.AngleRange, FMath::Atan2(Cross.Size(), FVector::DotProduct(DirA, DirB)), Cross.Dot(Up) < 0);
	};

	double OutAngle = 0;

	if (NumAdjacency == 2)
	{
		OutAngle = PairAngle(0, 1);
	}
	else
	{
		double Min = TNumericLimits<double>::Max();
		double Max = TNumericLimits<double>::Lowest();
		double Sum = 0;
		int32 NumPairs = 0;

		for (int32 i = 0; i < NumAdjacency - 1; i++)
		{
			for (int32 j = i + 1; j < NumAdjacency; j++)
			{
				const double Angle = PairAngle(i, j);
				Min = FMath::Min(Min, Angle);
				Max = FMath::Max(Max, Angle);
				Sum += Angle;
				NumPairs++;
			}
		}

		switch (Config.NonBinaryAggregation)
		{
		default:
		case EPCGExEdgeAngleAggregation::Min:
			OutAngle = Min;
			break;
		case EPCGExEdgeAngleAggregation::Max:
			OutAngle = Max;
			break;
		case EPCGExEdgeAngleAggregation::Average:
			OutAngle = Sum / NumPairs;
			break;
		}
	}

	AngleBuffer->SetValue(Node.PointIndex, OutAngle);
}

#if WITH_EDITOR
FString UPCGExVtxPropertyEdgeAngleSettings::GetDisplayName() const
{
	return Config.AngleAttributeName.ToString();
}
#endif

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertyEdgeAngleFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertyEdgeAngle)
	PCGEX_VTX_EXTRA_CREATE
	return NewOperation;
}

void UPCGExVtxPropertyEdgeAngleFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.UpVector.RegisterBufferDependencies(InContext, FacadePreloader);
}

UPCGExFactoryData* UPCGExVtxPropertyEdgeAngleSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExVtxPropertyEdgeAngleFactory* NewFactory = InContext->ManagedObjects->New<UPCGExVtxPropertyEdgeAngleFactory>();
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
