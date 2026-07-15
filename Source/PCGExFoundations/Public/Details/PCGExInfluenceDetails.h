// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExInfluenceDetails.generated.h"

struct FPCGExContext;
class UPCGSettings;
class UPCGNode;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExInfluenceDetails
{
	GENERATED_BODY()

	FPCGExInfluenceDetails()
	{
	}

	/** How much effect is applied. Range is -1 to 1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Influence"))
	FPCGExInputShorthandSelectorDouble11 InfluenceValue = FPCGExInputShorthandSelectorDouble11(FName("@Last"), 1.0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType InfluenceInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalInfluence_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Influence_DEPRECATED = 1.0;

#pragma endregion

	/** If enabled, applies influence after each iteration; otherwise applies once at the end of the relaxing.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bProgressiveInfluence = true;

	TSharedPtr<PCGExDetails::TSettingValue<double>> InfluenceBuffer;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPointDataFacade);
	double GetInfluence(const int32 PointIndex) const;

#if WITH_EDITOR
	void ApplyDeprecation();
	/** Rewires the pre-shorthand override pins; call from the embedder's PCGExApplyDeprecationBeforeUpdatePins under the same version gate as ApplyDeprecation. */
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};
