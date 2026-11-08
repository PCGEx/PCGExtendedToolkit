// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExPartitionCells.h"

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

#define LOCTEXT_NAMESPACE "PCGExPartitionCells"
#define PCGEX_NAMESPACE PartitionCells

#pragma region UPCGExPartitionCellsSettings

TArray<FPCGPinProperties> UPCGExPartitionCellsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_SPATIALS(PCGExPartitionCells::SourceBoundsLabel, "Reference bounds, like a partitioned PCG volume's. Several inputs are merged into one box.", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPartitionCellsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPartitionCells::OutputPartitionsLabel, "One single-point data per cell, at the cell center.", Normal)
	PCGEX_PIN_PARAMS(PCGExPartitionCells::OutputMetadataLabel, "One single-row attribute set per cell, in the same order as Partitions.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PartitionCells)

#pragma endregion

#pragma region FPCGExPartitionCellsElement

void FPCGExPartitionCellsElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGExElement::GetDependenciesCrc(InParams, Crc);

	if (const UPCGExPartitionCellsSettings* Settings = Cast<UPCGExPartitionCellsSettings>(InParams.Settings))
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

bool FPCGExPartitionCellsElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT()
	PCGEX_SETTINGS(PartitionCells)

	if (!PCGExPartitionGrid::HasBoundedGrid(Settings->Grid, Cast<UPCGComponent>(Context->ExecutionSource.Get())))
	{
		return Context->CancelExecution(TEXT("The grid resolves to an explicit grid with no size (Unbounded): pick a sized grid."));
	}

	return true;
}

bool FPCGExPartitionCellsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(PartitionCells)

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
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGExPartitionCells::SourceBoundsLabel))
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

	const int64 NumCellRange = PCGExPartitionGrid::CountCellRange(Reference, GridSize, b2D);
	if (NumCellRange < 0)
	{
		return Context->CancelExecution(FString::Printf(TEXT("The reference bounds cannot be split into %u cells: they are malformed or too far from the origin."), GridSize));
	}

	// Each cell costs four UObjects (two data and their metadata); running out of object slots is fatal.
	const int64 MaxCells = FMath::Max(0, GUObjectArray.GetObjectArrayEstimatedAvailable()) / 4;
	if (NumCellRange > MaxCells)
	{
		return Context->CancelExecution(FString::Printf(TEXT("The reference bounds span up to %lld cells of %u, past the %lld the engine has object slots left for. Use a coarser grid or smaller bounds."), NumCellRange, GridSize, MaxCells));
	}

	TArray<PCGExPartitionGrid::FCell> Cells;
	PCGExPartitionGrid::GetOverlappingCells(Reference, GridSize, b2D, Cells, MaxCells);

	if (!Settings->bIncludeTouchingCells)
	{
		Cells.RemoveAll([&Reference](const PCGExPartitionGrid::FCell& Cell) { return !PCGExPartitionGrid::IsCellGenerated(Cell, Reference); });
	}

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
		if (TextureAttr) { TextureAttr->SetValue(Key, PCGExPartitionGrid::GetTextureTransform(Cell)); }

		Context->StageOutput(PointData, PCGExPartitionCells::OutputPartitionsLabel, PCGExData::EStaging::Mutable);
		Context->StageOutput(ParamData, PCGExPartitionCells::OutputMetadataLabel, PCGExData::EStaging::Mutable);
	}

	Context->Done();
	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
