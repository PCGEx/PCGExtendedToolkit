// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSplineToPath.h"


#include "Helpers/PCGExRandomHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSplineData.h"
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
			: FPCGExIndexedTask(InTaskIndex), PointDataFacade(InPointDataFacade)

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

			UPCGBasePointData* MutablePoints = PointDataFacade->Source->GetOut();
			const int32 LastIndex = Spline.bClosedLoop ? NumSegments - 1 : NumSegments;
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, Spline.bClosedLoop ? NumSegments : NumSegments + 1, EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed);

			PCGEX_FOREACH_FIELD_SPLINETOPATH(PCGEX_OUTPUT_DECL)

			{
				const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade.ToSharedRef();
				PCGEX_FOREACH_FIELD_SPLINETOPATH(PCGEX_OUTPUT_INIT)
			}

			TPCGValueRange<FTransform> OutTransforms = MutablePoints->GetTransformValueRange(false);
			TPCGValueRange<int32> OutSeeds = MutablePoints->GetSeedValueRange(false);

			auto ApplyTransform = [&](const int32 Index, const FTransform& Transform)
			{
				if (Settings->TransformDetails.bInheritRotation && Settings->TransformDetails.bInheritScale)
				{
					OutTransforms[Index] = Transform;
				}
				else if (Settings->TransformDetails.bInheritRotation)
				{
					OutTransforms[Index].SetLocation(Transform.GetLocation());
					OutTransforms[Index].SetRotation(Transform.GetRotation());
				}
				else if (Settings->TransformDetails.bInheritScale)
				{
					OutTransforms[Index].SetLocation(Transform.GetLocation());
					OutTransforms[Index].SetScale3D(Transform.GetScale3D());
				}
				else
				{
					OutTransforms[Index].SetLocation(Transform.GetLocation());
				}

				OutSeeds[Index] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[Index].GetLocation());
			};

			auto GetPointType = [](EInterpCurveMode Mode)-> int32
			{
				switch (Mode)
				{
				case CIM_Linear: return 0;
				case CIM_CurveAuto: return 1;
				case CIM_Constant: return 2;
				case CIM_CurveUser: return 4;
				case CIM_CurveAutoClamped: return 3;
				default: case CIM_Unknown:
				case CIM_CurveBreak: return -1;
				}
			};

			for (int i = 0; i < NumSegments; i++)
			{
				const double LengthAtPoint = Spline.GetDistanceAlongSplineAtSplinePoint(i);
				const FTransform SplineTransform = Spline.GetTransform();

				ApplyTransform(i, Spline.GetTransformAtDistanceAlongSpline(LengthAtPoint, ESplineCoordinateSpace::Type::World, true));

				int32 PtType = -1;

				PCGEX_OUTPUT_VALUE(LengthAtPoint, i, LengthAtPoint);
				PCGEX_OUTPUT_VALUE(Alpha, i, LengthAtPoint / TotalLength);
				PCGEX_OUTPUT_VALUE(ArriveTangent, i, SplineTransform.TransformVector(SplinePositions.Points[i].ArriveTangent));
				PCGEX_OUTPUT_VALUE(LeaveTangent, i, SplineTransform.TransformVector(SplinePositions.Points[i].LeaveTangent));
				PCGEX_OUTPUT_VALUE(PointType, i, GetPointType(SplinePositions.Points[i].InterpMode));
			}

			PCGExPaths::Helpers::SetClosedLoop(PointDataFacade->GetOut(), Spline.bClosedLoop);

			if (!Spline.bClosedLoop)
			{
				ApplyTransform(MutablePoints->GetNumPoints() - 1, Spline.GetTransformAtDistanceAlongSpline(TotalLength, ESplineCoordinateSpace::Type::World, true));

				PCGEX_OUTPUT_VALUE(LengthAtPoint, LastIndex, TotalLength);
				PCGEX_OUTPUT_VALUE(Alpha, LastIndex, 1);
				PCGEX_OUTPUT_VALUE(ArriveTangent, LastIndex, SplinePositions.Points[NumSegments].ArriveTangent);
				PCGEX_OUTPUT_VALUE(LeaveTangent, LastIndex, SplinePositions.Points[NumSegments].LeaveTangent);
				PCGEX_OUTPUT_VALUE(PointType, LastIndex, GetPointType(SplinePositions.Points[NumSegments].InterpMode));
			}

			// Copy attributes

			TArray<PCGExData::FAttributeIdentity> SourceAttributes;
			PCGExData::FAttributeIdentity::Get(SplineData->Metadata, SourceAttributes, nullptr);
			Context->CarryOverDetails.Prune(SourceAttributes);

			if (!SourceAttributes.IsEmpty())
			{
				TPCGValueRange<int64> OutMeta = MutablePoints->GetMetadataEntryValueRange();

				for (int64& Key : OutMeta) { MutablePoints->Metadata->InitializeOnSet(Key); }

				const TSharedPtr<FPCGAttributeAccessorKeysEntries> Keys = MakeShared<FPCGAttributeAccessorKeysEntries>(SplineData->Metadata);

				for (PCGExData::FAttributeIdentity Identity : SourceAttributes)
				{
					PCGExMetaHelpers::ExecuteWithRightType(Identity.UnderlyingType, [&](auto DummyValue)
					{
						using T = decltype(DummyValue);
						const FPCGMetadataAttribute<T>* SourceAttr = SplineData->Metadata->GetConstTypedAttribute<T>(Identity.Identifier);

						if (!SourceAttr) { return; }

						TSharedPtr<PCGExData::TBuffer<T>> OutBuffer = PointDataFacade->GetWritable<T>(SourceAttr, PCGExData::EBufferInit::New);

						if (Identity.InDataDomain())
						{
							OutBuffer->SetValue(0, PCGExData::Helpers::ReadDataValue(SourceAttr));
							return;
						}

						TSharedPtr<PCGExData::TArrayBuffer<T>> OutArrayBuffer = StaticCastSharedPtr<PCGExData::TArrayBuffer<T>>(OutBuffer);
						TArrayView<T> InRange = MakeArrayView(OutArrayBuffer->GetOutValues()->GetData(), OutArrayBuffer->GetOutValues()->Num());

						if (Keys)
						{
							TUniquePtr<const IPCGAttributeAccessor> InAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(SourceAttr, SourceAttr->GetMetadataDomain());
							InAccessor->GetRange(InRange, 0, *Keys.Get());
						}
						else
						{
							//PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Attribute {0} could not be copied."), FText::FromName(Identity.Identifier.Name)));
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

	if (Context->InputData.GetAllInputs().IsEmpty()) { return false; } //Get rid of errors and warning when there is no input

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
			if (!SplineData || SplineData->SplineStruct.GetNumberOfSplineSegments() <= 0) { continue; }

			switch (Settings->SampleInputs)
			{
			default: case EPCGExSplineSamplingIncludeMode::All: Context->Targets.Add(SplineData);
				AddTags(TaggedData.Tags);
				break;
			case EPCGExSplineSamplingIncludeMode::ClosedLoopOnly: if (SplineData->SplineStruct.bClosedLoop)
				{
					Context->Targets.Add(SplineData);
					AddTags(TaggedData.Tags);
				}
				break;
			case EPCGExSplineSamplingIncludeMode::OpenSplineOnly: if (!SplineData->SplineStruct.bClosedLoop)
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
	for (const UPCGSplineData* SplineData : Context->Targets) { Context->Splines.Add(SplineData->SplineStruct); }

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

			if (!NewOutput) { return true; }

			PCGEX_MAKE_SHARED(PointDataFacade, PCGExData::FFacade, NewOutput.ToSharedRef())
			PCGEX_LAUNCH(PCGExSplineToPath::FWriteTask, i, PointDataFacade)

			NewOutput->Tags->Append(Context->Tags[i]);
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
