// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSplitPath.h"


#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSplitPathElement"
#define PCGEX_NAMESPACE SplitPath

#if WITH_EDITOR
void UPCGExSplitPathSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (SplitAction == EPCGExPathSplitAction::Switch)
	{
	}
}
#endif

PCGEX_INITIALIZE_ELEMENT(SplitPath)
PCGEX_ELEMENT_BATCH_POINT_IMPL(SplitPath)

bool FPCGExSplitPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SplitPath)

	Context->MainPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->MainPaths->OutputPin = Settings->GetMainOutputPin();

	return true;
}

bool FPCGExSplitPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSplitPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SplitPath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					if (!Settings->bOmitSinglePointOutputs)
					{
						Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					}
					else
					{
						bHasInvalidInputs = true;
					}
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to split."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPaths->Pairs.Reserve(Context->MainPaths->Pairs.Num() + Context->MainBatch->GetNumProcessors());
	Context->MainBatch->Output();

	PCGEX_OUTPUT_VALID_PATHS(MainPaths)
	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExSplitPath
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSplitPath::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn());

		const int32 NumPoints = PointDataFacade->GetNum();
		const int32 ChunkSize = PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize();

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, TaskGroup)

#define PCGEX_SPLIT_ACTION(_NAME)\
		TaskGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope){\
					PCGEX_ASYNC_THIS \
					This->PointDataFacade->Fetch(Scope);\
					This->FilterScope(Scope);\
					PCGEX_SCOPE_LOOP(i) { This->_NAME(i); } };

		if (Settings->SplitAction == EPCGExPathSplitAction::Partition || Settings->SplitAction == EPCGExPathSplitAction::Switch)
		{
			PointDataFacade->Fetch(PCGExMT::FScope(0, 1));
			FilterScope(PCGExMT::FScope(0, 1));

			switch (Settings->InitialBehavior)
			{
			default: case EPCGExPathSplitInitialValue::Constant:
				bLastResult = static_cast<int8>(Settings->bInitialValue);
				break;
			case EPCGExPathSplitInitialValue::ConstantPreserve:
				bLastResult = static_cast<int8>(Settings->bInitialValue) == PointFilterCache[0] ? !bLastResult : bLastResult;
				break;
			case EPCGExPathSplitInitialValue::FromPoint:
				bLastResult = PointFilterCache[0];
				break;
			case EPCGExPathSplitInitialValue::FromPointPreserve:
				bLastResult = !PointFilterCache[0];
				break;
			}
		}

		switch (Settings->SplitAction)
		{
		case EPCGExPathSplitAction::Split: PCGEX_SPLIT_ACTION(DoActionSplit)
			break;
		case EPCGExPathSplitAction::Remove: PCGEX_SPLIT_ACTION(DoActionRemove)
			break;
		case EPCGExPathSplitAction::Disconnect: PCGEX_SPLIT_ACTION(DoActionDisconnect)
			break;
		case EPCGExPathSplitAction::Partition: PCGEX_SPLIT_ACTION(DoActionPartition)
			break;
		case EPCGExPathSplitAction::Switch: PCGEX_SPLIT_ACTION(DoActionSwitch)
			break;
		default: ;
		}

#undef PCGEX_SPLIT_ACTION

		TaskGroup->StartSubLoops(NumPoints, ChunkSize, true);

		return true;
	}

	void FProcessor::DoActionSplit(const int32 Index)
	{
		if (!PointFilterCache[Index])
		{
			if (CurrentSubPath == -1)
			{
				CurrentSubPath = SubPaths.Emplace();
				FSubPath& NewPath = SubPaths[CurrentSubPath];
				NewPath.Start = Index;
			}

			FSubPath& SubPath = SubPaths[CurrentSubPath];
			SubPath.Count++;
			return;
		}

		if (CurrentSubPath != -1)
		{
			FSubPath& ClosedPath = SubPaths[CurrentSubPath];
			ClosedPath.End = Index;
			ClosedPath.Count++;
		}

		CurrentSubPath = SubPaths.Emplace();
		FSubPath& NewSubPath = SubPaths[CurrentSubPath];
		NewSubPath.Start = Index;
		NewSubPath.Count++;
	}

	void FProcessor::DoActionRemove(const int32 Index)
	{
		if (!PointFilterCache[Index])
		{
			if (CurrentSubPath == -1)
			{
				CurrentSubPath = SubPaths.Emplace();
				FSubPath& NewSubPath = SubPaths[CurrentSubPath];
				NewSubPath.Start = Index;
			}

			FSubPath& SubPath = SubPaths[CurrentSubPath];
			SubPath.Count++;
			return;
		}

		if (CurrentSubPath != -1)
		{
			FSubPath& Path = SubPaths[CurrentSubPath];
			Path.End = Index - 1;
		}

		CurrentSubPath = -1;
	}

	void FProcessor::DoActionDisconnect(const int32 Index)
	{
		if (!PointFilterCache[Index])
		{
			if (CurrentSubPath == -1)
			{
				CurrentSubPath = SubPaths.Emplace();
				FSubPath& NewSubPath = SubPaths[CurrentSubPath];
				NewSubPath.Start = Index;
			}

			FSubPath& SubPath = SubPaths[CurrentSubPath];
			SubPath.Count++;
			return;
		}

		if (CurrentSubPath != -1)
		{
			FSubPath& ClosedSubPath = SubPaths[CurrentSubPath];
			ClosedSubPath.End = Index;
			ClosedSubPath.Count++;
		}

		CurrentSubPath = -1;
	}

	void FProcessor::DoActionPartition(const int32 Index)
	{
		if (PointFilterCache[Index] != bLastResult)
		{
			bLastResult = !bLastResult;

			if (CurrentSubPath != -1)
			{
				FSubPath& ClosedSubPath = SubPaths[CurrentSubPath];
				if (Settings->bInclusive)
				{
					ClosedSubPath.End = Index;
					ClosedSubPath.Count++;
				}
				else
				{
					ClosedSubPath.End = Index - 1;
				}

				CurrentSubPath = -1;
			}
		}

		if (CurrentSubPath == -1)
		{
			CurrentSubPath = SubPaths.Emplace();
			FSubPath& NewSubPath = SubPaths[CurrentSubPath];
			NewSubPath.bEven = bEven;
			bEven = !bEven;
			NewSubPath.Start = Index;
		}

		FSubPath& SubPath = SubPaths[CurrentSubPath];
		SubPath.Count++;
	}

	void FProcessor::DoActionSwitch(const int32 Index)
	{
		auto CloseSubPath = [&]()
		{
			if (CurrentSubPath != -1)
			{
				FSubPath& ClosedSubPath = SubPaths[CurrentSubPath];
				if (Settings->bInclusive)
				{
					ClosedSubPath.End = Index;
					ClosedSubPath.Count++;
				}
				else
				{
					ClosedSubPath.End = Index - 1;
				}
			}

			CurrentSubPath = -1;
		};

		if (PointFilterCache[Index])
		{
			bLastResult = !bLastResult;
		}

		if (bLastResult)
		{
			if (CurrentSubPath == -1)
			{
				CurrentSubPath = SubPaths.Emplace();
				FSubPath& NewSubPath = SubPaths[CurrentSubPath];
				NewSubPath.Start = Index;
			}

			FSubPath& SubPath = SubPaths[CurrentSubPath];
			SubPath.Count++;
			return;
		}

		CloseSubPath();
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const FSubPath& SubPath = SubPaths[Index];

			if (Index == 0 && bWrapLastPath)
			{
				continue;
			}
			const bool bLastPath = SubPath.End == -1;

			const bool bAppendStartPath = bWrapLastPath && bLastPath;
			int32 NumPathPoints = bAppendStartPath ? SubPath.Count + SubPaths[0].Count : SubPath.Count;
			int32 NumIterations = SubPath.Count;

			if (!bAppendStartPath && bLastPath && bClosedLoop)
			{
				if (PointFilterCache[0] && Settings->SplitAction == EPCGExPathSplitAction::Remove)
				{
					// First point got removed, don't wrap
				}
				else
				{
					// First point added last
					NumPathPoints++;
					NumIterations++;
				}
			}

			if (NumPathPoints == 1 && Settings->bOmitSinglePointOutputs)
			{
				continue;
			}

			const TSharedPtr<PCGExData::FPointIO> SubPathIO = NewPointIO(PointDataFacade->Source);
			PCGEX_INIT_IO_VOID(SubPathIO, PCGExData::EIOInit::New)

			SubPathsIOs[Index] = SubPathIO;

			const UPCGBasePointData* OriginalPoints = PointDataFacade->GetIn();
			UPCGBasePointData* MutablePoints = SubPathIO->GetOut();
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumPathPoints, OriginalPoints->GetAllocatedProperties());

			TArray<int32>& IdxMapping = SubPathIO->GetIdxMapping();

			const int32 IndexWrap = OriginalPoints->GetNumPoints();
			for (int i = 0; i < NumIterations; i++)
			{
				IdxMapping[i] = (SubPath.Start + i) % IndexWrap;
			}

			if (bAppendStartPath)
			{
				// There was a cut somewhere in the closed path.
				const FSubPath& StartPathInfos = SubPaths[0];
				for (int i = 0; i < StartPathInfos.Count; i++)
				{
					IdxMapping[SubPath.Count + i] = StartPathInfos.Start + i;
				}
			}

			SubPathIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);
		}
	}

	void FProcessor::CompleteWork()
	{
		if (SubPaths.IsEmpty() || (SubPaths.Num() == 1 && SubPaths[0].Count == PointDataFacade->GetNum()))
		{
			bool bHasFilteredOutPoints = false;
			for (const int8 Filtered : PointFilterCache)
			{
				if (Filtered)
				{
					bHasFilteredOutPoints = true;
					break;
				}
			}

			if (!bHasFilteredOutPoints)
			{
				// No splits, forward
				PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			}
			else if ((SubPaths.Num() == 1 && SubPaths[0].Count == PointDataFacade->GetNum()) && bClosedLoop)
			{
				// Disconnecting closed loop last point will produce an open path
				PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
				PCGExPaths::Helpers::SetClosedLoop(PointDataFacade->GetOut(), false);
			}
			else
			{
				// Filtered points didn't produce an effective split, forward as-is
				PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			}

			return;
		}

		if (bClosedLoop)
		{
			if (SubPaths.Num() > 1)
			{
				if (Settings->SplitAction == EPCGExPathSplitAction::Partition || Settings->SplitAction == EPCGExPathSplitAction::Switch)
				{
					bWrapLastPath = SubPaths[0].Start == 0 && SubPaths.Last().End == -1
						&& PointFilterCache[0] == PointFilterCache[PointDataFacade->GetNum() - 1];
				}
				else
				{
					bWrapLastPath = SubPaths[0].Start == 0 && SubPaths.Last().End == -1 && !PointFilterCache[0];
				}
			}

			if (SubPaths.Num() > 1 || SubPaths[0].End != -1 || SubPaths[0].Start != 0)
			{
				bAddOpenTag = true;
			}
		}

		SubPathsIOs.Init(nullptr, SubPaths.Num());

		StartParallelLoopForRange(SubPaths.Num());

		//TODO : If closed path is enabled, and if the first & last points are not removed after the split
		//re-order them and join them so as to recreate the "missing" edge
	}

	void FProcessor::Output()
	{
		int32 OddEven = 0;
		for (const TSharedPtr<PCGExData::FPointIO>& PathIO : SubPathsIOs)
		{
			if (!PathIO)
			{
				continue;
			}

			PCGExPaths::Helpers::SetClosedLoop(PathIO->GetOut(), false);

			if ((OddEven & 1) == 0)
			{
				if (Settings->bTagIfEvenSplit)
				{
					PathIO->Tags->AddRaw(Settings->IsEvenTag);
				}
			}
			else if (Settings->bTagIfOddSplit)
			{
				PathIO->Tags->AddRaw(Settings->IsOddTag);
			}
			Context->MainPaths->Add_Unsafe(PathIO);
			OddEven++;
		}

		SubPathsIOs.Empty();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
