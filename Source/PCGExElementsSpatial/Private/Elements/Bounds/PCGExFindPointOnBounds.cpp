// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExFindPointOnBounds.h"

#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExBlendingHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Types/PCGExAttributeIdentity.h"


#define LOCTEXT_NAMESPACE "PCGExFindPointOnBoundsElement"
#define PCGEX_NAMESPACE FindPointOnBounds

#if WITH_EDITOR
void UPCGExFindPointOnBoundsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		// Rewire UVW
		PCGEX_SHORTHAND_RENAME_PIN(LocalUVW, UVW, UVWValue)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFindPointOnBoundsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		UVWValue.Update(UVWInput_DEPRECATED, LocalUVW_DEPRECATED, UVW_DEPRECATED);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_INITIALIZE_ELEMENT(FindPointOnBounds)
PCGEX_ELEMENT_BATCH_POINT_IMPL(FindPointOnBounds)

bool FPCGExFindPointOnBoundsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindPointOnBounds)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
	{
		TSet<FName> AttributeMismatches;

		Context->BestIndices.Init(-1, Context->MainPoints->Num());

		Context->MergedOut = PCGExData::NewPointIO(Context, Settings->GetMainOutputPin(), 0);
		Context->MergedAttributesInfos = PCGExData::FAttributesInfos::Get(Context->MainPoints, AttributeMismatches);

		Context->CarryOverDetails.Attributes.Prune(*Context->MergedAttributesInfos);
		Context->CarryOverDetails.Attributes.Prune(AttributeMismatches);

		Context->MergedOut->InitializeOutput(PCGExData::EIOInit::New);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->MergedOut->GetOut(), Context->MainPoints->Num());
		Context->MergedOut->GetOutKeys(true);

		if (!AttributeMismatches.IsEmpty() && !Settings->bQuietAttributeMismatchWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Some attributes on incoming data share the same name but not the same type. Whatever type was discovered first will be used."));
		}
	}

	return true;
}

bool FPCGExFindPointOnBoundsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindPointOnBoundsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindPointOnBounds)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				//NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
	{
		PCGExBlending::Helpers::MergeBestCandidatesAttributes(Context->MergedOut, Context->MainPoints->Pairs, Context->BestIndices, *Context->MergedAttributesInfos);

		(void)Context->MergedOut->StageOutput(Context);
	}
	else
	{
		Context->MainPoints->StageOutputs();
	}

	return Context->TryComplete();
}


namespace PCGExFindPointOnBounds
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindPointOnBounds::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		FBox Bounds = FBox(ForceInit);
		FVector UVW = Settings->UVWValue.GetValueSetting(Context, PointDataFacade->GetIn())->Read(0);

		if (Settings->bBestFitBounds)
		{
			PCGExMath::FBestFitPlane BestFitPlane(PointDataFacade->GetIn()->GetConstTransformValueRange());

			FTransform T = BestFitPlane.GetTransform(Settings->AxisOrder);
			UVW = T.TransformVector(UVW);
			Bounds = FBox(BestFitPlane.Centroid - BestFitPlane.Extents, BestFitPlane.Centroid + BestFitPlane.Extents).TransformBy(T);
		}
		else
		{
			Bounds = PointDataFacade->Source->GetIn()->GetBounds();
		}

		SearchPosition = Bounds.GetCenter() + Bounds.GetExtent() * UVW;

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FindPointOnBounds::ProcessPoints);

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			const double Dist = FVector::Dist(InTransforms[Index].GetLocation(), SearchPosition);

			{
				FWriteScopeLock WriteLock(BestIndexLock);
				if (Dist > BestDistance)
				{
					continue;
				}
			}

			{
				FWriteScopeLock WriteLock(BestIndexLock);

				if (Dist > BestDistance)
				{
					continue;
				}

				BestPosition = InTransforms[Index].GetLocation();
				BestIndex = Index;
				BestDistance = Dist;
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		const FVector Offset = (BestPosition - PointDataFacade->Source->GetIn()->GetBounds().GetCenter()).GetSafeNormal() * Settings->Offset;

		if (Settings->OutputMode == EPCGExPointOnBoundsOutputMode::Merged)
		{
			const int32 TargetIndex = PointDataFacade->Source->IOIndex;

			TPCGValueRange<FTransform> OutTransforms = Context->MergedOut->GetOut()->GetTransformValueRange(false);
			TPCGValueRange<int64> OutMetadataEntry = Context->MergedOut->GetOut()->GetMetadataEntryValueRange(false);
			const PCGMetadataEntryKey OriginalKey = OutMetadataEntry[TargetIndex];

			PointDataFacade->Source->GetIn()->CopyPointsTo(Context->MergedOut->GetOut(), BestIndex, TargetIndex, 1);

			OutTransforms[TargetIndex].AddToTranslation(Offset);
			OutMetadataEntry[TargetIndex] = OriginalKey;
		}
		else
		{
			PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::New)
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(PointDataFacade->GetOut(), 1);

			TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
			TPCGValueRange<int64> OutMetadataEntry = PointDataFacade->GetOut()->GetMetadataEntryValueRange(false);

			PointDataFacade->Source->GetOut()->Metadata->InitializeOnSet(OutMetadataEntry[0]);
			OutTransforms[0].AddToTranslation(Offset);
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
