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
#include "Helpers/PCGExMetaHelpers.h"
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
	FString MakeIdPrefix(const UPCGExPartitionIdentifierSettings* Settings, const bool bRuntime)
	{
		return PCGExPartitionGrid::ResolveIdPrefix(Settings->bMatchEnginePartitionActorName, bRuntime, Settings->IdPrefix);
	}

	FName SuffixedName(const FName Base, const FName Suffix)
	{
		return Suffix.IsNone() ? Base : FName(Base.ToString() + Suffix.ToString());
	}

	void WarnIfComponentless(FPCGContext* Context, const UPCGExPartitionIdentifierSettings* Settings, const IPCGGraphExecutionSource* Source)
	{
		PCGExPartitionGrid::FComponentReads Reads;
		for (const FPCGExPartitionGrid& Cfg : Settings->Grids) { Reads.Add(Cfg); }
		PCGExPartitionGrid::WarnIfComponentless(Reads, Source, Context);
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

		FVector Anchor = FVector::ZeroVector;
		if (!PCGExPartitionGrid::TryGetAnchor(Source, Anchor))
		{
			PCGLog::LogWarningOnGraph(FTEXT("Partition Identifier (Executing Component): the execution source has no valid bounds; cannot determine the current partition."), Context);
			return;
		}

		WarnIfComponentless(Context, Settings, Source);

		TSet<FName> Used;
		const FString IdPrefix = MakeIdPrefix(Settings, bRuntime);

		if (Settings->Layout == EPCGExPartitionLayout::Columns)
		{
			// One suffixed attribute per entry on the @Data domain (single value each).
			auto Emit = [&]<typename T>(const FName Base, const FName Suffix, const T& Value)
			{
				const FName Name = SuffixedName(Base, Suffix);
				if (PCGExPartitionGrid::AcceptOutputName(Name, Used, Context)) { PCGExData::Helpers::SetDataValue<T>(ParamData, Name, Value); }
			};

			for (const FPCGExPartitionGrid& Cfg : Settings->Grids)
			{
				const PCGExPartitionGrid::FCell Info = PCGExPartitionGrid::MakeCell(Cfg, Anchor, Component);
				if (Settings->bOutputPartitionId) { Emit(Settings->PartitionIdAttributeName, Cfg.Suffix, PCGExPartitionGrid::FormatId(Info, IdPrefix)); }
				if (Settings->bOutputGridSize) { Emit(Settings->GridSizeAttributeName, Cfg.Suffix, static_cast<int32>(Info.GridSize)); }
				if (Settings->Outputs.bGridCoord) { Emit(Settings->Outputs.GridCoordAttributeName, Cfg.Suffix, Info.CoordAsVector()); }
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

		PCGExPartitionGrid::FCellColumns Columns;
		Columns.IdPrefix = IdPrefix;
		if (Settings->bOutputPartitionId && PCGExPartitionGrid::AcceptOutputName(Settings->PartitionIdAttributeName, Used, Context)) { Columns.PartitionId = Settings->PartitionIdAttributeName; }
		if (Settings->bOutputGridSize && PCGExPartitionGrid::AcceptOutputName(Settings->GridSizeAttributeName, Used, Context)) { Columns.GridSize = Settings->GridSizeAttributeName; }
		PCGExPartitionGrid::AddCellColumns(Settings->Outputs, Columns, Used, Context);
		const bool bLabel = Settings->Outputs.bRowLabel && PCGExPartitionGrid::AcceptOutputName(Settings->Outputs.RowLabelAttributeName, Used, Context);

		const PCGExPartitionGrid::FCellRowWriter Rows(Domain, Columns);
		FPCGMetadataAttribute<FString>* LabelAttr = bLabel ? Domain->FindOrCreateAttribute<FString>(Settings->Outputs.RowLabelAttributeName, FString(), false, true) : nullptr;

		for (const FPCGExPartitionGrid& Cfg : Settings->Grids)
		{
			const PCGExPartitionGrid::FCell Info = PCGExPartitionGrid::MakeCell(Cfg, Anchor, Component);
			const PCGMetadataEntryKey Key = Metadata->AddEntry();
			Rows.Write(Key, Info);
			if (LabelAttr) { LabelAttr->SetValue(Key, Cfg.Suffix.IsNone() ? FString() : Cfg.Suffix.ToString()); }
		}
	}

	// Input Positions mode: classify each input point into one id column per grid entry
	// (per-point, Elements domain); grid sizes are data-level and go on the @Data domain.
	void ExecutePositions(
		FPCGContext* Context, const UPCGExPartitionIdentifierSettings* Settings, const IPCGGraphExecutionSource* Source,
		const UPCGComponent* Component, const bool bRuntime)
	{
		WarnIfComponentless(Context, Settings, Source);

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
				PCGExPartitionGrid::ResolveGrid(Cfg, Component, GridSize, b2D);

				if (Settings->bOutputGridSize)
				{
					const FName Name = SuffixedName(Settings->GridSizeAttributeName, Cfg.Suffix);
					if (PCGExPartitionGrid::AcceptOutputName(Name, UsedData, Context)) { PCGExData::Helpers::SetDataValue<int32>(OutPointData, Name, static_cast<int32>(GridSize)); }
				}

				if (Settings->bOutputPartitionId)
				{
					const FName Name = SuffixedName(Settings->PartitionIdAttributeName, Cfg.Suffix);
					if (PCGExPartitionGrid::AcceptOutputName(Name, UsedElements, Context))
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

			// One batch for every entry InitializeOnSet would create, rather than a lock per point.
			TPCGValueRange<PCGMetadataEntryKey> MetadataEntries = OutPointData->GetMetadataEntryValueRange(/*bAllocate=*/true);
			PCGExMetaHelpers::InitializeMetadataEntries(OutPointData->MutableMetadata(), MetadataEntries, /*bConservative=*/true);

			const int32 NumPoints = FMath::Min(OutPointData->GetNumPoints(), Positions.Num());
			TArray<PCGMetadataEntryKey> EntryKeys;
			EntryKeys.SetNumUninitialized(NumPoints);
			for (int32 i = 0; i < NumPoints; ++i) { EntryKeys[i] = MetadataEntries[i]; }

			const FString IdPrefix = MakeIdPrefix(Settings, bRuntime);
			const TConstArrayView<FVector> PointPositions = TConstArrayView<FVector>(Positions).Left(NumPoints);
			for (const FColumn& Column : Columns)
			{
				if (!PCGExPartitionGrid::WriteCellIds(Column.IdAttr, Domain, EntryKeys, PointPositions, Column.GridSize, Column.b2D, Column.Offset, IdPrefix))
				{
					PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Partition Identifier: could not write '%s'; it is left unset."), *Column.IdAttr->Name.ToString())), Context);
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
		PCGExPartitionIdentifier::ExecutePositions(Context, Settings, Source, Component, bRuntime);
	}
	else
	{
		PCGExPartitionIdentifier::ExecuteCurrent(Context, Settings, Source, Component, bRuntime);
	}

	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
