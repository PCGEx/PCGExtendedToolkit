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
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "UObject/GarbageCollection.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionDetails"

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

	// Cell coordinate range of the bounds; false when the bounds cannot be split.
	bool ComputeCellRange(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D, FIntVector& OutMinCoord, FIntVector& OutMaxCoord)
	{
		if (!InBounds.IsValid || InGridSize == 0 || InBounds.ContainsNaN()) { return false; }

		// FBox(Min, Max) flags a box valid without ordering its corners.
		if (InBounds.Min.X > InBounds.Max.X || InBounds.Min.Y > InBounds.Max.Y || InBounds.Min.Z > InBounds.Max.Z) { return false; }

		// UPCGActorHelpers::GetCellCoord floors into int32: refuse coordinates it cannot hold. Z is not floored in 2D.
		const double GridSize = static_cast<double>(InGridSize);
		const int32 NumAxes = bIn2D ? 2 : 3;
		for (int32 Axis = 0; Axis < NumAxes; ++Axis)
		{
			if (FMath::Abs(InBounds.Min[Axis] / GridSize) >= MAX_int32 || FMath::Abs(InBounds.Max[Axis] / GridSize) >= MAX_int32) { return false; }
		}

		// Ordered corners floor to ordered coordinates: every axis spans at least one cell.
		OutMinCoord = UPCGActorHelpers::GetCellCoord(InBounds.Min, InGridSize, bIn2D);
		OutMaxCoord = UPCGActorHelpers::GetCellCoord(InBounds.Max, InGridSize, bIn2D);
		return true;
	}

	// Along one axis of a cell range, run 0 is the first coordinate, run 1 the interior ones, run 2 the last.
	int32 RunOf(const int32 InCoord, const int32 InMin, const int32 InMax)
	{
		return InCoord == InMin ? 0 : InCoord == InMax ? 2 : 1;
	}

	int64 RunLength(const int32 InRun, const int32 InMin, const int32 InMax)
	{
		const int64 Span = static_cast<int64>(InMax) - InMin + 1;
		if (InRun == 0) { return 1; }
		if (InRun == 2) { return Span > 1 ? 1 : 0; }
		return FMath::Max<int64>(Span - 2, 0);
	}

	// Any coordinate of the run; only valid when RunLength is not 0.
	int32 RunCoord(const int32 InRun, const int32 InMin, const int32 InMax)
	{
		return InRun == 0 ? InMin : InRun == 2 ? InMax : InMin + 1;
	}

	// Non-negative operands only.
	int64 MulSaturated(const int64 A, const int64 B)
	{
		return A != 0 && B > MAX_int64 / A ? MAX_int64 : A * B;
	}

	int64 AddSaturated(const int64 A, const int64 B)
	{
		return A > MAX_int64 - B ? MAX_int64 : A + B;
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

	void WarnIfComponentless(const FComponentReads& InReads, const IPCGGraphExecutionSource* InSource, FPCGContext* InContext)
	{
		if ((!InReads.bGridSize && !InReads.b2D) || Cast<UPCGComponent>(InSource)) { return; }

		const FText Warning =
			!InReads.b2D ? LOCTEXT("ComponentlessGridSize", "No PCG component executes this node: grid sizes set to From Executing Component use the Explicit Grid instead.")
			: !InReads.bGridSize ? LOCTEXT("Componentless2D", "No PCG component executes this node: Grid 2D set to Auto uses 3D.")
			: LOCTEXT("ComponentlessBoth", "No PCG component executes this node: grid sizes set to From Executing Component use the Explicit Grid instead, and Grid 2D set to Auto uses 3D.");

		PCGLog::LogWarningOnGraph(Warning, InContext);
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

	bool IsCellGenerated(const FCell& InCell, const FBox& InBounds)
	{
		// FPCGTrackingManager::ForAllIntersectingPartitionActors: bounds with no volume overlap nothing, so any contact counts.
		const FBox FixedBounds = GetFixedBounds(InCell);
		return FMath::IsNearlyZero(InBounds.GetVolume())
			       ? FixedBounds.Intersect(InBounds)
			       : !FMath::IsNearlyZero(FixedBounds.Overlap(InBounds).GetVolume());
	}

#pragma region FOverlappingCells

	FOverlappingCells::FOverlappingCells(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D, const bool bInIncludeTouching)
		: GridSize(InGridSize), b2D(bIn2D)
	{
		if (!PCGExPartitionDetails::ComputeCellRange(InBounds, InGridSize, bIn2D, MinCoord, MaxCoord)) { return; }

		// The subsystem clamps Z to the single 2D layer before testing overlaps.
		FBox TestedBounds = InBounds;
		if (bIn2D)
		{
			TestedBounds.Min.Z = 0;
			TestedBounds.Max.Z = static_cast<double>(InGridSize);
		}

		// The bounds cover every interior cell of an axis across its full width, so all cells of a run combination share
		// one verdict: test one of each.
		NumCells = 0;
		for (int32 RunZ = 0; RunZ < 3; ++RunZ)
		{
			const int64 NumZ = PCGExPartitionDetails::RunLength(RunZ, MinCoord.Z, MaxCoord.Z);
			for (int32 RunY = 0; RunY < 3; ++RunY)
			{
				const int64 NumY = PCGExPartitionDetails::RunLength(RunY, MinCoord.Y, MaxCoord.Y);
				for (int32 RunX = 0; RunX < 3; ++RunX)
				{
					const int64 NumX = PCGExPartitionDetails::RunLength(RunX, MinCoord.X, MaxCoord.X);
					if (NumX == 0 || NumY == 0 || NumZ == 0) { continue; }

					const FIntVector Coord(
						PCGExPartitionDetails::RunCoord(RunX, MinCoord.X, MaxCoord.X),
						PCGExPartitionDetails::RunCoord(RunY, MinCoord.Y, MaxCoord.Y),
						PCGExPartitionDetails::RunCoord(RunZ, MinCoord.Z, MaxCoord.Z));
					const FCell Cell(InGridSize, Coord, bIn2D);

					if (!TestedBounds.Overlap(Cell.CellBounds()).IsValid) { continue; }
					if (!bInIncludeTouching && !IsCellGenerated(Cell, InBounds)) { continue; }

					bKeep[RunZ][RunY][RunX] = true;
					NumCells = PCGExPartitionDetails::AddSaturated(NumCells, PCGExPartitionDetails::MulSaturated(PCGExPartitionDetails::MulSaturated(NumX, NumY), NumZ));
				}
			}
		}
	}

	bool FOverlappingCells::GetCells(TArray<FCell>& OutCells) const
	{
		OutCells.Reset();
		if (NumCells < 0 || NumCells > MAX_int32) { return false; }

		// Nothing kept: the range may still be huge, so skip the walk.
		if (NumCells == 0) { return true; }

		OutCells.Reserve(static_cast<int32>(NumCells));
		for (int32 Z = MinCoord.Z; Z <= MaxCoord.Z; ++Z)
		{
			const int32 RunZ = PCGExPartitionDetails::RunOf(Z, MinCoord.Z, MaxCoord.Z);
			for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; ++Y)
			{
				const int32 RunY = PCGExPartitionDetails::RunOf(Y, MinCoord.Y, MaxCoord.Y);
				for (int32 X = MinCoord.X; X <= MaxCoord.X; ++X)
				{
					if (bKeep[RunZ][RunY][PCGExPartitionDetails::RunOf(X, MinCoord.X, MaxCoord.X)]) { OutCells.Emplace(GridSize, FIntVector(X, Y, Z), b2D); }
				}
			}
		}

		return true;
	}

#pragma endregion

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

	bool WriteCellIds(FPCGMetadataAttribute<FString>* InAttribute, FPCGMetadataDomain* InDomain, const TConstArrayView<PCGMetadataEntryKey> InEntryKeys, const TConstArrayView<FVector> InPositions, const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, const FString& InPrefix)
	{
		check(InAttribute && InDomain);
		check(InEntryKeys.Num() == InPositions.Num());

		const int32 NumEntries = InEntryKeys.Num();
		if (NumEntries == 0) { return true; }

		// One id per cell, written on the first entry that falls in it.
		TMap<FIntVector, int32> CellIndices;
		TArray<FString> CellIds;
		TArray<PCGMetadataEntryKey> CellEntries;
		TArray<int32> EntryCells;
		EntryCells.SetNumUninitialized(NumEntries);

		for (int32 i = 0; i < NumEntries; ++i)
		{
			const FCell Cell = MakeCell(InGridSize, bIn2D, InOffset, InPositions[i]);
			const int32 CellIndex = CellIndices.FindOrAdd(Cell.Coord, CellIds.Num());
			if (CellIndex == CellIds.Num())
			{
				CellIds.Add(FormatId(Cell, InPrefix));
				CellEntries.Add(InEntryKeys[i]);
			}
			EntryCells[i] = CellIndex;
		}

		// An accessor dedups compressed values through a hash, where SetValues would AddUnique each value against the others.
		const TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(InAttribute, InDomain);
		FPCGAttributeAccessorKeysEntries CellKeys(MakeArrayView(CellEntries));
		if (!Accessor || !Accessor->SetRange<FString>(TArrayView<const FString>(CellIds), 0, CellKeys)) { return false; }

		TArray<PCGMetadataValueKey> CellValueKeys;
		InAttribute->GetValueKeys(TArrayView<const PCGMetadataEntryKey>(CellEntries), CellValueKeys);

		TArray<PCGMetadataValueKey> EntryValueKeys;
		EntryValueKeys.SetNumUninitialized(NumEntries);
		for (int32 i = 0; i < NumEntries; ++i) { EntryValueKeys[i] = CellValueKeys[EntryCells[i]]; }

		InAttribute->SetValuesFromValueKeys(InEntryKeys, TArrayView<const PCGMetadataValueKey>(EntryValueKeys));
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
