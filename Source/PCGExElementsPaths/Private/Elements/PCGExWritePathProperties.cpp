// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExWritePathProperties.h"
#include "Curve/CurveUtil.h"
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

#if WITH_EDITOR
void UPCGExWritePathPropertiesSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 11)
	{
		// UpVector was merged into the projection normal; carry customized values over.
		// Method is left untouched: BestFit graphs keep their projection (and thus Area/winding/inclusion) intact.
		if (ProjectionDetails.Method == EPCGExProjectionMethod::Normal && !UpVector_DEPRECATED.Equals(FVector::UpVector))
		{
			ProjectionDetails.ProjectionVector.Input = EPCGExInputValueType::Constant;
			ProjectionDetails.ProjectionVector.Constant = UpVector_DEPRECATED;
		}
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

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
		PCGEX_PIN_POINTS(PCGExWritePathProperties::OutputPathMedian, "Paths that are inside at least another path, with an even inclusion depth", Normal)
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

		Path = MakeShared<PCGExPaths::FPath>(PointDataFacade->GetIn(), 0);
		Path->BuildProjection(ProjectionDetails);

		if (Settings->bWriteConcavity)
		{
			// Must run before OffsetProjection: the inclusion offset distorts projected points
			const TArray<FVector2D>& Projected = Path->GetProjectedPoints();
			const double WindingSign = UE::Geometry::CurveUtil::SignedArea2<double, FVector2D>(Projected) < 0 ? -1 : 1;

			PCGExArrayHelpers::InitArray(ConcavitySigns, Projected.Num());
			for (int i = 0; i < Projected.Num(); i++)
			{
				const FVector2D DirIn = (Projected[i] - Projected[Path->PrevPointIndex(i)]).GetSafeNormal();
				const FVector2D DirOut = (Projected[Path->NextPointIndex(i)] - Projected[i]).GetSafeNormal();
				const double Turn = FVector2D::CrossProduct(DirIn, DirOut) * WindingSign;
				ConcavitySigns[i] = FMath::IsNearlyZero(Turn) ? 0 : Turn > 0 ? 1 : -1;
			}
		}

		Path->OffsetProjection(Settings->InclusionDetails.InclusionOffset);
		Path->Idx = PointDataFacade->Source->IOIndex;

		bClosedLoop = Path->IsClosedLoop();

		Path->IOIndex = PointDataFacade->Source->IOIndex;
		PathLength = Path->AddExtra<PCGExPaths::FPathEdgeLength>(true); // Force compute length
		if (Settings->bWritePointNormal || Settings->bWritePointBinormal)
		{
			PathBinormal = Path->AddExtra<PCGExPaths::FPathEdgeBinormal>(false, ProjectionDetails.Normal);
		}
		if (Settings->bWritePointAvgNormal)
		{
			PathAvgNormal = Path->AddExtra<PCGExPaths::FPathEdgeAvgNormal>(false, ProjectionDetails.Normal);
		}

		// Open paths: the last two points both map onto LastEdge; compute it once here so the parallel loop doesn't double-compute it concurrently
		if (!bClosedLoop)
		{
			Path->ComputeEdgeExtra(Path->LastEdge);
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_OUTPUT_INIT)
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
			const FVector ToPrev = Path->DirToPrevPoint(Index);
			const FVector ToNext = Path->DirToNextPoint(Index);

			// LastEdge is precomputed in Process for open paths -- two points map onto it, skip both to avoid a concurrent duplicate compute
			const int32 ExtraIndex = !bClosedLoop && Index == Path->LastIndex ? Path->LastEdge : Index;
			if (bClosedLoop || ExtraIndex != Path->LastEdge)
			{
				Path->ComputeEdgeExtra(ExtraIndex);
			}

			PCGEX_OUTPUT_VALUE(PointNormal, Index, PathBinormal->Normals[ExtraIndex]);
			PCGEX_OUTPUT_VALUE(PointBinormal, Index, PathBinormal->Get(ExtraIndex));
			PCGEX_OUTPUT_VALUE(PointAvgNormal, Index, PathAvgNormal->Get(ExtraIndex));

			PCGEX_OUTPUT_VALUE(DirectionToNext, Index, ToNext);
			PCGEX_OUTPUT_VALUE(DirectionToPrev, Index, ToPrev);

			PCGEX_OUTPUT_VALUE(DistanceToNext, Index, !Path->IsClosedLoop() && Index == Path->LastIndex ? 0 : PathLength->Get(Index))
			PCGEX_OUTPUT_VALUE(DistanceToPrev, Index, Index == 0 ? Path->IsClosedLoop() ? PathLength->Get(Path->LastEdge) : 0 : PathLength->Get(Index-1))

			// Edge dirs are unit length; a single dot/cross pair feeds Dot, Angle and the right-angle test
			const double MainDot = ToPrev.Dot(ToNext);
			const FVector Cross = FVector::CrossProduct(ToPrev, ToNext);
			const double CornerRadians = FMath::Atan2(Cross.Size(), MainDot);

			PCGEX_OUTPUT_VALUE(Dot, Index, -MainDot);
			PCGEX_OUTPUT_VALUE(Angle, Index, PCGExSampling::Helpers::MapAngle(Settings->AngleRange, CornerRadians, Cross.Dot(ProjectionDetails.Normal) < 0));

			if (ConcavityWriter)
			{
				int32 Concavity = ConcavitySigns[Index];
				if (Concavity != 0 && Settings->bFlagRightAngles &&
					FMath::Abs(FMath::RadiansToDegrees(CornerRadians) - 90) <= Settings->RightAngleTolerance)
				{
					Concavity = 0;
				}
				ConcavityWriter->SetValue(Index, Concavity);
			}

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
		FVector PathDir = FVector::ZeroVector;

		// Path-wide accumulation, skipped entirely when nothing consumes it
		const bool bUpdateConvexity = Settings->bTagConcave || Settings->bTagConvex;
		if (bUpdateConvexity || Context->bWritePathDirection || Context->bWritePathCentroid)
		{
			for (int i = 0; i < Path->NumPoints; i++)
			{
				if (bUpdateConvexity)
				{
					Path->UpdateConvexity(i);
				}

				PathDir += Path->DirToNextPoint(i);
				PathCentroid += Path->GetPos_Unsafe(i);
			}
		}

		if (!bClosedLoop)
		{
			// Open path endpoints have no corner: prev/next dirs are anti-parallel, a flat PI angle
			PCGEX_OUTPUT_VALUE(Dot, 0, -1);
			PCGEX_OUTPUT_VALUE(Angle, 0, PCGExSampling::Helpers::MapAngle(Settings->AngleRange, UE_DOUBLE_PI, false));

			PCGEX_OUTPUT_VALUE(Dot, Path->LastIndex, -1);
			PCGEX_OUTPUT_VALUE(Angle, Path->LastIndex, PCGExSampling::Helpers::MapAngle(Settings->AngleRange, UE_DOUBLE_PI, false));
		}

		if (Settings->WriteAnyPathData())
		{
			PathAttributeSet = Context->PathAttributeSet ? Context->PathAttributeSet.Get() : Context->ManagedObjects->New<UPCGParamData>();
			const int64 Key = Context->PathAttributeSet ? Context->MergedAttributeSetKeys[PointDataFacade->Source->IOIndex] : PathAttributeSet->Metadata->AddEntry();

#define PCGEX_OUTPUT_PATH_VALUE(_NAME, _TYPE, _VALUE) if(Context->bWrite##_NAME){\
	if (Settings->bWritePathDataToPoints) { WriteMark(PointIO, Settings->_NAME##AttributeName, _VALUE);}\
			PathAttributeSet->Metadata->FindOrCreateAttribute<_TYPE>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->_NAME##AttributeName, PathAttributeSet).Name, _VALUE)->SetValue(Key, _VALUE); }

			PCGEX_OUTPUT_PATH_VALUE(PathLength, double, PathLength->TotalLength)
			PCGEX_OUTPUT_PATH_VALUE(PathDirection, FVector, (PathDir / Path->NumPoints).GetSafeNormal())
			PCGEX_OUTPUT_PATH_VALUE(PathCentroid, FVector, (PathCentroid / Path->NumPoints))

			if (Context->bWriteIsClockwise || Context->bWriteArea || Context->bWritePerimeter || Context->bWriteCompactness)
			{
				const PCGExMath::FPolygonInfos PolyInfos = PCGExMath::FPolygonInfos(Path->GetProjectedPoints());
				PCGEX_OUTPUT_PATH_VALUE(IsClockwise, bool, PolyInfos.bIsClockwise)
				PCGEX_OUTPUT_PATH_VALUE(Area, double, PolyInfos.Area * 0.01)
				PCGEX_OUTPUT_PATH_VALUE(Perimeter, double, PolyInfos.Perimeter)
				PCGEX_OUTPUT_PATH_VALUE(Compactness, double, PolyInfos.Compactness)
			}

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
