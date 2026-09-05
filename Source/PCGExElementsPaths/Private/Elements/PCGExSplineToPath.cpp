// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSplineToPath.h"


#include "Curve/CurveUtil.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSplineData.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExProjectionDetails.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSplineToPathElement"
#define PCGEX_NAMESPACE SplineToPath

PCGEX_INITIALIZE_ELEMENT(SplineToPath)


namespace PCGExSplineToPath
{
	class FWriteTask final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FWriteTask(const int32 InTaskIndex, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
			: FPCGExIndexedTask(InTaskIndex)
			  , PointDataFacade(InPointDataFacade)

		{
		}

		TSharedPtr<PCGExData::FFacade> PointDataFacade;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			FPCGExSplineToPathContext* Context = TaskManager->GetContext<FPCGExSplineToPathContext>();
			PCGEX_SETTINGS_C(Context, SplineToPath)

			const UPCGSplineData* SplineData = Context->Targets[TaskIndex];
			check(SplineData)
			const FPCGSplineStruct& Spline = Context->Splines[TaskIndex];
			const FInterpCurveVector& SplinePositions = Spline.GetSplinePointsPosition();

			const int32 NumSegments = Spline.GetNumberOfSplineSegments();
			const double TotalLength = Spline.GetSplineLength();
			const int32 NumPoints = Spline.bClosedLoop ? NumSegments : NumSegments + 1;

			UPCGBasePointData* MutablePoints = PointDataFacade->Source->GetOut();
			const int32 LastIndex = Spline.bClosedLoop ? NumSegments - 1 : NumSegments;
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumPoints, EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);
			(void)PointDataFacade->Source->GetOutKeys(true);
			
			// Detect winding and determine if we need to reverse the output order
			bool bReverse = false;
			if (Spline.bClosedLoop && Settings->Winding != EPCGExWindingMutation::Unchanged)
			{
				const PCGExMath::FBestFitPlane FitPlane(
					NumSegments,
					[&Spline](const int32 Idx) -> FVector
					{
						const double Dist = Spline.GetDistanceAlongSplineAtSplinePoint(Idx);
						return Spline.GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
					});

				FPCGExGeo2DProjectionDetails WindingProjection;
				WindingProjection.Init(FitPlane);

				TArray<FVector2D> Projected;
				Projected.SetNum(NumSegments);
				for (int32 Idx = 0; Idx < NumSegments; Idx++)
				{
					const double Dist = Spline.GetDistanceAlongSplineAtSplinePoint(Idx);
					Projected[Idx] = FVector2D(WindingProjection.Project(Spline.GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World)));
				}

				const bool bIsClockwise = UE::Geometry::CurveUtil::SignedArea2<double, FVector2D>(Projected) < 0;
				bReverse = !PCGExMath::IsWinded(Settings->Winding, bIsClockwise);
			}

			PCGEX_FOREACH_FIELD_SPLINETOPATH(PCGEX_OUTPUT_DECL)

			{
				const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade.ToSharedRef();
				PCGEX_FOREACH_FIELD_SPLINETOPATH(PCGEX_OUTPUT_INIT)
			}

			TPCGValueRange<FTransform> OutTransforms = MutablePoints->GetTransformValueRange(false);
			TPCGValueRange<int32> OutSeeds = MutablePoints->GetSeedValueRange(false);

			auto ApplyTransform = [&](const int32 Index, const FTransform& Transform)
			{
				Settings->TransformDetails.ApplyTo(OutTransforms[Index], Transform);
				OutSeeds[Index] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[Index].GetLocation());
			};

			const FTransform SplineTransform = Spline.GetTransform();

			for (int i = 0; i < NumSegments; i++)
			{
				const int32 ReadIndex = bReverse ? (NumSegments - 1 - i) : i;
				const double LengthAtPoint = Spline.GetDistanceAlongSplineAtSplinePoint(ReadIndex);

				ApplyTransform(i, Spline.GetTransformAtDistanceAlongSpline(LengthAtPoint, ESplineCoordinateSpace::Type::World, true));

				PCGEX_OUTPUT_VALUE(LengthAtPoint, i, LengthAtPoint);
				PCGEX_OUTPUT_VALUE(Alpha, i, LengthAtPoint / TotalLength);

				if (bReverse)
				{
					PCGEX_OUTPUT_VALUE(ArriveTangent, i, -SplineTransform.TransformVector(SplinePositions.Points[ReadIndex].LeaveTangent));
					PCGEX_OUTPUT_VALUE(LeaveTangent, i, -SplineTransform.TransformVector(SplinePositions.Points[ReadIndex].ArriveTangent));
				}
				else
				{
					PCGEX_OUTPUT_VALUE(ArriveTangent, i, SplineTransform.TransformVector(SplinePositions.Points[ReadIndex].ArriveTangent));
					PCGEX_OUTPUT_VALUE(LeaveTangent, i, SplineTransform.TransformVector(SplinePositions.Points[ReadIndex].LeaveTangent));
				}

				PCGEX_OUTPUT_VALUE(PointType, i, PCGExPaths::Helpers::SplinePointTypeToInt(SplinePositions.Points[ReadIndex].InterpMode));
			}

			PCGExPaths::Helpers::SetClosedLoop(PointDataFacade->GetOut(), Spline.bClosedLoop);

			if (!Spline.bClosedLoop)
			{
				ApplyTransform(NumPoints - 1, Spline.GetTransformAtDistanceAlongSpline(TotalLength, ESplineCoordinateSpace::Type::World, true));

				PCGEX_OUTPUT_VALUE(LengthAtPoint, LastIndex, TotalLength);
				PCGEX_OUTPUT_VALUE(Alpha, LastIndex, 1);

				if (bReverse)
				{
					PCGEX_OUTPUT_VALUE(ArriveTangent, LastIndex, -SplineTransform.TransformVector(SplinePositions.Points[NumSegments].LeaveTangent));
					PCGEX_OUTPUT_VALUE(LeaveTangent, LastIndex, -SplineTransform.TransformVector(SplinePositions.Points[NumSegments].ArriveTangent));
				}
				else
				{
					PCGEX_OUTPUT_VALUE(ArriveTangent, LastIndex, SplineTransform.TransformVector(SplinePositions.Points[NumSegments].ArriveTangent));
					PCGEX_OUTPUT_VALUE(LeaveTangent, LastIndex, SplineTransform.TransformVector(SplinePositions.Points[NumSegments].LeaveTangent));
				}

				PCGEX_OUTPUT_VALUE(PointType, LastIndex, PCGExPaths::Helpers::SplinePointTypeToInt(SplinePositions.Points[NumSegments].InterpMode));
			}

			// Copy attributes

			TArray<PCGExData::FAttributeIdentity> SourceAttributes;
			PCGExData::FAttributeIdentity::Get(SplineData->Metadata, SourceAttributes, nullptr);
			Context->CarryOverDetails.Prune(SourceAttributes);

			if (!SourceAttributes.IsEmpty())
			{
				TPCGValueRange<int64> OutMeta = MutablePoints->GetMetadataEntryValueRange();

				const TSharedPtr<FPCGAttributeAccessorKeysEntries> Keys = MakeShared<FPCGAttributeAccessorKeysEntries>(SplineData->Metadata);

				for (PCGExData::FAttributeIdentity Identity : SourceAttributes)
				{
					PCGExMetaHelpers::ExecuteWithRightType(
						Identity,
						[&](auto DummyValue)
						{
							using T = decltype(DummyValue);
							const FPCGMetadataAttribute<T>* SourceAttr = SplineData->Metadata->GetConstTypedAttribute<T>(Identity.GetIdentifier());

							if (!SourceAttr)
							{
								return;
							}

							TSharedPtr<PCGExData::TBuffer<T>> OutBuffer = PointDataFacade->GetWritable<T>(SourceAttr, PCGExData::EBufferInit::New);

							if (Identity.InDataDomain())
							{
								OutBuffer->SetValue(0, PCGExData::Helpers::ReadDataValue<T>(SourceAttr));
								return;
							}

							TSharedPtr<PCGExData::TArrayBuffer<T>> OutArrayBuffer = StaticCastSharedPtr<PCGExData::TArrayBuffer<T>>(OutBuffer);
							TArrayView<T> InRange = MakeArrayView(OutArrayBuffer->GetOutValues()->GetData(), OutArrayBuffer->GetOutValues()->Num());

							if (Keys)
							{
								TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(SourceAttr, SourceAttr->GetMetadataDomain());
								InAccessor->GetRange(InRange, 0, *Keys.Get());

								// Reverse carried-over attributes to match the reversed point order
								if (bReverse && !Identity.InDataDomain())
								{
									Algo::Reverse(InRange);
								}
							}
							else
							{
							}
						},
						[&]()
						{
							// Property-backed spline-to-path carry. Data domain is straightforward (single value);
							// the elements-domain path needs per-entry keys from the spline metadata, which
							// FPCGAttributeAccessorKeysEntries doesn't expose directly. Iterate via the
							// engine accessor's GetMetadataEntryKeys interface for the elements case.
							const FPCGMetadataAttributeBase* SourceAttr = Identity.Attribute;
							if (!SourceAttr)
							{
								return;
							}

							TSharedPtr<PCGExData::IBuffer> OutBuffer = PointDataFacade->GetWritable(Identity.GetType(), SourceAttr, PCGExData::EBufferInit::New);
							if (!OutBuffer)
							{
								return;
							}

							if (Identity.InDataDomain())
							{
								PCGExData::Helpers::PropertyBroadcastAttribute(SourceAttr, PCGDefaultValueKey, OutBuffer);
								return;
							}

							if (!Keys || !OutBuffer->IsPropertyBacked())
							{
								return;
							}
							const TSharedPtr<PCGExData::FPropertyArrayBuffer> ArrayBuffer = StaticCastSharedPtr<PCGExData::FPropertyArrayBuffer>(OutBuffer);

							// Pull per-entry keys via the IPCGAttributeAccessorKeys public templated interface
							// (GetMetadataEntryKeys is protected on FPCGAttributeAccessorKeysEntries).
							const int32 Num = OutBuffer->GetNumValues(PCGExData::EIOSide::Out);
							const int32 NumKeys = Keys->GetNum();
							TArray<PCGMetadataEntryKey> EntryKeyBuf;
							EntryKeyBuf.SetNumZeroed(NumKeys);
							TArray<PCGMetadataEntryKey*> KeyPtrs;
							KeyPtrs.Reserve(NumKeys);

							for (int32 k = 0; k < NumKeys; k++)
							{
								KeyPtrs.Add(&EntryKeyBuf[k]);
							}
							if (!Keys->GetKeys<PCGMetadataEntryKey>(0, MakeArrayView(KeyPtrs)))
							{
								return;
							}

							for (int32 i = 0; i < Num; i++)
							{
								const int32 ReadIdx = bReverse ? (NumKeys - 1 - i) : i;
								if (!EntryKeyBuf.IsValidIndex(ReadIdx))
								{
									continue;
								}
								if (const void* SrcAddr = SourceAttr->GetReadAddressFromEntryKey_Unsafe(EntryKeyBuf[ReadIdx]))
								{
									ArrayBuffer->SetFromVoidProperty(i, SrcAddr);
								}
							}
						});
				}
			}

			PCGEx::TagsToData(PointDataFacade->Source, Settings->TagsToData);

			PointDataFacade->WriteFastest(TaskManager);
		}
	};
}

TArray<FPCGPinProperties> UPCGExSplineToPathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = TArray<FPCGPinProperties>();
	PCGEX_PIN_POLYLINES(PCGExSplineToPath::SourceSplineLabel, "The splines to convert to paths.", Required)
	return PinProperties;
}

bool FPCGExSplineToPathElement::Boot(FPCGExContext* InContext) const
{
	// Do not boot normally, as we care only about spline inputs

	PCGEX_CONTEXT_AND_SETTINGS(SplineToPath)

	if (Context->InputData.GetAllInputs().IsEmpty())
	{
		return false;
	} //Get rid of errors and warning when there is no input

	TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGExSplineToPath::SourceSplineLabel);
	PCGEX_FWD(TagForwarding)
	Context->TagForwarding.Init();

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	Context->MainPoints = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->MainPoints->OutputPin = Settings->GetMainOutputPin();

	// Seems there is a bug with Static Analysis where lambda's result in false positive warnings with dereferenced pointers. The Context ptr was being referenced directly
	// To bypass this, we dereference the pointer ourselves passing in the reference instead
	auto AddTags = [&ContextRef = *Context](const TSet<FString>& SourceTags)
	{
		TArray<FString> Tags = SourceTags.Array();
		ContextRef.TagForwarding.Prune(Tags);
		ContextRef.Tags.Add(Tags);
	};

	if (!Targets.IsEmpty())
	{
		for (const FPCGTaggedData& TaggedData : Targets)
		{
			const UPCGSplineData* SplineData = Cast<UPCGSplineData>(TaggedData.Data);
			if (!SplineData || SplineData->SplineStruct.GetNumberOfSplineSegments() <= 0)
			{
				continue;
			}

			switch (Settings->SampleInputs)
			{
			default: case EPCGExSplineSamplingIncludeMode::All:
				Context->Targets.Add(SplineData);
				AddTags(TaggedData.Tags);
				break;
			case EPCGExSplineSamplingIncludeMode::ClosedLoopOnly:
				if (SplineData->SplineStruct.bClosedLoop)
				{
					Context->Targets.Add(SplineData);
					AddTags(TaggedData.Tags);
				}
				break;
			case EPCGExSplineSamplingIncludeMode::OpenSplineOnly:
				if (!SplineData->SplineStruct.bClosedLoop)
				{
					Context->Targets.Add(SplineData);
					AddTags(TaggedData.Tags);
				}
				break;
			}
		}

		Context->NumTargets = Context->Targets.Num();
	}

	if (Context->NumTargets <= 0)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No targets (no input matches criteria or empty dataset)"));
		return false;
	}

	Context->Splines.Reserve(Context->NumTargets);
	for (const UPCGSplineData* SplineData : Context->Targets)
	{
		Context->Splines.Add(SplineData->SplineStruct);
	}

	PCGEX_FOREACH_FIELD_SPLINETOPATH(PCGEX_OUTPUT_VALIDATE_NAME)


	return true;
}

bool FPCGExSplineToPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSplineToPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SplineToPath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		const TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		for (int i = 0; i < Context->NumTargets; i++)
		{
			TSharedPtr<PCGExData::FPointIO> NewOutput = Context->MainPoints->Emplace_GetRef(PCGExData::EIOInit::New);

			if (!NewOutput)
			{
				return true;
			}

			PCGEX_MAKE_SHARED(PointDataFacade, PCGExData::FFacade, NewOutput.ToSharedRef())

			// Tags must be fully appended before the task is published -- FWriteTask
			// reads them off-thread (TagsToData), and FTags iteration is lock-free.
			NewOutput->Tags->Append(Context->Tags[i]);

			PCGEX_LAUNCH(PCGExSplineToPath::FWriteTask, i, PointDataFacade)
		}

		Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		PCGEX_OUTPUT_VALID_PATHS(MainPoints)
		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
