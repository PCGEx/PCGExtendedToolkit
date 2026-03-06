// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSettingsCacheBody.h"

#include "PCGExBlendingSettingsCache.generated.h"

class UPCGPin;

UENUM()
enum class EPCGExBlendingTypeDefault : uint8
{
	Default          = 100 UMETA(DisplayName = "Default", ToolTip="Use the node' default"),
	None             = 0 UMETA(DisplayName = "None", ToolTip="No blending is applied, keep the original value."),
	Average          = 1 UMETA(DisplayName = "Average", ToolTip="Average all sampled values."),
	Weight           = 2 UMETA(DisplayName = "Weight", ToolTip="Weights based on distance to blend targets. If the results are unexpected, try 'Lerp' instead"),
	Min              = 3 UMETA(DisplayName = "Min", ToolTip="Component-wise MIN operation"),
	Max              = 4 UMETA(DisplayName = "Max", ToolTip="Component-wise MAX operation"),
	Copy             = 5 UMETA(DisplayName = "Copy (Target)", ToolTip = "Copy target data (second value)"),
	Sum              = 6 UMETA(DisplayName = "Sum", ToolTip = "Sum"),
	WeightedSum      = 7 UMETA(DisplayName = "Weighted Sum", ToolTip = "Sum of all the data, weighted"),
	Lerp             = 8 UMETA(DisplayName = "Lerp", ToolTip="Uses weight as lerp. If the results are unexpected, try 'Weight' instead."),
	Subtract         = 9 UMETA(DisplayName = "Subtract", ToolTip="Subtract."),
	UnsignedMin      = 10 UMETA(DisplayName = "Unsigned Min", ToolTip="Component-wise MIN on unsigned value, but keeps the sign on written data."),
	UnsignedMax      = 11 UMETA(DisplayName = "Unsigned Max", ToolTip="Component-wise MAX on unsigned value, but keeps the sign on written data."),
	AbsoluteMin      = 12 UMETA(DisplayName = "Unsigned Min", ToolTip="Component-wise MIN on unsigned value, but keeps the sign on written data."),
	AbsoluteMax      = 13 UMETA(DisplayName = "Unsigned Max", ToolTip="Component-wise MAX on unsigned value, but keeps the sign on written data."),
	WeightedSubtract = 14 UMETA(DisplayName = "Weighted Subtract", ToolTip="Substraction of all the data, weighted"),
	CopyOther        = 15 UMETA(DisplayName = "Copy (Source)", ToolTip="Copy source data (first value)"),
	Hash             = 16 UMETA(DisplayName = "Hash", ToolTip="Combine the values into a hash"),
	UnsignedHash     = 17 UMETA(DisplayName = "Hash (Sorted)", ToolTip="Combine the values into a hash but sort the values first to create an order-independent hash."),
};

#define PCGEX_BLENDING_SETTINGS PCGEX_SETTINGS_INST(Blending)

struct PCGEXBLENDING_API FPCGExBlendingSettingsCache
{
	PCGEX_SETTING_CACHE_BODY(Blending)
	EPCGExBlendingTypeDefault DefaultBooleanBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultFloatBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultDoubleBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultInteger32BlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultInteger64BlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultVector2BlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultVectorBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultVector4BlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultQuaternionBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultTransformBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultRotatorBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultStringBlendMode = EPCGExBlendingTypeDefault::Copy;
	EPCGExBlendingTypeDefault DefaultNameBlendMode = EPCGExBlendingTypeDefault::Copy;
	EPCGExBlendingTypeDefault DefaultSoftObjectPathBlendMode = EPCGExBlendingTypeDefault::Copy;
	EPCGExBlendingTypeDefault DefaultSoftClassPathBlendMode = EPCGExBlendingTypeDefault::Copy;
	EPCGExBlendingTypeDefault DefaultByteBlendMode = EPCGExBlendingTypeDefault::Default;
	EPCGExBlendingTypeDefault DefaultTextBlendMode = EPCGExBlendingTypeDefault::Copy;
};
