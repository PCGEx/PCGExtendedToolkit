// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingPlotNavmesh.h"

#include "Clusters/PCGExClusterCommon.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPathsCommon.h"
#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"

#define LOCTEXT_NAMESPACE "PCGExPathfindingPlotNavmeshElement"
#define PCGEX_NAMESPACE PathfindingPlotNavmesh

TArray<FPCGPinProperties> UPCGExPathfindingPlotNavmeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBlending::Labels::SourceOverridesBlendingOps)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPathfindingPlotNavmeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Paths output.", Required)
	return PinProperties;
}

#if WITH_EDITOR
void UPCGExPathfindingPlotNavmeshSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Blending) { Blending = NewObject<UPCGExSubPointsBlendInterpolate>(this, TEXT("Blending")); }
	}
	Super::PostInitProperties();
}

void UPCGExPathfindingPlotNavmeshSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Blending) { Blending->UpdateUserFacingInfos(); }
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FName UPCGExPathfindingPlotNavmeshSettings::GetMainInputPin() const { return PCGExClusters::Labels::SourcePlotsLabel; }

PCGEX_INITIALIZE_ELEMENT(PathfindingPlotNavmesh)


bool FPCGExPathfindingPlotNavmeshElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingPlotNavmesh)

	PCGEX_BIND_INSTANCED_FACTORY(Blending, UPCGExSubPointsBlendInstancedFactory, PCGExBlending::Labels::SourceOverridesBlendingOps)

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);

	PCGEX_FWD(bAddSeedToPath)
	PCGEX_FWD(bAddGoalToPath)
	PCGEX_FWD(bAddPlotPointsToPath)

	PCGEX_FWD(NavAgentProperties)
	PCGEX_FWD(bRequireNavigableEndLocation)
	PCGEX_FWD(PathfindingMode)

	Context->FuseDistance = Settings->FuseDistance;

	return true;
}

bool FPCGExPathfindingPlotNavmeshElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathfindingPlotNavmeshElement::Execute);

	PCGEX_CONTEXT(PathfindingPlotNavmesh)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		while (Context->AdvancePointsIO(false))
		{
			if (Context->CurrentIO->GetNum() < 2) { continue; }
			PCGEX_LAUNCH(FPCGExPlotNavmeshTask, Context->CurrentIO)
		}
		Context->SetState(PCGExCommon::States::State_ProcessingPoints);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_ProcessingPoints)
	{
		Context->OutputPaths->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

void FPCGExPlotNavmeshTask::ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	FPCGExPathfindingPlotNavmeshContext* Context = TaskManager->GetContext<FPCGExPathfindingPlotNavmeshContext>();
	PCGEX_SETTINGS(PathfindingPlotNavmesh)

	const int32 NumPlots = PointIO->GetNum();

	TArray<PCGExNavmesh::FNavmeshQuery> PlotQueries;
	PlotQueries.Reserve(NumPlots);

	// Build all queries
	for (int32 i = 0; i < NumPlots - 1; i++)
	{
		PCGExNavmesh::FNavmeshQuery& Query = PlotQueries.Emplace_GetRef(PCGExPathfinding::FSeedGoalPair(PointIO->GetInPoint(i), PointIO->GetInPoint(i + 1)));
		Query.FindPath(Context);
	}

	if (Settings->bClosedLoop)
	{
		PCGExNavmesh::FNavmeshQuery& Query = PlotQueries.Emplace_GetRef(PCGExPathfinding::FSeedGoalPair(PointIO->GetInPoint(NumPlots - 1), PointIO->GetInPoint(0)));
		Query.FindPath(Context);
	}

	const int32 NumQueries = PlotQueries.Num();
	if (NumQueries == 0) { return; }

	// Trim boundary duplicates from positions.
	// Navmesh paths include start/end points that overlap with explicit seed/goal/plot points
	// and with neighboring query endpoints.
	for (int32 qi = 0; qi < NumQueries; qi++)
	{
		TArray<FVector>& Positions = PlotQueries[qi].Positions;
		if (Positions.IsEmpty()) { continue; }

		const bool bIsLast = (qi == NumQueries - 1);
		const bool bIsClosingQuery = Settings->bClosedLoop && bIsLast;

		// Skip first position: it duplicates the previous query's last point OR explicit seed
		const bool bSkipFirst = (qi > 0) || Settings->bAddSeedToPath;

		// Skip last position: it duplicates the explicit point that follows
		bool bSkipLast = false;
		if (bIsClosingQuery) { bSkipLast = true; }
		else if (bIsLast && !Settings->bClosedLoop && Settings->bAddGoalToPath) { bSkipLast = true; }
		else if (!bIsLast && Settings->bAddPlotPointsToPath) { bSkipLast = true; }

		if (bSkipFirst && !Positions.IsEmpty()) { Positions.RemoveAt(0); }
		if (bSkipLast && !Positions.IsEmpty()) { Positions.Pop(); }
	}

	// Count total points
	PCGExPointArrayDataHelpers::FReadWriteScope PlotScope(NumPlots + 2, false);
	int32 NumPoints = 0;

	if (Settings->bAddSeedToPath)
	{
		PlotScope.Add(PlotQueries[0].SeedGoalPair.Seed, NumPoints++);
	}

	for (int32 qi = 0; qi < NumQueries; qi++)
	{
		NumPoints += PlotQueries[qi].Positions.Num();

		const bool bIsLast = (qi == NumQueries - 1);
		const bool bIsClosingQuery = Settings->bClosedLoop && bIsLast;

		if (!bIsClosingQuery)
		{
			if (Settings->bAddPlotPointsToPath || (bIsLast && !Settings->bClosedLoop && Settings->bAddGoalToPath))
			{
				PlotScope.Add(PlotQueries[qi].SeedGoalPair.Goal, NumPoints++);
			}
		}
	}

	if (NumPoints <= 2) { return; }

	// Initialize data
	TSharedPtr<PCGExData::FPointIO> PathIO = Context->OutputPaths->Emplace_GetRef(PointIO, PCGExData::EIOInit::New);
	PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())

	UPCGBasePointData* OutPathData = PathIO->GetOut();
	PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPathData, NumPoints);

	// Copy seed/goal properties
	PlotScope.CopyPoints(PointIO->GetIn(), PathIO->GetOut());

	TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending = Context->Blending->CreateOperation();
	if (!SubBlending->PrepareForData(Context, PathDataFacade)) { return; }

	TPCGValueRange<FTransform> OutTransforms = OutPathData->GetTransformValueRange(false);

	int32 WriteIndex = Settings->bAddSeedToPath ? 1 : 0;
	for (int32 qi = 0; qi < NumQueries; qi++)
	{
		PCGExNavmesh::FNavmeshQuery& Query = PlotQueries[qi];

		const int32 PositionCount = Query.Positions.Num();
		const int32 StartIndex = WriteIndex;
		Query.CopyPositions(OutTransforms, WriteIndex, false, false);

		PCGExData::FScope SubScope(PathIO->GetOut(), StartIndex, PositionCount);
		if (SubScope.IsValid())
		{
			SubBlending->BlendSubPoints(PointIO->GetInPoint(Query.SeedGoalPair.Seed), PointIO->GetInPoint(Query.SeedGoalPair.Goal), SubScope, Query.SeedGoalMetrics);
		}

		const bool bIsLast = (qi == NumQueries - 1);
		const bool bIsClosingQuery = Settings->bClosedLoop && bIsLast;

		// Skip over explicit point slot if one was counted
		if (!bIsClosingQuery)
		{
			if (Settings->bAddPlotPointsToPath || (bIsLast && !Settings->bClosedLoop && Settings->bAddGoalToPath))
			{
				WriteIndex++;
			}
		}
	}

	PathDataFacade->WriteFastest(TaskManager);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
