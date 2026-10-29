// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDistributeTuple.h"

#include "PCGExPropertyWriter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Metadata/PCGMetadataAttribute.h"

#if WITH_EDITOR
#include "Editor.h"
#include "UObject/UObjectGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExDistributeTupleElement"
#define PCGEX_NAMESPACE DistributeTuple

#pragma region UPCGExDistributeTupleSettings

TArray<FPCGPinProperties> UPCGExDistributeTupleSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExProperties::AddOutputMapPin(PinProperties, bOutputMap);
	return PinProperties;
}

void FPCGExDistributeTupleContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	const UPCGExDistributeTupleSettings* Settings = GetInputSettings<UPCGExDistributeTupleSettings>();
	if (!Settings)
	{
		return;
	}

	TSet<FSoftObjectPath> Paths;
	PCGExProperties::GatherOutputDependencies(Settings->Composition, Paths);
	for (const FPCGExWeightedPropertyOverrides& Row : Settings->Values)
	{
		PCGExProperties::GatherOutputDependencies(Row, Paths);
	}
	AddAssetDependencies(Paths);
}

#if WITH_EDITOR
const FPCGExPropertySchemaCollection* UPCGExDistributeTupleSettings::EDITOR_GetRowSchema() const
{
	return &Composition;
}

void UPCGExDistributeTupleSettings::EDITOR_ForEachPropertyRows(TFunctionRef<void(FPCGExPropertyOverrides& Rows, bool bIsImportLayer)> Visitor)
{
	Visitor(Composition.ImportOverrides, true);
	for (FPCGExWeightedPropertyOverrides& Row : Values)
	{
		Visitor(Row, false);
	}
}

void UPCGExDistributeTupleSettings::EDITOR_NotifyRowsChanged()
{
	// A value-level edit of Values: dirties the cache and refreshes the node, no schema sync.
	FPropertyChangedEvent Event(FindFProperty<FProperty>(GetClass(), GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values)), EPropertyChangeType::ValueSet);
	PostEditChangeProperty(Event);
}

void UPCGExDistributeTupleSettings::EDITOR_OnImportedSchemaChanged(UPCGExPropertySchemaAsset* Asset)
{
	bool bChanged = Composition.SyncImportOverridesInPlace();
	if (Composition.ApplyToRows(Values))
	{
		bChanged = true;
		// The rows may have been rebuilt, exactly as after an edit of Composition.
		PCGExPropertyRowHost::BroadcastRowsRebuilt(this, GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values));
	}
	if (bChanged)
	{
		(void)MarkPackageDirty();
	}
	// Whatever the rows did: a disabled column outputs the asset's own default, which may be what changed.
	EDITOR_NotifyRowsChanged();
}

void UPCGExDistributeTupleSettings::PostLoad()
{
	// An import can change while this graph is unloaded. Before Super, so the CRC it caches sees the healed rows.
	bool bChanged = Composition.ReconcileImportOverridesAtLoad();
	bChanged |= Composition.SyncRowsAtLoad(Values);
	if (bChanged)
	{
		(void)MarkPackageDirty();
	}

	Super::PostLoad();
}

void UPCGExDistributeTupleSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExDistributeTupleSettings::PostEditChangeProperty);

	const PCGExPropertyRowHost::FEditKind Edit = PCGExPropertyRowHost::ClassifyEdit(
		PropertyChangedEvent, GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values));

	if (Edit.bSyncRows)
	{
		// Remap rows before the per-row SyncToSchema: it aliases collided rows otherwise.
		Composition.SyncAllSchemasAndRemapRows(Values);
		Composition.ReconcileImportOverrides();
		Composition.ApplyToRows(Values);

		(void)MarkPackageDirty();

		if (Edit.bSchemaChanged)
		{
			PCGExPropertyRowHost::BroadcastRowsRebuilt(this, GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values));
		}
	}

	DirtyCache();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

PCGExData::EIOInit UPCGExDistributeTupleSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_INITIALIZE_ELEMENT(DistributeTuple)
PCGEX_ELEMENT_BATCH_POINT_IMPL(DistributeTuple)

#pragma endregion

#pragma region PCGExDistributeTuple::FRowPicker

namespace PCGExDistributeTuple
{
	bool FRowPicker::Init(const UPCGExDistributeTupleSettings* InSettings, const UPCGComponent* InComponent)
	{
		Settings = InSettings;

		switch (Settings->Distribution)
		{
		case EPCGExDistribution::Index:
		case EPCGExDistribution::Random:
		case EPCGExDistribution::WeightedRandom:
			break;
		default:
			return false;
		}

		// Parity: Resolve must equal GetSeed(BaseSeed, Settings->SeedComponents, Settings->LocalSeed, Settings, InComponent) for every Pick.
		SeedResolver.Init(Settings->SeedComponents, Settings->LocalSeed, Settings, InComponent);

		const int32 NumRows = Settings->Values.Num();
		MaxRowIndex = NumRows - 1;

		// Build cumulative weight table
		CumulativeWeights.SetNum(NumRows);
		TotalWeight = 0;
		for (int32 i = 0; i < NumRows; ++i)
		{
			TotalWeight += FMath::Max(0, Settings->Values[i].Weight);
			CumulativeWeights[i] = TotalWeight;
		}

		if (TotalWeight == 0 && Settings->Distribution == EPCGExDistribution::WeightedRandom)
		{
			// All weights are zero - fall back to uniform random
			TotalWeight = NumRows;
			for (int32 i = 0; i < NumRows; ++i)
			{
				CumulativeWeights[i] = i + 1;
			}
		}

		return true;
	}

	int32 FRowPicker::Pick(const int32 Index, const int32 BaseSeed) const
	{
		switch (Settings->Distribution)
		{
		case EPCGExDistribution::Index:
			return PCGExMath::SanitizeIndex(Index, MaxRowIndex, Settings->IndexSafety);

		case EPCGExDistribution::Random:
			{
				FRandomStream RandomStream(SeedResolver.Resolve(BaseSeed));
				return RandomStream.RandRange(0, MaxRowIndex);
			}

		case EPCGExDistribution::WeightedRandom:
			{
				FRandomStream RandomStream(SeedResolver.Resolve(BaseSeed));
				const int32 Roll = RandomStream.RandRange(1, TotalWeight);

				// Binary search through cumulative weights
				int32 Lo = 0, Hi = MaxRowIndex;
				while (Lo < Hi)
				{
					const int32 Mid = (Lo + Hi) >> 1;
					if (CumulativeWeights[Mid] < Roll)
					{
						Lo = Mid + 1;
					}
					else
					{
						Hi = Mid;
					}
				}
				return Lo;
			}

		default:
			// Init rejects unknown enumerators.
			checkNoEntry();
			return INDEX_NONE;
		}
	}
}

#pragma endregion

#pragma region PCGExDistributeTuple

namespace PCGExDistributeTuple
{
	void CompileColumns(FPCGExDistributeTupleContext* Context, const UPCGExDistributeTupleSettings* Settings, const TArray<FPCGExPropertyResolved>& Resolved)
	{
		// The edit and load syncs keep Values[k].Overrides parallel to Resolved; nothing below relies on it.
		const int32 NumRows = Settings->Values.Num();

		TSet<FName> WrittenNames;
		auto ClaimName = [&](const FName Name)
		{
			bool bAlreadyClaimed = false;
			WrittenNames.Add(Name, &bAlreadyClaimed);
			if (bAlreadyClaimed)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("More than one output writes \"{0}\"."), FText::FromName(Name)));
			}
		};

		if (Settings->bOutputRowIndex)
		{
			ClaimName(Settings->RowIndexAttributeName);
		}

		if (Settings->bOutputWeight)
		{
			ClaimName(Settings->WeightAttributeName);
		}

		Context->Columns.Reserve(Resolved.Num());
		for (int32 ColIdx = 0; ColIdx < Resolved.Num(); ++ColIdx)
		{
			const FPCGExPropertyResolved& Entry = Resolved[ColIdx];
			const FInstancedStruct& EffectiveProperty = Entry.GetEffectiveProperty();
			const FPCGExProperty* Property = EffectiveProperty.GetPtr<FPCGExProperty>();

			if (!Property || !Property->SupportsOutput())
			{
				continue;
			}

			const FName OutputName = Property->ResolveOutputAttributeName(Entry.Source->Name);

			// IsValidName, not IsWritableAttributeName: selector-shaped names ("Foo.X", "@Last") are reparsed, not written verbatim.
			if (OutputName.IsNone() || !FPCGMetadataAttributeBase::IsValidName(OutputName))
			{
				if (Settings->bOutputToDataDomain)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Column \"{0}\" writes \"{1}\", which is not a valid attribute name -- skipped."), FText::FromName(Entry.Source->Name), FText::FromName(OutputName)));
					continue;
				}

				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Column \"{0}\" writes \"{1}\", which is not a valid attribute name."), FText::FromName(Entry.Source->Name), FText::FromName(OutputName)));
			}

			ClaimName(OutputName);

			FColumn& Column = Context->Columns.Emplace_GetRef();
			Column.OutputName = OutputName;
			Column.EffectiveProperty = &EffectiveProperty;
			Column.RowSources.SetNumUninitialized(NumRows);
			// A cell is its row's enabled entry of this column's name and type, never the entry at ColIdx on position
			// alone: WriteOutputFrom casts a row source to the column's type unchecked.
			for (int32 RowIdx = 0; RowIdx < NumRows; ++RowIdx)
			{
				const FInstancedStruct* Cell = Settings->Values[RowIdx].FindEnabledSlot(Entry.Source->Name, EffectiveProperty.GetScriptStruct(), ColIdx);
				Column.RowSources[RowIdx] = Cell ? Cell->GetPtr<FPCGExProperty>() : nullptr;
			}
		}
	}

	bool HasDataValue(const UPCGData* InData, const FName Name)
	{
		return PCGExMetaHelpers::HasAttribute(InData, PCGExMetaHelpers::MakeDataIdentifier(Name));
	}

	void WriteDataDomainOutputs(FPCGExDistributeTupleContext* Context, const UPCGExDistributeTupleSettings* Settings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExDistributeTuple::WriteDataDomainOutputs);

		const bool bSeedFromIndex = (Settings->SeedComponents & static_cast<uint8>(EPCGExSeedComponents::Local)) != 0;

		bool bWriteRowIndex = Settings->bOutputRowIndex;
		bool bWriteWeight = Settings->bOutputWeight;
		TBitArray<> RefusedColumns(false, Context->Columns.Num());

		// Nothing picked keeps an existing value, else writes 0: the per-point New-init buffers' fallback.
		auto WriteOptional = [&](UPCGData* OutData, const bool bPicked, bool& bWrite, const FName Name, const int32 Value)
		{
			if (!bWrite || (!bPicked && HasDataValue(OutData, Name)))
			{
				return;
			}

			if (!PCGExData::Helpers::SetDataValue<int32>(OutData, Name, bPicked ? Value : 0))
			{
				bWrite = false;
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("\"{0}\" can't be written to @Data -- skipped."), FText::FromName(Name)));
			}
		};

		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
		{
			if (!IO->InitializeOutput(Settings->GetMainDataInitializationPolicy()))
			{
				continue;
			}

			UPCGData* OutData = IO->GetOut();

			// Local swaps the point seed for the input index; without it every input rolls the same row.
			const int32 BaseSeed = bSeedFromIndex ? PCGExRandomHelpers::SeedFromIndex(IO->IOIndex) : 0;
			const int32 PickedRow = Context->RowPicker.Pick(IO->IOIndex, BaseSeed);
			const bool bPicked = PickedRow != INDEX_NONE;

			WriteOptional(OutData, bPicked, bWriteRowIndex, Settings->RowIndexAttributeName, PickedRow);
			WriteOptional(OutData, bPicked, bWriteWeight, Settings->WeightAttributeName, bPicked ? Settings->Values[PickedRow].Weight : 0);

			for (int32 i = 0; i < Context->Columns.Num(); ++i)
			{
				if (RefusedColumns[i])
				{
					continue;
				}

				const FColumn& Column = Context->Columns[i];
				const FPCGExProperty* Source = bPicked ? Column.RowSources[PickedRow] : nullptr;
				if (!Source)
				{
					// Disabled cell or no pick: an existing value survives, as through the per-point Inherit buffer.
					if (HasDataValue(OutData, Column.OutputName))
					{
						continue;
					}

					Source = Column.EffectiveProperty->GetPtr<FPCGExProperty>();
				}

				if (!PCGExProperties::WriteDataDomainValue(OutData, Column.OutputName, *Source))
				{
					RefusedColumns[i] = true;
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Column \"{0}\" ({1}) can't be written to @Data -- skipped."), FText::FromName(Column.OutputName), FText::FromName(Source->GetTypeName())));
					continue;
				}

				if (Settings->bOutputMap && !Source->GetOutputSidecarPin().IsNone())
				{
					Context->SidecarSources.AddUnique(Source);
				}
			}
		}
	}
}

#pragma endregion

#pragma region FPCGExDistributeTupleElement

bool FPCGExDistributeTupleElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(DistributeTuple)

	// Imported schemas are columns too: the resolved table decides, not the locals alone.
	TArray<FPCGExPropertyResolved> Resolved;
	Settings->Composition.Resolve(Resolved);

	// AdvanceWork passes empty tuples through.
	Context->bPassThrough = Resolved.IsEmpty() || Settings->Values.IsEmpty();
	if (Context->bPassThrough)
	{
		return true;
	}

	TArray<FName> Duplicates;
	if (!Settings->Composition.ValidateUniqueNames(Duplicates))
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Composition has duplicate column names."));
		return false;
	}

	if (Settings->bOutputRowIndex)
	{
		PCGEX_VALIDATE_NAME(Settings->RowIndexAttributeName)
	}

	if (Settings->bOutputWeight)
	{
		PCGEX_VALIDATE_NAME(Settings->WeightAttributeName)
	}

	if (!Context->RowPicker.Init(Settings, Context->GetComponent()))
	{
		PCGE_LOG(Error, GraphAndLog, FText::Format(FTEXT("Unresolvable Distribution ({0})."), FText::AsNumber(static_cast<int32>(Settings->Distribution))));
		return false;
	}

	PCGExDistributeTuple::CompileColumns(Context, Settings, Resolved);

	return true;
}

bool FPCGExDistributeTupleElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDistributeTupleElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(DistributeTuple)
	PCGEX_EXECUTION_CHECK

	if (Context->bPassThrough)
	{
		DisabledPassThroughData(InContext);
		Context->Done();
		return Context->TryComplete();
	}

	PCGEX_ON_INITIAL_EXECUTION
	{
		// One row per input needs no per-point pass: no batch starts, so the batch step below falls through.
		if (Settings->bOutputToDataDomain)
		{
			PCGExDistributeTuple::WriteDataDomainOutputs(Context, Settings);
		}
		else if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	if (Settings->bOutputMap && !Context->SidecarSources.IsEmpty())
	{
		PCGExProperties::StageSidecars(Context, Context->SidecarSources);
	}

	Context->Done();

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExDistributeTuple::FProcessor

namespace PCGExDistributeTuple
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExDistributeTuple::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		Columns.Reserve(Context->Columns.Num());
		for (const FColumn& Column : Context->Columns)
		{
			FColumnOutput& Col = Columns.Emplace_GetRef();
			Col.Column = &Column;

			// Deep-copy the effective property (override-or-source) so we own the output buffer
			Col.OwnedProperty = *Column.EffectiveProperty;

			FPCGExProperty* OutputProperty = Col.OwnedProperty.GetMutablePtr<FPCGExProperty>();
			if (!OutputProperty || !OutputProperty->InitializeOutput(PointDataFacade, Column.OutputName))
			{
				Columns.Pop(EAllowShrinking::No);
				continue;
			}

			Col.WriterPtr = OutputProperty;
			OutputProperty->BindWriteSources(Column.RowSources);

			// Parallel writes go through WriteOutputFrom (no clone bookkeeping): sidecar rows come from
			// the per-row sources, plus the column's own value wherever a point can be left holding it.
			if (Settings->bOutputMap && !OutputProperty->GetOutputSidecarPin().IsNone())
			{
				// Index + Ignore leaves out-of-range points unpicked; they keep the buffer default.
				bool bOwnValueReachable = Settings->Distribution == EPCGExDistribution::Index && Settings->IndexSafety == EPCGExIndexSafety::Ignore;

				FScopeLock ScopeLock(&Context->SidecarLock);
				for (const FPCGExProperty* RowSource : Column.RowSources)
				{
					if (RowSource)
					{
						Context->SidecarSources.AddUnique(RowSource);
					}
					else
					{
						bOwnValueReachable = true;
					}
				}

				if (bOwnValueReachable)
				{
					Context->SidecarSources.AddUnique(Column.EffectiveProperty->GetPtr<FPCGExProperty>());
				}
			}
		}

		// Optional output writers
		if (Settings->bOutputRowIndex)
		{
			RowIndexWriter = PointDataFacade->GetWritable<int32>(Settings->RowIndexAttributeName, PCGExData::EBufferInit::New);
		}

		if (Settings->bOutputWeight)
		{
			WeightWriter = PointDataFacade->GetWritable<int32>(Settings->WeightAttributeName, PCGExData::EBufferInit::New);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExDistributeTuple::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		const UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		const TConstPCGValueRange<int32> Seeds = OutPointData->GetConstSeedValueRange();
		const FRowPicker& RowPicker = Context->RowPicker;

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 PickedRow = RowPicker.Pick(Index, Seeds[Index]);
			if (PickedRow == INDEX_NONE)
			{
				continue;
			}

			// Write optional outputs
			if (RowIndexWriter)
			{
				RowIndexWriter->SetValue(Index, PickedRow);
			}
			if (WeightWriter)
			{
				WeightWriter->SetValue(Index, Settings->Values[PickedRow].Weight);
			}

			// Write column values from the picked row
			for (const FColumnOutput& Col : Columns)
			{
				if (const FPCGExProperty* RowSource = Col.Column->RowSources[PickedRow])
				{
					Col.WriterPtr->WriteOutputFrom(Index, RowSource);
				}
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
