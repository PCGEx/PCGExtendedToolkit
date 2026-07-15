// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "Engine/EngineTypes.h"

#include "PCGExCommon.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExUVW.generated.h"

struct FPCGExContext;
class UPCGBasePointData;
class UPCGSettings;
class UPCGNode;
enum class EPCGExMinimalAxis : uint8;

namespace PCGExData
{
	struct FConstPoint;
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExUVW
{
	GENERATED_BODY()

	FPCGExUVW()
	{
	}

	explicit FPCGExUVW(const double DefaultW)
		: W(FName("@Last"), DefaultW, false)
	{
	}

	/** Which bounds to use for UVW coordinate calculations. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsReference = EPCGExPointBoundsSource::ScaledBounds;

	/** U coordinate within bounds (0 = min X, 1 = max X). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="U"))
	FPCGExInputShorthandSelectorDouble U = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0, false);

	/** V coordinate within bounds (0 = min Y, 1 = max Y). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="V"))
	FPCGExInputShorthandSelectorDouble V = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0, false);

	/** W coordinate within bounds (0 = min Z, 1 = max Z). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="W"))
	FPCGExInputShorthandSelectorDouble W = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType UInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector UAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double UConstant_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType VInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector VAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double VConstant_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType WInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector WAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double WConstant_DEPRECATED = 0;

#pragma endregion

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade);

	// Without axis

	FVector GetUVW(const int32 PointIndex) const;

	FVector GetPosition(const int32 PointIndex) const;

	FVector GetPosition(const int32 PointIndex, FVector& OutOffset) const;

	// With axis

	FVector GetUVW(const int32 PointIndex, const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;

	FVector GetPosition(const int32 PointIndex, const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;

	FVector GetPosition(const int32 PointIndex, FVector& OutOffset, const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;

#if WITH_EDITOR
	void ApplyDeprecation();
	/** Rewires the pre-shorthand override pins; call from the embedder's PCGExApplyDeprecationBeforeUpdatePins under the same version gate as ApplyDeprecation. */
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif

protected:
	TSharedPtr<PCGExDetails::TSettingValue<double>> UGetter;
	TSharedPtr<PCGExDetails::TSettingValue<double>> VGetter;
	TSharedPtr<PCGExDetails::TSettingValue<double>> WGetter;

	const UPCGBasePointData* PointData = nullptr;
};

namespace PCGExMath
{
	struct PCGEXCORE_API FPCGExConstantUVW
	{
		FPCGExConstantUVW()
		{
		}

		EPCGExPointBoundsSource BoundsReference = EPCGExPointBoundsSource::ScaledBounds;
		double U = 0;
		double V = 0;
		double W = 0;

		FORCEINLINE FVector GetUVW() const
		{
			return FVector(U, V, W);
		}

		FVector GetPosition(const PCGExData::FConstPoint& Point) const;

		FVector GetPosition(const PCGExData::FConstPoint& Point, FVector& OutOffset) const;

		// With axis

		FVector GetUVW(const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;

		FVector GetPosition(const PCGExData::FConstPoint& Point, const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;

		FVector GetPosition(const PCGExData::FConstPoint& Point, FVector& OutOffset, const EPCGExMinimalAxis Axis, const bool bMirrorAxis = false) const;
	};
}
