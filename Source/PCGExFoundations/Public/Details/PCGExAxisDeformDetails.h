// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "Engine/EngineTypes.h"

#include "Core/PCGExContext.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExAxisDeformDetails.generated.h"

namespace PCGExData
{
	class FFacade;
}

struct FPCGExTaggedData;

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}


UENUM()
enum class EPCGExTransformAlphaUsage : uint8
{
	StartAndEnd   = 0 UMETA(DisplayName = "Start & End", Tooltip="First alpha is to be used as start % along the axis, and second alpha is the end % along that same axis."),
	StartAndSize  = 1 UMETA(DisplayName = "Start & Size", Tooltip="First alpha is to be used as start % along the axis, and second alpha is a % of the axis length, from first alpha."),
	CenterAndSize = 2 UMETA(DisplayName = "Center & Size", Tooltip="First alpha is to be used as center % along the axis, and second alpha is a % of the axis length, before and after the center.")
};

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExAxisDeformDetails
{
	GENERATED_BODY()

	FPCGExAxisDeformDetails() = default;
	FPCGExAxisDeformDetails(const FString InFirst, const FString InSecond, const double InFirstValue = 0, const double InSecondValue = 1);

	/** How the two alpha values define the deformation range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExTransformAlphaUsage Usage = EPCGExTransformAlphaUsage::StartAndEnd;

	/** Where first alpha value comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSampleSource FirstAlphaInput = EPCGExSampleSource::Constant;

	/** Attribute to read start value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="First Alpha (Attr)", EditCondition="FirstAlphaInput != EPCGExSampleSource::Constant", EditConditionHides))
	FName FirstAlphaAttribute = FName("@Data.FirstAlpha");

	/** Constant start value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="First Alpha", EditCondition="FirstAlphaInput == EPCGExSampleSource::Constant", EditConditionHides))
	double FirstAlphaConstant = 0;

	PCGEX_SETTING_DATA_VALUE_DECL(FirstAlpha, double)
	PCGEX_SETTING_VALUE_DECL(FirstAlpha, double)

	/** Where second alpha value comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSampleSource SecondAlphaInput = EPCGExSampleSource::Constant;

	/** Attribute to read end value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Second Alpha (Attr)", EditCondition="SecondAlphaInput != EPCGExSampleSource::Constant", EditConditionHides))
	FName SecondAlphaAttribute = FName("@Data.SecondAlpha");

	/** Constant end value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Second Alpha", EditCondition="SecondAlphaInput == EPCGExSampleSource::Constant", EditConditionHides))
	double SecondAlphaConstant = 1;

	PCGEX_SETTING_DATA_VALUE_DECL(SecondAlpha, double)
	PCGEX_SETTING_VALUE_DECL(SecondAlpha, double)

	bool Validate(FPCGExContext* InContext, const bool bSupportPoints = false) const;

	bool Init(FPCGExContext* InContext, const TArray<FPCGExTaggedData>& InTargets);
	bool Init(FPCGExContext* InContext, const FPCGExAxisDeformDetails& Parent, const TSharedRef<PCGExData::FFacade>& InDataFacade, const int32 InTargetIndex, const bool bSupportPoint = false);

	void GetAlphas(const int32 Index, double& OutFirst, double& OutSecond, const bool bSort = true) const;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<double>> FirstValueGetter;
	TSharedPtr<PCGExDetails::TSettingValue<double>> SecondValueGetter;

	TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> TargetsFirstValueGetter;
	TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> TargetsSecondValueGetter;
};


USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExAxisTwistDetails
{
	GENERATED_BODY()

	// Start/End
	// or
	// Per-point angle
};
