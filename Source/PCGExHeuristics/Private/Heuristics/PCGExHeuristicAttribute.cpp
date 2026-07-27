// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicAttribute.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExCreateHeuristicAttribute"
#define PCGEX_NAMESPACE CreateHeuristicAttribute

void FPCGExHeuristicAttribute::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);

	const TSharedPtr<PCGExData::FFacade> DataFacade = Source == EPCGExClusterElement::Vtx ? PrimaryDataFacade : SecondaryDataFacade;

	const int32 NumPoints = Source == EPCGExClusterElement::Vtx ? InCluster->Nodes->Num() : InCluster->Edges->Num();
	CachedScores.SetNumZeroed(NumPoints);

	const bool bCaptureMinMax = Mode == EPCGExAttributeHeuristicInputMode::AutoCurve;

	// Grab all attribute values
	const TSharedPtr<PCGExData::TBuffer<double>> Values = DataFacade->GetBroadcaster<double>(Attribute, false, bCaptureMinMax);

	if (!Values)
	{
		PCGEX_LOG_INVALID_SELECTOR_C(Context, Heuristic, Attribute)
		return;
	}

	// Note: ReferenceWeight already includes WeightFactor from the init macro
	const double Factor = ReferenceWeight;

	if (Mode == EPCGExAttributeHeuristicInputMode::Raw)
	{
		if (Source == EPCGExClusterElement::Vtx)
		{
			for (const PCGExClusters::FNode& Node : (*InCluster->Nodes))
			{
				CachedScores[Node.Index] += FMath::Max(0, Values->Read(Node.PointIndex)) * Factor;
			}
		}
		else
		{
			for (int i = 0; i < NumPoints; i++)
			{
				CachedScores[i] += FMath::Max(0, Values->Read(i)) * Factor;
			}
		}
	}
	else
	{
		// Grab min & max
		const double InMinValue = bCaptureMinMax ? Values->Min : InMin;
		const double InMaxValue = bCaptureMinMax ? Values->Max : InMax;

		const double OutMin = bInvert ? 1 : 0;
		const double OutMax = bInvert ? 0 : 1;

		if (InMinValue == InMaxValue)
		{
			// There is no value range, we can't normalize anything
			// Use desired or "auto" fallback instead.
			FallbackValue = FMath::Max(0, ScoreCurve->Eval(bUseCustomFallback ? FallbackValue : FMath::Clamp(InMinValue, 0, 1))) * Factor;
			CachedScores.Init(FallbackValue, NumPoints);
		}
		else
		{
			if (Source == EPCGExClusterElement::Vtx)
			{
				for (const PCGExClusters::FNode& Node : (*InCluster->Nodes))
				{
					const double NormalizedValue = PCGExMath::Remap(Values->Read(Node.PointIndex), InMinValue, InMaxValue, OutMin, OutMax);
					CachedScores[Node.Index] += FMath::Max(0, ScoreCurve->Eval(NormalizedValue)) * Factor;
				}
			}
			else
			{
				for (int i = 0; i < NumPoints; i++)
				{
					const double NormalizedValue = PCGExMath::Remap(Values->Read(i), InMinValue, InMaxValue, OutMin, OutMax);
					CachedScores[i] += FMath::Max(0, ScoreCurve->Eval(NormalizedValue)) * Factor;
				}
			}
		}
	}
}

double FPCGExHeuristicAttribute::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	return CachedScores[Source == EPCGExClusterElement::Edge ? Edge.PointIndex : To.Index];
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryAttribute::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicAttribute)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->Source = Config.Source;
	NewOperation->Attribute = Config.Attribute;
	NewOperation->bUseCustomFallback = Config.bUseCustomFallback;
	NewOperation->FallbackValue = Config.FallbackValue;
	NewOperation->Mode = Config.Mode;
	NewOperation->InMin = Config.InMin;
	NewOperation->InMax = Config.InMax;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Attribute, {})

void UPCGExHeuristicsFactoryAttribute::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.Source == EPCGExClusterElement::Vtx)
	{
		FacadePreloader.Register<double>(InContext, Config.Attribute);
	}
}

#if WITH_EDITOR
void UPCGExCreateHeuristicAttributeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Config.bRawSettings = Config.Mode == EPCGExAttributeHeuristicInputMode::Raw;
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

UPCGExFactoryData* UPCGExCreateHeuristicAttributeSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryAttribute* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryAttribute>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExCreateHeuristicAttributeSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		TEXT("HX : ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
