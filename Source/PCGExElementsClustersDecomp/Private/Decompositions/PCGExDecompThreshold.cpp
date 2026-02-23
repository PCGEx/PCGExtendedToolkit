// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompThreshold.h"

#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"

#pragma region FPCGExDecompThreshold

bool FPCGExDecompThreshold::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0) { return false; }
	if (AttributeName == NAME_None) { return false; }

	const int32 NumNodes = Cluster->Nodes->Num();
	const int32 SafeNumBins = FMath::Max(NumBins, 2);

	// Read attribute values from VtxFacade (set as PrimaryDataFacade)
	const TSharedPtr<PCGExData::TBuffer<double>> Buffer = PrimaryDataFacade->GetReadable<double>(AttributeName);
	if (!Buffer) { return false; }

	// Gather valid node values
	struct FNodeValue
	{
		int32 NodeIndex;
		double Value;
	};

	TArray<FNodeValue> NodeValues;
	NodeValues.Reserve(NumNodes);

	for (int32 i = 0; i < NumNodes; i++)
	{
		if (!Cluster->GetNode(i)->bValid) { continue; }

		const int32 PointIndex = Cluster->GetNodePointIndex(i);
		NodeValues.Add({i, Buffer->Read(PointIndex)});
	}

	if (NodeValues.Num() == 0) { return false; }

	if (BinningMode == EPCGExDecompBinningMode::Uniform)
	{
		// Find value range
		double MinVal = NodeValues[0].Value;
		double MaxVal = NodeValues[0].Value;
		for (const FNodeValue& NV : NodeValues)
		{
			MinVal = FMath::Min(MinVal, NV.Value);
			MaxVal = FMath::Max(MaxVal, NV.Value);
		}

		const double Range = MaxVal - MinVal;
		if (Range < KINDA_SMALL_NUMBER)
		{
			// All same value - single cell
			for (const FNodeValue& NV : NodeValues) { OutResult.NodeCellIDs[NV.NodeIndex] = 0; }
			OutResult.NumCells = 1;
			return true;
		}

		const double BinWidth = Range / SafeNumBins;
		for (const FNodeValue& NV : NodeValues)
		{
			int32 Bin = FMath::FloorToInt((NV.Value - MinVal) / BinWidth);
			Bin = FMath::Clamp(Bin, 0, SafeNumBins - 1);
			OutResult.NodeCellIDs[NV.NodeIndex] = Bin;
		}

		OutResult.NumCells = SafeNumBins;
	}
	else // Quantile
	{
		// Sort by value
		NodeValues.Sort([](const FNodeValue& A, const FNodeValue& B) { return A.Value < B.Value; });

		const int32 TotalNodes = NodeValues.Num();
		const int32 NodesPerBin = FMath::Max(1, TotalNodes / SafeNumBins);

		int32 ActualBins = 0;
		for (int32 i = 0; i < TotalNodes; i++)
		{
			const int32 Bin = FMath::Min(i / NodesPerBin, SafeNumBins - 1);
			OutResult.NodeCellIDs[NodeValues[i].NodeIndex] = Bin;
			ActualBins = FMath::Max(ActualBins, Bin + 1);
		}

		OutResult.NumCells = ActualBins;
	}

	return true;
}

#pragma endregion

#pragma region UPCGExDecompThreshold

void UPCGExDecompThreshold::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader)
{
	FacadePreloader.Register<double>(InContext, AttributeSelector);
}

void UPCGExDecompThreshold::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompThreshold* TypedOther = Cast<UPCGExDecompThreshold>(Other))
	{
		AttributeSelector = TypedOther->AttributeSelector;
		NumBins = TypedOther->NumBins;
		BinningMode = TypedOther->BinningMode;
	}
}

#pragma endregion
