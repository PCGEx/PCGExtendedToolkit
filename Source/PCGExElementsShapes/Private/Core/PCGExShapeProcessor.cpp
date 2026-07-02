// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExShapeProcessor.h"

#include "Core/PCGExPointFilter.h"

#define LOCTEXT_NAMESPACE "PCGExShapeProcessorElement"

UPCGExShapeProcessorSettings::UPCGExShapeProcessorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TArray<FPCGPinProperties> UPCGExShapeProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (!IsInputless())
	{
		if (GetMainAcceptMultipleData())
		{
			PCGEX_PIN_POINTS(GetMainInputPin(), "The point data to be processed.", Required)
		}
		else
		{
			PCGEX_PIN_POINT(GetMainInputPin(), "The point data to be processed.", Required)
		}
	}

	PCGEX_PIN_FACTORIES(PCGExShapes::Labels::SourceShapeBuildersLabel, "Shape builders that will be used by this element.", Required, FPCGExDataTypeInfoShape::AsId())

	if (SupportsPointFilters())
	{
		if (RequiresPointFilters())
		{
			PCGEX_PIN_FILTERS(GetPointFilterPin(), GetPointFilterTooltip(), Required)
		}
		else
		{
			PCGEX_PIN_FILTERS(GetPointFilterPin(), GetPointFilterTooltip(), Normal)
		}
	}

	return PinProperties;
}

FName UPCGExShapeProcessorSettings::GetMainInputPin() const
{
	return PCGExCommon::Labels::SourceSeedsLabel;
}

bool FPCGExShapeProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ShapeProcessor)

	if (!PCGExFactories::GetInputFactories(Context, PCGExShapes::Labels::SourceShapeBuildersLabel, Context->BuilderFactories, {FPCGExDataTypeInfoShape::AsId()}))
	{
		return false;
	}

	return true;
}


#undef LOCTEXT_NAMESPACE
