// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExMeanFilter.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExMeanFilterDefinition"
#define PCGEX_NAMESPACE MeanFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExMeanFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FMeanFilter>(this);
}

void UPCGExMeanFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	//FacadePreloader.Register<double>(InContext, Config.Target); // TODO SUPPORT MIN MAX FETCH
}

bool UPCGExMeanFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.Target, Consumable)

	return true;
}

bool PCGExPointFilter::FMeanFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	const TSharedPtr<PCGExData::TBuffer<double>> Buffer = PointDataFacade->GetBroadcaster<double>(TypedFilterFactory->Config.Target, false, true, PCGEX_QUIET_HANDLING);

	if (!Buffer)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Target, TypedFilterFactory->Config.Target)
		return false;
	}

	DataMin = Buffer->Min;
	DataMax = Buffer->Max;

	bInvert = TypedFilterFactory->Config.bInvert;

	Values.SetNumUninitialized(InPointDataFacade->GetNum());
	Buffer->DumpValues(Values);

	return true;
}

void PCGExPointFilter::FMeanFilter::PostInit()
{
	const int32 NumPoints = PointDataFacade->Source->GetNum();
	Results.Init(false, NumPoints);

	double SumValue = 0;
	for (int i = 0; i < NumPoints; i++)
	{
		SumValue += Values[i];
	}

	// Normalize values to [0..1] range by dividing by DataMax, mutating Values in-place.
	// DataMin/DataMax are recomputed from normalized values for subsequent mean calculation.
	if (TypedFilterFactory->Config.Measure == EPCGExMeanMeasure::Relative)
	{
		double RelativeMinEdgeLength = TNumericLimits<double>::Max();
		double RelativeMaxEdgeLength = TNumericLimits<double>::Min();
		SumValue = 0;
		for (int i = 0; i < NumPoints; i++)
		{
			const double Normalized = (Values[i] /= DataMax);
			RelativeMinEdgeLength = FMath::Min(Normalized, RelativeMinEdgeLength);
			RelativeMaxEdgeLength = FMath::Max(Normalized, RelativeMaxEdgeLength);
			SumValue += Normalized;
		}
		DataMin = RelativeMinEdgeLength;
		DataMax = RelativeMaxEdgeLength;
	}

	switch (TypedFilterFactory->Config.MeanMethod)
	{
	default: case EPCGExMeanMethod::Average:
		ReferenceValue = SumValue / NumPoints;
		break;
	case EPCGExMeanMethod::Median:
		ReferenceValue = PCGExMath::GetMedian(Values);
		break;
	case EPCGExMeanMethod::Fixed:
		ReferenceValue = TypedFilterFactory->Config.MeanValue;
		break;
	case EPCGExMeanMethod::ModeMin:
		ReferenceValue = PCGExMath::GetMode(Values, false, TypedFilterFactory->Config.ModeTolerance);
		break;
	case EPCGExMeanMethod::ModeMax:
		ReferenceValue = PCGExMath::GetMode(Values, true, TypedFilterFactory->Config.ModeTolerance);
		break;
	case EPCGExMeanMethod::Central:
		ReferenceValue = DataMin + (DataMax - DataMin) * 0.5;
		break;
	}

	// Build the acceptable range around the reference mean. Disabled sides default to +/-infinity.
	const double RMin = TypedFilterFactory->Config.bDoExcludeBelowMean ? ReferenceValue - TypedFilterFactory->Config.ExcludeBelow : TNumericLimits<double>::Lowest();
	const double RMax = TypedFilterFactory->Config.bDoExcludeAboveMean ? ReferenceValue + TypedFilterFactory->Config.ExcludeAbove : TNumericLimits<double>::Max();

	// Min/Max swap ensures ReferenceMin <= ReferenceMax regardless of which sides are enabled.
	ReferenceMin = FMath::Min(RMin, RMax);
	ReferenceMax = FMath::Max(RMin, RMax);
}

// Pass if value falls within the acceptable band around the mean; outliers fail.
bool PCGExPointFilter::FMeanFilter::Test(const int32 PointIndex) const
{
	return FMath::IsWithin(Values[PointIndex], ReferenceMin, ReferenceMax) ? !bInvert : bInvert;
}

PCGEX_CREATE_FILTER_FACTORY(Mean)

#if WITH_EDITOR
FString UPCGExMeanFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = "";

	if (Config.bDoExcludeBelowMean)
	{
		DisplayName += FString::Printf(TEXT("< %.3f "), (static_cast<int32>(1000 * Config.ExcludeBelow) / 1000.0));
	}
	if (Config.bDoExcludeBelowMean && Config.bDoExcludeAboveMean)
	{
		DisplayName += "&& ";
	}
	if (Config.bDoExcludeAboveMean)
	{
		DisplayName += FString::Printf(TEXT("> %.3f "), (static_cast<int32>(1000 * Config.ExcludeAbove) / 1000.0));
	}

	DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.Target) + "' ";

	switch (Config.MeanMethod)
	{
	case EPCGExMeanMethod::Average:
		DisplayName += "' Average";
		break;
	case EPCGExMeanMethod::Median:
		DisplayName += "' Median";
		break;
	case EPCGExMeanMethod::ModeMin:
		DisplayName += "' Mode (min)";
		break;
	case EPCGExMeanMethod::ModeMax:
		DisplayName += "' Mode (max)";
		break;
	case EPCGExMeanMethod::Central:
		DisplayName += "' Central";
		break;
	case EPCGExMeanMethod::Fixed:
		DisplayName += FString::Printf(TEXT(" %.3f"), (static_cast<int32>(1000 * Config.MeanValue) / 1000.0));
		break;
	default: ;
	}

	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
