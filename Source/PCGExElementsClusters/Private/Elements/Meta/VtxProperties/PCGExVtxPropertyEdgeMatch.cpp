// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertyEdgeMatch.h"

#include "PCGPin.h"
#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"


#define LOCTEXT_NAMESPACE "PCGExVtxPropertyEdgeMatch"
#define PCGEX_NAMESPACE PCGExVtxPropertyEdgeMatch

#if WITH_EDITOR
void FPCGExEdgeMatchConfig::ApplyDeprecation()
{
	DirectionValue.Update(DirectionInput_DEPRECATED, Direction_DEPRECATED, DirectionConstant_DEPRECATED);
	DirectionValue.bFlip = bInvertDirection_DEPRECATED;
}

void FPCGExEdgeMatchConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("DirectionConstant")), FName(TEXT("DirectionValue")), FName(TEXT("Constant")), FName(TEXT("Direction")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Direction")), FName(TEXT("DirectionValue")), FName(TEXT("Attribute")), FName(TEXT("Direction (Attr)")));
}
#endif

bool FPCGExVtxPropertyEdgeMatch::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!FPCGExVtxPropertyOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!Config.MatchingEdge.Validate(InContext))
	{
		bIsValidOperation = false;
		return false;
	}

	if (!Config.DotComparisonDetails.Init(InContext, InVtxDataFacade.ToSharedRef()))
	{
		bIsValidOperation = false;
		return false;
	}

	DirCache = Config.DirectionValue.GetValueSetting();
	if (!DirCache->Init(PrimaryDataFacade, false))
	{
		bIsValidOperation = false;
		return false;
	}

	DirectionMultiplier = Config.DirectionValue.bFlip ? -1 : 1;

	Config.MatchingEdge.Init(InVtxDataFacade.ToSharedRef());

	return bIsValidOperation;
}

void FPCGExVtxPropertyEdgeMatch::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
	const FTransform& PointTransform = PrimaryDataFacade->Source->GetIn()->GetTransform(Node.PointIndex);

	double BestDot = TNumericLimits<double>::Lowest();
	int32 IBest = -1;
	const double DotThreshold = Config.DotComparisonDetails.GetComparisonThreshold(Node.PointIndex);

	FVector NodeDirection = DirCache->Read(Node.PointIndex).GetSafeNormal() * DirectionMultiplier;
	if (Config.bTransformDirection)
	{
		NodeDirection = PointTransform.TransformVectorNoScale(NodeDirection);
	}

	for (int i = 0; i < Adjacency.Num(); i++)
	{
		const PCGExClusters::FAdjacencyData& A = Adjacency[i];
		const double DotA = FVector::DotProduct(NodeDirection, A.Direction);

		if (Config.DotComparisonDetails.Test(DotA, DotThreshold))
		{
			if (DotA > BestDot)
			{
				BestDot = DotA;
				IBest = i;
			}
		}
	}

	if (IBest != -1)
	{
		Config.MatchingEdge.Set(Node.PointIndex, Adjacency[IBest], Cluster->GetNode(Adjacency[IBest].NodeIndex)->Num());
	}
	else
	{
		Config.MatchingEdge.Set(Node.PointIndex, 0, FVector::ZeroVector, -1, -1, 0);
	}
}

#if WITH_EDITOR
void UPCGExVtxPropertyEdgeMatchSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExVtxPropertyEdgeMatchSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExVtxPropertyEdgeMatchSettings::GetDisplayName() const
{
	/*
	if (Config.SourceAttributes.IsEmpty()) { return TEXT(""); }
	TArray<FName> Names = Config.SourceAttributes.Array();

	if (Names.Num() == 1) { return Names[0].ToString(); }
	if (Names.Num() == 2) { return Names[0].ToString() + TEXT(" (+1 other)"); }

	return Names[0].ToString() + FString::Printf(TEXT(" (+%d others)"), (Names.Num() - 1));
	*/
	return TEXT("");
}
#endif

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertyEdgeMatchFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertyEdgeMatch)
	PCGEX_VTX_EXTRA_CREATE
	return NewOperation;
}

TArray<FPCGPinProperties> UPCGExVtxPropertyEdgeMatchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

UPCGExFactoryData* UPCGExVtxPropertyEdgeMatchSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExVtxPropertyEdgeMatchFactory* NewFactory = InContext->ManagedObjects->New<UPCGExVtxPropertyEdgeMatchFactory>();
	NewFactory->Config = Config;
	NewFactory->Config.Sanitize();
	return Super::CreateFactory(InContext, NewFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
