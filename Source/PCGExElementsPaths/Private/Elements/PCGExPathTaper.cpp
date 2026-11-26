// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathTaper.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPathTaperElement"
#define PCGEX_NAMESPACE PathTaper

#pragma region UPCGExPathTaperSettings

PCGEX_INITIALIZE_ELEMENT(PathTaper)

PCGExData::EIOInit UPCGExPathTaperSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

#pragma endregion

#pragma region FPCGExPathTaperContext

PCGEX_ELEMENT_BATCH_POINT_IMPL(PathTaper)

#pragma endregion

#pragma region FPCGExPathTaperElement

bool FPCGExPathTaperElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PathTaper)

	if (Settings->Output.GetSelection() == EPCGAttributePropertySelection::ExtraProperty)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Output cannot be an extra property such as $Index: they are read-only."));
		return false;
	}

	if (Settings->Output.GetSelection() == EPCGAttributePropertySelection::Attribute)
	{
		// One value per path cannot hold a per-point taper.
		if (PCGExMetaHelpers::IsDataDomainAttribute(Settings->Output))
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Output must be a point attribute or property, not @Data."));
			return false;
		}

		PCGEX_VALIDATE_NAME(Settings->Output.GetAttributeName())
	}

	switch (Settings->LengthMeasure)
	{
	case EPCGExMeanMeasure::Relative:
	case EPCGExMeanMeasure::Discrete:
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, FTEXT("Unsupported length measure."));
		return false;
	}

	switch (Settings->Overlap)
	{
	case EPCGExPathTaperOverlap::Fit:
	case EPCGExPathTaperOverlap::Min:
	case EPCGExPathTaperOverlap::Multiply:
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, FTEXT("Unsupported overlap mode."));
		return false;
	}

	switch (Settings->BlendMode)
	{
	case EPCGExPathTaperBlend::None:
	case EPCGExPathTaperBlend::Add:
	case EPCGExPathTaperBlend::Multiply:
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, FTEXT("Unsupported blend mode."));
		return false;
	}

	Context->StartCurve = Settings->Curve.MakeLookup(EPCGExCurveLUTMode::Lookup, 256);
	Context->EndCurve = Settings->bUseEndCurve ? Settings->EndCurve.MakeLookup(EPCGExCurveLUTMode::Lookup, 256) : Context->StartCurve;

	return true;
}

bool FPCGExPathTaperElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathTaperElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathTaper)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				PCGEX_SKIP_INVALID_PATH_ENTRY
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to taper."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

#pragma endregion

namespace PCGExPathTaper
{
	FSpans ResolveSpans(const double InStart, const double InEnd, const double InPathLength, const EPCGExMeanMeasure InMeasure, const EPCGExPathTaperOverlap InOverlap)
	{
		FSpans ResolvedSpans;
		ResolvedSpans.Start = FMath::Max(InStart, 0.0);
		ResolvedSpans.End = FMath::Max(InEnd, 0.0);

		if (InMeasure == EPCGExMeanMeasure::Relative)
		{
			ResolvedSpans.Start *= InPathLength;
			ResolvedSpans.End *= InPathLength;
		}

		if (InOverlap == EPCGExPathTaperOverlap::Fit)
		{
			const double SpanSum = ResolvedSpans.Start + ResolvedSpans.End;
			if (SpanSum > InPathLength)
			{
				const double FitScale = InPathLength / SpanSum;
				ResolvedSpans.Start *= FitScale;
				ResolvedSpans.End *= FitScale;
			}
		}

		return ResolvedSpans;
	}

#pragma region FProcessor

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathTaper::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		PCGExData::FProxyDescriptor OutputDescriptor(PointDataFacade, PCGExData::EProxyRole::Write);
		const bool bTargetExists = OutputDescriptor.Capture(Context, Settings->Output, PCGExData::EIOSide::Out, false);
		if (!bTargetExists)
		{
			// A missing target is created as a double attribute, which has no component to write.
			if (OutputDescriptor.SubSelection.HasSelection())
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(FTEXT("\"{0}\" does not exist: a component can only be written on an existing attribute."), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(Settings->Output))));
				return false;
			}

			OutputDescriptor.RealType = EPCGMetadataTypes::Double;
		}

		OutputDescriptor.WorkingType = EPCGMetadataTypes::Double;

		// A target attribute that doesn't exist yet has no value to blend with.
		BlendMode = bTargetExists ? Settings->BlendMode : EPCGExPathTaperBlend::None;
		if (BlendMode != EPCGExPathTaperBlend::None)
		{
			// Reader before writer, as Attribute Remap does when its source and target are the same attribute.
			PCGExData::FProxyDescriptor TargetDescriptor(PointDataFacade, PCGExData::EProxyRole::Read);
			if (!TargetDescriptor.Capture(Context, Settings->Output, PCGExData::EIOSide::In, true))
			{
				return false;
			}

			TargetDescriptor.WorkingType = EPCGMetadataTypes::Double;

			TargetReader = PCGExData::GetProxyBuffer(Context, TargetDescriptor);
			if (!TargetReader)
			{
				return false;
			}
		}

		OutputProxy = PCGExData::GetProxyBuffer(Context, OutputDescriptor);
		if (!OutputProxy)
		{
			return false;
		}

		ScaleGetter = Settings->Scale.GetValueSetting();
		if (!ScaleGetter->Init(PointDataFacade))
		{
			return false;
		}

		OffsetGetter = Settings->Offset.GetValueSetting();
		if (!OffsetGetter->Init(PointDataFacade))
		{
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		const bool bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->Source);

		if (!bClosedLoop)
		{
			const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();
			PCGExPaths::FPathMetrics Metrics(InTransforms[0].GetLocation());

			DistToStart.SetNumUninitialized(NumPoints);
			for (int32 i = 0; i < NumPoints; i++)
			{
				DistToStart[i] = Metrics.Add(InTransforms[i].GetLocation());
			}

			PathLength = Metrics.Length;
		}

		if (bClosedLoop || PathLength <= UE_KINDA_SMALL_NUMBER)
		{
			ClosedLoopWeightGetter = Settings->ClosedLoopWeight.GetValueSetting();
			if (!ClosedLoopWeightGetter->Init(PointDataFacade))
			{
				return false;
			}
		}
		else
		{
			double StartLength = 0;
			double EndLength = 0;

			// Unscoped: only the endpoint values are read, and before the parallel loop starts.
			if (Settings->bTaperStart)
			{
				const TSharedPtr<PCGExDetails::TSettingValue<double>> StartGetter = Settings->StartLength.GetValueSetting();
				if (!StartGetter->Init(PointDataFacade, false))
				{
					return false;
				}
				StartLength = StartGetter->Read(0);
			}

			if (Settings->bTaperEnd)
			{
				const TSharedPtr<PCGExDetails::TSettingValue<double>> EndGetter = Settings->EndLength.GetValueSetting();
				if (!EndGetter->Init(PointDataFacade, false))
				{
					return false;
				}
				EndLength = EndGetter->Read(NumPoints - 1);
			}

			Spans = ResolveSpans(StartLength, EndLength, PathLength, Settings->LengthMeasure, Settings->Overlap);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::PathTaper::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		if (ClosedLoopWeightGetter)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				WriteValue(Index, ClosedLoopWeightGetter->Read(Index) * ScaleGetter->Read(Index) + OffsetGetter->Read(Index));
			}
			return;
		}

		const FPCGExCurveFloatLookup& StartLUT = *Context->StartCurve;
		const FPCGExCurveFloatLookup& EndLUT = *Context->EndCurve;
		const EPCGExPathTaperOverlap OverlapMode = Settings->Overlap;

		PCGEX_SCOPE_LOOP(Index)
		{
			const double TaperWeight = ComputeWeight(DistToStart[Index], PathLength, Spans, StartLUT, EndLUT, OverlapMode);
			WriteValue(Index, TaperWeight * ScaleGetter->Read(Index) + OffsetGetter->Read(Index));
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::WriteValue(const int32 Index, const double Value) const
	{
		switch (BlendMode)
		{
		case EPCGExPathTaperBlend::None:
			OutputProxy->Set(Index, Value);
			break;
		case EPCGExPathTaperBlend::Add:
			OutputProxy->Set(Index, TargetReader->Get<double>(Index) + Value);
			break;
		case EPCGExPathTaperBlend::Multiply:
			OutputProxy->Set(Index, TargetReader->Get<double>(Index) * Value);
			break;
		default:
			// Boot rejects any other value.
			checkNoEntry();
			break;
		}
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
