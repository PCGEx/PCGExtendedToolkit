// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPartitionDetails.h"

#include "EngineDefines.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Helpers/PCGActorHelpers.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "UObject/GarbageCollection.h"
#include "Utils/PCGLogErrors.h"

namespace PCGExPartitionDetails
{
	// The component's generation grid size. Only a local component (one running as a partition) carries a bounded one.
	bool TryGetComponentGridSize(const UPCGComponent* InComponent, uint32& OutGridSize)
	{
		if (!InComponent) { return false; }

		const uint32 ComponentGrid = InComponent->GetGenerationGridSize();
		if (ComponentGrid == 0 || ComponentGrid == PCGHiGenGrid::UnboundedGridSize() || ComponentGrid == PCGHiGenGrid::UninitializedGridSize()) { return false; }

		OutGridSize = ComponentGrid;
		return true;
	}

	// Cell coordinate range of the bounds and its cell count (saturated), or -1 when the bounds cannot be split.
	int64 ComputeCellRange(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D, FIntVector& OutMinCoord, FIntVector& OutMaxCoord)
	{
		if (!InBounds.IsValid || InGridSize == 0 || InBounds.ContainsNaN()) { return -1; }

		// FBox(Min, Max) flags a box valid without ordering its corners.
		if (InBounds.Min.X > InBounds.Max.X || InBounds.Min.Y > InBounds.Max.Y || InBounds.Min.Z > InBounds.Max.Z) { return -1; }

		// UPCGActorHelpers::GetCellCoord floors into int32: refuse coordinates it cannot hold. Z is not floored in 2D.
		const double GridSize = static_cast<double>(InGridSize);
		const int32 NumAxes = bIn2D ? 2 : 3;
		for (int32 Axis = 0; Axis < NumAxes; ++Axis)
		{
			if (FMath::Abs(InBounds.Min[Axis] / GridSize) >= MAX_int32 || FMath::Abs(InBounds.Max[Axis] / GridSize) >= MAX_int32) { return -1; }
		}

		OutMinCoord = UPCGActorHelpers::GetCellCoord(InBounds.Min, InGridSize, bIn2D);
		OutMaxCoord = UPCGActorHelpers::GetCellCoord(InBounds.Max, InGridSize, bIn2D);

		// Ordered corners floor to ordered coordinates, so every span is at least 1.
		const int64 SpanX = static_cast<int64>(OutMaxCoord.X) - OutMinCoord.X + 1;
		const int64 SpanY = static_cast<int64>(OutMaxCoord.Y) - OutMinCoord.Y + 1;
		const int64 SpanZ = static_cast<int64>(OutMaxCoord.Z) - OutMinCoord.Z + 1;
		if (SpanX > MAX_int64 / SpanY) { return MAX_int64; }

		const int64 NumXY = SpanX * SpanY;
		return NumXY > MAX_int64 / SpanZ ? MAX_int64 : NumXY * SpanZ;
	}
}

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

	uint32 ResolveGridSize(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent)
	{
		uint32 GridSize = PCGHiGenGrid::GridToGridSize(InQuery.ExplicitGrid);
		if (InQuery.GridSizeResolution == EPCGExPartitionResolution::FromComponent) { PCGExPartitionDetails::TryGetComponentGridSize(InComponent, GridSize); }
		GridSize = OffsetGridSize(GridSize, InQuery.GridSizeOffset);
		return FMath::Max<uint32>(1u, GridSize);
	}

	bool HasBoundedGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent)
	{
		// A size offset always lands on the grid ladder, whatever it starts from.
		if (InQuery.GridSizeOffset != 0) { return true; }

		uint32 ComponentGrid = 0;
		if (InQuery.GridSizeResolution == EPCGExPartitionResolution::FromComponent && PCGExPartitionDetails::TryGetComponentGridSize(InComponent, ComponentGrid)) { return true; }

		return PCGHiGenGrid::IsValidGrid(InQuery.ExplicitGrid);
	}

	bool ResolveGrid2D(const FPCGExPartitionQuery& InQuery, const bool bInAuto2D)
	{
		switch (InQuery.Grid2D)
		{
		case EPCGExGrid2DMode::Force2D:
			return true;
		case EPCGExGrid2DMode::Force3D:
			return false;
		case EPCGExGrid2DMode::Auto:
			return bInAuto2D;
		default:
			ensureMsgf(false, TEXT("Unresolvable EPCGExGrid2DMode (%d)"), static_cast<int32>(InQuery.Grid2D));
			return bInAuto2D;
		}
	}

	void ResolveGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent, uint32& OutGridSize, bool& bOut2D)
	{
		OutGridSize = ResolveGridSize(InQuery, InComponent);
		bOut2D = ResolveGrid2D(InQuery, InComponent ? InComponent->Use2DGrid() : false);
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

	FString ResolveIdPrefix(const bool bInEngineName, const bool bInRuntime, const FString& InUserPrefix)
	{
		if (!bInEngineName) { return InUserPrefix; }

		// FPCGGridDescriptor::GetPartitionActorName's prefixes; its editor-only DataLayer/HLOD suffix is not reproduced.
		return bInRuntime ? TEXT("PCGRuntimePartitionGridActor_") : TEXT("PCGPartitionGridActor_");
	}

	bool TryGetAnchor(const IPCGGraphExecutionSource* InSource, FVector& OutAnchor)
	{
		if (!InSource) { return false; }

		// A partitioned component's bounds reach its original through a soft pointer; Partition Identifier runs on workers.
		FBox Bounds(ForceInit);
		{
			FGCScopeGuard GCGuard;
			Bounds = InSource->GetExecutionState().GetBounds();
		}
		if (!Bounds.IsValid) { return false; }

		OutAnchor = Bounds.GetCenter();
		return true;
	}

	FBox GetFixedBounds(const FCell& InCell)
	{
		const double HalfGridSize = static_cast<double>(InCell.GridSize) * 0.5;
		const FVector Extent(HalfGridSize, HalfGridSize, InCell.b2D ? HALF_WORLD_MAX1 : HalfGridSize);
		const FVector Center = InCell.CellCenter();
		return FBox(Center - Extent, Center + Extent);
	}

	FTransform GetTextureTransform(const FCell& InCell)
	{
		// The partition actor sits at the cell center, unrotated and unscaled; Get Texture Data scales by its local bounds extent.
		return FTransform(FQuat::Identity, InCell.CellCenter(), GetFixedBounds(InCell).GetExtent());
	}

	int64 CountCellRange(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D)
	{
		FIntVector MinCoord;
		FIntVector MaxCoord;
		return PCGExPartitionDetails::ComputeCellRange(InBounds, InGridSize, bIn2D, MinCoord, MaxCoord);
	}

	bool GetOverlappingCells(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D, TArray<FCell>& OutCells, const int64 InMaxCells)
	{
		OutCells.Reset();

		FIntVector MinCoord;
		FIntVector MaxCoord;
		const int64 NumCells = PCGExPartitionDetails::ComputeCellRange(InBounds, InGridSize, bIn2D, MinCoord, MaxCoord);
		if (NumCells < 0 || NumCells > FMath::Min<int64>(InMaxCells, MAX_int32)) { return false; }

		// The subsystem clamps Z to the single 2D layer before testing overlaps.
		FBox TestedBounds = InBounds;
		if (bIn2D)
		{
			TestedBounds.Min.Z = 0;
			TestedBounds.Max.Z = static_cast<double>(InGridSize);
		}

		OutCells.Reserve(static_cast<int32>(NumCells));
		for (int32 Z = MinCoord.Z; Z <= MaxCoord.Z; ++Z)
		{
			for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; ++Y)
			{
				for (int32 X = MinCoord.X; X <= MaxCoord.X; ++X)
				{
					const FCell Cell(InGridSize, FIntVector(X, Y, Z), bIn2D);
					if (TestedBounds.Overlap(Cell.CellBounds()).IsValid) { OutCells.Add(Cell); }
				}
			}
		}

		return true;
	}

	bool IsCellGenerated(const FCell& InCell, const FBox& InBounds)
	{
		// FPCGTrackingManager::ForAllIntersectingPartitionActors: bounds with no volume overlap nothing, so any contact counts.
		const FBox FixedBounds = GetFixedBounds(InCell);
		return FMath::IsNearlyZero(InBounds.GetVolume())
			       ? FixedBounds.Intersect(InBounds)
			       : !FMath::IsNearlyZero(FixedBounds.Overlap(InBounds).GetVolume());
	}

	bool AcceptOutputName(const FName InName, TSet<FName>& InOutUsed, FPCGContext* InContext)
	{
		if (InName.IsNone() || !FPCGMetadataAttributeBase::IsValidName(InName))
		{
			PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Output attribute name '%s' is invalid; that output is skipped."), *InName.ToString())), InContext);
			return false;
		}

		// A duplicate would make FindOrCreateAttribute overwrite on a type mismatch, dangling the earlier attribute pointer.
		bool bDuplicate = false;
		InOutUsed.Add(InName, &bDuplicate);
		if (bDuplicate)
		{
			PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Output attribute name '%s' is used by more than one output; the duplicate is skipped."), *InName.ToString())), InContext);
			return false;
		}

		return true;
	}

	void AddCellColumns(const FPCGExPartitionCellOutputs& InOutputs, FCellColumns& InOutColumns, TSet<FName>& InOutUsed, FPCGContext* InContext)
	{
		if (InOutputs.bGridCoord && AcceptOutputName(InOutputs.GridCoordAttributeName, InOutUsed, InContext)) { InOutColumns.GridCoord = InOutputs.GridCoordAttributeName; }
		if (InOutputs.bCellCenter && AcceptOutputName(InOutputs.CellCenterAttributeName, InOutUsed, InContext)) { InOutColumns.CellCenter = InOutputs.CellCenterAttributeName; }
		if (InOutputs.bCellBounds)
		{
			if (AcceptOutputName(InOutputs.CellMinAttributeName, InOutUsed, InContext)) { InOutColumns.CellMin = InOutputs.CellMinAttributeName; }
			if (AcceptOutputName(InOutputs.CellMaxAttributeName, InOutUsed, InContext)) { InOutColumns.CellMax = InOutputs.CellMaxAttributeName; }
		}
	}

#pragma region FCellRowWriter

	FCellRowWriter::FCellRowWriter(FPCGMetadataDomain* InDomain, const FCellColumns& InColumns)
		: IdPrefix(&InColumns.IdPrefix)
	{
		check(InDomain);

		if (!InColumns.PartitionId.IsNone()) { IdAttr = InDomain->FindOrCreateAttribute<FString>(InColumns.PartitionId, FString(), false, true); }
		if (!InColumns.GridSize.IsNone()) { GridSizeAttr = InDomain->FindOrCreateAttribute<int32>(InColumns.GridSize, 0, false, true); }
		if (!InColumns.GridCoord.IsNone()) { CoordAttr = InDomain->FindOrCreateAttribute<FVector>(InColumns.GridCoord, FVector::ZeroVector, false, true); }
		if (!InColumns.CellCenter.IsNone()) { CenterAttr = InDomain->FindOrCreateAttribute<FVector>(InColumns.CellCenter, FVector::ZeroVector, false, true); }
		if (!InColumns.CellMin.IsNone()) { MinAttr = InDomain->FindOrCreateAttribute<FVector>(InColumns.CellMin, FVector::ZeroVector, false, true); }
		if (!InColumns.CellMax.IsNone()) { MaxAttr = InDomain->FindOrCreateAttribute<FVector>(InColumns.CellMax, FVector::ZeroVector, false, true); }
	}

	void FCellRowWriter::Write(const PCGMetadataEntryKey InKey, const FCell& InCell) const
	{
		if (IdAttr) { IdAttr->SetValue(InKey, FormatId(InCell, *IdPrefix)); }
		if (GridSizeAttr) { GridSizeAttr->SetValue(InKey, static_cast<int32>(InCell.GridSize)); }
		if (CoordAttr) { CoordAttr->SetValue(InKey, InCell.CoordAsVector()); }
		if (CenterAttr) { CenterAttr->SetValue(InKey, InCell.CellCenter()); }
		if (MinAttr) { MinAttr->SetValue(InKey, InCell.CellMinCorner()); }
		if (MaxAttr) { MaxAttr->SetValue(InKey, InCell.CellMaxCorner()); }
	}

#pragma endregion
}
