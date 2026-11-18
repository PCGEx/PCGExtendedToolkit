// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCopyAttributesByIndex.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Types/PCGExAttributeIdentity.h"

#define LOCTEXT_NAMESPACE "PCGExCopyAttributesByIndexElement"
#define PCGEX_NAMESPACE CopyAttributesByIndex

#pragma region UPCGExCopyAttributesByIndexSettings

UPCGExCopyAttributesByIndexSettings::UPCGExCopyAttributesByIndexSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IndexAttribute.Update(TEXT("CurrentIndex"));
}

TArray<FPCGPinProperties> UPCGExCopyAttributesByIndexSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceSourcesLabel, "Points to copy attributes from. Paired with the inputs in order; a single data is paired with every input.", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExCopyAttributesByIndexSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_INITIALIZE_ELEMENT(CopyAttributesByIndex)
PCGEX_ELEMENT_BATCH_POINT_IMPL(CopyAttributesByIndex)

#pragma endregion

#pragma region FPCGExCopyAttributesByIndexContext

TSharedPtr<PCGExData::FFacade> FPCGExCopyAttributesByIndexContext::GetSourceFacade(const int32 InputIndex) const
{
	const int32 SourceIndex = SourceFacades.Num() == 1 ? 0 : InputIndex;
	return SourceFacades.IsValidIndex(SourceIndex) ? SourceFacades[SourceIndex] : nullptr;
}

bool FPCGExCopyAttributesByIndexContext::CanStealInput(const int32 InputIndex) const
{
	return bWantsDataStealing && StealableInputs.IsValidIndex(InputIndex) && StealableInputs[InputIndex];
}

#pragma endregion

#pragma region FPCGExCopyAttributesByIndexElement

bool FPCGExCopyAttributesByIndexElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CopyAttributesByIndex)

	TArray<TSharedPtr<PCGExData::FFacade>> Facades;
	if (!PCGExData::TryGetFacades(Context, PCGExCommon::Labels::SourceSourcesLabel, Facades, true))
	{
		return false;
	}

	const TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGExCommon::Labels::SourceSourcesLabel);
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
	const int32 NumSources = Sources.Num();
	const int32 NumInputs = Inputs.Num();

	if (NumSources != 1 && NumSources != NumInputs)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(FTEXT("Sources holds {0} data for {1} inputs. Only one Sources data per input, or a single one for every input, is supported."), FText::AsNumber(NumSources), FText::AsNumber(NumInputs)));
		return false;
	}

	// Facades skip empty and duplicate entries; keyed back by data so pin positions stay aligned with the inputs.
	TMap<const UPCGData*, TSharedPtr<PCGExData::FFacade>> FacadeByData;
	FacadeByData.Reserve(Facades.Num());
	for (const TSharedPtr<PCGExData::FFacade>& Facade : Facades)
	{
		FacadeByData.Add(Facade->Source->InitializationData.Get(), Facade);
	}

	Context->SourceFacades.Init(nullptr, NumSources);
	for (int32 i = 0; i < NumSources; i++)
	{
		if (const TSharedPtr<PCGExData::FFacade>* Facade = FacadeByData.Find(Sources[i].Data.Get()))
		{
			Context->SourceFacades[i] = *Facade;
		}
	}

	if (Context->bWantsDataStealing)
	{
		// Processors run in parallel and a stolen input is written in place: it must be invisible to every other
		// processor's Sources reads, and must not be the metadata parent of a Sources data, whose attributes hold
		// raw pointers to the parent's (a type-mismatched forward deletes and recreates the stolen attribute).
		Context->StealableInputs.Init(true, NumInputs);

		bool bRefusedSteal = false;
		for (int32 i = 0; i < NumInputs; i++)
		{
			const UPCGData* InputData = Inputs[i].Data.Get();
			const UPCGMetadata* InputMetadata = InputData ? InputData->ConstMetadata() : nullptr;

			for (int32 j = 0; j < NumSources; j++)
			{
				const UPCGData* SourceData = Sources[j].Data.Get();
				if (!SourceData)
				{
					continue;
				}

				bool bObservable = false;
				if (SourceData == InputData)
				{
					// Reading itself is fine as long as this input's processor is that entry's only reader.
					bObservable = NumSources == 1 ? NumInputs != 1 : j != i;
				}
				else if (const UPCGMetadata* SourceMetadata = SourceData->ConstMetadata())
				{
					bObservable = InputMetadata && SourceMetadata->HasParent(InputMetadata);
				}

				if (bObservable)
				{
					Context->StealableInputs[i] = false;
					bRefusedSteal = true;
					break;
				}
			}
		}

		if (bRefusedSteal)
		{
			PCGE_LOG_C(Verbose, LogOnly, Context, FTEXT("Steal Data skipped on an input that Sources also reads, or that is the metadata parent of a Sources data; it is duplicated instead."));
		}
	}

	static_cast<FPCGExNameFiltersDetails&>(Context->ForwardDetails) = Settings->Attributes;
	Context->ForwardDetails.Init();

	Context->TagFilters = Settings->Tags;
	Context->TagFilters.Init();

	// Never forwarded, so not worth reading; a selector resolved per input (@Last) simply matches nothing here.
	TSet<FName> NotPreloaded;
	if (Settings->IndexAttribute.GetSelection() == EPCGAttributePropertySelection::Attribute)
	{
		NotPreloaded.Add(Settings->IndexAttribute.GetAttributeName());
	}

	Context->SourcesPreloader = MakeShared<PCGExData::FMultiFacadePreloader>(Facades);
	Context->SourcesPreloader->ForEach([&](PCGExData::FFacadePreloader& Preloader)
	{
		const TSharedPtr<PCGExData::FFacade> Facade = Preloader.GetDataFacade();

		// A small source is read by the handlers directly: an empty preloader completes inline, with no async hop.
		if (!Facade || Facade->GetNum() < PCGEX_CORE_SETTINGS.SmallPointsSize)
		{
			return;
		}

		TArray<PCGExData::FAttributeIdentity> Identities;
		PCGExData::FAttributeIdentity::Get(Facade->GetIn()->Metadata, Identities, &NotPreloaded);
		Context->ForwardDetails.Filter(Identities);

		// Whole identities, not Register(): it keeps only the name and value type, which misreads container attributes.
		for (const PCGExData::FAttributeIdentity& Identity : Identities)
		{
			Preloader.BufferConfigs.Emplace(Identity);
		}
	});

	return true;
}

bool FPCGExCopyAttributesByIndexElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyAttributesByIndexElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CopyAttributesByIndex)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
		Context->SourcesPreloader->OnCompleteCallback = [Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

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
				Context->CancelExecution(TEXT("Could not find any points to process."));
			}
		};

		Context->SourcesPreloader->StartLoading(Context->GetTaskManager());
		if (Context->IsWaitingForTasks())
		{
			return false;
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	Context->Done();

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExCopyAttributesByIndex::FProcessor

namespace PCGExCopyAttributesByIndex
{
	void CopyMissingTags(const PCGExData::FTags& From, PCGExData::FTags& To, const FPCGExNameFiltersDetails& Filter)
	{
		TArray<FString> NewTags;

		// A raw tag is its own key.
		for (const FString& Raw : From.RawTags)
		{
			if (!To.IsTagged(Raw) && Filter.Test(Raw))
			{
				NewTags.Add(Raw);
			}
		}

		for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : From.ValueTags)
		{
			if (Pair.Value && !To.IsTagged(Pair.Key) && Filter.Test(Pair.Key))
			{
				NewTags.Add(Pair.Value->Flatten(Pair.Key));
			}
		}

		To.Append(NewTags);
	}

	bool ResolvesEveryIndex(const EPCGExIndexSafety Safety)
	{
		return Safety == EPCGExIndexSafety::Tile || Safety == EPCGExIndexSafety::Clamp || Safety == EPCGExIndexSafety::Yoyo;
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCopyAttributesByIndex::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		const int32 InputIndex = PointDataFacade->Source->InitializationIndex;

		const TSharedPtr<PCGExData::FFacade> SourceFacade = Context->GetSourceFacade(InputIndex);
		if (!SourceFacade)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("An input is paired with a Sources data that holds no points; it is forwarded untouched."));
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			return false;
		}

		const TSharedPtr<PCGExData::TBuffer<int32>> IndexReader = PointDataFacade->GetBroadcaster<int32>(Settings->IndexAttribute, true);
		if (!IndexReader)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(Context, Index Attribute, Settings->IndexAttribute)
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Context->CanStealInput(InputIndex) ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate)

		MaxSourceIndex = SourceFacade->GetNum() - 1;

		if (IndexReader->GetUnderlyingDomain() == PCGExData::EDomainType::Elements)
		{
			// The input array is allocated at full length on creation and only filled by Fetch, so the pointer is stable.
			IndexData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(IndexReader)->GetInValues()->GetData();

			// Every point commits a row, so no fill is needed.
			SourceRows.SetNumUninitialized(PointDataFacade->GetNum());
		}
		else
		{
			// A single-value index sends every point to the same row.
			SourceRows.Init(PCGExMath::SanitizeIndex(IndexReader->Read(0), MaxSourceIndex, Settings->IndexSafety), PointDataFacade->GetNum());
		}

		// The index attribute is this data's own key: a same-named Sources attribute never replaces it.
		TSet<FName> IgnoredAttributes;
		const UPCGBasePointData* InData = PointDataFacade->GetIn();
		const FPCGAttributePropertyInputSelector FixedIndexAttribute = Settings->IndexAttribute.CopyAndFixLast(InData);
		if (FixedIndexAttribute.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			IgnoredAttributes.Add(PCGExMetaHelpers::GetAttributeIdentifier(FixedIndexAttribute, InData).Name);
		}

		// Only an ignored index leaves a point untouched; otherwise every target value is replaced and need not be read first.
		Forward = Context->ForwardDetails.GetHandler(SourceFacade, PointDataFacade, PCGExData::EForwardDomain::Inherit, &IgnoredAttributes, !ResolvesEveryIndex(Settings->IndexSafety));

		if (Settings->bCopyTags)
		{
			CopyMissingTags(*SourceFacade->Source->Tags, *PointDataFacade->Source->Tags, Context->TagFilters);
		}

		StartParallelLoopForPoints();

		return true;
	}

	template <EPCGExIndexSafety Safety>
	void FProcessor::ResolveRows(const PCGExMT::FScope& Scope)
	{
		// Locals: the row writes could otherwise alias the members and force a reload per point.
		const int32* Indices = IndexData;
		const int32 MaxIndex = MaxSourceIndex;
		int32* Rows = SourceRows.GetData();

		PCGEX_SCOPE_LOOP(Index)
		{
			Rows[Index] = PCGExMath::SanitizeIndex<int32, Safety>(Indices[Index], MaxIndex);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCopyAttributesByIndex::ProcessPoints);

		if (IndexData)
		{
			PointDataFacade->Fetch(Scope);

			switch (Settings->IndexSafety)
			{
			case EPCGExIndexSafety::Ignore:
				ResolveRows<EPCGExIndexSafety::Ignore>(Scope);
				break;
			case EPCGExIndexSafety::Tile:
				ResolveRows<EPCGExIndexSafety::Tile>(Scope);
				break;
			case EPCGExIndexSafety::Clamp:
				ResolveRows<EPCGExIndexSafety::Clamp>(Scope);
				break;
			case EPCGExIndexSafety::Yoyo:
				ResolveRows<EPCGExIndexSafety::Yoyo>(Scope);
				break;
			default:
				// Same fallback as the runtime PCGExMath::SanitizeIndex: rows must be defined before they are forwarded.
				checkNoEntry();
				ResolveRows<EPCGExIndexSafety::Ignore>(Scope);
				break;
			}
		}

		Forward->ForwardScoped(Scope, SourceRows);
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
