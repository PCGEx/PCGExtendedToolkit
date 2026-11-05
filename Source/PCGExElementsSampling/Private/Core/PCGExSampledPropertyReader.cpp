// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExSampledPropertyReader.h"

#include "Data/PCGExSubSelection.h"
#include "Helpers/PCGExMetaHelpers.h"

namespace PCGExSampling
{
	bool FSampledPropertyReader::Init(const FPCGAttributePropertyInputSelector& InSelector, const EPCGMetadataTypes InWorkingType)
	{
		if (InSelector.GetSelection() != EPCGAttributePropertySelection::Property || !InSelector.GetDomainName().IsNone())
		{
			return false;
		}

		return Init(InSelector.GetPointProperty(), InSelector.GetExtraNames(), InWorkingType);
	}

	bool FSampledPropertyReader::Init(const EPCGPointProperties InProperty, const TArray<FString>& InExtraNames, const EPCGMetadataTypes InWorkingType)
	{
		// Unknown for Invalid and for properties a point does not carry.
		const EPCGMetadataTypes RealType = PCGExMetaHelpers::GetPropertyType(InProperty);
		if (RealType == EPCGMetadataTypes::Unknown)
		{
			return false;
		}

		Property = InProperty;

		// The compiled chain tells what the extractors produce; recompile to convert to that, or to the requested type.
		const PCGExData::FSubSelection Selection(InExtraNames);
		SubSelection.Initialize(Selection, RealType, RealType);

		const EPCGMetadataTypes OutputType = InWorkingType == EPCGMetadataTypes::Unknown ? SubSelection.FinalChainType : InWorkingType;
		if (OutputType != RealType)
		{
			SubSelection.Initialize(Selection, RealType, OutputType);
		}

		return true;
	}

	void FSampledPropertyReader::Read(const FPCGPoint& InPoint, void* OutValue) const
	{
		switch (Property)
		{
		case EPCGPointProperties::Density:
			Extract(InPoint.Density, OutValue);
			break;
		case EPCGPointProperties::BoundsMin:
			Extract(InPoint.BoundsMin, OutValue);
			break;
		case EPCGPointProperties::BoundsMax:
			Extract(InPoint.BoundsMax, OutValue);
			break;
		case EPCGPointProperties::Extents:
			Extract(InPoint.GetExtents(), OutValue);
			break;
		case EPCGPointProperties::Color:
			Extract(InPoint.Color, OutValue);
			break;
		case EPCGPointProperties::Position:
			Extract(InPoint.Transform.GetLocation(), OutValue);
			break;
		case EPCGPointProperties::Rotation:
			Extract(InPoint.Transform.GetRotation(), OutValue);
			break;
		case EPCGPointProperties::Scale:
			Extract(InPoint.Transform.GetScale3D(), OutValue);
			break;
		case EPCGPointProperties::Transform:
			Extract(InPoint.Transform, OutValue);
			break;
		case EPCGPointProperties::Steepness:
			Extract(InPoint.Steepness, OutValue);
			break;
		case EPCGPointProperties::LocalCenter:
			Extract(InPoint.GetLocalCenter(), OutValue);
			break;
		case EPCGPointProperties::Seed:
			Extract(InPoint.Seed, OutValue);
			break;
		case EPCGPointProperties::LocalSize:
			Extract(InPoint.GetLocalSize(), OutValue);
			break;
		case EPCGPointProperties::ScaledLocalSize:
			Extract(InPoint.GetScaledLocalSize(), OutValue);
			break;
		default:
			// Init refuses every other property.
			checkNoEntry();
			break;
		}
	}
}
