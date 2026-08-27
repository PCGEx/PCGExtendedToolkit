// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathShrink.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExShrinkPathElement"
#define PCGEX_NAMESPACE ShrinkPath

bool FPCGExShrinkPathEndpointDistanceDetails::SanityCheck(const FPCGContext* Context) const
{
	if (AmountInput == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_C(Context, DistanceAttribute.GetName())
	}
	return true;
}

bool FPCGExShrinkPathEndpointCountDetails::SanityCheck(const FPCGContext* Context) const
{
	if (ValueSource == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_C(Context, CountAttribute.GetName())
	}
	return true;
}

UPCGExShrinkPathSettings::UPCGExShrinkPathSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportClosedLoops = false;
}

PCGEX_INITIALIZE_ELEMENT(ShrinkPath)

PCGExData::EIOInit UPCGExShrinkPathSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(ShrinkPath)

void FPCGExShrinkPathContext::GetShrinkAmounts(const TSharedRef<PCGExData::FPointIO>& PointIO, double& Start, double& End, EPCGExPathShrinkDistanceCutType& StartCut, EPCGExPathShrinkDistanceCutType& EndCut) const
{
	PCGEX_SETTINGS_LOCAL(ShrinkPath)

	StartCut = Settings->PrimaryDistanceDetails.CutType;
	EndCut = Settings->PrimaryDistanceDetails.CutType;

	constexpr int32 StartIndex = 0;
	const int32 EndIndex = PointIO->GetNum() - 1;

	if (Settings->PrimaryDistanceDetails.AmountInput == EPCGExInputValueType::Attribute)
	{
		const TUniquePtr<PCGExData::TAttributeBroadcaster<double>> Getter = MakeUnique<PCGExData::TAttributeBroadcaster<double>>();
		if (!Getter->Prepare(Settings->PrimaryDistanceDetails.DistanceAttribute, PointIO))
		{
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Could not read primary Distance value attribute on some inputs."));
		}

		Start = Getter->FetchSingle(PointIO->GetInPoint(StartIndex), 0);
		End = Getter->FetchSingle(PointIO->GetInPoint(EndIndex), 0);
	}
	else
	{
		Start = End = Settings->PrimaryDistanceDetails.Distance;
	}

	if (Settings->SettingsMode == EPCGExShrinkConstantMode::Separate)
	{
		EndCut = Settings->SecondaryDistanceDetails.CutType;

		if (Settings->SecondaryDistanceDetails.AmountInput == EPCGExInputValueType::Attribute)
		{
			const TUniquePtr<PCGExData::TAttributeBroadcaster<double>> Getter = MakeUnique<PCGExData::TAttributeBroadcaster<double>>();
			if (!Getter->Prepare(Settings->SecondaryDistanceDetails.DistanceAttribute, PointIO))
			{
				PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Could not read secondary Distance attribute on some inputs."));
			}
			End = Getter->FetchSingle(PointIO->GetInPoint(EndIndex), 0);
		}
		else
		{
			End = Settings->SecondaryDistanceDetails.Distance;
		}
	}
}

void FPCGExShrinkPathContext::GetShrinkAmounts(const TSharedRef<PCGExData::FPointIO>& PointIO, int32& Start, int32& End) const
{
	PCGEX_SETTINGS_LOCAL(ShrinkPath)

	constexpr int32 StartIndex = 0;
	const int32 EndIndex = PointIO->GetNum() - 1;

	if (Settings->PrimaryCountDetails.ValueSource == EPCGExInputValueType::Attribute)
	{
		const TUniquePtr<PCGExData::TAttributeBroadcaster<int32>> Getter = MakeUnique<PCGExData::TAttributeBroadcaster<int32>>();
		if (!Getter->Prepare(Settings->PrimaryCountDetails.CountAttribute, PointIO))
		{
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Could not read primary Distance value attribute on some inputs."));
		}
		Start = Getter->FetchSingle(PointIO->GetInPoint(StartIndex), 0);
		End = Getter->FetchSingle(PointIO->GetInPoint(EndIndex), 0);
	}
	else
	{
		Start = End = Settings->PrimaryCountDetails.Count;
	}

	if (Settings->SettingsMode == EPCGExShrinkConstantMode::Separate)
	{
		if (Settings->SecondaryCountDetails.ValueSource == EPCGExInputValueType::Attribute)
		{
			const TUniquePtr<PCGExData::TAttributeBroadcaster<int32>> Getter = MakeUnique<PCGExData::TAttributeBroadcaster<int32>>();
			if (!Getter->Prepare(Settings->SecondaryCountDetails.CountAttribute, PointIO))
			{
				PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Could not read secondary Count attribute on some inputs."));
			}
			End = Getter->FetchSingle(PointIO->GetInPoint(EndIndex), 0);
		}
		else
		{
			End = Settings->SecondaryCountDetails.Count;
		}
	}

	Start = FMath::Clamp(Start, 0, MAX_int32);
	End = FMath::Clamp(End, 0, MAX_int32);
}

bool FPCGExShrinkPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ShrinkPath)

	if (Settings->ShrinkMode == EPCGExPathShrinkMode::Count)
	{
		if (!Settings->PrimaryCountDetails.SanityCheck(Context))
		{
			return false;
		}
		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::Both && Settings->SettingsMode == EPCGExShrinkConstantMode::Separate)
		{
			if (!Settings->SecondaryCountDetails.SanityCheck(Context))
			{
				return false;
			}
		}
	}
	else
	{
		if (!Settings->PrimaryDistanceDetails.SanityCheck(Context))
		{
			return false;
		}
		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::Both && Settings->SettingsMode == EPCGExShrinkConstantMode::Separate)
		{
			if (!Settings->SecondaryDistanceDetails.SanityCheck(Context))
			{
				return false;
			}
		}
	}

	return true;
}

bool FPCGExShrinkPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExShrinkPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ShrinkPath)
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
						PCGE_LOG(Warning, GraphAndLog, FTEXT("Some inputs are closed loop and cannot be shrinked. You must split them first."));
					}
					PCGEX_INIT_IO(Entry, PCGExData::EIOInit::Forward)
					return false;
				}

				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to shrink."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExShrinkPath
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExShrinkPath::Process);

		PointDataFacade->bSupportsScopedGet = false;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		NumPoints = PointDataFacade->GetNum();
		LastPointIndex = NumPoints - 1;

		const UPCGBasePointData* InData = PointDataFacade->Source->GetIn();

		NewStart = FPCGPoint(InData->GetTransform(0), 0, 0);
		NewStart.MetadataEntry = InData->GetMetadataEntry(0);

		NewEnd = FPCGPoint(InData->GetTransform(LastPointIndex), 0, 0);
		NewEnd.MetadataEntry = InData->GetMetadataEntry(LastPointIndex);

		Mask.Init(true, NumPoints);

		// Force-fetching if some filters are registered
		if (PrimaryFilters)
		{
			PointDataFacade->Fetch(PointDataFacade->GetInFullScope());
		}

		// Initialize stop conditions through filters
		FilterAll();

		// Update filter value for endpoints if we have a desired override
		if (Settings->bEndpointsIgnoreStopConditions)
		{
			PointFilterCache[0] = false;
			PointFilterCache.Last() = false;
		}

		if (Settings->ShrinkMode == EPCGExPathShrinkMode::Count)
		{
			ShrinkByCount();
		}
		else
		{
			ShrinkByDistance();
		}

		if (bUnaltered)
		{
			// No shrinkage occured on this path, just forward it.
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)
			return true;
		}

		int32 Remainder = 0;
		int32 StartIndex = MAX_int32;
		int32 EndIndex = 0;
		for (int32 i = 0; i < NumPoints; i++)
		{
			if (Mask[i])
			{
				Remainder++;
				StartIndex = FMath::Min(StartIndex, i);
				EndIndex = FMath::Max(EndIndex, i);
			}
		}


		// Clear "crossing" shrinks
		const double Dot = StartIndex < NumPoints ? FVector::DotProduct((PointDataFacade->GetIn()->GetTransform(StartIndex).GetLocation() - PointDataFacade->GetIn()->GetTransform(EndIndex).GetLocation()), (NewStart.Transform.GetLocation() - NewEnd.Transform.GetLocation())) : 1;

		if (Remainder < 2 || StartIndex == EndIndex || Dot < 0)
		{
			// No valid path is left for gathering, simply omit output.
			PointDataFacade->Source->Disable();
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)
		(void)PointDataFacade->Source->Gather(Mask);

		UPCGBasePointData* OutData = PointDataFacade->Source->GetOut();

		if (Settings->bPreserveFirstMetadata)
		{
			NewStart.MetadataEntry = InData->GetMetadataEntry(0);
		}
		if (Settings->bPreserveLastMetadata)
		{
			NewEnd.MetadataEntry = InData->GetMetadataEntry(LastPointIndex);
		}

		OutData->GetTransformValueRange(false)[0] = NewStart.Transform;
		OutData->GetMetadataEntryValueRange(false)[0] = NewStart.MetadataEntry;

		OutData->GetTransformValueRange(false)[OutData->GetNumPoints() - 1] = NewEnd.Transform;
		OutData->GetMetadataEntryValueRange(false)[OutData->GetNumPoints() - 1] = NewEnd.MetadataEntry;

		return true;
	}

	bool FProcessor::MaskIndex(const int32 Index)
	{
		if (!Mask[Index])
		{
			return false;
		}
		if (PointFilterCache[Index])
		{
			return false;
		}
		Mask[Index] = false;
		return true;
	}

	void FProcessor::ShrinkByCount()
	{
		int32 StartAmount = 0;
		int32 EndAmount = 0;

		Context->GetShrinkAmounts(PointDataFacade->Source, StartAmount, EndAmount);

		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::Start || PointFilterCache[LastPointIndex])
		{
			EndAmount = 0;
		}
		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::End || PointFilterCache[0])
		{
			StartAmount = 0;
		}

		// Just so we avoid wasting cycles... Not that this whole code is that efficient anyway
		StartAmount = FMath::Min(StartAmount, NumPoints);
		EndAmount = FMath::Min(EndAmount, NumPoints);

		if (StartAmount <= 0 && EndAmount <= 0)
		{
			bUnaltered = true;
			return;
		}

		int32 FromStartIndex = 0;
		int32 FromEndIndex = 0;

		auto ShrinkStep = [this](int32& Amount, int32& Index, const bool bReverse)
		{
			if (Amount <= 0)
			{
				return false;
			}
			if (MaskIndex(bReverse ? (LastPointIndex - Index) : Index))
			{
				++Index;
				--Amount;
				return true;
			}

			Amount = -1;
			return false;
		};

		while (StartAmount > 0 || EndAmount > 0)
		{
			ShrinkStep(StartAmount, FromStartIndex, false);
			ShrinkStep(EndAmount, FromEndIndex, true);
		}

		// TODO : Tag if amounts == -1 -- those have been stopped prematurely

		NewStart.Transform = PointDataFacade->GetIn()->GetTransform(FromStartIndex);
		NewStart.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(FromStartIndex);

		NewEnd.Transform = PointDataFacade->GetIn()->GetTransform(LastPointIndex - FromEndIndex);
		NewEnd.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(LastPointIndex - FromEndIndex);
	}

	void FProcessor::UpdateCut(FPCGPoint& Point, const int32 FromIndex, const int32 ToIndex, const double Dist, const EPCGExPathShrinkDistanceCutType Cut)
	{
		if (Cut == EPCGExPathShrinkDistanceCutType::NewPoint)
		{
			Mask[FromIndex] = true; // Restore point
			Point.Transform = PointDataFacade->GetIn()->GetTransform(FromIndex);
			Point.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(FromIndex);

			FVector From = Point.Transform.GetLocation();
			FVector To = PointDataFacade->GetIn()->GetTransform(ToIndex).GetLocation();

			Point.Transform.SetLocation(To + (From - To).GetSafeNormal() * Dist);
		}
		else if (Cut == EPCGExPathShrinkDistanceCutType::Previous)
		{
			Mask[FromIndex] = true; // Restore point
			Point.Transform = PointDataFacade->GetIn()->GetTransform(FromIndex);
			Point.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(FromIndex);
		}
		else if (Cut == EPCGExPathShrinkDistanceCutType::Next)
		{
			Mask[FromIndex] = false; // Force invalidation of "From" point to avoid two points overlapping
			Point.Transform = PointDataFacade->GetIn()->GetTransform(ToIndex);
			Point.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(ToIndex);
		}
		else if (Cut == EPCGExPathShrinkDistanceCutType::Closest)
		{
			if (Dist > FVector::Dist(PointDataFacade->GetIn()->GetTransform(FromIndex).GetLocation(), PointDataFacade->GetIn()->GetTransform(ToIndex).GetLocation()) * 0.5)
			{
				UpdateCut(Point, FromIndex, ToIndex, Dist, EPCGExPathShrinkDistanceCutType::Next);
			}
			else
			{
				UpdateCut(Point, FromIndex, ToIndex, Dist, EPCGExPathShrinkDistanceCutType::Previous);
			}
		}
	};

	void FProcessor::ShrinkByDistance()
	{
		double StartAmount = 0;
		double EndAmount = 0;

		EPCGExPathShrinkDistanceCutType StartCut;
		EPCGExPathShrinkDistanceCutType EndCut;

		Context->GetShrinkAmounts(PointDataFacade->Source, StartAmount, EndAmount, StartCut, EndCut);

		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::Start || PointFilterCache[LastPointIndex])
		{
			EndAmount = 0;
		}
		if (Settings->ShrinkEndpoint == EPCGExShrinkEndpoint::End || PointFilterCache[0])
		{
			StartAmount = 0;
		}

		if (StartAmount == 0 && EndAmount == 0)
		{
			bUnaltered = true;
			return;
		}

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		// Handle "reverse" shrink values first
		// Those are only extending the path along the existing normal

		if (StartAmount < 0)
		{
			const FVector Pos = NewStart.Transform.GetLocation();
			const FVector Direction = (InTransforms[1].GetLocation() - Pos).GetSafeNormal() * StartAmount;
			NewStart.Transform.SetLocation(Pos + Direction);
			StartAmount = 0;
		}

		if (EndAmount < 0)
		{
			const FVector Pos = NewEnd.Transform.GetLocation();
			const FVector Direction = (InTransforms[InTransforms.Num() - 2].GetLocation() - Pos).GetSafeNormal() * EndAmount;
			NewEnd.Transform.SetLocation(Pos + Direction);
			EndAmount = 0;
		}

		if (StartAmount == 0 && EndAmount == 0)
		{
			return;
		}


		PCGExPaths::FPathMetrics Metrics = PCGExPaths::FPathMetrics(InTransforms[0].GetLocation());
		TArray<double> DistFromStart;

		DistFromStart.Init(0, NumPoints);

		if (StartAmount <= 0)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				DistFromStart[i] = Metrics.Add(InTransforms[i].GetLocation());
			}
		}
		else
		{
			int32 StartIndex = -1;
			for (int i = 0; i < NumPoints; i++)
			{
				const double Dist = Metrics.Add(InTransforms[i].GetLocation());
				DistFromStart[i] = Dist;

				if (StartIndex != -1)
				{
					continue;
				}

				const double Remainder = Dist - StartAmount;
				if (Remainder >= 0)
				{
					// Stopped by distance
					StartIndex = i - 1;
					if (StartIndex >= 0)
					{
						UpdateCut(NewStart, StartIndex, i, Remainder, StartCut);
					}
				}
				else if (!MaskIndex(i))
				{
					// Stopped by inhability to mask, or filter
					StartIndex = i;
					NewStart.Transform = PointDataFacade->GetIn()->GetTransform(StartIndex);
					NewStart.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(StartIndex);
				}
			}
		}

		if (EndAmount != 0)
		{
			int32 EndIndex = -1;
			for (int i = LastPointIndex; i >= 0; i--)
			{
				const double Dist = Metrics.Length - DistFromStart[i];
				const double Remainder = Dist - EndAmount;

				if (Remainder >= 0)
				{
					// Stopped by distance
					EndIndex = i + 1;
					if (EndIndex <= LastPointIndex)
					{
						UpdateCut(NewEnd, EndIndex, i, Remainder, EndCut);
					}
				}
				else if (!MaskIndex(i))
				{
					// Stopped by inhability to mask, or filter
					EndIndex = i;
					NewEnd.Transform = PointDataFacade->GetIn()->GetTransform(EndIndex);
					NewEnd.MetadataEntry = PointDataFacade->GetIn()->GetMetadataEntry(EndIndex);
				}

				if (EndIndex != -1)
				{
					break;
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
