// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExPartitionIdentifier.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGCommon.h"
#include "PCGPoint.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionIdentifierElement"

// File-local helpers live in a named namespace (matching the file) so the Unity
// build cannot collide them with same-named helpers from other translation units.
namespace PCGExPartitionIdentifier
{
	// A resolved partition: grid size, integer cell coordinate, and 2D flag. World-space
	// derivations are anchored at the world origin (0,0,0) -- exactly why they line up with
	// real PCG partition actors -- and mirror UPCGActorHelpers::GetCellCoord/GetCellCenter:
	// the cell spans [Coord, Coord+1] * GridSize on every axis. b2D only drives the id token
	// count (Coord.Z is already 0 in 2D).
	struct FInfo
	{
		uint32 GridSize;
		FIntVector Coord;
		bool b2D;

		FInfo(const uint32 InGridSize, const FIntVector& InCoord, const bool bIn2D)
			: GridSize(InGridSize), Coord(InCoord), b2D(bIn2D)
		{
		}

		FVector CellMinCorner() const
		{
			return FVector(static_cast<double>(Coord.X), static_cast<double>(Coord.Y), static_cast<double>(Coord.Z)) * static_cast<double>(GridSize);
		}

		FVector CellCenter() const
		{
			return FVector(Coord.X + 0.5, Coord.Y + 0.5, Coord.Z + 0.5) * static_cast<double>(GridSize);
		}

		FVector CellMaxCorner() const
		{
			return FVector(Coord.X + 1.0, Coord.Y + 1.0, Coord.Z + 1.0) * static_cast<double>(GridSize);
		}
	};

	FVector CoordVector(const FIntVector& Coord)
	{
		return FVector(static_cast<double>(Coord.X), static_cast<double>(Coord.Y), static_cast<double>(Coord.Z));
	}

	FString FormatId(const FInfo& Info, const FString& Prefix)
	{
		return Info.b2D
			       ? FString::Printf(TEXT("%s%u_%d_%d"), *Prefix, Info.GridSize, Info.Coord.X, Info.Coord.Y)
			       : FString::Printf(TEXT("%s%u_%d_%d_%d"), *Prefix, Info.GridSize, Info.Coord.X, Info.Coord.Y, Info.Coord.Z);
	}

	FString MakeId(const FInfo& Info, const UPCGExPartitionIdentifierSettings* Settings, const bool bRuntime)
	{
		// When matching the engine actor name, swap in the engine prefix. The editor-only
		// DataLayer/HLOD hash suffix is intentionally not reproduced.
		const FString Prefix = Settings->bMatchEnginePartitionActorName
			                       ? FString(bRuntime ? TEXT("PCGRuntimePartitionGridActor_") : TEXT("PCGPartitionGridActor_"))
			                       : Settings->IdPrefix;
		return FormatId(Info, Prefix);
	}

	// Steps a resolved grid size along the power-of-two grid ladder by the given number of
	// levels (+ coarser / - finer), clamped to the editor-exposed range Grid4 (400cm) ..
	// Grid2048 (204800cm). Offset 0 returns the input untouched, so the default never reshapes
	// a base the resolver produced (including hidden/unbounded grids).
	uint32 OffsetGridSize(const uint32 InGridSize, const int32 Offset)
	{
		if (Offset == 0) { return InGridSize; }

		// EPCGHiGenGrid values are the grid size in METERS (a power of two); the cm size is
		// meters * 100. Grid2048 is the largest grid the editor dropdown exposes.
		const int32 MinLog = static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(EPCGHiGenGrid::Grid4)));    // 2
		const int32 MaxLog = static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(EPCGHiGenGrid::Grid2048))); // 11

		const uint32 BaseMeters = InGridSize / 100;
		const int32 BaseLog = BaseMeters > 0 ? static_cast<int32>(FMath::FloorLog2(BaseMeters)) : MinLog;

		const int32 SteppedLog = FMath::Clamp(BaseLog + Offset, MinLog, MaxLog);
		return (1u << SteppedLog) * 100u;
	}

	// Resolves an entry's grid size (cm) and 2D flag against the executing component.
	void ResolveGrid(const FPCGExPartitionGrid& Cfg, const UPCGComponent* Component, uint32& OutGridSize, bool& Outb2D)
	{
		uint32 GridSize = PCGHiGenGrid::GridToGridSize(Cfg.ExplicitGrid);
		if (Cfg.GridSizeResolution == EPCGExPartitionResolution::FromComponent && Component)
		{
			const uint32 ComponentGrid = Component->GetGenerationGridSize();
			if (ComponentGrid > 0 && ComponentGrid != PCGHiGenGrid::UnboundedGridSize() && ComponentGrid != PCGHiGenGrid::UninitializedGridSize())
			{
				GridSize = ComponentGrid;
			}
		}
		GridSize = OffsetGridSize(GridSize, Cfg.GridSizeOffset);
		OutGridSize = FMath::Max<uint32>(1u, GridSize);

		switch (Cfg.Grid2D)
		{
		case EPCGExGrid2DMode::Force2D: Outb2D = true; break;
		case EPCGExGrid2DMode::Force3D: Outb2D = false; break;
		default: Outb2D = Component ? Component->Use2DGrid() : false; break;
		}
	}

	FInfo MakeInfo(const FPCGExPartitionGrid& Cfg, const FVector& Anchor, const UPCGComponent* Component)
	{
		uint32 GridSize;
		bool b2D;
		ResolveGrid(Cfg, Component, GridSize, b2D);
		const FIntVector Base = UPCGActorHelpers::GetCellCoord(Anchor, GridSize, b2D);
		const FIntVector Coord = Base + (b2D ? FIntVector(Cfg.Offset.X, Cfg.Offset.Y, 0) : Cfg.Offset);
		return FInfo(GridSize, Coord, b2D);
	}

	FName SuffixedName(const FName Base, const FName Suffix)
	{
		return Suffix.IsNone() ? Base : FName(Base.ToString() + Suffix.ToString());
	}

	// Validates an attribute name and guards against duplicates within one output set (domain).
	// A duplicate would, via FindOrCreateAttribute's type-mismatch overwrite, dangle an earlier
	// attribute pointer -- so we skip-and-warn instead.
	bool AcceptName(const FName Name, TSet<FName>& Used, FPCGContext* Context)
	{
		if (Name.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name))
		{
			PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Partition Identifier: output attribute name '%s' is invalid; that output is skipped."), *Name.ToString())), Context);
			return false;
		}
		bool bDuplicate = false;
		Used.Add(Name, &bDuplicate);
		if (bDuplicate)
		{
			PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Partition Identifier: output attribute name '%s' is used by more than one output; the duplicate is skipped."), *Name.ToString())), Context);
			return false;
		}
		return true;
	}

	// Executing Component mode: introspect the current cell (+ relatives) and emit an attribute set.
	void ExecuteCurrent(
		FPCGContext* Context, const UPCGExPartitionIdentifierSettings* Settings, const IPCGGraphExecutionSource* Source,
		const UPCGComponent* Component, const bool bRuntime)
	{
		UPCGParamData* ParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		check(ParamData && ParamData->Metadata);

		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
			Output.Data = ParamData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}

		if (!Source)
		{
			PCGLog::LogWarningOnGraph(FTEXT("Partition Identifier (Executing Component) ran without an execution source; nothing to introspect."), Context);
			return;
		}

		const FBox Bounds = Source->GetExecutionState().GetBounds();
		if (!Bounds.IsValid)
		{
			PCGLog::LogWarningOnGraph(FTEXT("Partition Identifier (Executing Component): the execution source has no valid bounds; cannot determine the current partition."), Context);
			return;
		}
		const FVector Anchor = Bounds.GetCenter();

		TSet<FName> Used;

		if (Settings->Layout == EPCGExPartitionLayout::Columns)
		{
			// One suffixed attribute per entry on the @Data domain (single value each).
			auto Emit = [&]<typename T>(const FName Base, const FName Suffix, const T& Value)
			{
				const FName Name = SuffixedName(Base, Suffix);
				if (AcceptName(Name, Used, Context)) { PCGExData::Helpers::SetDataValue<T>(ParamData, Name, Value); }
			};

			for (const FPCGExPartitionGrid& Cfg : Settings->Grids)
			{
				const FInfo Info = MakeInfo(Cfg, Anchor, Component);
				if (Settings->bOutputPartitionId) { Emit(Settings->PartitionIdAttributeName, Cfg.Suffix, MakeId(Info, Settings, bRuntime)); }
				if (Settings->bOutputGridSize) { Emit(Settings->GridSizeAttributeName, Cfg.Suffix, static_cast<int32>(Info.GridSize)); }
				if (Settings->Outputs.bGridCoord) { Emit(Settings->Outputs.GridCoordAttributeName, Cfg.Suffix, CoordVector(Info.Coord)); }
				if (Settings->Outputs.bCellCenter) { Emit(Settings->Outputs.CellCenterAttributeName, Cfg.Suffix, Info.CellCenter()); }
				if (Settings->Outputs.bCellBounds)
				{
					Emit(Settings->Outputs.CellMinAttributeName, Cfg.Suffix, Info.CellMinCorner());
					Emit(Settings->Outputs.CellMaxAttributeName, Cfg.Suffix, Info.CellMaxCorner());
				}
			}
			return;
		}

		// Rows: one entry per grid config, structured outputs per row.
		UPCGMetadata* Metadata = ParamData->Metadata;
		FPCGMetadataDomain* Domain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		check(Domain);

		FPCGMetadataAttribute<FString>* IdAttr = nullptr;
		FPCGMetadataAttribute<int32>* GridSizeAttr = nullptr;
		FPCGMetadataAttribute<FVector>* CoordAttr = nullptr;
		FPCGMetadataAttribute<FVector>* CenterAttr = nullptr;
		FPCGMetadataAttribute<FVector>* MinAttr = nullptr;
		FPCGMetadataAttribute<FVector>* MaxAttr = nullptr;
		FPCGMetadataAttribute<FString>* LabelAttr = nullptr;

		if (Settings->bOutputPartitionId && AcceptName(Settings->PartitionIdAttributeName, Used, Context)) { IdAttr = Domain->FindOrCreateAttribute<FString>(Settings->PartitionIdAttributeName, FString(), false, true); }
		if (Settings->bOutputGridSize && AcceptName(Settings->GridSizeAttributeName, Used, Context)) { GridSizeAttr = Domain->FindOrCreateAttribute<int32>(Settings->GridSizeAttributeName, 0, false, true); }
		if (Settings->Outputs.bGridCoord && AcceptName(Settings->Outputs.GridCoordAttributeName, Used, Context)) { CoordAttr = Domain->FindOrCreateAttribute<FVector>(Settings->Outputs.GridCoordAttributeName, FVector::ZeroVector, false, true); }
		if (Settings->Outputs.bCellCenter && AcceptName(Settings->Outputs.CellCenterAttributeName, Used, Context)) { CenterAttr = Domain->FindOrCreateAttribute<FVector>(Settings->Outputs.CellCenterAttributeName, FVector::ZeroVector, false, true); }
		if (Settings->Outputs.bCellBounds)
		{
			if (AcceptName(Settings->Outputs.CellMinAttributeName, Used, Context)) { MinAttr = Domain->FindOrCreateAttribute<FVector>(Settings->Outputs.CellMinAttributeName, FVector::ZeroVector, false, true); }
			if (AcceptName(Settings->Outputs.CellMaxAttributeName, Used, Context)) { MaxAttr = Domain->FindOrCreateAttribute<FVector>(Settings->Outputs.CellMaxAttributeName, FVector::ZeroVector, false, true); }
		}
		if (Settings->Outputs.bRowLabel && AcceptName(Settings->Outputs.RowLabelAttributeName, Used, Context)) { LabelAttr = Domain->FindOrCreateAttribute<FString>(Settings->Outputs.RowLabelAttributeName, FString(), false, true); }

		for (const FPCGExPartitionGrid& Cfg : Settings->Grids)
		{
			const FInfo Info = MakeInfo(Cfg, Anchor, Component);
			const PCGMetadataEntryKey Key = Metadata->AddEntry();
			if (IdAttr) { IdAttr->SetValue(Key, MakeId(Info, Settings, bRuntime)); }
			if (GridSizeAttr) { GridSizeAttr->SetValue(Key, static_cast<int32>(Info.GridSize)); }
			if (CoordAttr) { CoordAttr->SetValue(Key, CoordVector(Info.Coord)); }
			if (CenterAttr) { CenterAttr->SetValue(Key, Info.CellCenter()); }
			if (MinAttr) { MinAttr->SetValue(Key, Info.CellMinCorner()); }
			if (MaxAttr) { MaxAttr->SetValue(Key, Info.CellMaxCorner()); }
			if (LabelAttr) { LabelAttr->SetValue(Key, Cfg.Suffix.IsNone() ? FString() : Cfg.Suffix.ToString()); }
		}
	}

	// Input Positions mode: classify each input point into one id column per grid entry
	// (per-point, Elements domain); grid sizes are data-level and go on the @Data domain.
	void ExecutePositions(
		FPCGContext* Context, const UPCGExPartitionIdentifierSettings* Settings,
		const UPCGComponent* Component, const bool bRuntime)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

		for (const FPCGTaggedData& Input : Inputs)
		{
			if (!Cast<UPCGBasePointData>(Input.Data))
			{
				continue;
			}

			UPCGData* DuplicateData = Input.Data->DuplicateData(Context, /*bInitializeMetadata=*/true);
			UPCGBasePointData* OutPointData = Cast<UPCGBasePointData>(DuplicateData);
			if (!OutPointData)
			{
				continue;
			}

			// Read query positions (defaults to point location). Resolved against the output
			// copy so the selector sees the same attributes as the input. AllowConstructible
			// (no broadcast) so a scalar source errors instead of silently fanning to (v,v,v).
			const FPCGAttributePropertyInputSelector Selector = Settings->PositionSource.CopyAndFixLast(OutPointData);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(OutPointData, Selector);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(OutPointData, Selector);
			if (!Accessor || !Keys)
			{
				PCGLog::LogWarningOnGraph(FTEXT("Partition Identifier: could not resolve the Position Source on an input; skipping it."), Context);
				continue;
			}

			TArray<FVector> Positions;
			Positions.SetNumUninitialized(Keys->GetNum());
			if (!Accessor->GetRange<FVector>(Positions, 0, *Keys, EPCGAttributeAccessorFlags::AllowConstructible))
			{
				PCGLog::LogWarningOnGraph(FTEXT("Partition Identifier: the Position Source is not convertible to a vector; skipping it."), Context);
				continue;
			}

			FPCGMetadataDomain* Domain = OutPointData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
			check(Domain);

			// Per entry: resolve its grid once, create the per-point id attribute, and write the
			// data-level grid size to @Data. Names are deduped per domain (ids vs @Data grid sizes).
			struct FColumn
			{
				uint32 GridSize;
				bool b2D;
				FIntVector Offset;
				FPCGMetadataAttribute<FString>* IdAttr;

				FColumn(const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, FPCGMetadataAttribute<FString>* InIdAttr)
					: GridSize(InGridSize), b2D(bIn2D), Offset(InOffset), IdAttr(InIdAttr)
				{
				}
			};

			TArray<FColumn> Columns;
			Columns.Reserve(Settings->Grids.Num());
			TSet<FName> UsedElements;
			TSet<FName> UsedData;

			for (const FPCGExPartitionGrid& Cfg : Settings->Grids)
			{
				uint32 GridSize;
				bool b2D;
				ResolveGrid(Cfg, Component, GridSize, b2D);

				if (Settings->bOutputGridSize)
				{
					const FName Name = SuffixedName(Settings->GridSizeAttributeName, Cfg.Suffix);
					if (AcceptName(Name, UsedData, Context)) { PCGExData::Helpers::SetDataValue<int32>(OutPointData, Name, static_cast<int32>(GridSize)); }
				}

				if (Settings->bOutputPartitionId)
				{
					const FName Name = SuffixedName(Settings->PartitionIdAttributeName, Cfg.Suffix);
					if (AcceptName(Name, UsedElements, Context))
					{
						FPCGMetadataAttribute<FString>* IdAttr = Domain->FindOrCreateAttribute<FString>(Name, FString(), false, true);
						if (IdAttr) { Columns.Emplace(GridSize, b2D, Cfg.Offset, IdAttr); }
					}
				}
			}

			if (Columns.IsEmpty())
			{
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
				Output.Data = OutPointData;
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
				continue;
			}

			TPCGValueRange<PCGMetadataEntryKey> MetadataEntries = OutPointData->GetMetadataEntryValueRange(/*bAllocate=*/true);

			// Sequential: InitializeOnSet and SetValue both mutate shared metadata structures,
			// so this loop must not be parallelized over points.
			const int32 NumPoints = FMath::Min(OutPointData->GetNumPoints(), Positions.Num());
			for (int32 i = 0; i < NumPoints; ++i)
			{
				Domain->InitializeOnSet(MetadataEntries[i]);
				for (const FColumn& Column : Columns)
				{
					const FIntVector Base = UPCGActorHelpers::GetCellCoord(Positions[i], Column.GridSize, Column.b2D);
					const FIntVector Coord = Base + (Column.b2D ? FIntVector(Column.Offset.X, Column.Offset.Y, 0) : Column.Offset);
					Column.IdAttr->SetValue(MetadataEntries[i], MakeId(FInfo(Column.GridSize, Coord, Column.b2D), Settings, bRuntime));
				}
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
			Output.Data = OutPointData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
}

#pragma region UPCGExPartitionIdentifierSettings

UPCGExPartitionIdentifierSettings::UPCGExPartitionIdentifierSettings()
{
	PositionSource.SetPointProperty(EPCGPointProperties::Position);
	// Seed one default entry (the cell itself) for new nodes; a cleared array stays empty.
	Grids.Add(FPCGExPartitionGrid());
}

TArray<FPCGPinProperties> UPCGExPartitionIdentifierSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (Source == EPCGExPartitionSource::InputPositions)
	{
		PCGEX_PIN_POINTS(PCGPinConstants::DefaultInputLabel, "Positions to classify into partitions.", Required)
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPartitionIdentifierSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (Source == EPCGExPartitionSource::InputPositions)
	{
		PCGEX_PIN_POINTS(PCGPinConstants::DefaultOutputLabel, "Input points, with the partition id(s) written per point.", Normal)
	}
	else
	{
		PCGEX_PIN_PARAMS(PCGPinConstants::DefaultOutputLabel, "Partition info: the current cell plus any configured relatives.", Normal)
	}
	return PinProperties;
}

FPCGElementPtr UPCGExPartitionIdentifierSettings::CreateElement() const
{
	return MakeShared<FPCGExPartitionIdentifierElement>();
}

#pragma endregion

#pragma region FPCGExPartitionIdentifierElement

bool FPCGExPartitionIdentifierElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPartitionIdentifierElement::Execute);
	check(Context);

	const UPCGExPartitionIdentifierSettings* Settings = Context->GetInputSettings<UPCGExPartitionIdentifierSettings>();
	check(Settings);

	const IPCGGraphExecutionSource* Source = Context->ExecutionSource.Get();
	const UPCGComponent* Component = Cast<UPCGComponent>(Source);
	const bool bRuntime = PCGHelpers::IsRuntimeGeneration(Source);

	if (Settings->Source == EPCGExPartitionSource::InputPositions)
	{
		PCGExPartitionIdentifier::ExecutePositions(Context, Settings, Component, bRuntime);
	}
	else
	{
		PCGExPartitionIdentifier::ExecuteCurrent(Context, Settings, Source, Component, bRuntime);
	}

	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
