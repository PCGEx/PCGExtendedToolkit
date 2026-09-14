// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetPathData.h"

#include "Components/SplineComponent.h"
#include "LandscapeSplinesComponent.h"
#include "GameFramework/Actor.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGLandscapeSplineData.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineData.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Helpers/PCGHelpers.h"                  // DefaultPCGTag

#include "Core/PCGExMTCommon.h"                  // ParallelOrSequential
#include "Data/PCGExDataHelpers.h"               // SetDataValue
#include "Data/PCGExDataTags.h"                  // FTags
#include "Details/PCGExFilterDetails.h"          // PCGEx::TagsToData
#include "Helpers/PCGExPointArrayDataHelpers.h"  // SetNumPointsAllocated
#include "Helpers/PCGExRandomHelpers.h"          // ComputeSpatialSeed
#include "Paths/PCGExPathsHelpers.h"             // SetClosedLoop

#define LOCTEXT_NAMESPACE "PCGExGetPathData"

namespace PCGExGetPathData
{
	const FName OutputPathsLabel = TEXT("Paths");
	const FName OutputSplinesLabel = TEXT("Splines");

	// One per spline component that will emit a path. Everything here is created single-threaded in
	// PrepareActor (Phase 1); FillPath (Phase 2) only writes per-point values into these pre-allocated
	// buffers, so concurrent work items never touch shared state.
	struct FPathWork
	{
		const UPCGPolyLineData* PolyData = nullptr; // source: UPCGSplineData or UPCGLandscapeSplineData
		const UPCGSplineData* SplineData = nullptr; // non-null only for regular splines (point-type source)
		UPCGBasePointData* PathData = nullptr;
		FPCGMetadataAttribute<FVector>* ArriveAttr = nullptr;
		FPCGMetadataAttribute<FVector>* LeaveAttr = nullptr;
		FPCGMetadataAttribute<double>* LengthAttr = nullptr;
		FPCGMetadataAttribute<double>* AlphaAttr = nullptr;
		FPCGMetadataAttribute<int32>* PointTypeAttr = nullptr; // non-null only for regular splines
	};

	// Phase 2 -- runs on a worker thread. Writes only into Work's own pre-allocated buffers/attributes.
	// Samples through the UPCGPolyLineData interface so regular and landscape splines share one path.
	void FillPath(const FPathWork& Work, const UPCGExGetPathDataSettings* Settings)
	{
		const UPCGPolyLineData* Poly = Work.PolyData;
		const int32 NumSegments = Poly->GetNumSegments();
		const bool bClosedLoop = Poly->IsClosed();
		const double TotalLength = Poly->GetLength();
		const FTransform LineTransform = Poly->GetTransform();

		UPCGBasePointData* OutData = Work.PathData;
		TPCGValueRange<FTransform> OutTransforms = OutData->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = OutData->GetSeedValueRange(false);
		TPCGValueRange<int64> OutMeta = OutData->GetMetadataEntryValueRange();

		// Point type only exists for regular splines (CIM interp modes); landscape splines have none.
		const FInterpCurveVector* SplinePositions = (Work.PointTypeAttr && Work.SplineData) ? &Work.SplineData->SplineStruct.GetSplinePointsPosition() : nullptr;

		auto WritePoint = [&](const int32 PointIndex, const int32 SegmentIndex, const FTransform& Transform, const double LengthAtPoint)
		{
			Settings->TransformDetails.ApplyTo(OutTransforms[PointIndex], Transform);
			OutSeeds[PointIndex] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[PointIndex].GetLocation());

			const PCGMetadataEntryKey Key = OutMeta[PointIndex];
			if (Work.ArriveAttr || Work.LeaveAttr)
			{
				// GetTangentsAtSegmentStart returns spline-local tangents; bring them to world via the line transform.
				FVector ArriveTangent = FVector::ZeroVector;
				FVector LeaveTangent = FVector::ZeroVector;
				Poly->GetTangentsAtSegmentStart(SegmentIndex, ArriveTangent, LeaveTangent);
				if (Work.ArriveAttr) { Work.ArriveAttr->SetValue(Key, LineTransform.TransformVector(ArriveTangent)); }
				if (Work.LeaveAttr) { Work.LeaveAttr->SetValue(Key, LineTransform.TransformVector(LeaveTangent)); }
			}
			if (Work.LengthAttr) { Work.LengthAttr->SetValue(Key, LengthAtPoint); }
			if (Work.AlphaAttr) { Work.AlphaAttr->SetValue(Key, TotalLength > 0 ? LengthAtPoint / TotalLength : 0); }
			if (SplinePositions) { Work.PointTypeAttr->SetValue(Key, PCGExPaths::Helpers::SplinePointTypeToInt(SplinePositions->Points[SegmentIndex].InterpMode)); }
		};

		for (int32 i = 0; i < NumSegments; i++)
		{
			WritePoint(i, i, Poly->GetTransformAtDistance(i, 0.0, /*bWorldSpace=*/true), Poly->GetDistanceAtSegmentStart(i));
		}

		if (!bClosedLoop)
		{
			// Last vertex of an open line is the end of the final segment.
			WritePoint(NumSegments, NumSegments, Poly->GetTransformAtDistance(NumSegments - 1, Poly->GetSegmentLength(NumSegments - 1), true), TotalLength);
		}
	}

	// Phase 1 -- runs on the game thread. Creates every UObject + metadata structure for one actor's
	// spline components, emits outputs, and queues path-fill work items. No per-point values yet.
	void PrepareActor(FPCGDataFromActorContext* Context, const UPCGExGetPathDataSettings* Settings, AActor* Actor, TArray<FPathWork>& OutPathWork)
	{
		if (!IsValid(Actor)) { return; }

		const FSoftObjectPath ActorPath(Actor);

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component) { continue; }
			if (!Context->ComponentSelector.FilterComponent(Component)) { continue; }
			// Mirror the engine getter: skip components PCG spawned (tagged DefaultPCGTag).
			if (Settings->bIgnorePCGGeneratedComponents && Component->ComponentTags.Contains(PCGHelpers::DefaultPCGTag)) { continue; }

			// Read the raw component as poly-line data (bakes its world transform). Regular splines also
			// expose CIM point types via SplineData; landscape splines share only the polyline interface.
			UPCGPolyLineData* PolyData = nullptr;
			const UPCGSplineData* SplineData = nullptr;
			if (USplineComponent* SplineComp = Cast<USplineComponent>(Component))
			{
				UPCGSplineData* TypedData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
				TypedData->Initialize(SplineComp);
				PolyData = TypedData;
				SplineData = TypedData;
				PCGExPaths::Helpers::SetClosedLoop(TypedData, TypedData->IsClosed());
			}
			else if (ULandscapeSplinesComponent* LandscapeComp = Cast<ULandscapeSplinesComponent>(Component))
			{
				UPCGLandscapeSplineData* TypedData = FPCGContext::NewObject_AnyThread<UPCGLandscapeSplineData>(Context);
				TypedData->Initialize(LandscapeComp);
				PolyData = TypedData;
				PCGExPaths::Helpers::SetClosedLoop(TypedData, TypedData->IsClosed());
			}
			else { continue; }

			const int32 NumSegments = PolyData->GetNumSegments();
			if (NumSegments <= 0) { continue; }

			const bool bClosedLoop = PolyData->IsClosed();
			if (Settings->SampleInputs == EPCGExSplineSamplingIncludeMode::ClosedLoopOnly && !bClosedLoop) { continue; }
			if (Settings->SampleInputs == EPCGExSplineSamplingIncludeMode::OpenSplineOnly && bClosedLoop) { continue; }

			// Actor ref + tags are shared by both outputs; compute once and stamp whichever are enabled.
			TSet<FString> GatheredTags;
			if (Settings->bForwardSourceTags)
			{
				for (const FName& Tag : Actor->Tags) { GatheredTags.Add(Tag.ToString()); }
				for (const FName& Tag : Component->ComponentTags) { GatheredTags.Add(Tag.ToString()); }
			}
			const TSharedPtr<PCGExData::FTags> Tags = MakeShared<PCGExData::FTags>(GatheredTags);

			auto StampAndEmit = [&](UPCGData* Data, const FName Pin)
			{
				// TagsToData first, then the actor reference -- so a source tag literally named
				// "ActorReference" can't clobber the node's own actor-reference stamp.
				PCGEx::TagsToData(Data, Tags, Settings->TagsToData);

				if (Settings->bWriteActorReference)
				{
					const FName ActorRefName = Settings->ActorReferenceAttributeName.IsNone() ? PCGPointDataConstants::ActorReferenceAttribute : Settings->ActorReferenceAttributeName;
					PCGExData::Helpers::SetDataValue<FSoftObjectPath>(Data, ActorRefName, ActorPath);
				}

				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
				Output.Data = Data;
				Output.Pin = Pin;
				Tags->DumpTo(Output.Tags);
			};

			// Poly-line data is complete after Initialize; emit it directly when requested (no fill needed).
			if (Settings->bOutputSplines) { StampAndEmit(PolyData, OutputSplinesLabel); }

			if (!Settings->bOutputPaths) { continue; }

			// Point type only applies to regular splines (landscape splines have no interp modes).
			const bool bWritePointType = Settings->bWritePointType && SplineData != nullptr;
			const bool bWriteAnyAttr =
				Settings->bWriteArriveTangent || Settings->bWriteLeaveTangent ||
				Settings->bWriteLengthAtPoint || Settings->bWriteAlpha || bWritePointType;

			const int32 NumPoints = bClosedLoop ? NumSegments : NumSegments + 1;

			UPCGBasePointData* PathData = FPCGContext::NewPointData_AnyThread(Context);
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(
				PathData, NumPoints,
				EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);

			FPathWork Work;
			Work.PolyData = PolyData;
			Work.SplineData = SplineData;
			Work.PathData = PathData;

			if (bWriteAnyAttr)
			{
				// Each path point needs a real metadata entry before Phase 2 can set per-point values.
				TPCGValueRange<int64> OutMeta = PathData->GetMetadataEntryValueRange();
				for (int32 i = 0; i < NumPoints; i++) { OutMeta[i] = PathData->Metadata->AddEntry(); }

				if (Settings->bWriteArriveTangent) { Work.ArriveAttr = PathData->Metadata->FindOrCreateAttribute<FVector>(Settings->ArriveTangentAttributeName, FVector::ZeroVector, false, false, true); }
				if (Settings->bWriteLeaveTangent) { Work.LeaveAttr = PathData->Metadata->FindOrCreateAttribute<FVector>(Settings->LeaveTangentAttributeName, FVector::ZeroVector, false, false, true); }
				if (Settings->bWriteLengthAtPoint) { Work.LengthAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->LengthAtPointAttributeName, 0, false, false, true); }
				if (Settings->bWriteAlpha) { Work.AlphaAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->AlphaAttributeName, 0, false, false, true); }
				if (bWritePointType) { Work.PointTypeAttr = PathData->Metadata->FindOrCreateAttribute<int32>(Settings->PointTypeAttributeName, 0, false, false, true); }
			}

			// Closed-loop marker + actor ref + tags are metadata structure -> set single-threaded now.
			// Emitting here is fine: Phase 2 fills PathData's value ranges in place (no realloc).
			PCGExPaths::Helpers::SetClosedLoop(PathData, bClosedLoop);
			StampAndEmit(PathData, OutputPathsLabel);

			OutPathWork.Add(Work);
		}
	}
}

#pragma region UPCGExGetPathDataSettings

#if WITH_EDITOR
FText UPCGExGetPathDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Reads spline (and landscape spline) components directly off the selected actors and outputs each as a path and/or spline data, stamped with the source actor reference. Replaces the GetSplineData -> SplineToPath flow when the actor reference matters.");
}
#endif

TArray<FPCGPinProperties> UPCGExGetPathDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGExGetPathData::OutputPathsLabel, EPCGDataType::Point, true, true, LOCTEXT("PathsPinTooltip", "One point path per spline component."));
	PinProperties.Emplace(PCGExGetPathData::OutputSplinesLabel, EPCGDataType::PolyLine, true, true, LOCTEXT("SplinesPinTooltip", "The source spline data (stamped with the actor reference when Write Actor Reference is enabled)."));
	return PinProperties;
}

bool UPCGExGetPathDataSettings::IsPinStaticallyActive(const FName& PinLabel) const
{
	if (PinLabel == PCGExGetPathData::OutputPathsLabel) { return bOutputPaths; }
	if (PinLabel == PCGExGetPathData::OutputSplinesLabel) { return bOutputSplines; }
	return Super::IsPinStaticallyActive(PinLabel);
}

bool UPCGExGetPathDataSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin && InPin->IsOutputPin())
	{
		if (InPin->Properties.Label == PCGExGetPathData::OutputPathsLabel) { return bOutputPaths; }
		if (InPin->Properties.Label == PCGExGetPathData::OutputSplinesLabel) { return bOutputSplines; }
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

FPCGElementPtr UPCGExGetPathDataSettings::CreateElement() const
{
	return MakeShared<FPCGExGetPathDataElement>();
}

#pragma endregion

#pragma region FPCGExGetPathDataElement

void FPCGExGetPathDataElement::ProcessActors(FPCGContext* InContext, const UPCGDataFromActorSettings* InSettings, const TArray<AActor*>& FoundActors) const
{
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);
	const UPCGExGetPathDataSettings* Settings = Cast<UPCGExGetPathDataSettings>(InSettings);
	check(Settings);

	// Tell the executor which output pins produced nothing (bit j == output pin index j: 0=Paths, 1=Splines).
	uint64 InactiveMask = 0;
	if (!Settings->bOutputPaths) { InactiveMask |= 1ull << 0; }
	if (!Settings->bOutputSplines) { InactiveMask |= 1ull << 1; }
	Context->OutputData.InactiveOutputPinBitmask = InactiveMask;

	if (!Settings->bOutputPaths && !Settings->bOutputSplines) { return; }

	// Phase 1 (single-threaded): create every UObject + metadata structure and emit outputs.
	// NewObject_AnyThread and metadata mutation are not safe to call from multiple threads at once.
	TArray<PCGExGetPathData::FPathWork> PathWork;
	for (AActor* Actor : FoundActors)
	{
		PCGExGetPathData::PrepareActor(Context, Settings, Actor, PathWork);
	}

	// Phase 2 (parallel): fill per-point values into the pre-allocated path buffers. Each work item owns
	// its output exclusively, so writes never collide. Threshold 1: each item is a full spline conversion
	// (heavy), so parallelize at any count -- the default 512 cheap-iteration threshold would keep typical
	// selections (far fewer than 512 splines) sequential.
	PCGExMT::ParallelOrSequential(
		PathWork.Num(),
		[&](const int32 i) { PCGExGetPathData::FillPath(PathWork[i], Settings); },
		/*Threshold=*/2);
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
