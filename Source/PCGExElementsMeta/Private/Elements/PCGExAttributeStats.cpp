// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/PCGExAttributeStats.h"

#include "Core/PCGExPointFilter.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"


#define PCGEX_FOREACH_STAT(MACRO, _TYPE)\
MACRO(Identifier, FString, TEXT("None"))\
MACRO(DefaultValue, _TYPE, _TYPE{})\
MACRO(MinValue, _TYPE, _TYPE{})\
MACRO(MaxValue, _TYPE, _TYPE{})\
MACRO(SetMinValue, _TYPE, _TYPE{})\
MACRO(SetMaxValue, _TYPE, _TYPE{})\
MACRO(AverageValue, _TYPE, _TYPE{})\
MACRO(UniqueValuesNum, int32, 0)\
MACRO(UniqueSetValuesNum, int32, 0)\
MACRO(DifferentValuesNum, int32, 0)\
MACRO(DifferentSetValuesNum, int32, 0)\
MACRO(DefaultValuesNum, int32, 0)\
MACRO(HasOnlyDefaultValues, bool, false)\
MACRO(HasOnlySetValues, bool, false)\
MACRO(HasOnlyUniqueValues, bool, false)\
MACRO(Samples, int32, 0)\
MACRO(IsValid, bool, false)


#define LOCTEXT_NAMESPACE "PCGExAttributeStats"
#define PCGEX_NAMESPACE AttributeStats

TArray<FPCGPinProperties> UPCGExAttributeStatsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_PARAMS(PCGExAttributeStats::OutputAttributeStats, "Per-attribute stats, one row per input dataset.", Required)
	if (bOutputPerUniqueValuesStats)
	{
		PCGEX_PIN_PARAMS(PCGExAttributeStats::OutputAttributeUniqueValues, "Per-dataset, per-attribute unique values.", Normal)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(AttributeStats)
PCGEX_ELEMENT_BATCH_POINT_IMPL(AttributeStats)

bool FPCGExAttributeStatsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}
	PCGEX_CONTEXT_AND_SETTINGS(AttributeStats)

	FPCGExNameFiltersDetails Filters = Settings->Filters;
	Filters.Init();

	Context->AttributesInfos = MakeShared<PCGExData::FAttributesInfos>();
	TSet<FName> OutMismatch;

	TSet<FName> UniqueNames;
#define PCGEX_STAT_CHECK(_NAME, _TYPE, _DEFAULT) if(Settings->bOutput##_NAME){	PCGEX_VALIDATE_NAME(Settings->_NAME##AttributeName);\
	bool bAlreadySet = false; UniqueNames.Add(Settings->_NAME##AttributeName, &bAlreadySet);\
	if(bAlreadySet){ PCGE_LOG(Error, GraphAndLog, FText::Format(FTEXT("Duplicate attribute name: {0}."), FText::FromName(Settings->_NAME##AttributeName))); }}
	PCGEX_FOREACH_STAT(PCGEX_STAT_CHECK, nullptr)
#undef PCGEX_STAT_CHECK

	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(IO->GetIn()->Metadata);
		Context->AttributesInfos->Append(Infos, OutMismatch);
	}

	if (!OutMismatch.IsEmpty() && !Settings->bQuietTypeMismatchWarning)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Some attributes share the same name but not the same type; only the first type found will be processed."));
	}

	if (Context->AttributesInfos->Identities.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No attributes found!"));
		return false;
	}

	Filters.Prune(*Context->AttributesInfos, true);

	if (Settings->bFeedbackLoopFailsafe)
	{
		TArray<FString> Affixes;
		Affixes.Reserve(15);
#define PCGEX_STAT_FILTER(_NAME, _TYPE, _DEFAULT) if(Settings->bOutput##_NAME){ Affixes.Add(Settings->_NAME##AttributeName.ToString()); }
		PCGEX_FOREACH_STAT(PCGEX_STAT_FILTER, bool)
#undef PCGEX_STAT_FILTER

		Context->AttributesInfos->Filter([&Affixes](const FName& InName)
		{
			const FString StrName = InName.ToString();
			for (const FString& Affix : Affixes)
			{
				if (StrName.StartsWith(Affix) || StrName.EndsWith(Affix))
				{
					return false;
				}
			}
			return true;
		});
	}

	if (Context->AttributesInfos->Identities.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("The node does not output any data after filtering is applied."));
		return false;
	}

	const int32 NumRows = Context->MainPoints->Num();

#define PCGEX_STAT_DECL(_NAME, _TYPE, _DEFAULT) if(Settings->bOutput##_NAME){ NewParamData->Metadata->FindOrCreateAttribute<_TYPE>(Settings->_NAME##AttributeName, _DEFAULT); }

	Context->Rows.Reserve(NumRows);
	Context->OutputParams.Reserve(Context->AttributesInfos->Identities.Num());
	for (const PCGExData::FAttributeIdentity& Identity : Context->AttributesInfos->Identities)
	{
		UPCGParamData* NewParamData = Context->ManagedObjects->New<UPCGParamData>();
		Context->OutputParams.Add(NewParamData);
		Context->OutputParamsMap.Add(Identity.Name, NewParamData);

		for (int i = 0; i < NumRows; i++)
		{
			Context->Rows.Add(NewParamData->Metadata->AddEntry());
		}

		PCGExMetaHelpers::ExecuteWithRightType(
			Identity,
			[&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				PCGEX_FOREACH_STAT(PCGEX_STAT_DECL, T)
			},
			[&]()
			{
				// Stats are inherently arithmetic (min/max/avg/etc.) -- no meaningful semantics for
				// container or extended (Struct/Object/...) attributes.
				PCGEX_LOG_UNSUPPORTED_TYPE(Context, Identity, FTEXT("Attribute Stats"));
			});
	}
#undef PCGEX_STAT_DECL

	return true;
}

bool FPCGExAttributeStatsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributeStatsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AttributeStats)
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
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	for (int i = 0; i < Context->OutputParams.Num(); i++)
	{
		UPCGParamData* ParamData = Context->OutputParams[i];
		Context->StageOutput(
			ParamData, PCGExAttributeStats::OutputAttributeStats, PCGExData::EStaging::None,
			{Context->AttributesInfos->Identities[i].Name.ToString()});
	}

	return Context->TryComplete();
}

namespace PCGExAttributeStats
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExAttributeStats::Process);

		// Must be set before process for filters
		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->OutputToPoints == EPCGExStatsOutputToPoints::None ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate)

		const int64 Key = Context->Rows[PointDataFacade->Source->IOIndex];
		const int32 NumAttributes = Context->AttributesInfos->Identities.Num();

		Stats.Reserve(NumAttributes);
		for (int i = 0; i < NumAttributes; i++)
		{
			const PCGExData::FAttributeIdentity& Identity = Context->AttributesInfos->Identities[i];

			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T_REAL = decltype(DummyValue);
					PCGEX_MAKE_SHARED(S, TAttributeStats<T_REAL>, Identity, Key)
					Stats.Add(StaticCastSharedPtr<IAttributeStats>(S));
				},
				[&]()
				{
					// Container/extended-typed attributes were skipped at the OutputParams stage;
					// keep the slot count consistent by inserting a null entry.
					Stats.Add(nullptr);
				});
		}


		PCGEX_ASYNC_GROUP_CHKD(TaskManager, FilterScope)

		FilterScope->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->PointDataFacade->Fetch(Scope);
			This->FilterScope(Scope);
		};

		FilterScope->StartSubLoops(PointDataFacade->GetNum(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());

		return true;
	}

	void FProcessor::CompleteWork()
	{
		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, AttributeStatProcessing)
		AttributeStatProcessing->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			// Skip nullptr slots -- those are container/extended-typed attributes that stats can't analyze.
			if (const TSharedPtr<IAttributeStats>& Slot = This->Stats[Scope.Start])
			{
				Slot->Process(This->PointDataFacade, This->Context, This->Settings, This->PointFilterCache);
			}
		};

		AttributeStatProcessing->StartSubLoops(Stats.Num(), 1);
	}
}

#undef PCGEX_FOREACH_STAT

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
