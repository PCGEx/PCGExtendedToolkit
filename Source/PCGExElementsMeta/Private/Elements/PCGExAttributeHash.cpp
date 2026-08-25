// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExAttributeHash.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExAttributeHashElement"
#define PCGEX_NAMESPACE AttributeHash

bool UPCGExAttributeHashSettings::HasDynamicPins() const
{
	return true;
}

PCGEX_INITIALIZE_ELEMENT(AttributeHash)
PCGEX_ELEMENT_BATCH_POINT_IMPL(AttributeHash)

bool FPCGExAttributeHashElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(AttributeHash)

	PCGEX_VALIDATE_NAME(Settings->OutputName)

	Context->ValidHash.Init(false, Context->MainPoints->Num());
	Context->Hashes.Init(0, Context->ValidHash.Num());

	return true;
}

bool FPCGExAttributeHashElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributeHashElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AttributeHash)
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

	for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
	{
		if (const int32 Idx = IO->IOIndex;
			Context->ValidHash[Idx] && Settings->bOutputToAttribute)
		{
			UPCGData* OutputCopy = Context->ManagedObjects->DuplicateData<UPCGData>(IO->InitializationData);
			PCGExData::Helpers::SetDataValue<int32>(OutputCopy, Settings->OutputName, Context->Hashes[Idx]);
			Context->StageOutput(
				OutputCopy, Settings->GetMainInputPin(), PCGExData::EStaging::MutableAndManaged,
				IO->Tags->Flatten());
		}
		else
		{
			IO->StageAnyOutput(Context);
		}
	}

	return Context->TryComplete();
}

namespace PCGExAttributeHash
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExAttributeHash::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Hasher = MakeShared<PCGEx::FAttributeHasher>(Settings->HashConfig);
		if (!Hasher->Init(Context, PointDataFacade))
		{
			return false;
		}
		if (Hasher->RequiresCompilation())
		{
			Hasher->Compile(TaskManager, [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->CompleteWork();
			});
		}
		else
		{
			CompleteWork();
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
		Context->ValidHash[PointDataFacade->Source->IOIndex] = true;
		Context->Hashes[PointDataFacade->Source->IOIndex] = Hasher->GetHash();
		if (Settings->bOutputToTags)
		{
			PointDataFacade->Source->Tags->Set<int32>(Settings->OutputName.ToString(), Hasher->GetHash());
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
