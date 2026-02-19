// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBoundsPathIntersection.h"

#include "PCGParamData.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExAsyncHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Math/OBB/PCGExOBBCollection.h"
#include "Math/PCGExMathBounds.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"

#define LOCTEXT_NAMESPACE "PCGExBoundsPathIntersectionElement"
#define PCGEX_NAMESPACE BoundsPathIntersection

#if WITH_EDITORONLY_DATA
void UPCGExBoundsPathIntersectionSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Blending) { Blending = NewObject<UPCGExSubPointsBlendInterpolate>(this, TEXT("Blending")); }
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExBoundsPathIntersectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceBoundsLabel, "Intersection points (bounds)", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal, EPCGExBlendingInterface::Individual);
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBlending::Labels::SourceOverridesBlendingOps)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExBoundsPathIntersectionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BoundsPathIntersection)

void UPCGExBoundsPathIntersectionSettings::AddTags(const TSharedPtr<PCGExData::FPointIO>& IO, bool bIsCut) const
{
	if (bIsCut && bTagIfHasCuts) { IO->Tags->AddRaw(HasCutsTag); }
	else if (!bIsCut && bTagIfUncut) { IO->Tags->AddRaw(UncutTag); }
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(BoundsPathIntersection)

bool FPCGExBoundsPathIntersectionElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BoundsPathIntersection)

	if (!Settings->OutputSettings.Validate(Context)) { return false; }

	PCGEX_BIND_INSTANCED_FACTORY(Blending, UPCGExSubPointsBlendInstancedFactory, PCGExBlending::Labels::SourceOverridesBlendingOps)

	PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {PCGExFactories::EType::Blending}, false);

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->NumMaxTargets = Context->TargetsHandler->Init(Context, PCGExCommon::Labels::SourceBoundsLabel);

	if (!Context->NumMaxTargets)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No valid bounds"));
		return false;
	}

	{
		PCGExAsyncHelpers::FAsyncExecutionScope CollectionBuildingTasks(Context->NumMaxTargets);
		Context->TargetsHandler->ForEachPreloader([&](PCGExData::FFacadePreloader& Preloader)
		{
			// Build OBB collection from facade data
			auto Facade = Preloader.GetDataFacade();
			auto Collection = MakeShared<PCGExMath::OBB::FCollection>();
			Collection->CloudIndex = Context->Collections.Num();
			Context->Collections.Add(Collection);

			CollectionBuildingTasks.Execute(
				[CtxHandle = Context->GetOrCreateHandle(), Collection, Facade, BoundsSource = Settings->OutputSettings.BoundsSource]()
				{
					PCGEX_SHARED_CONTEXT_VOID(CtxHandle);
					Collection->BuildFrom(Facade->Source, BoundsSource);
				});

			PCGExBlending::RegisterBuffersDependencies_SourceA(Context, Preloader, Context->BlendingFactories);
		});
	}

	return true;
}

bool FPCGExBoundsPathIntersectionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBoundsPathIntersectionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BoundsPathIntersection)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetOrCreateHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

			Context->TargetsHandler->SetMatchingDetails(Context, &Settings->DataMatching);

			PCGEX_ON_INVALILD_INPUTS_C(SharedContext.Get(), FTEXT("Some inputs have less than 2 points and won't be processed."))

			const bool bWritesAny = Settings->OutputSettings.WillWriteAny();
			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					if (Entry->GetNum() < 2)
					{
						if (!Settings->bOmitInvalidPathsOutputs)
						{
							if (bWritesAny)
							{
								Entry->InitializeOutput(PCGExData::EIOInit::Duplicate);
								Settings->OutputSettings.Mark(Entry.ToSharedRef());
							}
							else
							{
								Entry->InitializeOutput(PCGExData::EIOInit::Forward);
							}

							Settings->AddTags(Entry, false);
						}

						bHasInvalidInputs = true;
						return false;
					}
					return true;
				}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				Context->CancelExecution(TEXT("Could not find any paths to intersect with."));
			}
		};

		Context->TargetsHandler->StartLoading(Context->GetTaskManager());
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExBoundsPathIntersection
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBoundsPathIntersection::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetIn());

		SubBlending = Context->Blending->CreateOperation();
		SubBlending->bClosedLoop = bClosedLoop;

		IgnoreList.Add(PointDataFacade->GetIn());
		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true); !Context->TargetsHandler->PopulateIgnoreList(PointDataFacade->Source, MatchingScope, IgnoreList))
		{
			(void)Context->TargetsHandler->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		LastIndex = NumPoints - 1;

		Details = Settings->OutputSettings;
		Intersections.Init(nullptr, NumPoints);
		StartIndices.Init(-1, NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}


	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			int32 NextIndex = Index + 1;

			if (Index == LastIndex)
			{
				if (bClosedLoop) { NextIndex = 0; }
				else { continue; }
			}

			TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

			const TSharedPtr<PCGExMath::OBB::FIntersections> LocalIntersections = MakeShared<PCGExMath::OBB::FIntersections>(InTransforms[Index].GetLocation(), InTransforms[NextIndex].GetLocation());

			// For each collection...
			Context->TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& InTarget, const int32 InTargetIndex)
			{
				Context->Collections[InTargetIndex]->FindIntersections(*LocalIntersections);
			}, &IgnoreList);

			if (!LocalIntersections->IsEmpty())
			{
				LocalIntersections->SortAndDedupe();
				Intersections[Index] = LocalIntersections;
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// Find how many new points were added and at which index do they need to be inserted		
		int32 WriteIndex = 0;
		for (int i = 0; i < Intersections.Num(); i++)
		{
			StartIndices[i] = WriteIndex++;

			TSharedPtr<PCGExMath::OBB::FIntersections> LocalIntersection = Intersections[i];

			if (!LocalIntersection) { continue; }

			const int32 CutsNum = LocalIntersection->Cuts.Num();
			NewPointsNum += CutsNum;
			WriteIndex += CutsNum;
		}

		if (!NewPointsNum)
		{
			// TODO : Add "no intersections" tag
			bIsProcessorValid = false;
			if (Details.WillWriteAny())
			{
				PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Duplicate);
				Settings->OutputSettings.Mark(PointDataFacade->Source);
			}
			else
			{
				PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Forward);
			}

			Settings->AddTags(PointDataFacade->Source, false);
			return;
		}

		Settings->AddTags(PointDataFacade->Source, true);

		// Allocate new points
		PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::Duplicate);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointDataFacade->Source->GetOut(), WriteIndex, PointDataFacade->Source->GetAllocations() | EPCGPointNativeProperties::MetadataEntry | EPCGPointNativeProperties::Seed);

		// Copy/Move existing points to their new index
		TArray<int32>& IdxMapping = PointDataFacade->Source->GetIdxMapping();

		const UPCGMetadata* InMetadata = PointDataFacade->GetIn()->Metadata;
		UPCGMetadata* OutMetadata = PointDataFacade->GetOut()->MutableMetadata();

		TConstPCGValueRange<int64> InMetadataEntries = PointDataFacade->GetIn()->GetConstMetadataEntryValueRange();
		TPCGValueRange<int64> OutMetadataEntries = PointDataFacade->GetOut()->GetMetadataEntryValueRange();

		// We need to initialize metadata in bulk to avoid loosing ton of time with RW locks
		WriteIndex = 0;
		for (int i = 0; i < Intersections.Num(); i++)
		{
			IdxMapping[WriteIndex] = i;

			int64 ParentKey = InMetadataEntries[i];
			OutMetadataEntries[WriteIndex] = ParentKey;
			WriteIndex++;

			TSharedPtr<PCGExMath::OBB::FIntersections> LocalIntersection = Intersections[i];

			if (!LocalIntersection) { continue; }

			for (int j = 0; j < LocalIntersection->Cuts.Num(); j++)
			{
				IdxMapping[WriteIndex] = i;
				OutMetadataEntries[WriteIndex] = PCGInvalidEntryKey;
				OutMetadata->InitializeOnSet(OutMetadataEntries[WriteIndex], ParentKey, InMetadata);

				WriteIndex++;
			}
		}

		// Consume all but metadata entry, as we copied + initialized them previously
		EPCGPointNativeProperties CopyProperties = EPCGPointNativeProperties::All;
		EnumRemoveFlags(CopyProperties, EPCGPointNativeProperties::MetadataEntry);
		PointDataFacade->Source->ConsumeIdxMapping(CopyProperties);

		// TODO : Initialize blenders

		if (!SubBlending->PrepareForData(Context, PointDataFacade, &ProtectedAttributes))
		{
			bIsProcessorValid = false;
			return;
		}

		// Initialize details after, so as to avoid creating "sub-blend-able" attributes 
		Details.Init(PointDataFacade, Context->TargetsHandler);

		StartParallelLoopForRange(PointDataFacade->GetIn()->GetNumPoints());
	}


	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BoundsPathIntersection::ProcessPoints);

		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = PointDataFacade->GetOut()->GetSeedValueRange(false);

		bool bWillWriteAny = Details.WillWriteAny();

		PCGEX_SCOPE_LOOP(Index)
		{
			TSharedPtr<PCGExMath::OBB::FIntersections> LocalIntersection = Intersections[Index];
			if (!LocalIntersection) { continue; }

			int32 NextIndex = Index + 1;

			if (Index == LastIndex)
			{
				if (bClosedLoop) { NextIndex = 0; }
				else { continue; }
			}

			const int32 StartIndex = StartIndices[Index];
			const int32 EndIndex = StartIndices[NextIndex];

			PCGExPaths::FPathMetrics Metrics = PCGExPaths::FPathMetrics(OutTransforms[StartIndex].GetLocation());

			const int32 CutsNum = LocalIntersection->Cuts.Num();
			for (int j = 0; j < CutsNum; j++)
			{
				const PCGExMath::OBB::FCut& Cut = LocalIntersection->Cuts[j];
				const int32 CutIndex = StartIndex + j + 1;

				Metrics.Add(Cut.Position);
				OutSeeds[CutIndex] = PCGExRandomHelpers::ComputeSpatialSeed(Cut.Position);
				OutTransforms[CutIndex].SetLocation(Cut.Position);

				if (bWillWriteAny) { Details.SetIntersection(CutIndex, Cut); }
			}

			Metrics.Add(OutTransforms[EndIndex].GetLocation());

			PCGExData::FScope SubScope = PointDataFacade->GetOutScope(StartIndex + 1, CutsNum);
			SubBlending->ProcessSubPoints(PointDataFacade->GetOutPoint(StartIndex), PointDataFacade->GetOutPoint(EndIndex), SubScope, Metrics);
		}
	}

	void FProcessor::CompleteWork()
	{
		TProcessor<FPCGExBoundsPathIntersectionContext, UPCGExBoundsPathIntersectionSettings>::CompleteWork();
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
