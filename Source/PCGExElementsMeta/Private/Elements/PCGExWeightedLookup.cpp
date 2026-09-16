// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWeightedLookup.h"

#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExWeightedLookupElement"
#define PCGEX_NAMESPACE WeightedLookup

#pragma region UPCGExWeightedLookupSettings

UPCGExWeightedLookupSettings::UPCGExWeightedLookupSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MapKey.Update(TEXT("Key"));
	PointKey.Update(TEXT("Key"));
	WeightsKey.Update(TEXT("Key"));
}

TArray<FPCGPinProperties> UPCGExWeightedLookupSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PARAM(PCGExWeightedLookup::Labels::SourceMapLabel, "One row per candidate. The key column names the attribute holding the row's weight; every other column can be forwarded to the picked point.", Required)
	if (WeightsSource == EPCGExWeightedLookupWeightsSource::External)
	{
		PCGEX_PIN_ANY(PCGExWeightedLookup::Labels::SourceWeightsLabel, "Rows carrying the weight attributes named by the Map. Each point selects its row by key.", Required)
	}
	return PinProperties;
}

PCGExData::EIOInit UPCGExWeightedLookupSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_INITIALIZE_ELEMENT(WeightedLookup)
PCGEX_ELEMENT_BATCH_POINT_IMPL(WeightedLookup)

#pragma endregion

#pragma region FPCGExWeightedLookupElement

bool FPCGExWeightedLookupElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(WeightedLookup)

	// Output attributes take precedence over same-named Map columns: two writers on one attribute name
	// would otherwise replace each other's attribute on a type mismatch.
	if (Settings->bOutputKey)
	{
		PCGEX_VALIDATE_NAME(Settings->KeyAttributeName)
		Context->IgnoredColumns.Add(Settings->KeyAttributeName);
	}

	if (Settings->bOutputRowIndex)
	{
		PCGEX_VALIDATE_NAME(Settings->RowIndexAttributeName)
		Context->IgnoredColumns.Add(Settings->RowIndexAttributeName);
	}

	if (Settings->bOutputWeight)
	{
		PCGEX_VALIDATE_NAME(Settings->WeightAttributeName)
		Context->IgnoredColumns.Add(Settings->WeightAttributeName);
	}

	if (Context->InputData.GetInputsByPin(PCGExWeightedLookup::Labels::SourceMapLabel).Num() > 1)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Map holds more than one data; only the first one is used."));
	}

	Context->MapFacade = PCGExData::TryGetSingleFacade(Context, PCGExWeightedLookup::Labels::SourceMapLabel, true, true);
	if (!Context->MapFacade)
	{
		return false;
	}

	Context->NumRows = Context->MapFacade->GetNum();

	const TSharedPtr<PCGExData::TBuffer<FName>> MapKeyReader = Context->MapFacade->GetBroadcaster<FName>(Settings->MapKey);
	if (!MapKeyReader)
	{
		PCGEX_LOG_INVALID_SELECTOR_C(Context, Map Key, Settings->MapKey)
		return false;
	}

	// CopyAndFixLast resolves @Last the same way the broadcaster does.
	const FPCGAttributePropertyInputSelector FixedMapKey = Settings->MapKey.CopyAndFixLast(Context->MapFacade->GetIn());
	Context->IgnoredColumns.Add(PCGExMetaHelpers::GetAttributeIdentifier(FixedMapKey, Context->MapFacade->GetIn()).Name);

	static_cast<FPCGExNameFiltersDetails&>(Context->ForwardDetails) = Settings->Columns;

	Context->MapKeys.SetNumUninitialized(Context->NumRows);
	for (int32 Row = 0; Row < Context->NumRows; Row++)
	{
		Context->MapKeys[Row] = MapKeyReader->Read(Row);
	}

	if (Settings->Fallback == EPCGExWeightedLookupFallback::FixedRow)
	{
		Context->FallbackRow = FMath::Clamp(Settings->FallbackRowIndex, 0, Context->NumRows - 1);
		if (Context->FallbackRow != Settings->FallbackRowIndex)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Fallback row index {0} is out of range, clamped to {1}."), FText::AsNumber(Settings->FallbackRowIndex), FText::AsNumber(Context->FallbackRow)));
		}
	}

	if (Settings->WeightsSource == EPCGExWeightedLookupWeightsSource::External)
	{
		if (Context->InputData.GetInputsByPin(PCGExWeightedLookup::Labels::SourceWeightsLabel).Num() > 1)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Weights holds more than one data; only the first one is used."));
		}

		Context->WeightsFacade = PCGExData::TryGetSingleFacade(Context, PCGExWeightedLookup::Labels::SourceWeightsLabel, true, true);
		if (!Context->WeightsFacade)
		{
			return false;
		}

		const UPCGBasePointData* WeightsData = Context->WeightsFacade->GetIn();
		const FPCGAttributeIdentifier KeyIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->WeightsKey.CopyAndFixLast(WeightsData), WeightsData);
		const TSharedPtr<PCGExData::IBuffer> WeightsKeyReader = Context->WeightsFacade->GetDefaultReadable(KeyIdentifier, PCGExData::EIOSide::In, false);
		if (!WeightsKeyReader)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(Context, Weights Key, Settings->WeightsKey)
			return false;
		}

		Context->WeightsKeyType = WeightsKeyReader->GetTypeId();

		const int32 NumWeightRows = WeightsKeyReader->GetNumValues(PCGExData::EIOSide::In);
		Context->WeightsRowByKey.Reserve(NumWeightRows);

		bool bDuplicateKeys = false;
		for (int32 Row = 0; Row < NumWeightRows; Row++)
		{
			const PCGExValueHash Hash = WeightsKeyReader->ReadValueHash(Row);
			if (Context->WeightsRowByKey.Contains(Hash))
			{
				bDuplicateKeys = true;
				continue;
			}
			Context->WeightsRowByKey.Add(Hash, Row);
		}

		if (bDuplicateKeys)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Weights contain duplicate keys; the first row of each key is used."));
		}

		PCGExWeightedLookup::BuildWeightReaders(Context, Settings, Context->WeightsFacade, false, Context->ExternalWeightReaders);
	}

	const bool bRandom = Settings->PickMode == EPCGExWeightedLookupPickMode::WeightedRandom;
	Context->Exponent = FMath::Max(Settings->Exponent, 0.001);
	Context->Contrast = FMath::Max(Settings->Contrast, 0.001);
	Context->bApplyExponent = bRandom && !FMath::IsNearlyEqual(Context->Exponent, 1.0);
	Context->bApplyContrast = bRandom && !FMath::IsNearlyEqual(Context->Contrast, 1.0);

	return true;
}

bool FPCGExWeightedLookupElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWeightedLookupElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WeightedLookup)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
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
	Context->Done();

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExWeightedLookup::FProcessor

namespace PCGExWeightedLookup
{
	void BuildWeightReaders(FPCGExWeightedLookupContext* InContext, const UPCGExWeightedLookupSettings* InSettings, const TSharedPtr<PCGExData::FFacade>& InFacade, const bool bScoped, TArray<TSharedPtr<PCGExData::TBuffer<double>>>& OutReaders)
	{
		const int32 NumRows = InContext->MapKeys.Num();
		OutReaders.Init(nullptr, NumRows);

		for (int32 Row = 0; Row < NumRows; Row++)
		{
			const FName Key = InContext->MapKeys[Row];
			OutReaders[Row] = InFacade->GetBroadcaster<double>(Key, bScoped, false, true);

			if (OutReaders[Row] || InSettings->bQuietMissingWeightAttribute)
			{
				continue;
			}

			bool bAlreadyReported = false;
			{
				FScopeLock Lock(&InContext->MissingWeightsLock);
				InContext->ReportedMissingWeights.Add(Key, &bAlreadyReported);
			}

			if (!bAlreadyReported)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Weight attribute \"{0}\" (Map row {1}) is missing; that row reads as weight 0."), FText::FromName(Key), FText::AsNumber(Row)));
			}
		}
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWeightedLookup::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		NumRows = Context->NumRows;
		Component = Context->GetComponent();

		if (Settings->WeightsSource == EPCGExWeightedLookupWeightsSource::External)
		{
			WeightReaders = &Context->ExternalWeightReaders;

			const UPCGBasePointData* InData = PointDataFacade->GetIn();
			const FPCGAttributeIdentifier KeyIdentifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->PointKey.CopyAndFixLast(InData), InData);
			PointKeyReader = PointDataFacade->GetDefaultReadable(KeyIdentifier, PCGExData::EIOSide::In, true);
			if (!PointKeyReader)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(Context, Point Key, Settings->PointKey)
				return false;
			}

			// Keys compare by value hash; hashes of different types only coincide by accident.
			if (PointKeyReader->GetTypeId() != Context->WeightsKeyType)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Point key and Weights key have different types; keys are unlikely to match."));
			}
		}
		else
		{
			BuildWeightReaders(Context, Settings, PointDataFacade, true, SelfWeightReaders);
			WeightReaders = &SelfWeightReaders;
		}

		ActiveRows.Reserve(NumRows);
		for (int32 Row = 0; Row < NumRows; Row++)
		{
			if ((*WeightReaders)[Row])
			{
				ActiveRows.Add(Row);
			}
		}

		Forward = Context->ForwardDetails.GetHandler(Context->MapFacade, PointDataFacade, false, &Context->IgnoredColumns);

		if (Settings->bOutputKey)
		{
			KeyWriter = PointDataFacade->GetWritable<FName>(Settings->KeyAttributeName, NAME_None, false, PCGExData::EBufferInit::New);
		}

		if (Settings->bOutputRowIndex)
		{
			RowIndexWriter = PointDataFacade->GetWritable<int32>(Settings->RowIndexAttributeName, -1, false, PCGExData::EBufferInit::New);
		}

		if (Settings->bOutputWeight)
		{
			WeightWriter = PointDataFacade->GetWritable<double>(Settings->WeightAttributeName, 0.0, true, PCGExData::EBufferInit::New);
		}

		StartParallelLoopForPoints();

		return true;
	}

	template <bool bExternal, bool bExponent, bool bContrast>
	void FProcessor::ProcessScope(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);

		const TArray<TSharedPtr<PCGExData::TBuffer<double>>>& Readers = *WeightReaders;
		const TConstPCGValueRange<int32> Seeds = PointDataFacade->GetOut()->GetConstSeedValueRange();

		const bool bRandom = Settings->PickMode == EPCGExWeightedLookupPickMode::WeightedRandom;
		const int32 ContrastCurve = static_cast<int32>(Settings->ContrastCurve);

		// Missing rows are never written and stay at 0 across the whole scope.
		TArray<double, TInlineAllocator<32>> Weights;
		TArray<double, TInlineAllocator<32>> Shaped;
		Weights.Init(0.0, NumRows);
		if constexpr (bExponent || bContrast)
		{
			Shaped.SetNumUninitialized(NumRows);
		}

		PCGEX_SCOPE_LOOP(Index)
		{
			int32 WeightsIndex = Index;
			if constexpr (bExternal)
			{
				const int32* Row = Context->WeightsRowByKey.Find(PointKeyReader->ReadValueHash(Index));
				if (!Row)
				{
					CommitFallback(Index, Seeds[Index]);
					continue;
				}
				WeightsIndex = *Row;
			}

			double Total = 0.0;
			double MaxWeight = 0.0;
			int32 Best = -1;

			for (const int32 Row : ActiveRows)
			{
				// Not FMath::Max: NaN must read as 0, not propagate into the total.
				const double Raw = Readers[Row]->Read(WeightsIndex);
				const double Weight = Raw > 0.0 ? Raw : 0.0;
				Weights[Row] = Weight;
				Total += Weight;
				if (Weight > MaxWeight)
				{
					MaxWeight = Weight;
					Best = Row;
				}
			}

			if (Total <= 0.0)
			{
				CommitFallback(Index, Seeds[Index]);
				continue;
			}

			if (!bRandom)
			{
				Commit(Index, Best, MaxWeight);
				continue;
			}

			int32 Picked = -1;

			if constexpr (bExponent || bContrast)
			{
				// Shaping runs on max-normalized weights: the dominant row maps to exactly 1 under every
				// curve, so the shaped total stays strictly positive, and 0 stays 0 (knobs are > 0).
				const double InvMax = 1.0 / MaxWeight;
				double ShapedTotal = 0.0;
				for (int32 Row = 0; Row < NumRows; Row++)
				{
					double Weight = Weights[Row] * InvMax;
					if constexpr (bExponent)
					{
						Weight = FMath::Pow(Weight, Context->Exponent);
					}
					if constexpr (bContrast)
					{
						Weight = PCGExMath::Contrast::ApplyContrast(Weight, Context->Contrast, ContrastCurve);
					}
					Shaped[Row] = Weight;
					ShapedTotal += Weight;
				}

				Picked = PCGExRandomHelpers::RollWeightedStreaming(NumRows, [&](const int32 Row) { return Shaped[Row]; }, ShapedTotal, SeedFor(Seeds[Index]));
			}
			else
			{
				Picked = PCGExRandomHelpers::RollWeightedStreaming(NumRows, [&](const int32 Row) { return Weights[Row]; }, Total, SeedFor(Seeds[Index]));
			}

			// Total > 0 here, so the roll always lands on a row.
			Commit(Index, Picked, Weights[Picked]);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWeightedLookup::ProcessPoints);

		const int32 Path = (Settings->WeightsSource == EPCGExWeightedLookupWeightsSource::External ? 4 : 0) | (Context->bApplyExponent ? 2 : 0) | (Context->bApplyContrast ? 1 : 0);

		switch (Path)
		{
		case 0: ProcessScope<false, false, false>(Scope);
			break;
		case 1: ProcessScope<false, false, true>(Scope);
			break;
		case 2: ProcessScope<false, true, false>(Scope);
			break;
		case 3: ProcessScope<false, true, true>(Scope);
			break;
		case 4: ProcessScope<true, false, false>(Scope);
			break;
		case 5: ProcessScope<true, false, true>(Scope);
			break;
		case 6: ProcessScope<true, true, false>(Scope);
			break;
		case 7: ProcessScope<true, true, true>(Scope);
			break;
		default: checkNoEntry();
			break;
		}
	}

	int32 FProcessor::SeedFor(const int32 PointSeed) const
	{
		return PCGExRandomHelpers::GetSeed(PointSeed, Settings->SeedComponents, Settings->LocalSeed, Settings, Component);
	}

	void FProcessor::Commit(const int32 Index, const int32 Row, const double Weight)
	{
		if (Row >= 0)
		{
			Forward->Forward(Row, Index);
		}

		if (KeyWriter)
		{
			KeyWriter->SetValue(Index, Row >= 0 ? Context->MapKeys[Row] : NAME_None);
		}

		if (RowIndexWriter)
		{
			RowIndexWriter->SetValue(Index, Row);
		}

		if (WeightWriter)
		{
			WeightWriter->SetValue(Index, Weight);
		}
	}

	void FProcessor::CommitFallback(const int32 Index, const int32 PointSeed)
	{
		int32 Row = -1;

		switch (Settings->Fallback)
		{
		case EPCGExWeightedLookupFallback::None:
			break;
		case EPCGExWeightedLookupFallback::UniformRandom:
			Row = FRandomStream(SeedFor(PointSeed)).RandRange(0, NumRows - 1);
			break;
		case EPCGExWeightedLookupFallback::FixedRow:
			Row = Context->FallbackRow;
			break;
		default:
			checkNoEntry();
			break;
		}

		Commit(Index, Row, 0.0);
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
