// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleNearestBounds.h"

#include "PCGExVersion.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Blenders/PCGExUnionOpsManager.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExAsyncHelpers.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Math/PCGExMathBounds.h"
#include "Math/PCGExMathDistances.h"
#include "Math/OBB/PCGExOBBCollection.h"
#include "Math/OBB/PCGExOBBSampling.h"
#include "Sampling/PCGExSamplingHelpers.h"
#include "Sampling/PCGExSamplingUnionData.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Types/PCGExTypes.h"

PCGEX_SETTING_VALUE_IMPL_BOOL(UPCGExSampleNearestBoundsSettings, LookAtUp, FVector, LookAtUpSelection != EPCGExSampleSource::Constant, LookAtUpSource, LookAtUpConstant)

#define LOCTEXT_NAMESPACE "PCGExSampleNearestBoundsElement"
#define PCGEX_NAMESPACE SampleNearestBounds

UPCGExSampleNearestBoundsSettings::UPCGExSampleNearestBoundsSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (LookAtUpSource.GetName() == FName("@Last"))
	{
		LookAtUpSource.Update(TEXT("$Transform.Up"));
	}
	if (!WeightRemap)
	{
		WeightRemap = PCGExCurves::WeightDistributionLinear;
	}
}

#if WITH_EDITOR
void UPCGExSampleNearestBoundsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameInputPin(PCGPinConstants::DefaultInputLabel, PCGExSampling::Labels::SourceSourceLabel);
	
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}
	
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleNearestBoundsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

FName UPCGExSampleNearestBoundsSettings::GetMainInputPin() const
{
	return PCGExSampling::Labels::SourceSourceLabel;
}

TArray<FPCGPinProperties> UPCGExSampleNearestBoundsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceBoundsLabel, "The bounds data set to check against.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SampleMethod == EPCGExBoundsSampleMethod::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Normal, BlendingInterface);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSampleNearestBoundsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

bool UPCGExSampleNearestBoundsSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SampleMethod == EPCGExBoundsSampleMethod::BestCandidate;
	}
	if (InPin->Properties.Label == PCGExBlending::Labels::SourceBlendingLabel)
	{
		return BlendingInterface == EPCGExBlendingInterface::Individual && InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_INITIALIZE_ELEMENT(SampleNearestBounds)

PCGExData::EIOInit UPCGExSampleNearestBoundsSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleNearestBounds)

bool FPCGExSampleNearestBoundsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestBounds)

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	PCGEX_FOREACH_FIELD_NEARESTBOUNDS(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->BlendingInterface == EPCGExBlendingInterface::Individual)
	{
		PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()}, false);
	}

	Context->TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->NumMaxTargets = Context->TargetsHandler->Init(Context, PCGExCommon::Labels::SourceBoundsLabel);

	if (!Context->NumMaxTargets)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("No valid bounds"));
		return false;
	}

	if (Settings->SampleMethod == EPCGExBoundsSampleMethod::BestCandidate)
	{
		Context->Sorter = MakeShared<PCGExSorting::FSorter>(PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules));
		Context->Sorter->SortDirection = Settings->SortDirection;
	}

	if (!Context->BlendingFactories.IsEmpty())
	{
		// Resolve blend op configs once, here, single-threaded: per-processor blender init then
		// only instantiates ops (no concurrent metadata enumeration on shared target facades),
		// and the preloader warms exactly the attribute set the ops will read.
		Context->BlendOpsSchema = MakeShared<PCGExBlending::FBlendOpsSchema>();
		if (!Context->BlendOpsSchema->Init(Context, Context->BlendingFactories, Context->TargetsHandler->GetFacades()))
		{
			return false;
		}
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
				[CtxHandle = Context->GetWeakSelfHandle(), Collection, Facade, BoundsSource = Settings->BoundsSource]()
				{
					PCGEX_SHARED_CONTEXT_VOID(CtxHandle);
					Collection->BuildFrom(Facade->Source, BoundsSource);
				});

			if (Context->BlendOpsSchema)
			{
				Context->BlendOpsSchema->RegisterBuffersDependencies(Context, Preloader);
			}
		});
	}

	Context->WeightCurve = Settings->WeightCurveLookup.MakeLookup(
		Settings->bUseLocalCurve, Settings->LocalWeightRemap, Settings->WeightRemap,
		[](FRichCurve& CurveData)
		{
			CurveData.AddKey(0, 0);
			CurveData.AddKey(1, 1);
		});

	return true;
}

bool FPCGExSampleNearestBoundsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleNearestBoundsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleNearestBounds)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_FacadePreloading);

		TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
		Context->TargetsHandler->TargetsPreloader->OnCompleteCallback = [Settings, Context, WeakHandle]()
		{
			PCGEX_SHARED_CONTEXT_VOID(WeakHandle)

			const bool bError = Context->TargetsHandler->ForEachTarget(
				[&](const TSharedRef<PCGExData::FFacade>& Target, const int32 TargetIndex, bool& bBreak)
				{
					// Prep look up getters
					if (Settings->LookAtUpSelection == EPCGExSampleSource::Target)
					{
						// TODO : Preload if relevant
						TSharedPtr<PCGExDetails::TSettingValue<FVector>> LookAtUpGetter = Settings->GetValueSettingLookAtUp();
						if (!LookAtUpGetter->Init(Target, false))
						{
							bBreak = true;
							return;
						}

						Context->TargetLookAtUpGetters.Add(LookAtUpGetter);
					}
				});

			if (bError)
			{
				Context->CancelExecution();
				return;
			}

			Context->TargetsHandler->SetMatchingDetails(Context, &Settings->DataMatching);

			if (Context->Sorter && !Context->Sorter->Init(Context, Context->TargetsHandler->GetFacades()))
			{
				Context->CancelExecution(TEXT("Invalid sort rules"));
				return;
			}

			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				Context->CancelExecution(TEXT("Could not find any points to sample."));
			}
		};

		Context->TargetsHandler->StartLoading(Context->GetTaskManager());
		if (Context->IsWaitingForTasks())
		{
			return false;
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleNearestBounds
{
	FProcessor::~FProcessor()
	{
	}

	void FProcessor::SamplingFailed(const int32 Index)
	{
		SamplingMask[Index] = false;

		const TConstPCGValueRange<FTransform> Transforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		constexpr double FailSafeDist = -1;
		PCGEX_OUTPUT_VALUE(Success, Index, false)
		PCGEX_OUTPUT_VALUE(Transform, Index, Transforms[Index])
		PCGEX_OUTPUT_VALUE(LookAtTransform, Index, Transforms[Index])
		PCGEX_OUTPUT_VALUE(Distance, Index, Settings->bOutputNormalizedDistance ? FailSafeDist : FailSafeDist * Settings->DistanceScale)
		PCGEX_OUTPUT_VALUE(SignedDistance, Index, FailSafeDist * Settings->SignedDistanceScale)
		PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, FVector(FailSafeDist))
		PCGEX_OUTPUT_VALUE(NumSamples, Index, 0)
		PCGEX_OUTPUT_VALUE(SampledIndex, Index, -1)
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleNearestBounds::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Settings->bIgnoreSelf)
		{
			IgnoreList.Add(PointDataFacade->GetIn());
		}
		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true);
			!Context->TargetsHandler->PopulateIgnoreList(PointDataFacade->Source, MatchingScope, IgnoreList))
		{
			(void)Context->TargetsHandler->HandleUnmatchedOutput(PointDataFacade, true);
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;
		if (Context->ApplySampling.WantsApply())
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}
		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		SamplingMask.SetNumUninitialized(PointDataFacade->GetNum());

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_NEARESTBOUNDS(PCGEX_OUTPUT_INIT)
		}

		Distances = PCGExMath::GetDistances(EPCGExDistance::Center, EPCGExDistance::Center, false, Settings->DistanceType);

		if (!Context->BlendingFactories.IsEmpty())
		{
			UnionBlendOpsManager = MakeShared<PCGExBlending::FUnionOpsManager>(&Context->BlendingFactories, Distances);
			if (!UnionBlendOpsManager->Init(Context, PointDataFacade, Context->TargetsHandler->GetFacades(), Context->BlendOpsSchema))
			{
				return false;
			}
			DataBlender = UnionBlendOpsManager;
		}
		else if (Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic)
		{
			TSet<FName> MissingAttributes;
			PCGExBlending::AssembleBlendingDetails(Settings->PointPropertiesBlendingSettings, Settings->TargetAttributes, Context->TargetsHandler->GetFacades(), BlendingDetails, MissingAttributes);

			UnionBlender = MakeShared<PCGExBlending::FUnionBlender>(&BlendingDetails, nullptr, Distances);
			UnionBlender->AddSources(Context->TargetsHandler->GetFacades());
			if (!UnionBlender->Init(Context, PointDataFacade))
			{
				return false;
			}
			DataBlender = UnionBlender;
		}

		if (!DataBlender)
		{
			TSharedPtr<PCGExBlending::FDummyUnionBlender> DummyUnionBlender = MakeShared<PCGExBlending::FDummyUnionBlender>();
			DummyUnionBlender->Init(PointDataFacade, Context->TargetsHandler->GetFacades());
			DataBlender = DummyUnionBlender;
		}

		if (Settings->bWriteLookAtTransform)
		{
			if (Settings->LookAtUpSelection != EPCGExSampleSource::Target)
			{
				LookAtUpGetter = Settings->GetValueSettingLookAtUp();
				if (!LookAtUpGetter->Init(PointDataFacade))
				{
					return false;
				}
			}
		}
		else
		{
			LookAtUpGetter = PCGExDetails::MakeSettingValue(Settings->LookAtUpConstant);
		}

		bSingleSample = Settings->SampleMethod != EPCGExBoundsSampleMethod::WithinRange;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExSampleNearestBoundsContext, UPCGExSampleNearestBoundsSettings>::PrepareLoopScopesForPoints(Loops);
		MaxSampledDistanceScoped = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleNearestBounds::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		bool bLocalAnySuccess = false;

		TArray<PCGExData::FWeightedPoint> OutWeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;

		DataBlender->InitTrackers(Trackers);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();
		Union->Reserve(Context->TargetsHandler->Num());
		Union->WeightRange = -2; // Don't remap

		PCGExMath::OBB::FSample OBBSample;

		double DefaultDet = 0;

		switch (Settings->SampleMethod)
		{
		case EPCGExBoundsSampleMethod::BestCandidate:
			DefaultDet = -1;
			break;
		default: case EPCGExBoundsSampleMethod::ClosestBounds:
		case EPCGExBoundsSampleMethod::SmallestBounds:
			DefaultDet = TNumericLimits<double>::Max();
			break;
		case EPCGExBoundsSampleMethod::FarthestBounds:
		case EPCGExBoundsSampleMethod::LargestBounds:
			DefaultDet = TNumericLimits<double>::Min();
			break;
		}

		PCGEX_SCOPE_LOOP(Index)
		{
			Union->Reset();

			if (!PointFilterCache[Index])
			{
				if (Settings->bProcessFilteredOutAsFails)
				{
					SamplingFailed(Index);
				}
				continue;
			}

			PCGExData::FElement SinglePick(-1, -1);
			double Det = DefaultDet;

			const PCGExData::FMutablePoint Point = PointDataFacade->GetOutPoint(Index);
			const FVector Origin = InTransforms[Index].GetLocation();

			const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(Origin, PCGExMath::GetLocalBounds(Point, BoundsSource).GetExtent());

			auto SampleSingle = [&](const PCGExData::FElement& Current, const PCGExMath::OBB::FOBB& NearbyOBB)
			{
				double DetCandidate = Det;
				bool bReplaceWithCurrent = Union->IsEmpty();

				switch (Settings->SampleMethod)
				{
				case EPCGExBoundsSampleMethod::BestCandidate:
					DetCandidate = NearbyOBB.GetIndex();
					if (SinglePick.Index != -1)
					{
						bReplaceWithCurrent = Context->Sorter->Sort(Current, SinglePick);
					}
					else
					{
						bReplaceWithCurrent = true;
					}
					break;
				default: case EPCGExBoundsSampleMethod::ClosestBounds:
					DetCandidate = OBBSample.Distances.SizeSquared();
					bReplaceWithCurrent = DetCandidate < Det;
					break;
				case EPCGExBoundsSampleMethod::FarthestBounds:
					DetCandidate = OBBSample.Distances.SizeSquared();
					bReplaceWithCurrent = DetCandidate > Det;
					break;
				case EPCGExBoundsSampleMethod::SmallestBounds:
					DetCandidate = NearbyOBB.Bounds.GetRadiusSq();
					bReplaceWithCurrent = DetCandidate < Det;
					break;
				case EPCGExBoundsSampleMethod::LargestBounds:
					DetCandidate = NearbyOBB.Bounds.GetRadiusSq();
					bReplaceWithCurrent = DetCandidate > Det;
					break;
				}

				if (bReplaceWithCurrent)
				{
					SinglePick = Current;
					Det = DetCandidate;
					Union->Reset();
					Union->AddWeighted_Unsafe(Current, OBBSample.Weight);
				}
			};

			Context->TargetsHandler->FindTargetsWithBoundsTest(BCAE, [&](const PCGExOctree::FItem& Target)
			{
				const TSharedPtr<PCGExMath::OBB::FCollection>& Collection = Context->Collections[Target.Index];
				PCGExOctree::FItemOctree* CollectionOctree = Collection->GetOctree();
				check(CollectionOctree)

				CollectionOctree->FindElementsWithBoundsTest(BCAE, [&](const PCGExOctree::FItem& NearbyItem)
				{
					const PCGExMath::OBB::FOBB NearbyOBB = Collection->GetOBB(NearbyItem.Index);
					PCGExMath::OBB::Sample(NearbyOBB, Origin, OBBSample);
					if (!OBBSample.bIsInside)
					{
						return;
					}

					const PCGExData::FElement Current(NearbyOBB.GetIndex(), Target.Index);
					if (bSingleSample)
					{
						SampleSingle(Current, NearbyOBB);
					}
					else
					{
						Union->AddWeighted_Unsafe(Current, OBBSample.Weight);
					}
				});
			}, &IgnoreList);

			if (Union->IsEmpty())
			{
				SamplingFailed(Index);
				continue;
			}

			DataBlender->ComputeWeights(Index, Union, OutWeightedPoints);

			FTransform WeightedTransform = FTransform::Identity;
			WeightedTransform.SetScale3D(FVector::ZeroVector);
			FVector WeightedUp = SafeUpVector;
			if (Settings->LookAtUpSelection == EPCGExSampleSource::Source && LookAtUpGetter)
			{
				WeightedUp = LookAtUpGetter->Read(Index);
			}

			FVector WeightedSignAxis = FVector::ZeroVector;
			FVector WeightedAngleAxis = FVector::ZeroVector;

			// Post-process weighted points and compute local data
			PCGEx::FOpStats SampleTracker{};

			for (PCGExData::FWeightedPoint& P : OutWeightedPoints)
			{
				const double W = Context->WeightCurve->Eval(P.Weight);

				// Don't remap blending if we use external blend ops; they have their own curve
				if (Settings->BlendingInterface == EPCGExBlendingInterface::Monolithic)
				{
					P.Weight = W;
				}

				SampleTracker.Count++;
				SampleTracker.TotalWeight += W;

				const FTransform& TargetTransform = Context->TargetsHandler->GetPoint(P).GetTransform();
				const FQuat TargetRotation = TargetTransform.GetRotation();

				WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::WeightedAdd(WeightedTransform, TargetTransform, W);

				if (Settings->LookAtUpSelection == EPCGExSampleSource::Target)
				{
					WeightedUp = PCGExTypeOps::FTypeOps<FVector>::WeightedAdd(WeightedUp, Context->TargetLookAtUpGetters[P.IO]->Read(P.Index), W);
				}

				WeightedSignAxis += PCGExMath::GetDirection(TargetRotation, Settings->SignAxis) * W;
				WeightedAngleAxis += PCGExMath::GetDirection(TargetRotation, Settings->AngleAxis) * W;
			}

			// Blend using updated weighted points
			DataBlender->Blend(Index, OutWeightedPoints, Trackers);

			if (SampleTracker.TotalWeight != 0) // Dodge NaN
			{
				WeightedUp = PCGExTypeOps::FTypeOps<FVector>::NormalizeWeight(WeightedUp, SampleTracker.TotalWeight);
				WeightedTransform = PCGExTypeOps::FTypeOps<FTransform>::NormalizeWeight(WeightedTransform, SampleTracker.TotalWeight);
			}
			else
			{
				WeightedTransform = InTransforms[Index];
			}

			WeightedUp.Normalize();

			const FVector CWDistance = Origin - WeightedTransform.GetLocation();
			FVector LookAt = CWDistance.GetSafeNormal();
			const double WeightedDistance = Distances->GetDist(Origin, WeightedTransform.GetLocation());

			FTransform LookAtTransform = PCGExMath::MakeLookAtTransform(LookAt, WeightedUp, Settings->LookAtAxisAlign);
			if (Context->ApplySampling.WantsApply())
			{
				PCGExData::FMutablePoint MutablePoint(OutPointData, Index);
				Context->ApplySampling.Apply(MutablePoint, WeightedTransform, LookAtTransform);
			}

			SamplingMask[Index] = !Union->IsEmpty();
			PCGEX_OUTPUT_VALUE(Success, Index, !Union->IsEmpty())
			PCGEX_OUTPUT_VALUE(Transform, Index, WeightedTransform)
			PCGEX_OUTPUT_VALUE(LookAtTransform, Index, LookAtTransform)
			PCGEX_OUTPUT_VALUE(Distance, Index, Settings->bOutputNormalizedDistance ? WeightedDistance : WeightedDistance * Settings->DistanceScale)
			PCGEX_OUTPUT_VALUE(SignedDistance, Index, FMath::Sign(WeightedSignAxis.Dot(LookAt)) * WeightedDistance * Settings->SignedDistanceScale)
			PCGEX_OUTPUT_VALUE(ComponentWiseDistance, Index, Settings->bAbsoluteComponentWiseDistance ? PCGExTypes::Abs(CWDistance) : CWDistance)
			PCGEX_OUTPUT_VALUE(Angle, Index, PCGExSampling::Helpers::GetAngle(Settings->AngleRange, WeightedAngleAxis, LookAt))
			PCGEX_OUTPUT_VALUE(NumSamples, Index, SampleTracker.Count)
			PCGEX_OUTPUT_VALUE(SampledIndex, Index, SinglePick.Index)

			MaxSampledDistanceScoped->Set(Scope, FMath::Max(MaxSampledDistanceScoped->Get(Scope), WeightedDistance));
			bLocalAnySuccess = true;
		}

		if (bLocalAnySuccess)
		{
			FPlatformAtomics::InterlockedExchange(&bAnySuccess, 1);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (Settings->bOutputNormalizedDistance && DistanceWriter)
		{
			MaxSampledDistance = MaxSampledDistanceScoped->Max();

			const int32 NumPoints = PointDataFacade->GetNum();

			if (Settings->bOutputOneMinusDistance)
			{
				const double InvMaxDist = 1.0 / MaxSampledDistance;
				const double Scale = Settings->DistanceScale;

				for (int i = 0; i < NumPoints; i++)
				{
					const double D = DistanceWriter->GetValue(i);
					DistanceWriter->SetValue(i, (1.0 - D * InvMaxDist) * Scale);
				}
			}
			else
			{
				const double Scale = (1.0 / MaxSampledDistance) * Settings->DistanceScale;

				for (int i = 0; i < NumPoints; i++)
				{
					const double D = DistanceWriter->GetValue(i);
					DistanceWriter->SetValue(i, D * Scale);
				}
			}
		}

		if (UnionBlendOpsManager)
		{
			UnionBlendOpsManager->Cleanup(Context);
		}
		PointDataFacade->WriteFastest(TaskManager);

		if (Settings->bTagIfHasSuccesses && bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasSuccessesTag);
		}
		if (Settings->bTagIfHasNoSuccesses && !bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasNoSuccessesTag);
		}
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->bPruneFailedSamples)
		{
			(void)PointDataFacade->Source->Gather(SamplingMask);
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExSampleNearestBoundsContext, UPCGExSampleNearestBoundsSettings>::Cleanup();
		UnionBlendOpsManager.Reset();
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
