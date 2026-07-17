// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/PCGExCopyToPaths.h"
#include "PCGExVersion.h"

#include "Misc/CoreMiscDefines.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct

#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSplineData.h"
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Data/PCGSplineStruct.h"
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS // FPCGSplineStruct
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"

#include "Helpers/PCGHelpers.h"
#include "Math/PCGExMathBounds.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExCopyToPathsElement"
#define PCGEX_NAMESPACE CopyToPaths

PCGEX_INITIALIZE_ELEMENT(CopyToPaths)

#if WITH_EDITOR
void UPCGExCopyToPathsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCopyToPathsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(CopyToPaths)

TArray<FPCGPinProperties> UPCGExCopyToPathsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY(PCGExCommon::Labels::SourceTargetsLabel, "Paths or splines to deform along", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceBoundsLabel, "Point data that will be used as unified bounds for all inputs", Normal)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCopyToPathsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExCopyToPathsSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExCommon::Labels::SourceBoundsLabel)
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool FPCGExCopyToPathsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CopyToPaths)

	if (!Settings->MainAxisSettings.Validate(InContext))
	{
		return false;
	}
	//if (!Settings->TwistSettings.Validate(InContext, true)) { return false; }

	if (!Context->Tangents.Init(Context, Settings->Tangents))
	{
		return false;
	}

	TArray<FPCGTaggedData> UnifiedBounds = Context->InputData.GetSpatialInputsByPin(PCGExCommon::Labels::SourceBoundsLabel);
	for (int i = 0; i < UnifiedBounds.Num(); ++i)
	{
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(UnifiedBounds[i].Data))
		{
			Context->bUseUnifiedBounds = true;
			Context->UnifiedBounds += PCGExMath::GetBounds(PointData, Settings->BoundsSource);
		}
	}

	TArray<FPCGTaggedData> Targets = Context->InputData.GetSpatialInputsByPin(PCGExCommon::Labels::SourceTargetsLabel);

	Context->Deformers.Reserve(Targets.Num());
	Context->DeformersData.Reserve(Targets.Num());
	Context->DeformersFacades.Reserve(Targets.Num());

	auto OnDataRegistered = [&](const UPCGData* InData)
	{
	};

	int32 DeformerIndex = 0;

	for (int i = 0; i < Targets.Num(); ++i)
	{
		FPCGTaggedData& TaggedData = Targets[i];

		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(TaggedData.Data))
		{
			if (PointData->GetNumPoints() < 2)
			{
				continue;
			}

			TSharedPtr<PCGExData::FPointIO> PointIO = MakeShared<PCGExData::FPointIO>(Context->GetWeakSelfHandle(), PointData);
			const TSharedPtr<PCGExData::FFacade> Facade = MakeShared<PCGExData::FFacade>(PointIO.ToSharedRef());
			const TSharedPtr<FPCGSplineStruct> SplineStruct = MakeShared<FPCGSplineStruct>();

			Facade->Idx = Context->DeformersFacades.Add(Facade);
			Context->LocalDeformers.Add(SplineStruct);

			Context->Deformers.Add(SplineStruct.Get());
			(void)Context->DeformersData.Emplace_GetRef(PointIO, DeformerIndex++);

			OnDataRegistered(PointIO->GetIn());

			continue;
		}

		if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(TaggedData.Data))
		{
			if (SplineData->SplineStruct.GetNumberOfPoints() < 2)
			{
				continue;
			}

			Context->Deformers.Add(&SplineData->SplineStruct);
			const TSharedPtr<PCGExData::FTags> Tags = MakeShared<PCGExData::FTags>(TaggedData.Tags);
			(void)Context->DeformersData.Emplace_GetRef(SplineData, DeformerIndex++, Tags, nullptr);

			OnDataRegistered(SplineData);
		}
	}

	if (Context->Deformers.IsEmpty())
	{
		return false;
	}

	PCGEX_FWD(MainAxisSettings)
	if (!Context->MainAxisSettings.Init(Context, Context->DeformersData))
	{
		return false;
	}

	PCGEX_FWD(TwistSettings)
	if (!Context->TwistSettings.Init(Context, Context->DeformersData))
	{
		return false;
	}

	Context->DataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	Context->DataMatcher->SetDetails(&Settings->DataMatching);
	if (!Context->DataMatcher->Init(Context, Context->DeformersData, true))
	{
		return false;
	}

	return true;
}

bool FPCGExCopyToPathsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyToPathsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CopyToPaths)
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
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any dataset to generate splines."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExCopyToPaths
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCopyToPaths::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		AxisTransform = PCGExMath::GetIdentity(Settings->AxisOrder);

		PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum);
		if (Context->DataMatcher->GetMatchingSourcesIndices(PointDataFacade->Source->GetTaggedData(), MatchingScope, Deformers) <= 0)
		{
			(void)Context->DataMatcher->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		Dupes.Reserve(Deformers.Num());
		Origins.Reserve(Deformers.Num());
		MainAxisDeformDetails.Reserve(Deformers.Num());

		// Init settings once from context copy
		// so we can graph an initialized local setting getter if one is created
		FPCGExAxisDeformDetails BaseMainAxisSettings = Context->MainAxisSettings;
		if (!BaseMainAxisSettings.Init(Context, Context->MainAxisSettings, PointDataFacade, -1))
		{
			return false;
		}

		FPCGExAxisDeformDetails BaseTwistSettings = Context->TwistSettings;
		if (!BaseTwistSettings.Init(Context, Context->TwistSettings, PointDataFacade, -1))
		{
			return false;
		}

		for (const int32 Index : Deformers)
		{
			TSharedPtr<PCGExData::FPointIO> Dupe = Context->MainPoints->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::Duplicate);
			Dupe->IOIndex = PointDataFacade->Source->IOIndex * 1000000 + Dupes.Num();
			Dupe->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

			FPCGExAxisDeformDetails& MainAxisDeform = MainAxisDeformDetails.Emplace_GetRef();
			if (!MainAxisDeform.Init(Context, BaseMainAxisSettings, PointDataFacade, Index))
			{
				return false;
			}

			FPCGExAxisDeformDetails& TwistDeform = TwistSettings.Emplace_GetRef();
			if (!TwistDeform.Init(Context, BaseTwistSettings, PointDataFacade, Index, true))
			{
				return false;
			}

			Origins.Emplace(FTransform::Identity); // TODO : Expose this
			//Origins.Emplace(Deformers.Last()->GetTransformAtSplineInputKey(0, ESplineCoordinateSpace::World).Inverse()); // Move this to CompleteWork

			Dupes.Add(Dupe);
		}

		// Set up box reference for this data

		if (Context->bUseUnifiedBounds)
		{
			Box = Context->UnifiedBounds;
		}
		else
		{
			Box = PCGExMath::GetBounds(PointDataFacade->GetIn(), Settings->BoundsSource);
		}

		Box = FBox(Box.Min + Settings->MinBoundsOffset, Box.Max + Settings->MaxBoundsOffset);
		PCGExMath::Swizzle(Box.Min, Settings->AxisOrder);
		PCGExMath::Swizzle(Box.Max, Settings->AxisOrder);
		Size = Box.GetSize();

		return true;
	}

	void FProcessor::CompleteWork()
	{
		StartParallelLoopForPoints(PCGExData::EIOSide::In);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::CopyToPaths::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		bool bMutateScale = static_cast<EPCGExApplySampledComponentFlags>(Settings->TransformScale) != EPCGExApplySampledComponentFlags::All;
		bool bMutateScaleX = !EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->TransformScale), EPCGExApplySampledComponentFlags::X);
		bool bMutateScaleY = !EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->TransformScale), EPCGExApplySampledComponentFlags::Y);
		bool bMutateScaleZ = !EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->TransformScale), EPCGExApplySampledComponentFlags::Z);

		for (int i = 0; i < Dupes.Num(); i++)
		{
			const int32 TargetIndex = Deformers[i];

			const FPCGSplineStruct* Deformer = Context->Deformers[TargetIndex];
			const TSharedPtr<PCGExData::FPointIO> Dupe = Dupes[i];
			TPCGValueRange<FTransform> OutTransforms = Dupe->GetOut()->GetTransformValueRange();

			double TotalLength = Deformer->GetSplineLength();
			float NumSegments = static_cast<float>(Deformer->GetNumberOfSplineSegments());

			const FTransform& InvT = Origins[i];

			bool bWrap = Deformer->IsClosedLoop() && Settings->bWrapClosedLoops;

			int32 j = 0;

			double Start = 0;
			double End = 0;

			double TwistStart = 0;
			double TwistEnd = 0;

			MainAxisDeformDetails[i].GetAlphas(0, Start, End);
			TwistSettings[i].GetAlphas(0, TwistStart, TwistEnd);
			const double TwistRange = TwistEnd - TwistStart;

			const double Coverage = TotalLength * (End - Start);
			const double CoverageRatio = Coverage / Size[0];

			PCGEX_SCOPE_LOOP(Index)
			{
				FTransform WorkingTransform = (InTransforms[Index] * AxisTransform);

				FVector UVW = (WorkingTransform.GetLocation() - Box.Min) / Size;

				if (Settings->bDoTwist)
				{
					// Twist
					WorkingTransform = WorkingTransform * FTransform(FQuat::MakeFromEuler(FVector(360 * (TwistStart + UVW[0] * TwistRange), 0, 0)));
				}

				UVW[0] = PCGExMath::Remap(UVW[0], 0, 1, Start, End);
				FVector Location = WorkingTransform.GetLocation();
				Location[0] = UVW[0];
				WorkingTransform.SetLocation(Location);

				FTransform Anchor = FTransform::Identity;

				if (bWrap)
				{
					Anchor = Deformer->GetTransformAtSplineInputKey(NumSegments * PCGExMath::Tile<double>(UVW[0], 0.0, 1.0), ESplineCoordinateSpace::World, true);
				}
				else
				{
					Anchor = Deformer->GetTransformAtSplineInputKey(NumSegments * FMath::Clamp<double>(UVW[0], 0.0, 1.0), ESplineCoordinateSpace::World, true);
				}

				if (bMutateScale)
				{
					FVector MutatedScale = Anchor.GetScale3D();
					if (bMutateScaleX)
					{
						MutatedScale.X = 1;
					}
					if (bMutateScaleY)
					{
						MutatedScale.Y = 1;
					}
					if (bMutateScaleZ)
					{
						MutatedScale.Z = 1;
					}
					Anchor.SetScale3D(MutatedScale);
				}

				FQuat Q = Anchor.GetRotation();
				if (Settings->FlattenAxis == EPCGExMinimalAxis::X)
				{
					Anchor = FTransform(FRotationMatrix::MakeFromZY(Q.GetUpVector(), Q.GetRightVector()).ToQuat(), Anchor.GetLocation(), Anchor.GetScale3D());
				}
				else if (Settings->FlattenAxis == EPCGExMinimalAxis::Y)
				{
					Anchor = FTransform(FRotationMatrix::MakeFromZX(Q.GetUpVector(), Q.GetForwardVector()).ToQuat(), Anchor.GetLocation(), Anchor.GetScale3D());
				}
				else if (Settings->FlattenAxis == EPCGExMinimalAxis::Z)
				{
					Anchor = FTransform(FRotationMatrix::MakeFromXY(Q.GetForwardVector(), Q.GetRightVector()).ToQuat(), Anchor.GetLocation(), Anchor.GetScale3D());
				}

				if (Settings->bPreserveAspectRatio)
				{
					Anchor.SetScale3D(Anchor.GetScale3D() * CoverageRatio);
				}

				OutTransforms[Index] = WorkingTransform * Anchor;

				if (Settings->bPreserveOriginalInputScale)
				{
					OutTransforms[Index].SetScale3D(WorkingTransform.GetScale3D());
				}

				j++;
			}
		}
	}

	void FBatch::OnInitialPostProcess()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(CopyToPaths)

		if (Context->DeformersFacades.IsEmpty())
		{
			TBatch<FProcessor>::OnInitialPostProcess();
			return;
		}

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, BuildSplines)

		BuildSplines->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnSplineBuildingComplete();
		};

		BuildSplines->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->BuildSpline(Index);
		};

		BuildSplines->StartIterations(Context->DeformersFacades.Num(), 1);
	}

	void FBatch::BuildSpline(const int32 InSplineIndex) const
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(CopyToPaths)

		TSharedPtr<FPCGSplineStruct> SplineStruct = Context->LocalDeformers[InSplineIndex];
		if (!SplineStruct)
		{
			return;
		}

		TSharedPtr<PCGExData::FFacade> PathFacade = Context->DeformersFacades[InSplineIndex];
		PathFacade->bSupportsScopedGet = false;

		const bool bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PathFacade->GetIn());

		TSharedPtr<PCGExData::TBuffer<int32>> CustomPointType;

		if (Settings->bApplyCustomPointType)
		{
			CustomPointType = PathFacade->GetBroadcaster<int32>(Settings->PointTypeAttribute, true);
			if (!CustomPointType)
			{
				PCGEX_LOG_INVALID_ATTR_C(Context, Point Type, Settings->PointTypeAttribute)
				return;
			}
		}

		TSharedPtr<PCGExTangents::FTangentsHandler> TangentsHandler = nullptr;

		if (Settings->bApplyCustomPointType || Settings->DefaultPointType == EPCGExSplinePointType::CurveCustomTangent)
		{
			TangentsHandler = MakeShared<PCGExTangents::FTangentsHandler>(bClosedLoop);
			if (!TangentsHandler->Init(Context, Context->Tangents, PathFacade))
			{
				return;
			}
		}

		const int32 NumPoints = PathFacade->GetNum();
		TArray<FSplinePoint> SplinePoints;
		SplinePoints.Reserve(NumPoints);

		const UPCGBasePointData* InPointData = PathFacade->GetIn();
		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		for (int i = 0; i < NumPoints; i++)
		{
			FVector OutArrive = FVector::ZeroVector;
			FVector OutLeave = FVector::ZeroVector;

			if (TangentsHandler)
			{
				TangentsHandler->GetSegmentTangents(i, OutArrive, OutLeave);
			}

			const FTransform& TR = InTransforms[i];

			EPCGExSplinePointType PointTypeProxy = Settings->DefaultPointType;
			ESplinePointType::Type PointType = ESplinePointType::Curve;

			if (CustomPointType)
			{
				const int32 Value = CustomPointType->Read(i);
				if (FMath::IsWithinInclusive(Value, 0, 4))
				{
					PointTypeProxy = static_cast<EPCGExSplinePointType>(static_cast<uint8>(Value));
				}
			}

			switch (PointTypeProxy)
			{
			case EPCGExSplinePointType::Linear:
				PointType = ESplinePointType::Linear;
				break;
			case EPCGExSplinePointType::Curve:
				PointType = ESplinePointType::Curve;
				break;
			case EPCGExSplinePointType::Constant:
				PointType = ESplinePointType::Constant;
				break;
			case EPCGExSplinePointType::CurveClamped:
				PointType = ESplinePointType::CurveClamped;
				break;
			case EPCGExSplinePointType::CurveCustomTangent:
				PointType = ESplinePointType::CurveCustomTangent;
				break;
			}

			SplinePoints.Emplace(static_cast<float>(i), TR.GetLocation(), OutArrive, OutLeave, TR.GetRotation().Rotator(), TR.GetScale3D(), PointType);
		}

		SplineStruct->Initialize(SplinePoints, bClosedLoop, FTransform::Identity);
	}

	void FBatch::OnSplineBuildingComplete()
	{
		TBatch<FProcessor>::OnInitialPostProcess();
	}
}

PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
