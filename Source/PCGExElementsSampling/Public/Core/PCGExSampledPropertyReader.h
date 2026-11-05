// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPoint.h"
#include "Data/PCGExCachedSubSelection.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Types/PCGExTypeTraits.h"

namespace PCGExSampling
{
	/** Reads one property of a sampled point ($Color, $Density, $Rotation...) through the selector's extractors, such as
	 *  $Color.R or $Position.Z. Set up once; Read is const and safe from any thread. */
	class PCGEXELEMENTSSAMPLING_API FSampledPropertyReader
	{
	public:
		FSampledPropertyReader() = default;
		FSampledPropertyReader(const FSampledPropertyReader&) = delete;
		FSampledPropertyReader& operator=(const FSampledPropertyReader&) = delete;
		FSampledPropertyReader(FSampledPropertyReader&&) = default;
		FSampledPropertyReader& operator=(FSampledPropertyReader&&) = default;

		/** False unless the selector names a point property, without a domain. Read writes InWorkingType; Unknown keeps
		 *  the type the extractors produce, such as a double for $Color.R and a vector 4 for $Color. */
		bool Init(const FPCGAttributePropertyInputSelector& InSelector, EPCGMetadataTypes InWorkingType = EPCGMetadataTypes::Unknown);
		bool Init(EPCGPointProperties InProperty, const TArray<FString>& InExtraNames, EPCGMetadataTypes InWorkingType = EPCGMetadataTypes::Unknown);

		EPCGPointProperties GetProperty() const { return Property; }

		/** The type Read writes. */
		EPCGMetadataTypes GetOutputType() const { return SubSelection.WorkingType; }

		/** Writes the property, extracted and converted, to OutValue, which must hold the output type. */
		void Read(const FPCGPoint& InPoint, void* OutValue) const;

		template <typename T>
		T Read(const FPCGPoint& InPoint) const
		{
			checkSlow(PCGExTypes::TTraits<T>::Type == GetOutputType());
			T Value{};
			Read(InPoint, &Value);
			return Value;
		}

	private:
		EPCGPointProperties Property = EPCGPointProperties::Invalid;
		PCGExData::FCachedSubSelection SubSelection;

		/** InValue must be the type PCGExMetaHelpers::GetPropertyType reports for Property, which Init compiled for. */
		template <typename T>
		void Extract(const T& InValue, void* OutValue) const
		{
			SubSelection.ApplyGet(&InValue, OutValue);
		}
	};
}
