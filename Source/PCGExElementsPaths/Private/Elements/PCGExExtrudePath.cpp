// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExExtrudePath.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPathProfile.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExExtrudePathElement"
#define PCGEX_NAMESPACE ExtrudePath

UPCGExExtrudePathSettings::UPCGExExtrudePathSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportClosedLoops = false;
}

TArray<FPCGPinProperties> UPCGExExtrudePathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (Type == EPCGExExtrudeProfileType::Custom)
	{
		PCGEX_PIN_POINT(PCGExExtrudePath::SourceCustomProfile, "Single path used as the extrusion profile", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ExtrudePath)

PCGExData::EIOInit UPCGExExtrudePathSettings::GetMainDataInitializationPolicy() const
{
	// Output is (re)initialized per-path in CompleteWork (New when extruding, Duplicate otherwise)
	return PCGExData::EIOInit::NoInit;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(ExtrudePath)

bool FPCGExExtrudePathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ExtrudePath)

	if (Settings->bFlagSubdivision)
	{
		PCGEX_VALIDATE_NAME(Settings->SubdivisionFlagName)
	}

	// Arc & Custom profiles curve the segment away from the path tangent, which needs the extrusion direction to
	// differ from the path. Path Direction is always collinear, so warn once here rather than per-path at runtime.
	const bool bWantsCurvedProfile = Settings->Type == EPCGExExtrudeProfileType::Custom || (Settings->Type == EPCGExExtrudeProfileType::Arc && Settings->bSubdivide);
	if (bWantsCurvedProfile && Settings->DirectionMode == EPCGExExtrudeDirection::PathDirection && !Settings->bQuietDegenerateProfileWarning)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Arc/Custom profiles need a Custom extrusion direction angled from the path; with Path Direction the new segment stays straight."));
	}

	if (Settings->Type == EPCGExExtrudeProfileType::Custom &&
		!PCGExPaths::Profile::TryBuildCustomProfile(Context, PCGExExtrudePath::SourceCustomProfile, Context->CustomProfileFacade, Context->CustomProfilePositions))
	{
		return false;
	}

	return true;
}

bool FPCGExExtrudePathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExExtrudePathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ExtrudePath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (PCGExPaths::Helpers::GetClosedLoop(Entry))
				{
					if (!Settings->bQuietClosedLoopWarning)
					{
						PCGE_LOG(Warning, GraphAndLog, FTEXT("Some inputs are closed loops and have no endpoints to extrude; they are forwarded as-is."));
					}
					PCGEX_INIT_IO(Entry, PCGExData::EIOInit::Forward)
					return false;
				}

				PCGEX_SKIP_INVALID_PATH_ENTRY
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = Settings->bFlagSubdivision;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to extrude."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

#pragma region FProcessor

namespace PCGExExtrudePath
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExExtrudePath::Process);

		// All reads (getters & filters) only ever touch the two endpoints; scoped buffers plus explicit
		// per-endpoint fetches below avoid loading attribute data for the whole path.
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		NumPoints = PointDataFacade->GetNum();
		LastPointIndex = NumPoints - 1;

		bProfileActive = Settings->Type == EPCGExExtrudeProfileType::Custom || Settings->bSubdivide;
		bSubdivideCount = Settings->SubdivideMethod != EPCGExSubdivideMode::Distance;

		LengthGetter = Settings->Length.GetValueSetting();
		if (!LengthGetter->Init(PointDataFacade))
		{
			return false;
		}

		if (Settings->DirectionMode == EPCGExExtrudeDirection::Custom)
		{
			DirectionGetter = Settings->Direction.GetValueSetting();
			if (!DirectionGetter->Init(PointDataFacade))
			{
				return false;
			}
		}

		if (bProfileActive && Settings->Type != EPCGExExtrudeProfileType::Custom)
		{
			if (Settings->SubdivideMethod == EPCGExSubdivideMode::Manhattan)
			{
				bIsManhattan = true;
				ManhattanDetails = Settings->ManhattanDetails;
				if (!ManhattanDetails.Init(Context, PointDataFacade))
				{
					return false;
				}
			}
			else
			{
				SubdivAmountGetter = Settings->SubdivisionAmount.GetValueSetting();
				if (!SubdivAmountGetter->Init(PointDataFacade))
				{
					return false;
				}
			}
		}

		// Getter reads and filters only need data at the two endpoints; one fetch per endpoint scope
		// readies every registered buffer there, and only those two points get filter-tested.
		const PCGExMT::FScope StartScope = PCGExMT::FScope(0, 1);
		const PCGExMT::FScope EndScope = PCGExMT::FScope(LastPointIndex, 1);

		PointDataFacade->Fetch(StartScope);
		FilterScope(StartScope);

		PointDataFacade->Fetch(EndScope);
		FilterScope(EndScope);

		const bool bAllowStart = Settings->Endpoint != EPCGExExtrudeEndpoint::End;
		const bool bAllowEnd = Settings->Endpoint != EPCGExExtrudeEndpoint::Start;

		bExtrudeStart = bAllowStart && PointFilterCache[0];
		bExtrudeEnd = bAllowEnd && PointFilterCache[LastPointIndex];

		const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		if (bExtrudeStart)
		{
			StartExtra = ComputeExtrusion(true, InTransforms, StartPositions);
			bExtrudeStart = StartExtra > 0;
		}

		if (bExtrudeEnd)
		{
			EndExtra = ComputeExtrusion(false, InTransforms, EndPositions);
			bExtrudeEnd = EndExtra > 0;
		}

		NumOutPoints = StartExtra + NumPoints + EndExtra;

		return true;
	}

	int32 FProcessor::ComputeExtrusion(const bool bIsStart, const TConstPCGValueRange<FTransform>& InTransforms, TArray<FVector>& OutPositions)
	{
		const int32 EndpointIdx = bIsStart ? 0 : LastPointIndex;

		const FVector EndpointPos = InTransforms[EndpointIdx].GetLocation();

		// Outward terminal-segment direction (inverted prev/next), i.e. what Shrink extends along.
		// Walks inward past coincident points (e.g. a duplicated terminal point) so a degenerate terminal
		// segment doesn't zero the tangent — which would kill Path Direction extrusions and flatten arcs.
		FVector T = FVector::ZeroVector;
		const int32 Step = bIsStart ? 1 : -1;
		for (int32 i = EndpointIdx + Step; i >= 0 && i < NumPoints; i += Step)
		{
			const FVector ToEndpoint = EndpointPos - InTransforms[i].GetLocation();
			if (ToEndpoint.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
			{
				T = ToEndpoint.GetSafeNormal();
				break;
			}
		}

		if (T.IsNearlyZero())
		{
			// The walk exhausted the path without finding a distinct point (e.g. a 2-point path with both
			// points collocated): no direction can be inferred, so the extrusion is meaningless.
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A path has all its points collocated; endpoint extrusion was skipped."));
			return 0;
		}

		const double Length = LengthGetter->Read(EndpointIdx);
		if (FMath::Abs(Length) <= UE_KINDA_SMALL_NUMBER)
		{
			return 0;
		}

		FVector Dir;
		if (Settings->DirectionMode == EPCGExExtrudeDirection::PathDirection)
		{
			Dir = T;
		}
		else
		{
			Dir = DirectionGetter->Read(EndpointIdx);
			if (Settings->Direction.bFlip)
			{
				Dir *= -1;
			}
			if (Settings->bTransformDirection)
			{
				Dir = InTransforms[EndpointIdx].GetRotation().RotateVector(Dir);
			}
			Dir = Dir.GetSafeNormal();
		}

		if (Dir.IsNearlyZero())
		{
			return 0;
		}

		const FVector Tip = EndpointPos + Dir * Length;

		// Subdivisions along the new segment [EndpointPos -> Tip], in EndpointPos->Tip order
		TArray<FVector> Subs;
		if (bProfileActive)
		{
			if (Settings->Type == EPCGExExtrudeProfileType::Custom)
			{
				// Same frame as the arc: plane spanned by the extrusion and the path tangent.
				// A custom profile can't be projected without a valid plane, so skip it (straight tip) when degenerate.
				const FVector PlaneNormal = FVector::CrossProduct(Tip - EndpointPos, T).GetSafeNormal() * -1;
				if (!PlaneNormal.IsNearlyZero())
				{
					// Tip = EndpointPos + Dir * Length with Dir normalized, so the span is |Length|
					const double ExtrudeLength = FMath::Abs(Length);

					const double MainAxisSize = PCGExPaths::Profile::ResolveAxisSize(Settings->MainAxisScaling, Settings->MainAxisScale, ExtrudeLength, ExtrudeLength);
					const double CrossAxisSize = PCGExPaths::Profile::ResolveAxisSize(Settings->CrossAxisScaling, Settings->CrossAxisScale, ExtrudeLength, ExtrudeLength);

					PCGExPaths::Profile::SubdivideCustom(Subs, Context->CustomProfilePositions, EndpointPos, Tip, PlaneNormal, MainAxisSize, CrossAxisSize);
				}
			}
			else if (bIsManhattan)
			{
				PCGExPaths::Profile::SubdivideManhattan(Subs, ManhattanDetails, EndpointIdx, EndpointPos, Tip, nullptr);
			}
			else
			{
				const double Amount = SubdivAmountGetter->Read(EndpointIdx);

				if (Settings->Type == EPCGExExtrudeProfileType::Arc)
				{
					// SubdivideArc degrades to a straight line when the arc is degenerate (extrusion collinear with
					// the path, e.g. Path Direction mode), so Arc keeps its subdivision points instead of dropping them.
					const PCGExMath::Geo::FExCenterArc Arc = PCGExMath::Geo::FExCenterArc::MakeTangent(EndpointPos, T, Tip);
					PCGExPaths::Profile::SubdivideArc(Subs, Arc, EndpointPos, Tip, Amount, bSubdivideCount);
				}
				else
				{
					PCGExPaths::Profile::SubdivideLine(Subs, EndpointPos, Tip, Amount, bSubdivideCount);
				}
			}
		}

		OutPositions.Reset(Subs.Num() + 1);
		if (bIsStart)
		{
			// Output runs tip -> ... -> kept endpoint, so the tip leads and subdivisions are reversed
			OutPositions.Add(Tip);
			for (int32 i = Subs.Num() - 1; i >= 0; i--)
			{
				OutPositions.Add(Subs[i]);
			}
		}
		else
		{
			// Output runs kept endpoint -> ... -> tip
			OutPositions.Append(Subs);
			OutPositions.Add(Tip);
		}

		return OutPositions.Num();
	}

	void FProcessor::CompleteWork()
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		if (!bExtrudeStart && !bExtrudeEnd)
		{
			// Nothing to extrude — forward the path unchanged
			PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::Duplicate)
			if (Settings->bFlagSubdivision)
			{
				WriteMark(PointIO, Settings->SubdivisionFlagName, false);
			}
			return;
		}

		PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::New)

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		UPCGMetadata* Metadata = OutPointData->Metadata;

		// Transform & Seed are written for every output point in ProcessRange, ensure they're allocated even when absent on the input
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPointData, NumOutPoints, PointDataFacade->GetAllocations() | EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed);

		// Metadata entries must be initialized single-threaded (shared metadata), before the parallel range fills transforms
		TConstPCGValueRange<int64> InMetadataEntry = InPointData->GetConstMetadataEntryValueRange();
		TPCGValueRange<int64> OutMetadataEntry = OutPointData->GetMetadataEntryValueRange();

		for (int32 Out = 0; Out < NumOutPoints; Out++)
		{
			const int32 Src = SourceIndex(Out);
			OutMetadataEntry[Out] = InMetadataEntry[Src];
			Metadata->InitializeOnSet(OutMetadataEntry[Out]);
		}

		if (Settings->bFlagSubdivision)
		{
			// The buffer defaults to false; only the new points that aren't tips need a write, and there are
			// few enough of them that doing it here beats re-testing the toggle for every point of the parallel loop.
			// StartPositions runs tip -> subdivisions, EndPositions runs subdivisions -> tip.
			const TSharedPtr<PCGExData::TBuffer<bool>> SubdivisionWriter = PointDataFacade->GetWritable<bool>(Settings->SubdivisionFlagName, false, true, PCGExData::EBufferInit::New);

			for (int32 i = 1; i < StartExtra; i++)
			{
				SubdivisionWriter->SetValue(i, true);
			}

			const int32 OriginalsEnd = StartExtra + NumPoints;
			for (int32 i = 0; i < EndExtra - 1; i++)
			{
				SubdivisionWriter->SetValue(OriginalsEnd + i, true);
			}
		}

		StartParallelLoopForRange(NumOutPoints);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		const TConstPCGValueRange<FTransform> InTransform = InPointData->GetConstTransformValueRange();
		const TConstPCGValueRange<int32> InSeed = InPointData->GetConstSeedValueRange();

		TPCGValueRange<FTransform> OutTransform = OutPointData->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeed = OutPointData->GetSeedValueRange(false);

		TArray<int32>& IdxMapping = PointDataFacade->Source->GetIdxMapping();

		const int32 OriginalsEnd = StartExtra + NumPoints;

		PCGEX_SCOPE_LOOP(Out)
		{
			const int32 Src = SourceIndex(Out);
			IdxMapping[Out] = Src;

			// Inherit the source transform (rotation/scale); location is overwritten for new points below
			OutTransform[Out] = InTransform[Src];

			if (Out < StartExtra)
			{
				const FVector Pos = StartPositions[Out];
				OutTransform[Out].SetLocation(Pos);
				OutSeed[Out] = PCGExRandomHelpers::ComputeSpatialSeed(Pos);
			}
			else if (Out < OriginalsEnd)
			{
				// Original point, kept verbatim
				OutSeed[Out] = InSeed[Src];
			}
			else
			{
				const FVector Pos = EndPositions[Out - OriginalsEnd];
				OutTransform[Out].SetLocation(Pos);
				OutSeed[Out] = PCGExRandomHelpers::ComputeSpatialSeed(Pos);
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		// Transform, Seed and MetadataEntry are written explicitly; carry the remaining native properties from source
		constexpr EPCGPointNativeProperties CarryOverProperties = static_cast<EPCGPointNativeProperties>(static_cast<uint8>(EPCGPointNativeProperties::All) & ~static_cast<uint8>(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry));

		PointDataFacade->Source->ConsumeIdxMapping(CarryOverProperties);
	}

	void FProcessor::Write()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
