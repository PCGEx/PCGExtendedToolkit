// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExCellTriage.h"

#include "PCGElement.h"
#include "PCGPin.h"
#include "PCGExCoreMacros.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"

namespace PCGExCellTriage
{
	void DeclareOutputPins(TArray<FPCGPinProperties>& PinProperties, const EPCGExCellTriageOutput InMode, const uint8 InFlags, const bool bOutputPaths, const bool bOutputBounds)
	{
		if (InMode == EPCGExCellTriageOutput::Separate)
		{
			// Every category keeps its pin, so the pin order never moves: an unwanted one is only Advanced.
			auto SetStatus = [&](const EPCGExCellTriageResult InResult)
			{
				PinProperties.Last().PinStatus = IsEnabled(InResult, InFlags) ? EPCGPinStatus::Normal : EPCGPinStatus::Advanced;
			};

			if (bOutputPaths)
			{
				PCGEX_PIN_POINTS(Labels::PathsInside, "Cell paths fully inside bounds", Normal)
				SetStatus(EPCGExCellTriageResult::Inside);

				PCGEX_PIN_POINTS(Labels::PathsTouching, "Cell paths touching bounds", Normal)
				SetStatus(EPCGExCellTriageResult::Touching);

				PCGEX_PIN_POINTS(Labels::PathsOutside, "Cell paths outside bounds", Normal)
				SetStatus(EPCGExCellTriageResult::Outside);
			}

			if (bOutputBounds)
			{
				PCGEX_PIN_POINTS(Labels::BoundsInside, "Cell OBB bounds fully inside", Normal)
				SetStatus(EPCGExCellTriageResult::Inside);

				PCGEX_PIN_POINTS(Labels::BoundsTouching, "Cell OBB bounds touching", Normal)
				SetStatus(EPCGExCellTriageResult::Touching);

				PCGEX_PIN_POINTS(Labels::BoundsOutside, "Cell OBB bounds outside", Normal)
				SetStatus(EPCGExCellTriageResult::Outside);
			}
		}
		else
		{
			if (bOutputPaths)
			{
				PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths (tagged with triage result)", Required)
			}

			if (bOutputBounds)
			{
				PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points (tagged with triage result)", Required)
			}
		}
	}

#pragma region FOutputs

	void FOutputs::Setup(const EPCGExCellTriageOutput InMode, const uint8 InFlags)
	{
		Mode = InMode;
		Flags = InFlags;
	}

	bool FOutputs::Init(FPCGExContext* InContext, const FName InBoundsPin, const EPCGExCellTriageOutput InMode, const uint8 InFlags, const bool bInOutputPaths, const bool bInOutputBounds)
	{
		Setup(InMode, InFlags);

		bOutputPaths = bInOutputPaths;
		bOutputBounds = bInOutputBounds;

		const TArray<FPCGTaggedData> BoundsData = InContext->InputData.GetSpatialInputsByPin(InBoundsPin);
		if (BoundsData.IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Missing required Bounds input."));
			return false;
		}

		if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(BoundsData[0].Data))
		{
			BoundsFilter = SpatialData->GetBounds();
		}
		else
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Invalid Bounds input - must be spatial data."));
			return false;
		}

		auto MakeCollection = [InContext](const FName InPin)
		{
			TSharedPtr<PCGExData::FPointIOCollection> Collection = MakeShared<PCGExData::FPointIOCollection>(InContext);
			Collection->OutputPin = InPin;
			return Collection;
		};

		if (Mode == EPCGExCellTriageOutput::Separate)
		{
			// One collection per pin, wanted or not: an empty one stages nothing and its pin goes inactive.
			if (bOutputPaths)
			{
				Paths[0] = MakeCollection(Labels::PathsInside);
				Paths[1] = MakeCollection(Labels::PathsTouching);
				Paths[2] = MakeCollection(Labels::PathsOutside);
			}

			if (bOutputBounds)
			{
				Bounds[0] = MakeCollection(Labels::BoundsInside);
				Bounds[1] = MakeCollection(Labels::BoundsTouching);
				Bounds[2] = MakeCollection(Labels::BoundsOutside);
			}
		}
		else
		{
			if (bOutputPaths)
			{
				Paths[0] = MakeCollection(PCGExCells::OutputLabels::Paths);
			}

			if (bOutputBounds)
			{
				Bounds[0] = MakeCollection(PCGExCells::OutputLabels::CellBounds);
			}
		}

		return true;
	}

	EPCGExCellTriageResult FOutputs::Classify(const PCGExClusters::FCell& InCell) const
	{
		return ClassifyCell(InCell.Data.Bounds, InCell.Data.Centroid, BoundsFilter);
	}

	const FString& FOutputs::GetTag(const EPCGExCellTriageResult InResult) const
	{
		static const FString NoTag;
		return Mode == EPCGExCellTriageOutput::Combined ? GetTriageTag(InResult) : NoTag;
	}

	const TSharedPtr<PCGExData::FPointIOCollection>& FOutputs::GetPaths(const EPCGExCellTriageResult InResult) const
	{
		return Paths[Mode == EPCGExCellTriageOutput::Separate ? GetCategoryIndex(InResult) : 0];
	}

	const TSharedPtr<PCGExData::FPointIOCollection>& FOutputs::GetBounds(const EPCGExCellTriageResult InResult) const
	{
		return Bounds[Mode == EPCGExCellTriageOutput::Separate ? GetCategoryIndex(InResult) : 0];
	}

	int32 FOutputs::StageOutputs(uint64& InOutInactivePinMask, const int32 InFirstPinIndex) const
	{
		int32 PinIndex = InFirstPinIndex;
		const int32 NumPinsPerArtifact = Mode == EPCGExCellTriageOutput::Separate ? NumCategories : 1;

		auto StagePin = [&](const TSharedPtr<PCGExData::FPointIOCollection>& Collection)
		{
			if (!Collection || !Collection->StageOutputs())
			{
				InOutInactivePinMask |= 1ULL << PinIndex;
			}
			PinIndex++;
		};

		// Same order as DeclareOutputPins: paths, then bounds.
		if (bOutputPaths)
		{
			for (int32 i = 0; i < NumPinsPerArtifact; i++)
			{
				StagePin(Paths[i]);
			}
		}

		if (bOutputBounds)
		{
			for (int32 i = 0; i < NumPinsPerArtifact; i++)
			{
				StagePin(Bounds[i]);
			}
		}

		return PinIndex;
	}

#pragma endregion

#pragma region FBuckets

	void FBuckets::Add(const TSharedPtr<PCGExClusters::FCell>& InCell, const FOutputs& InOutputs)
	{
		if (!InCell)
		{
			return;
		}

		const EPCGExCellTriageResult Result = InOutputs.Classify(*InCell);
		if (InOutputs.IsEnabled(Result))
		{
			Cells[GetCategoryIndex(Result)].Add(InCell);
		}
	}

	void FBuckets::Add(const TArray<TSharedPtr<PCGExClusters::FCell>>& InCells, const FOutputs& InOutputs)
	{
		for (const TSharedPtr<PCGExClusters::FCell>& Cell : InCells)
		{
			Add(Cell, InOutputs);
		}
	}

	int32 FBuckets::NumCells() const
	{
		return Cells[0].Num() + Cells[1].Num() + Cells[2].Num();
	}

	int32 FBuckets::NumPaths() const
	{
		return PathIOs[0].Num() + PathIOs[1].Num() + PathIOs[2].Num();
	}

	bool FBuckets::EmitBounds(
		const TSharedPtr<PCGExClusters::FCluster>& InCluster,
		const TSharedRef<PCGExData::FPointIO>& InVtxIO,
		const PCGExData::FIOSortKey& InSortKey,
		const FOutputs& InOutputs,
		const FPCGExCellArtifactsDetails& InArtifacts,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) const
	{
		for (int32 i = 0; i < NumCategories; i++)
		{
			const EPCGExCellTriageResult Category = static_cast<EPCGExCellTriageResult>(i);
			if (!PCGExClusters::ProcessCellsAsOBBPoints(
				InCluster, Cells[i], InOutputs.GetBounds(Category), InVtxIO, InSortKey.Derived(i),
				InArtifacts, InTaskManager, InOutputs.GetTag(Category)))
			{
				return false;
			}
		}

		return true;
	}

	bool FBuckets::PreparePaths(const TSharedRef<PCGExData::FPointIO>& InVtxIO, const FOutputs& InOutputs)
	{
		for (int32 i = 0; i < NumCategories; i++)
		{
			const TSharedPtr<PCGExData::FPointIOCollection>& Collection = InOutputs.GetPaths(static_cast<EPCGExCellTriageResult>(i));
			if (!Collection || Cells[i].IsEmpty())
			{
				continue;
			}

			PathIOs[i].SetNum(Cells[i].Num());
			if (!Collection->EmplaceBatch<UPCGPointArrayData>(PathIOs[i], InVtxIO, PCGExData::EIOInit::New))
			{
				return false;
			}
		}

		return true;
	}

#pragma endregion
}
