// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExPartitionGrid.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGParamData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "UObject/UObjectArray.h"

#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionGrid"
#define PCGEX_NAMESPACE PartitionGrid

#pragma region UPCGExPartitionGridSettings

TArray<FPCGPinProperties> UPCGExPartitionGridSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_SPATIALS(PCGExPartitionGrid::SourceBoundsLabel, "Reference bounds, like a partitioned PCG volume's. Several inputs are merged into one box.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPartitionGridSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPartitionGrid::OutputPartitionsLabel, "One single-point data per cell, at the cell center.", Normal)
	PCGEX_PIN_PARAMS(PCGExPartitionGrid::OutputMetadataLabel, "One single-row attribute set per cell, in the same order as Partitions.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PartitionGrid)

#pragma endregion

#pragma region FPCGExPartitionGridElement

void FPCGExPartitionGridElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGExElement::GetDependenciesCrc(InParams, Crc);

	if (const UPCGExPartitionGridSettings* Settings = Cast<UPCGExPartitionGridSettings>(InParams.Settings))
	{
		uint32 GridSize = 0;
		bool b2D = false;
		PCGExPartitionGrid::ResolveGrid(Settings->Grid, Cast<UPCGComponent>(InParams.ExecutionSource), GridSize, b2D);

		Crc.Combine(GridSize);
		Crc.Combine(static_cast<uint32>(b2D));

		if (Settings->bOutputPartitionId && Settings->bMatchEnginePartitionActorName)
		{
			Crc.Combine(static_cast<uint32>(PCGHelpers::IsRuntimeGeneration(InParams.ExecutionSource)));
		}
	}

	OutCrc = Crc;
}

bool FPCGExPartitionGridElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT()
	PCGEX_SETTINGS(PartitionGrid)

	PCGExPartitionGrid::FComponentReads Reads;
	Reads.Add(Settings->Grid);
	PCGExPartitionGrid::WarnIfComponentless(Reads, Context->ExecutionSource.Get(), Context);

	if (!PCGExPartitionGrid::HasBoundedGrid(Settings->Grid, Cast<UPCGComponent>(Context->ExecutionSource.Get())))
	{
		return Context->CancelExecution(TEXT("The grid resolves to an explicit grid with no size (Unbounded): pick a sized grid."));
	}

	return true;
}

bool FPCGExPartitionGridElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(PartitionGrid)

	bool bClipped = false;
	switch (Settings->PointBounds)
	{
	case EPCGExPartitionCellBounds::FullCell:
		bClipped = false;
		break;
	case EPCGExPartitionCellBounds::Clipped:
		bClipped = true;
		break;
	default:
		ensureMsgf(false, TEXT("Unresolvable EPCGExPartitionCellBounds (%d)"), static_cast<int32>(Settings->PointBounds));
		return Context->CancelExecution(TEXT("Unresolvable point bounds mode."));
	}

	FBox Reference(ForceInit);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGExPartitionGrid::SourceBoundsLabel))
	{
		const UPCGSpatialData* Spatial = Cast<UPCGSpatialData>(Input.Data);
		if (!Spatial) { continue; }

		const FBox Bounds = Spatial->GetBounds();
		if (Bounds.IsValid) { Reference += Bounds; }
	}

	if (!Reference.IsValid)
	{
		if (!Settings->bQuietInvalidInputWarning) { PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NoBounds", "The reference has no valid bounds; no cell is output.")); }
		Context->Done();
		return Context->TryComplete();
	}

	const UPCGComponent* Component = Cast<UPCGComponent>(Context->ExecutionSource.Get());

	uint32 GridSize = 0;
	bool b2D = false;
	PCGExPartitionGrid::ResolveGrid(Settings->Grid, Component, GridSize, b2D);

	const PCGExPartitionGrid::FOverlappingCells Overlapping(Reference, GridSize, b2D, Settings->bIncludeTouchingCells);
	const int64 NumCells = Overlapping.Num();
	if (NumCells < 0)
	{
		return Context->CancelExecution(FString::Printf(TEXT("The reference bounds cannot be split on grid size %u: they are malformed or too far from the origin."), GridSize));
	}

	if (NumCells > Settings->MaxCells)
	{
		return Context->CancelExecution(FString::Printf(TEXT("The reference bounds cover %lld cells at grid size %u, more than Max Cells (%d). Use a coarser grid or raise Max Cells."), NumCells, GridSize, Settings->MaxCells));
	}

	// Each cell costs four UObjects (two data and their metadata); running out of object slots is fatal.
	const int64 MaxCellsInBudget = FMath::Max(0, GUObjectArray.GetObjectArrayEstimatedAvailable()) / 4;
	if (NumCells > MaxCellsInBudget)
	{
		return Context->CancelExecution(FString::Printf(TEXT("The reference bounds cover %lld cells at grid size %u, past the %lld the engine has object slots left for. Use a coarser grid or smaller bounds."), NumCells, GridSize, MaxCellsInBudget));
	}

	// Cannot fail: NumCells is within Max Cells, an int32.
	TArray<PCGExPartitionGrid::FCell> Cells;
	Overlapping.GetCells(Cells);

	TSet<FName> UsedNames;
	PCGExPartitionGrid::FCellColumns Columns;
	if (Settings->bOutputPartitionId && PCGExPartitionGrid::AcceptOutputName(Settings->PartitionIdAttributeName, UsedNames, Context))
	{
		Columns.PartitionId = Settings->PartitionIdAttributeName;
		Columns.IdPrefix = PCGExPartitionGrid::ResolveIdPrefix(Settings->bMatchEnginePartitionActorName, PCGHelpers::IsRuntimeGeneration(Context->ExecutionSource.Get()), Settings->IdPrefix);
	}
	if (Settings->bOutputGridSize && PCGExPartitionGrid::AcceptOutputName(Settings->GridSizeAttributeName, UsedNames, Context)) { Columns.GridSize = Settings->GridSizeAttributeName; }
	PCGExPartitionGrid::AddCellColumns(Settings->Outputs, Columns, UsedNames, Context);
	const bool bTextureTransform = Settings->bOutputTextureTransform && PCGExPartitionGrid::AcceptOutputName(Settings->TextureTransformAttributeName, UsedNames, Context);

	Context->IncreaseStagedOutputReserve(Cells.Num() * 2);

	for (const PCGExPartitionGrid::FCell& Cell : Cells)
	{
		UPCGPointArrayData* PointData = Context->ManagedObjects->New<UPCGPointArrayData>();
		UPCGParamData* ParamData = Context->ManagedObjects->New<UPCGParamData>();

		// Only fails once the work is being torn down.
		if (!PointData || !ParamData) { return Context->CancelExecution(FString()); }

		const FVector Center = Cell.CellCenter();

		FBox Box;
		if (bClipped)
		{
			Box = PCGExPartitionGrid::GetFixedBounds(Cell).Overlap(Reference);
		}
		else
		{
			Box = Cell.CellBounds();
			// A 2D cell is unbounded along Z: span the reference instead.
			if (b2D)
			{
				Box.Min.Z = Reference.Min.Z;
				Box.Max.Z = Reference.Max.Z;
			}
		}

		Box = Box.ExpandBy(Settings->BoundsExpansion);

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			PointData, 1,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::Steepness);

		// Same point Get Actor Data reads from the partition actor: its transform, its seed, steepness 1.
		PointData->GetTransformValueRange(false)[0] = FTransform(Center);
		PointData->GetBoundsMinValueRange(false)[0] = Box.Min - Center;
		PointData->GetBoundsMaxValueRange(false)[0] = Box.Max - Center;
		PointData->GetSeedValueRange(false)[0] = PCGHelpers::ComputeSeedFromPosition(Center);
		PointData->GetSteepnessValueRange(false)[0] = 1.0f;

		FPCGMetadataDomain* Domain = ParamData->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		check(Domain);

		const PCGExPartitionGrid::FCellRowWriter Rows(Domain, Columns);
		FPCGMetadataAttribute<FTransform>* TextureAttr = bTextureTransform ? Domain->FindOrCreateAttribute<FTransform>(Settings->TextureTransformAttributeName, FTransform::Identity, false, true) : nullptr;

		const PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
		Rows.Write(Key, Cell);
		if (TextureAttr) { TextureAttr->SetValue(Key, PCGExPartitionGrid::GetTextureTransform(Cell, FVector2D(Settings->BoundsExpansion))); }

		Context->StageOutput(PointData, PCGExPartitionGrid::OutputPartitionsLabel, PCGExData::EStaging::Mutable);
		Context->StageOutput(ParamData, PCGExPartitionGrid::OutputMetadataLabel, PCGExData::EStaging::Mutable);
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
