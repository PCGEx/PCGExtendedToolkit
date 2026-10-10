// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Types/PCGExTypeTraits.h"

class FJsonObject;
class FJsonValue;
class UEnum;

/**
 * THE value dialect, keyed by EPCGMetadataTypes, for every codec in the ecosystem:
 *   number              -> JSON number; an int64 beyond 2^53 is written as a string, both read back
 *   Vector2/Vector/Vector4 -> [x, y], [x, y, z], [x, y, z, w]   (an {x, y, z, w} object is also read)
 *   Rotator             -> {"pitch", "yaw", "roll"} in degrees   ([p, y, r] is also read)
 *   Quaternion          -> [x, y, z, w]                           (an {x, y, z, w} object is also read)
 *   any rotation slot reads either shape; a rotator shape converts to a quat and vice versa
 *   Transform           -> {"location": [..], "rotation": <quat>, "scale": [..]}; location / scale / rotation optional
 *   String / Name       -> string; "None" is NAME_None
 *   SoftObjectPath / SoftClassPath -> path string; "" is null
 * Decode failures report at the current diagnostics path and leave the output untouched.
 */
namespace PCGExMediator::Values
{
	PCGEXMEDIATOR_API bool IsSupported(EPCGMetadataTypes Type);

	/** Null for an unsupported type. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> Encode(EPCGMetadataTypes Type, const void* Value);

	/** False (and a diagnostic) on any shape or type mismatch; OutValue is then untouched. */
	PCGEXMEDIATOR_API bool Decode(const TSharedPtr<FJsonValue>& Json, EPCGMetadataTypes Type, void* OutValue);

	template <typename T>
	TSharedPtr<FJsonValue> Encode(const T& Value)
	{
		static_assert(PCGExTypes::TTraits<T>::Type != EPCGMetadataTypes::Unknown, "Encode<T>: T must be a PCG-supported metadata type.");
		return Encode(PCGExTypes::TTraits<T>::Type, &Value);
	}

	template <typename T>
	bool Decode(const TSharedPtr<FJsonValue>& Json, T& OutValue)
	{
		static_assert(PCGExTypes::TTraits<T>::Type != EPCGMetadataTypes::Unknown, "Decode<T>: T must be a PCG-supported metadata type.");
		return Decode(Json, PCGExTypes::TTraits<T>::Type, &OutValue);
	}

	/**
	 * Enumerator AUTHORED short name (a user-defined enum's label, as Epic's JSON tools write it); for a Bitflags
	 * enum whose value is not a single enumerator, an array of the set enumerators' names. A value no enumerator
	 * describes (or a null enum) is written as a number.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeEnum(const UEnum* Enum, int64 Value);

	/** Reads a name (authored or internal, short or Enum::Name), an array of names (OR-ed), or a number. Unknown name = error. */
	PCGEXMEDIATOR_API bool DecodeEnum(const UEnum* Enum, const TSharedPtr<FJsonValue>& Json, int64& OutValue);

	/** JSON Schema fragment describing the dialect shape of Type; null for an unsupported type. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeShape(EPCGMetadataTypes Type);

	/** JSON Schema fragment for an enum value: the enumerator names, with the array form for Bitflags. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeEnumShape(const UEnum* Enum);

	/** Dialect name of a type, e.g. "Vector", "SoftObjectPath"; "Unknown" when unsupported. */
	PCGEXMEDIATOR_API FString TypeToString(EPCGMetadataTypes Type);
}
