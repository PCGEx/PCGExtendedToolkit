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
#include "Utils/PCGLogErrors.h"                  // LogWarningOnGraph

#include "PCGExCoreSettingsCache.h"              // PCGEX_CORE_SETTINGS
#include "Core/PCGExMT.h"                        // SubLoopScopes
#include "Core/PCGExMTCommon.h"                  // ParallelOrSequential
#include "Data/PCGExDataHelpers.h"               // SetDataValue
#include "Data/PCGExDataTags.h"                  // FTags
#include "Details/PCGExFilterDetails.h"          // PCGEx::TagsToData
#include "Helpers/PCGExMetaHelpers.h"            // InitializeMetadataEntries
#include "Helpers/PCGExPointArrayDataHelpers.h"  // SetNumPointsAllocated
#include "Helpers/PCGExRandomHelpers.h"          // ComputeSpatialSeed
#include "Helpers/PCGExSplineSampleHandler.h"    // FHandlerRegistry, FSource
#include "Math/PCGExMath.h"                      // TruncateDbl
#include "Paths/PCGExChordBoxes.h"               // ChordBoxes::Miter
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
		const FTransform LineTransform = Poly->GetTransform();

		// Queried once each: UPCGLandscapeSplineData::GetDistanceAtSegmentStart walks its reparam table per call.
		// GetLength() sums GetSegmentLength(i) = Start(i + 1) - Start(i) in order, so this total is bit-identical.
		TArray<double> SegmentStarts;
		SegmentStarts.SetNumUninitialized(NumSegments + 1);
		for (int32 i = 0; i <= NumSegments; i++) { SegmentStarts[i] = Poly->GetDistanceAtSegmentStart(i); }

		double TotalLength = 0.0;
		for (int32 i = 0; i < NumSegments; i++) { TotalLength += SegmentStarts[i + 1] - SegmentStarts[i]; }

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
			WritePoint(i, i, Poly->GetTransformAtDistance(i, 0.0, /*bWorldSpace=*/true), SegmentStarts[i]);
		}

		if (!bClosedLoop)
		{
			// Last vertex of an open line is the end of the final segment.
			WritePoint(NumSegments, NumSegments, Poly->GetTransformAtDistance(NumSegments - 1, SegmentStarts[NumSegments] - SegmentStarts[NumSegments - 1], true), TotalLength);
		}
	}

	// One per component with sample sources. Emitted once they are prepared: length decides filtering and counts.
	struct FSampledComponent
	{
		UActorComponent* Component = nullptr; // Source of the Splines pin data
		FSoftObjectPath ActorPath;
		TSharedPtr<PCGExData::FTags> Tags;
		int32 FirstSource = 0;
		int32 NumSources = 0;
	};

	// One sampled path. Sized and filled by workers, in disjoint ranges.
	struct FSampleWork
	{
		TSharedPtr<const PCGExSplineSampling::FSource> Source;
		UPCGBasePointData* PathData = nullptr;
		FPCGMetadataAttribute<double>* LengthAttr = nullptr;
		FPCGMetadataAttribute<double>* AlphaAttr = nullptr;
		int32 NumSamples = 0;
		double Step = 0; // Source->Length / NumSamples
	};

	// A run of one path's samples: the unit of fill parallelism.
	struct FSampleBlock
	{
		int32 WorkIndex = 0;
		PCGExMT::FScope Scope;
	};

	constexpr int32 StationBatchSize = 64;
	constexpr int32 MaxSamplesPerPath = 1 << 20;

	// Overrides bypass ClampMin, hence the clamps; non-finite values fall back to defaults.
	struct FSampleConfig
	{
		PCGExSplineSampling::FParams Params;
		double SampleLength = 100;
		EPCGExTruncateMode Truncate = EPCGExTruncateMode::Round;
		double Anchor = 0;
		bool bMiterJoints = false;
		double MiterLimit = 1;
		bool bWriteLength = false;
		bool bWriteAlpha = false;
		EPCGPointNativeProperties Allocations = EPCGPointNativeProperties::None;

		explicit FSampleConfig(const UPCGExGetPathDataSettings* Settings)
		{
			auto Finite = [](const double Value, const double Fallback) { return FMath::IsFinite(Value) ? Value : Fallback; };
			const FPCGExSampleInPlaceDetails& Details = Settings->SampleInPlaceDetails;

			Params.HalfWidth = FMath::Max(0.0, Finite(Details.HalfWidth, 0));
			Params.Above = FMath::Max(0.0, Finite(Details.Above, 0));
			Params.Below = FMath::Max(0.0, Finite(Details.Below, 0));
			Params.bIncludeFalloff = Details.bIncludeFalloff;

			SampleLength = FMath::Max(1.0, Finite(Details.SampleLength, 100));
			Truncate = Details.Truncate == EPCGExTruncateMode::None ? EPCGExTruncateMode::Round : Details.Truncate;
			Anchor = FMath::Clamp(Finite(Details.Anchor, 0), 0.0, 1.0);
			bMiterJoints = Details.bMiterJoints;
			MiterLimit = FMath::Clamp(Finite(Details.MiterLimit, 1), 1.0, 100.0);

			// Tangents and point type describe control points; sampled paths only carry length and alpha.
			bWriteLength = Settings->bWriteLengthAtPoint;
			bWriteAlpha = Settings->bWriteAlpha;

			// MetadataEntry is added per path, only when it has attributes.
			Allocations = EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax;
		}
	};

	using FHandlerCache = TMap<const UClass*, const UPCGExSplineSampleHandler*>;

	bool IsIncluded(const UPCGExGetPathDataSettings* Settings, const bool bClosedLoop)
	{
		if (Settings->SampleInputs == EPCGExSplineSamplingIncludeMode::ClosedLoopOnly) { return bClosedLoop; }
		if (Settings->SampleInputs == EPCGExSplineSamplingIncludeMode::OpenSplineOnly) { return !bClosedLoop; }
		return true;
	}

	TSharedPtr<PCGExData::FTags> GatherTags(const UPCGExGetPathDataSettings* Settings, const AActor* Actor, const UActorComponent* Component)
	{
		TSet<FString> GatheredTags;
		if (Settings->bForwardSourceTags)
		{
			for (const FName& Tag : Actor->Tags) { GatheredTags.Add(Tag.ToString()); }
			for (const FName& Tag : Component->ComponentTags) { GatheredTags.Add(Tag.ToString()); }
		}
		return MakeShared<PCGExData::FTags>(GatheredTags);
	}

	// TagsToData before the actor reference, so a tag named "ActorReference" can't clobber the node's stamp.
	// It can also replace a same-named attribute of another type: cache attribute pointers only after this.
	void StampAndEmit(FPCGDataFromActorContext* Context, const UPCGExGetPathDataSettings* Settings, UPCGData* Data, const FName Pin, const TSharedPtr<PCGExData::FTags>& Tags, const FSoftObjectPath& ActorPath)
	{
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
	}

	// Bakes the component's world transform. OutSplineData is only set for regular splines (CIM point types).
	UPCGPolyLineData* MakePolyLineData(FPCGDataFromActorContext* Context, UActorComponent* Component, const UPCGSplineData*& OutSplineData)
	{
		OutSplineData = nullptr;

		if (USplineComponent* SplineComp = Cast<USplineComponent>(Component))
		{
			UPCGSplineData* TypedData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
			TypedData->Initialize(SplineComp);
			PCGExPaths::Helpers::SetClosedLoop(TypedData, TypedData->IsClosed());
			OutSplineData = TypedData;
			return TypedData;
		}

		if (ULandscapeSplinesComponent* LandscapeComp = Cast<ULandscapeSplinesComponent>(Component))
		{
			UPCGLandscapeSplineData* TypedData = FPCGContext::NewObject_AnyThread<UPCGLandscapeSplineData>(Context);
			TypedData->Initialize(LandscapeComp);
			PCGExPaths::Helpers::SetClosedLoop(TypedData, TypedData->IsClosed());
			return TypedData;
		}

		return nullptr;
	}

	// Phase 1 -- runs on the game thread. Creates every UObject + metadata structure for one actor's
	// spline components, emits outputs, and queues path-fill work items. No per-point values yet.
	// Sample In Place only snapshots its sources here; they are emitted once prepared (EmitSampledPaths).
	void PrepareActor(
		FPCGDataFromActorContext* Context, const UPCGExGetPathDataSettings* Settings, AActor* Actor, const PCGExSplineSampling::FParams& SampleParams, FHandlerCache& HandlerCache,
		TArray<FPathWork>& OutPathWork, TArray<FSampledComponent>& OutSampledComponents, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources)
	{
		if (!IsValid(Actor)) { return; }

		const FSoftObjectPath ActorPath(Actor);
		const bool bSampledPaths = Settings->bOutputPaths && Settings->bSampleInPlace;

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component) { continue; }
			if (!Context->ComponentSelector.FilterComponent(Component)) { continue; }
			// Mirror the engine getter: skip components PCG spawned (tagged DefaultPCGTag).
			if (Settings->bIgnorePCGGeneratedComponents && Component->ComponentTags.Contains(PCGHelpers::DefaultPCGTag)) { continue; }

			if (bSampledPaths)
			{
				// Resolved through the handler registry, once per class: most components aren't splines.
				const UClass* ComponentClass = Component->GetClass();
				const UPCGExSplineSampleHandler* Handler = nullptr;
				if (const UPCGExSplineSampleHandler* const* Cached = HandlerCache.Find(ComponentClass)) { Handler = *Cached; }
				else { Handler = HandlerCache.Add(ComponentClass, PCGExSplineSampling::FHandlerRegistry::Get().Find(ComponentClass)); }

				if (!Handler) { continue; }

				const int32 FirstSource = OutSources.Num();
				Handler->CreateSources(Component, SampleParams, OutSources);

				// Open/closed is known at snapshot time: excluded sources are dropped before Prepare.
				int32 NumKept = FirstSource;
				for (int32 SourceIndex = FirstSource; SourceIndex < OutSources.Num(); SourceIndex++)
				{
					if (!IsIncluded(Settings, OutSources[SourceIndex]->bClosedLoop)) { continue; }
					if (NumKept != SourceIndex) { OutSources[NumKept] = OutSources[SourceIndex]; }
					NumKept++;
				}
				if (NumKept < OutSources.Num()) { OutSources.RemoveAt(NumKept, OutSources.Num() - NumKept, EAllowShrinking::No); }
				if (NumKept == FirstSource) { continue; }

				FSampledComponent& Sampled = OutSampledComponents.Emplace_GetRef();
				Sampled.Component = Component;
				Sampled.ActorPath = ActorPath;
				Sampled.Tags = GatherTags(Settings, Actor, Component);
				Sampled.FirstSource = FirstSource;
				Sampled.NumSources = NumKept - FirstSource;

				continue;
			}

			const UPCGSplineData* SplineData = nullptr;
			UPCGPolyLineData* PolyData = MakePolyLineData(Context, Component, SplineData);
			if (!PolyData) { continue; }

			const int32 NumSegments = PolyData->GetNumSegments();
			if (NumSegments <= 0) { continue; }

			const bool bClosedLoop = PolyData->IsClosed();
			if (!IsIncluded(Settings, bClosedLoop)) { continue; }

			const TSharedPtr<PCGExData::FTags> Tags = GatherTags(Settings, Actor, Component);

			// Poly-line data is complete after Initialize; emit it directly when requested (no fill needed).
			if (Settings->bOutputSplines) { StampAndEmit(Context, Settings, PolyData, OutputSplinesLabel, Tags, ActorPath); }

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

			auto CreateAttributes = [&]()
			{
				if (Settings->bWriteArriveTangent) { Work.ArriveAttr = PathData->Metadata->FindOrCreateAttribute<FVector>(Settings->ArriveTangentAttributeName, FVector::ZeroVector, false, false, true); }
				if (Settings->bWriteLeaveTangent) { Work.LeaveAttr = PathData->Metadata->FindOrCreateAttribute<FVector>(Settings->LeaveTangentAttributeName, FVector::ZeroVector, false, false, true); }
				if (Settings->bWriteLengthAtPoint) { Work.LengthAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->LengthAtPointAttributeName, 0, false, false, true); }
				if (Settings->bWriteAlpha) { Work.AlphaAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->AlphaAttributeName, 0, false, false, true); }
				if (bWritePointType) { Work.PointTypeAttr = PathData->Metadata->FindOrCreateAttribute<int32>(Settings->PointTypeAttributeName, 0, false, false, true); }
			};

			if (bWriteAnyAttr)
			{
				// Each path point needs a real metadata entry before FillPath sets per-point values.
				PCGExMetaHelpers::InitializeMetadataEntries(PathData->Metadata, PathData->GetMetadataEntryValueRange(), false);
				CreateAttributes();
			}

			// Closed-loop marker + actor ref + tags are metadata structure -> set single-threaded now.
			// Emitting here is fine: Phase 2 fills PathData's value ranges in place (no realloc).
			PCGExPaths::Helpers::SetClosedLoop(PathData, bClosedLoop);
			StampAndEmit(Context, Settings, PathData, OutputPathsLabel, Tags, ActorPath);

			// Re-resolve once stamped: a forwarded tag may have replaced one of them (the node's own wins).
			if (bWriteAnyAttr) { CreateAttributes(); }

			OutPathWork.Add(Work);
		}
	}

	// At least 2 points, 3 chords for a closed loop. Clamped before truncating: TruncateDbl rounds through int32.
	int32 GetNumSamples(const PCGExSplineSampling::FSource& Source, const FSampleConfig& Config, bool& bOutCapped)
	{
		const double Ratio = Source.Length / Config.SampleLength;
		bOutCapped = Ratio > MaxSamplesPerPath;
		const int32 Fitted = static_cast<int32>(PCGExMath::TruncateDbl(FMath::Min(Ratio, static_cast<double>(MaxSamplesPerPath)), Config.Truncate));
		return FMath::Clamp(Fitted, Source.bClosedLoop ? 3 : 2, MaxSamplesPerPath);
	}

	// Game thread, once sources are prepared. Spline data is emitted only with one of its paths, so both pins match.
	void EmitSampledPaths(
		FPCGDataFromActorContext* Context, const UPCGExGetPathDataSettings* Settings, const FSampleConfig& Config,
		const TArray<FSampledComponent>& SampledComponents, const TArray<TSharedRef<PCGExSplineSampling::FSource>>& Sources,
		TArray<FSampleWork>& OutSampleWork, int32& OutNumCappedPaths)
	{
		for (const FSampledComponent& Sampled : SampledComponents)
		{
			bool bEmittedSplineData = !Settings->bOutputSplines;

			for (int32 SourceIndex = Sampled.FirstSource; SourceIndex < Sampled.FirstSource + Sampled.NumSources; SourceIndex++)
			{
				const TSharedRef<PCGExSplineSampling::FSource>& Source = Sources[SourceIndex];
				if (!FMath::IsFinite(Source->Length) || Source->Length <= UE_KINDA_SMALL_NUMBER) { continue; }

				if (!bEmittedSplineData)
				{
					// Built here, not in PrepareActor: a component whose every source is dropped costs no object.
					bEmittedSplineData = true;
					const UPCGSplineData* SplineData = nullptr;
					if (UPCGPolyLineData* PolyData = MakePolyLineData(Context, Sampled.Component, SplineData))
					{
						StampAndEmit(Context, Settings, PolyData, OutputSplinesLabel, Sampled.Tags, Sampled.ActorPath);
					}
				}

				bool bCapped = false;
				const int32 NumSamples = GetNumSamples(*Source, Config, bCapped);
				if (bCapped) { OutNumCappedPaths++; }

				UPCGBasePointData* PathData = FPCGContext::NewPointData_AnyThread(Context);

				FSampleWork& Work = OutSampleWork.Emplace_GetRef();
				Work.Source = Source;
				Work.PathData = PathData;
				Work.NumSamples = NumSamples;
				Work.Step = Source->Length / NumSamples;

				// Stamp before adding the node's own attributes, so they win any clash with a forwarded tag.
				StampAndEmit(Context, Settings, PathData, OutputPathsLabel, Sampled.Tags, Sampled.ActorPath);
				PCGExPaths::Helpers::SetClosedLoop(PathData, Source->bClosedLoop);

				if (Config.bWriteLength) { Work.LengthAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->LengthAtPointAttributeName, 0, false, false, true); }
				if (Config.bWriteAlpha) { Work.AlphaAttr = PathData->Metadata->FindOrCreateAttribute<double>(Settings->AlphaAttributeName, 0, false, false, true); }
			}
		}
	}

	// Worker thread, one path each. Length and Alpha only depend on the sample index, so they are written here.
	void AllocateSampledPath(const FSampleWork& Work, const FSampleConfig& Config)
	{
		UPCGBasePointData* PathData = Work.PathData;
		const bool bWriteAttributes = Work.LengthAttr || Work.AlphaAttr;

		// Initialized: an uninitialized allocation leaves garbage on legacy point data and follows downstream copies.
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			PathData, Work.NumSamples,
			bWriteAttributes ? Config.Allocations | EPCGPointNativeProperties::MetadataEntry : Config.Allocations);

		if (!bWriteAttributes) { return; }

		// Fresh entries for every sample (the keys start invalid, so they get no parent).
		PCGExMetaHelpers::InitializeMetadataEntries(PathData->Metadata, PathData->GetMetadataEntryValueRange(false), false);

		// Contiguous copy: FPCGMetadataAttribute::SetValues has no TConstPCGValueRange overload before 5.8.
		const TConstPCGValueRange<int64> EntryRange = PathData->GetConstMetadataEntryValueRange();
		TArray<PCGMetadataEntryKey> EntryKeys;
		EntryKeys.SetNumUninitialized(Work.NumSamples);
		for (int32 i = 0; i < Work.NumSamples; i++) { EntryKeys[i] = EntryRange[i]; }

		TArray<double> Values;
		Values.SetNumUninitialized(Work.NumSamples);

		if (Work.LengthAttr)
		{
			for (int32 i = 0; i < Work.NumSamples; i++) { Values[i] = (i + Config.Anchor) * Work.Step; }
			Work.LengthAttr->SetValues(TConstArrayView<PCGMetadataEntryKey>(EntryKeys), MakeConstArrayView(Values));
		}

		if (Work.AlphaAttr)
		{
			for (int32 i = 0; i < Work.NumSamples; i++) { Values[i] = (i + Config.Anchor) / Work.NumSamples; }
			Work.AlphaAttr->SetValues(TConstArrayView<PCGMetadataEntryKey>(EntryKeys), MakeConstArrayView(Values));
		}
	}

	// Sample k spans stations k -> k + 1; the last station is exactly Length (FillSampleBlock handles a loop's seam).
	double GetStationDistance(const FSampleWork& Work, const int32 StationIndex)
	{
		return StationIndex >= Work.NumSamples ? Work.Source->Length : StationIndex * Work.Step;
	}

	// Worker thread. Sample i is the chord box from station i to i + 1, with up and extents averaged over both.
	void FillSampleBlock(const FSampleWork& Work, const PCGExMT::FScope& Scope, const double Anchor)
	{
		using namespace PCGExSplineSampling;

		UPCGBasePointData* OutData = Work.PathData;
		TPCGValueRange<FTransform> OutTransforms = OutData->GetTransformValueRange(false);
		TPCGValueRange<FVector> OutBoundsMin = OutData->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> OutBoundsMax = OutData->GetBoundsMaxValueRange(false);
		TPCGValueRange<int32> OutSeeds = OutData->GetSeedValueRange(false);

		auto WriteSample = [&](const int32 Index, const FStation& A, const FStation& B)
		{
			const FVector Chord = B.Position - A.Position;
			const double ChordLength = Chord.Size();

			FVector Up = A.Up + B.Up;
			if (!Up.Normalize()) { Up = A.Up; } // Ups flipped across the chord cancel out

			// A collapsed chord (the spline doubling back within one sample) only keeps its up axis.
			const FQuat Rotation = ChordLength > UE_KINDA_SMALL_NUMBER
				                       ? FRotationMatrix::MakeFromXZ(Chord / ChordLength, Up).ToQuat()
				                       : FRotationMatrix::MakeFromZ(Up).ToQuat();

			const FVector Location = A.Position + Chord * Anchor;
			OutTransforms[Index] = FTransform(Rotation, Location, FVector::OneVector);
			OutSeeds[Index] = PCGExRandomHelpers::ComputeSpatialSeed(Location);
			OutBoundsMin[Index] = FVector(-Anchor * ChordLength, -0.5 * (A.Left + B.Left), -0.5 * (A.Below + B.Below));
			OutBoundsMax[Index] = FVector((1.0 - Anchor) * ChordLength, 0.5 * (A.Right + B.Right), 0.5 * (A.Above + B.Above));
		};

		const FSource& Source = *Work.Source;

		// Batched; the last station carries over, so each is evaluated once.
		FStation Previous;
		const double FirstDistance = GetStationDistance(Work, Scope.Start);
		Source.Evaluate(MakeArrayView(&FirstDistance, 1), MakeArrayView(&Previous, 1));

		double Distances[StationBatchSize];
		FStation Stations[StationBatchSize];

		for (int32 Index = Scope.Start; Index < Scope.End;)
		{
			const int32 BatchCount = FMath::Min(StationBatchSize, Scope.End - Index);

			// A loop's last station is its first: evaluated on its own at 0, so one call's distances keep ascending.
			const bool bSeam = Source.bClosedLoop && Index + BatchCount == Work.NumSamples;
			const int32 NumAscending = bSeam ? BatchCount - 1 : BatchCount;

			for (int32 b = 0; b < NumAscending; b++) { Distances[b] = GetStationDistance(Work, Index + b + 1); }
			if (NumAscending > 0) { Source.Evaluate(MakeArrayView(Distances, NumAscending), MakeArrayView(Stations, NumAscending)); }

			if (bSeam)
			{
				const double SeamDistance = 0;
				Source.Evaluate(MakeArrayView(&SeamDistance, 1), MakeArrayView(&Stations[BatchCount - 1], 1));
			}

			for (int32 b = 0; b < BatchCount; b++, Index++) { WriteSample(Index, b == 0 ? Previous : Stations[b - 1], Stations[b]); }
			Previous = Stations[BatchCount - 1];
		}
	}
}

#pragma region UPCGExGetPathDataSettings

#if WITH_EDITOR
FText UPCGExGetPathDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Reads spline (and landscape spline) components directly off the selected actors and outputs each as a path and/or spline data, stamped with the source actor reference. Replaces the GetSplineData -> SplineToPath flow when the actor reference matters. Sample In Place turns each spline into evenly spaced samples whose bounds hold its footprint, read the same way for every spline type.");
}

bool UPCGExGetPathDataSettings::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty)) { return false; }

	// Gated here, by meta flag: an InlineEditConditionToggle can't carry a compound EditCondition.
	return !(bSampleInPlace && InProperty && InProperty->HasMetaData(TEXT("PCGExControlPointOnly")));
}
#endif

TArray<FPCGPinProperties> UPCGExGetPathDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGExGetPathData::OutputPathsLabel, EPCGDataType::Point, true, true, LOCTEXT("PathsPinTooltip", "One point path per spline component (per chain of a landscape spline network when sampling in place)."));
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

	const PCGExGetPathData::FSampleConfig Config(Settings);

	// Phase 1 (single-threaded): create every UObject + metadata structure and emit outputs.
	// NewObject_AnyThread and metadata mutation are not safe to call from multiple threads at once.
	TArray<PCGExGetPathData::FPathWork> PathWork;
	TArray<PCGExGetPathData::FSampledComponent> SampledComponents;
	TArray<TSharedRef<PCGExSplineSampling::FSource>> Sources;
	PCGExGetPathData::FHandlerCache HandlerCache;
	for (AActor* Actor : FoundActors)
	{
		PCGExGetPathData::PrepareActor(Context, Settings, Actor, Config.Params, HandlerCache, PathWork, SampledComponents, Sources);
	}

	// Phase 2 (parallel): fill per-point values into the pre-allocated path buffers. Each work item owns
	// its output exclusively, so writes never collide. Threshold 1: each item is a full spline conversion
	// (heavy), so parallelize at any count -- the default 512 cheap-iteration threshold would keep typical
	// selections (far fewer than 512 splines) sequential.
	PCGExMT::ParallelOrSequential(
		PathWork.Num(),
		[&](const int32 i) { PCGExGetPathData::FillPath(PathWork[i], Settings); },
		/*Threshold=*/2);

	if (Sources.IsEmpty()) { return; }

	// Sample In Place. Item costs vary widely (one long river next to many short paths), hence Unbalanced.

	PCGExMT::ParallelOrSequential(
		Sources.Num(),
		[&](const int32 i) { Sources[i]->Prepare(); },
		/*Threshold=*/2, EParallelForFlags::Unbalanced);

	TArray<PCGExGetPathData::FSampleWork> SampleWork;
	int32 NumCappedPaths = 0;
	PCGExGetPathData::EmitSampledPaths(Context, Settings, Config, SampledComponents, Sources, SampleWork, NumCappedPaths);

	if (NumCappedPaths > 0)
	{
		PCGLog::LogWarningOnGraph(
			FText::Format(
				LOCTEXT("SampleCountCapped", "{0} sampled path(s) would need more than {1} samples; they were capped, so their samples are longer than Sample Length."),
				NumCappedPaths, PCGExGetPathData::MaxSamplesPerPath),
			Context);
	}

	PCGExMT::ParallelOrSequential(
		SampleWork.Num(),
		[&](const int32 i) { PCGExGetPathData::AllocateSampledPath(SampleWork[i], Config); },
		/*Threshold=*/2, EParallelForFlags::Unbalanced);

	// One block per chunk-size scope, flattened across paths; a source without bConcurrentEval stays in one.
	TArray<PCGExGetPathData::FSampleBlock> SampleBlocks;
	{
		const int32 ChunkSize = PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize();
		TArray<PCGExMT::FScope> Scopes;
		for (int32 WorkIndex = 0; WorkIndex < SampleWork.Num(); WorkIndex++)
		{
			const PCGExGetPathData::FSampleWork& Work = SampleWork[WorkIndex];
			PCGExMT::SubLoopScopes(Scopes, Work.NumSamples, Work.Source->bConcurrentEval ? ChunkSize : Work.NumSamples);
			for (const PCGExMT::FScope& Scope : Scopes) { SampleBlocks.Add({WorkIndex, Scope}); }
		}
	}

	PCGExMT::ParallelOrSequential(
		SampleBlocks.Num(),
		[&](const int32 i)
		{
			const PCGExGetPathData::FSampleBlock& Block = SampleBlocks[i];
			PCGExGetPathData::FillSampleBlock(SampleWork[Block.WorkIndex], Block.Scope, Config.Anchor);
		},
		/*Threshold=*/2, EParallelForFlags::Unbalanced);

	if (Config.bMiterJoints)
	{
		// Once every box exists: a joint reads both of its boxes, which can sit in different blocks.
		PCGExMT::ParallelOrSequential(
			SampleBlocks.Num(),
			[&](const int32 i)
			{
				const PCGExGetPathData::FSampleBlock& Block = SampleBlocks[i];
				const PCGExGetPathData::FSampleWork& Work = SampleWork[Block.WorkIndex];
				PCGExPaths::ChordBoxes::Miter(Work.PathData, Work.NumSamples, Work.Source->bClosedLoop, Config.MiterLimit, Block.Scope);
			},
			/*Threshold=*/2, EParallelForFlags::Unbalanced);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
