// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Relaxations/PCGExFittingRelaxBase.h"

#include "Helpers/PCGExMetaHelpers.h"

#pragma region UPCGExFittingRelaxBase

bool UPCGExFittingRelaxBase::PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster)
{
	if (!Super::PrepareForCluster(InContext, InCluster))
	{
		return false;
	}

	if (EdgeFitting == EPCGExRelaxEdgeFitting::Attribute)
	{
		const TSharedPtr<PCGExData::TBuffer<double>> Buffer = SecondaryDataFacade->GetBroadcaster<double>(DesiredEdgeLengthAttribute);

		if (!Buffer)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(Context, Edge Length, DesiredEdgeLengthAttribute)
			return false;
		}

		EdgeLengths = MakeShared<TArray<double>>();
		EdgeLengths->SetNumUninitialized(Cluster->Edges->Num());
		Buffer->DumpValues(EdgeLengths);
	}
	else
	{
		if (EdgeFitting == EPCGExRelaxEdgeFitting::Fixed)
		{
			EdgeLengths = MakeShared<TArray<double>>();
			EdgeLengths->Init(DesiredEdgeLength, Cluster->Edges->Num());
			Scale = 1;
		}
		else if (EdgeFitting == EPCGExRelaxEdgeFitting::Existing)
		{
			Cluster->ComputeEdgeLengths();
			EdgeLengths = Cluster->EdgeLengths;
		}
	}

	return true;
}

EPCGExClusterElement UPCGExFittingRelaxBase::PrepareNextStep(const int32 InStep)
{
	// Step 1 : Apply spring forces for each edge
	if (InStep == 0)
	{
		Super::PrepareNextStep(InStep);
		Deltas.Reset(Cluster->Nodes->Num());
		Deltas.Init(FInt64Vector3(0), Cluster->Nodes->Num());
		return EPCGExClusterElement::Edge;
	}

	// Step 2 : Apply repulsion forces between all pairs of nodes
	// Step 3 : Update positions based on accumulated forces
	return EPCGExClusterElement::Vtx;
}

void UPCGExFittingRelaxBase::Step1(const PCGExGraphs::FEdge& Edge)
{
	// Apply spring forces for each edge

	// TODO : Refactor relax host to skip edge processing entirely
	if (EdgeFitting == EPCGExRelaxEdgeFitting::Ignore)
	{
		return;
	}

	const int32 Start = Cluster->GetEdgeStart(Edge)->Index;
	const int32 End = Cluster->GetEdgeEnd(Edge)->Index;

	const FVector& StartPos = (ReadBuffer->GetData() + Start)->GetLocation();
	const FVector& EndPos = (ReadBuffer->GetData() + End)->GetLocation();

	const FVector Delta = EndPos - StartPos;
	const double CurrentLength = Delta.Size();

	if (CurrentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Direction = Delta / CurrentLength;
	const double Displacement = CurrentLength - (*(EdgeLengths->GetData() + Edge.Index) * Scale);

	AddDelta(Start, End, (SpringConstant * Displacement * Direction));
}

void UPCGExFittingRelaxBase::Step3(const PCGExClusters::FNode& Node)
{
	// Update positions based on accumulated forces
	const FVector Position = (ReadBuffer->GetData() + Node.Index)->GetLocation();
	(*WriteBuffer)[Node.Index].SetLocation(Position + GetDelta(Node.Index) * TimeStep);
}

#pragma endregion
