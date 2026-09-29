// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPartitionDetails.h"

#include "PCGComponent.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Helpers/PCGActorHelpers.h"

namespace PCGExPartitionGrid
{
	uint32 OffsetGridSize(const uint32 InGridSize, const int32 InOffset)
	{
		if (InOffset == 0) { return InGridSize; }

		// EPCGHiGenGrid values are the grid size in METERS (a power of two); the cm size is meters * 100.
		const int32 MinLog = static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(EPCGHiGenGrid::Grid4)));    // 2
		const int32 MaxLog = static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(EPCGHiGenGrid::Grid2048))); // 11

		const uint32 BaseMeters = InGridSize / 100;
		const int32 BaseLog = BaseMeters > 0 ? static_cast<int32>(FMath::FloorLog2(BaseMeters)) : MinLog;

		const int32 SteppedLog = FMath::Clamp(BaseLog + InOffset, MinLog, MaxLog);
		return (1u << SteppedLog) * 100u;
	}

	void ResolveGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent, uint32& OutGridSize, bool& bOut2D)
	{
		uint32 GridSize = PCGHiGenGrid::GridToGridSize(InQuery.ExplicitGrid);
		if (InQuery.GridSizeResolution == EPCGExPartitionResolution::FromComponent && InComponent)
		{
			const uint32 ComponentGrid = InComponent->GetGenerationGridSize();
			if (ComponentGrid > 0 && ComponentGrid != PCGHiGenGrid::UnboundedGridSize() && ComponentGrid != PCGHiGenGrid::UninitializedGridSize())
			{
				GridSize = ComponentGrid;
			}
		}
		GridSize = OffsetGridSize(GridSize, InQuery.GridSizeOffset);
		OutGridSize = FMath::Max<uint32>(1u, GridSize);

		switch (InQuery.Grid2D)
		{
		case EPCGExGrid2DMode::Force2D:
			bOut2D = true;
			break;
		case EPCGExGrid2DMode::Force3D:
			bOut2D = false;
			break;
		case EPCGExGrid2DMode::Auto:
			bOut2D = InComponent ? InComponent->Use2DGrid() : false;
			break;
		default:
			ensureMsgf(false, TEXT("Unresolvable EPCGExGrid2DMode (%d)"), static_cast<int32>(InQuery.Grid2D));
			bOut2D = InComponent ? InComponent->Use2DGrid() : false;
			break;
		}
	}

	FCell MakeCell(const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, const FVector& InAnchor)
	{
		const FIntVector Base = UPCGActorHelpers::GetCellCoord(InAnchor, InGridSize, bIn2D);
		return FCell(InGridSize, Base + (bIn2D ? FIntVector(InOffset.X, InOffset.Y, 0) : InOffset), bIn2D);
	}

	FCell MakeCell(const FPCGExPartitionQuery& InQuery, const FVector& InAnchor, const UPCGComponent* InComponent)
	{
		uint32 GridSize = 0;
		bool b2D = false;
		ResolveGrid(InQuery, InComponent, GridSize, b2D);
		return MakeCell(GridSize, b2D, InQuery.Offset, InAnchor);
	}

	FString FormatId(const FCell& InCell, const FString& InPrefix)
	{
		return InCell.b2D
			       ? FString::Printf(TEXT("%s%u_%d_%d"), *InPrefix, InCell.GridSize, InCell.Coord.X, InCell.Coord.Y)
			       : FString::Printf(TEXT("%s%u_%d_%d_%d"), *InPrefix, InCell.GridSize, InCell.Coord.X, InCell.Coord.Y, InCell.Coord.Z);
	}

	bool TryGetAnchor(const IPCGGraphExecutionSource* InSource, FVector& OutAnchor)
	{
		if (!InSource) { return false; }

		const FBox Bounds = InSource->GetExecutionState().GetBounds();
		if (!Bounds.IsValid) { return false; }

		OutAnchor = Bounds.GetCenter();
		return true;
	}
}
