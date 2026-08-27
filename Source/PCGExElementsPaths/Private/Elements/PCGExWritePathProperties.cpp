// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWritePathProperties.h"
#include "MinVolumeBox3.h"
#include "OrientedBoxTypes.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExWinding.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Sampling/PCGExSamplingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExWritePathPropertiesElement"
#define PCGEX_NAMESPACE WritePathProperties

bool UPCGExWritePathPropertiesSettings::CanForwardData() const
{
#define PCGEX_PATH_MARK_FALSE(_NAME, _TYPE, _DEFAULT) if(bWrite##_NAME){return false;}
	PCGEX_FOREACH_FIELD_PATH(PCGEX_PATH_MARK_FALSE)
	PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_PATH_MARK_FALSE)
#undef PCGEX_PATH_MARK_FALSE

	return true;
}

bool UPCGExWritePathPropertiesSettings::WantsInclusionHelper() const
{
	return bTagInner || bTagOuter || bTagOddInclusionDepth || bWriteNumInside || bWriteInclusionDepth || bUseInclusionPins || bWriteIsHole || bTagPairing;
}

bool UPCGExWritePathPropertiesSettings::WriteAnyPathData() const
{
#define PCGEX_PATH_MARK_TRUE(_NAME, _TYPE, _DEFAULT) if(bWrite##_NAME){return true;}
	PCGEX_FOREACH_FIELD_PATH(PCGEX_PATH_MARK_TRUE)
#undef PCGEX_PATH_MARK_TRUE

	return bTagInner || bTagOuter || bTagOddInclusionDepth;
}

TArray<FPCGPinProperties> UPCGExWritePathPropertiesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (bUseInclusionPins)
	{
		PCGEX_PIN_POINTS(PCGExWritePathProperties::OutputPathOuter, "Paths that aren't inside any other path", Normal)
		PCGEX_PIN_POINTS(PCGExWritePathProperties::OutputPathInner, "Paths that are inside at least another path", Normal)
		PCGEX_PIN_POINTS(PCGExWritePathProperties::OutputPathMedian, "Paths that are inside at least another path, with an odd inclusion depth", Normal)
	}
	if (WriteAnyPathData())
	{
		PCGEX_PIN_PARAMS(PCGExWritePathProperties::OutputPathProperties, "...", Advanced)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(WritePathProperties)

PCGExData::EIOInit UPCGExWritePathPropertiesSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(WritePathProperties)

bool FPCGExWritePathPropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(WritePathProperties)

	PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_OUTPUT_VALIDATE_NAME)
	PCGEX_FOREACH_FIELD_PATH(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->PathAttributePackingMode == EPCGExAttributeSetPackingMode::Merged && Settings->WriteAnyPathData())
	{
		Context->PathAttributeSet = Context->ManagedObjects->New<UPCGParamData>();
		PCGExArrayHelpers::InitArray(Context->MergedAttributeSetKeys, Context->MainPoints->Num());
	}

	return true;
}

bool FPCGExWritePathPropertiesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWritePathPropertiesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WritePathProperties)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				if (Context->PathAttributeSet)
				{
					Context->MergedAttributeSetKeys[Entry->IOIndex] = Context->PathAttributeSet->Metadata->AddEntry();
				}

				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid path."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	if (Context->PathAttributeSet)
	{
		Context->IncreaseStagedOutputReserve(Context->MainPoints->Num() + 1);
		Context->StageOutput(Context->PathAttributeSet, PCGExWritePathProperties::OutputPathProperties);
	}
	else
	{
		Context->IncreaseStagedOutputReserve(Context->MainPoints->Num() * 2);
	}

	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExWritePathProperties
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWritePathProperties::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(PointDataFacade))
		{
			return false;
		}

		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		Path = MakeShared<PCGExPaths::FPath>(PointDataFacade->GetIn(), 0);
		Path->BuildProjection(ProjectionDetails);
		Path->OffsetProjection(Settings->InclusionDetails.InclusionOffset);
		Path->Idx = PointDataFacade->Source->IOIndex;

		bClosedLoop = Path->IsClosedLoop();

		Path->IOIndex = PointDataFacade->Source->IOIndex;
		PathLength = Path->AddExtra<PCGExPaths::FPathEdgeLength>(true); // Force compute length
		if (Settings->bWritePointNormal || Settings->bWritePointBinormal)
		{
			PathBinormal = Path->AddExtra<PCGExPaths::FPathEdgeBinormal>(false, Settings->UpVector);
		}
		if (Settings->bWritePointAvgNormal)
		{
			PathAvgNormal = Path->AddExtra<PCGExPaths::FPathEdgeAvgNormal>(false, Settings->UpVector);
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_OUTPUT_INIT)
		}

		///

		const int32 NumPoints = PointIO->GetIn()->GetNumPoints();

		PCGExArrayHelpers::InitArray(Details, NumPoints);

		for (int i = 0; i < NumPoints; i++)
		{
			Details[i] = {i, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::WritePathProperties::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			FPointDetails& Current = Details[Index];

			Current.ToPrev = Path->DirToPrevPoint(Index);
			Current.ToNext = Path->DirToNextPoint(Index);

			int32 ExtraIndex = !bClosedLoop && Index == Path->LastIndex ? Path->LastEdge : Index;
			Path->ComputeEdgeExtra(ExtraIndex);

			PCGEX_OUTPUT_VALUE(PointNormal, Index, PathBinormal->Normals[ExtraIndex]);
			PCGEX_OUTPUT_VALUE(PointBinormal, Index, PathBinormal->Get(ExtraIndex));
			PCGEX_OUTPUT_VALUE(PointAvgNormal, Index, PathAvgNormal->Get(ExtraIndex));

			PCGEX_OUTPUT_VALUE(DirectionToNext, Index, Current.ToNext);
			PCGEX_OUTPUT_VALUE(DirectionToPrev, Index, Current.ToPrev);

			PCGEX_OUTPUT_VALUE(DistanceToNext, Index, !Path->IsClosedLoop() && Index == Path->LastIndex ? 0 : PathLength->Get(Index))
			PCGEX_OUTPUT_VALUE(DistanceToPrev, Index, Index == 0 ? Path->IsClosedLoop() ? PathLength->Get(Path->LastEdge) : 0 : PathLength->Get(Index-1))

			PCGEX_OUTPUT_VALUE(Dot, Index, FVector::DotProduct(Current.ToPrev*-1, Current.ToNext));
			PCGEX_OUTPUT_VALUE(Angle, Index, PCGExSampling::Helpers::GetAngle(Settings->AngleRange, Current.ToPrev, Current.ToNext));

			// Compute distance from start using pre-computed cumulative length prefix sum
			// CumulativeLength[i] = sum of edge lengths 0..i, so DistanceToStart[i] = CumulativeLength[i-1] for i > 0
			const double DistToStart = Index == 0 ? 0.0 : PathLength->CumulativeLength[Index - 1];
			PCGEX_OUTPUT_VALUE(DistanceToStart, Index, DistToStart);
			PCGEX_OUTPUT_VALUE(DistanceToEnd, Index, PathLength->TotalLength - DistToStart);
			PCGEX_OUTPUT_VALUE(PointTime, Index, Settings->bTimeOneMinus ? 1.0 - (DistToStart / PathLength->TotalLength) : DistToStart / PathLength->TotalLength);
		}
	}

	void FProcessor::CompleteWork()
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		FVector PathCentroid = FVector::ZeroVector;
		FVector PathDir = Details[0].ToNext;

		// Compute path-wide data (DistanceToStart/DistanceToEnd/PointTime are now computed in parallel in ProcessPoints)
		for (int i = 0; i < Path->NumPoints; i++)
		{
			if (Settings->bTagConcave || Settings->bTagConvex)
			{
				Path->UpdateConvexity(i);
			}

			PathDir += Details[i].ToNext;
			PathCentroid += Path->GetPos_Unsafe(i);
		}

		if (!bClosedLoop)
		{
			const FPointDetails& First = Details[0];
			const FPointDetails& Last = Details[Path->LastIndex];

			// Open path endpoints have no corner: prev/next dirs are anti-parallel, a flat PI angle.
			// Dot is written as -(ToPrev . ToNext) above, so anti-parallel reads +1, not -1.
			PCGEX_OUTPUT_VALUE(Dot, 0, 1);
			PCGEX_OUTPUT_VALUE(Angle, 0, PCGExSampling::Helpers::GetAngle(Settings->AngleRange, First.ToNext *-1, First.ToNext));

			PCGEX_OUTPUT_VALUE(Dot, Path->LastIndex, 1);
			PCGEX_OUTPUT_VALUE(Angle, Path->LastIndex, PCGExSampling::Helpers::GetAngle(Settings->AngleRange, Last.ToPrev *-1, Last.ToPrev));
		}

		if (Settings->WriteAnyPathData())
		{
			const PCGExMath::FPolygonInfos PolyInfos = PCGExMath::FPolygonInfos(Path->GetProjectedPoints());

			PathAttributeSet = Context->PathAttributeSet ? Context->PathAttributeSet.Get() : Context->ManagedObjects->New<UPCGParamData>();
			const int64 Key = Context->PathAttributeSet ? Context->MergedAttributeSetKeys[PointDataFacade->Source->IOIndex] : PathAttributeSet->Metadata->AddEntry();

#define PCGEX_OUTPUT_PATH_VALUE(_NAME, _TYPE, _VALUE) if(Context->bWrite##_NAME){\
	if (Settings->bWritePathDataToPoints) { WriteMark(PointIO, Settings->_NAME##AttributeName, _VALUE);}\
			PathAttributeSet->Metadata->FindOrCreateAttribute<_TYPE>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->_NAME##AttributeName, PathAttributeSet).Name, _VALUE)->SetValue(Key, _VALUE); }

			PCGEX_OUTPUT_PATH_VALUE(PathLength, double, PathLength->TotalLength)
			PCGEX_OUTPUT_PATH_VALUE(PathDirection, FVector, (PathDir / Path->NumPoints).GetSafeNormal())
			PCGEX_OUTPUT_PATH_VALUE(PathCentroid, FVector, (PathCentroid / Path->NumPoints))
			PCGEX_OUTPUT_PATH_VALUE(IsClockwise, bool, PolyInfos.bIsClockwise)
			PCGEX_OUTPUT_PATH_VALUE(Area, double, PolyInfos.Area * 0.01)
			PCGEX_OUTPUT_PATH_VALUE(Perimeter, double, PolyInfos.Perimeter)
			PCGEX_OUTPUT_PATH_VALUE(Compactness, double, PolyInfos.Compactness)

			bool bIsOdd = false;
			bool bInner = false;

			if (PCGExPaths::FInclusionInfos Infos;
				Context->InclusionHelper && Context->InclusionHelper->Find(Path->Idx, Infos))
			{
				bIsOdd = Infos.bOdd;
				bInner = Infos.Depth > 0;
				PCGEX_OUTPUT_PATH_VALUE(InclusionDepth, int32, Infos.Depth)
				PCGEX_OUTPUT_PATH_VALUE(NumInside, int32, Infos.Children)
			}

			if (bIsOdd && Settings->bTagOddInclusionDepth && (!Settings->bOuterIsNotOdd || bInner))
			{
				PointIO->Tags->AddRaw(Settings->OddInclusionDepthTag);
			}
			if (bInner)
			{
				if (Settings->bTagInner)
				{
					PointIO->Tags->AddRaw(Settings->InnerTag);
				}
			}
			else
			{
				if (Settings->bTagOuter)
				{
					PointIO->Tags->AddRaw(Settings->OuterTag);
				}
			}

			if (Settings->bWriteBoundingBoxCenter || Settings->bWriteBoundingBoxExtent || Settings->bWriteBoundingBoxOrientation)
			{
				UE::Geometry::TMinVolumeBox3<double> Box;
				if (Box.Solve(Path->NumPoints, [PathPtr = Path.Get()](int32 i)
				{
					return PathPtr->GetPos_Unsafe(i);
				}))
				{
					UE::Geometry::FOrientedBox3d Result;
					Box.GetResult(Result);

					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxCenter, FVector, Result.Center());
					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxExtent, FVector, Result.Extents);
					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxOrientation, FQuat, FQuat(Result.Frame.Rotation));
				}
				else
				{
					const FBox Bounds = PointIO->GetIn()->GetBounds();
					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxCenter, FVector, Bounds.GetCenter());
					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxExtent, FVector, Bounds.GetExtent());
					PCGEX_OUTPUT_PATH_VALUE(BoundingBoxOrientation, FQuat, FQuat::Identity);
				}
			}


#undef PCGEX_OUTPUT_PATH_VALUE
		}

		// Pairing tag: give each outer and the holes directly nested inside it a shared id.
		// An outer (even inclusion depth) uses its own collection index; a hole (odd depth)
		// inherits its nearest enclosing outer's index, so odd/even stacks split into groups.
		if (Settings->bTagPairing && Context->InclusionHelper)
		{
			if (PCGExPaths::FInclusionInfos Infos; Context->InclusionHelper->Find(Path->Idx, Infos))
			{
				const bool bIsOuter = Infos.Depth % 2 == 0;
				const int32 PairingId = (bIsOuter || Infos.ParentIdx == -1) ? Path->Idx : Infos.ParentIdx;
				PointIO->Tags->Set<int32>(Settings->PairingTag, PairingId);
			}
		}

		///

		if (Path->ConvexitySign != 0)
		{
			if (Settings->bTagConcave && !Path->bIsConvex)
			{
				PointIO->Tags->AddRaw(Settings->ConcaveTag);
			}
			if (Settings->bTagConvex && Path->bIsConvex)
			{
				PointIO->Tags->AddRaw(Settings->ConvexTag);
			}
		}

		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Output()
	{
		const TSet<FString> FlattenedTags = PointDataFacade->Source->Tags->Flatten();

		TProcessor<FPCGExWritePathPropertiesContext, UPCGExWritePathPropertiesSettings>::Output();
		if (PathAttributeSet && !Context->PathAttributeSet)
		{
			Context->StageOutput(PathAttributeSet, OutputPathProperties, PCGExData::EStaging::None, FlattenedTags);
		}

		// A hole is a path at ODD inclusion depth (even-odd rule); depth 0 (outer) and even depths are not holes.
		if (Settings->bWriteIsHole && Context->InclusionHelper)
		{
			PCGExPaths::FInclusionInfos HoleInfos;
			PCGExPaths::Helpers::SetIsHole(PointDataFacade->Source, Context->InclusionHelper->Find(Path->Idx, HoleInfos) && HoleInfos.bOdd);
		}

		if (PCGExPaths::FInclusionInfos Infos;
			Settings->bUseInclusionPins && Context->InclusionHelper && Context->InclusionHelper->Find(Path->Idx, Infos))
		{
			if (!Infos.Depth)
			{
				Context->NumOuter++;
				Context->StageOutput(PointDataFacade->GetOut(), OutputPathOuter, PCGExData::EStaging::None, FlattenedTags);
			}
			else
			{
				Context->NumInner++;
				Context->StageOutput(PointDataFacade->GetOut(), OutputPathInner, PCGExData::EStaging::None, FlattenedTags);

				if (Infos.bOdd && (!Settings->bOuterIsNotOdd || Infos.Depth > 0))
				{
					Context->NumOdd++;
					Context->StageOutput(PointDataFacade->GetOut(), OutputPathMedian, PCGExData::EStaging::None, FlattenedTags);
				}
			}
		}
	}

	FBatch::FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
		: TBatch(InContext, InPointsCollection)
	{
	}

	void FBatch::OnInitialPostProcess()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WritePathProperties)

		if (Settings->WantsInclusionHelper())
		{
			Context->InclusionHelper = MakeShared<PCGExPaths::FPathInclusionHelper>();
			TArray<TSharedPtr<PCGExPaths::FPath>> Paths;
			Paths.Reserve(Processors.Num());

			for (const TSharedRef<PCGExPointsMT::IProcessor>& P : Processors)
			{
				Paths.Add(StaticCastSharedRef<FProcessor>(P)->Path);
			}
			Context->InclusionHelper->AddPaths(Paths, Settings->InclusionDetails.InclusionTolerance);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
