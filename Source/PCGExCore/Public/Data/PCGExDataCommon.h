// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.generated.h"

namespace PCGExData
{
	template <typename T>
	class TDataValue;
}

using PCGExValueHash = uint32;
using PCGExDataId = TSharedPtr<PCGExData::TDataValue<int64>>;

UENUM()
enum class EPCGExInputValueType : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Use a constant, user-defined value.", ActionIcon="Constant"),
	Attribute = 1 UMETA(DisplayName = "Attribute", Tooltip="Read the value from the input data.", ActionIcon="Attribute"),
};

UENUM()
enum class EPCGExDataInputValueType : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Constant.", ActionIcon="Constant"),
	Attribute = 1 UMETA(DisplayName = "@Data", Tooltip="Attribute. Can only read from @Data domain.", ActionIcon="DataAttribute"),
};

UENUM(BlueprintType)
enum class EPCGExNumericOutput : uint8
{
	Double = 0,
	Float  = 1,
	Int32  = 2,
	Int64  = 3,
};

UENUM()
enum class EPCGExAttributeDomainScope : uint8
{
	Any      = 0 UMETA(DisplayName = "Any", Tooltip="Any domain"),
	Data     = 1 UMETA(DisplayName = "Data", Tooltip="Data domain only"),
	Elements = 2 UMETA(DisplayName = "Elements", Tooltip="Elements domain only"),
};

namespace PCGExData
{
	enum class EIOInit : uint8
	{
		// No Output
		NoInit,
		// Create Empty Output Object
		New,
		// Duplicate Input Object
		Duplicate,
		//Forward Input Object
		Forward,
	};

	enum class EIOSide : uint8
	{
		In,
		Out
	};

	// How a collection treats non-point inputs. Points skips them; the other two convert them
	// to temp point data for processing (see PCGExPointIO::ToPointData). Transactional restages the
	// untouched original (StageAnyOutput); Dynamic leaves output staging to the node, per input type.
	enum class EIOHandling : uint8
	{
		Points,
		Transactional,
		Dynamic,
	};

	enum class EStaging : uint8
	{
		None              = 0,
		Managed           = 1 << 0,
		Mutable           = 1 << 1,
		Pinless           = 1 << 2,
		MutableAndManaged = Managed | Mutable
	};

	ENUM_CLASS_FLAGS(EStaging)
}
