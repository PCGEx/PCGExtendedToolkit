// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExSplineMeshDetails.generated.h"

class UPCGSettings;
class UPCGNode;

namespace PCGExData
{
	class FFacade;
}

struct FPCGExStaticMeshComponentDescriptor;

namespace PCGExPaths
{
	PCGEXFOUNDATIONS_API void GetAxisForEntry(const FPCGExStaticMeshComponentDescriptor& InDescriptor, ESplineMeshAxis::Type& OutAxis, int32& OutC1, int32& OutC2, const EPCGExSplineMeshAxis Default = EPCGExSplineMeshAxis::X);

	struct PCGEXFOUNDATIONS_API FSplineMeshSegment
	{
		FSplineMeshSegment() = default;
		virtual ~FSplineMeshSegment() = default;

		bool bSmoothInterpRollScale = true;
		bool bUseDegrees = true;
		FVector UpVector = FVector::UpVector;
		TSet<FName> Tags;

		ESplineMeshAxis::Type SplineMeshAxis = ESplineMeshAxis::Type::X;

		FSplineMeshParams Params;

		void ComputeUpVectorFromTangents();

		virtual void ApplySettings(USplineMeshComponent* Component) const;
	};
}

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExSplineMeshMutationDetails
{
	GENERATED_BODY()

	FPCGExSplineMeshMutationDetails() = default;

	/** Push the start point of the spline mesh segment. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bPushStart = false;

	/** How far to push the start point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ├─ Amount", EditCondition="bPushStart", EditConditionHides))
	FPCGExInputShorthandSelectorDouble StartPush = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0.1, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType StartPushInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector StartPushInputAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double StartPushConstant_DEPRECATED = 0.1;

#pragma endregion

	/** If enabled, value will relative to the size of the segment */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Relative", EditCondition="bPushStart", EditConditionHides))
	bool bRelativeStart = true;

	/** Push the end point of the spline mesh segment. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bPushEnd = false;

	/** How far to push the end point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ├─ Amount", EditCondition="bPushEnd", EditConditionHides))
	FPCGExInputShorthandSelectorDouble EndPush = FPCGExInputShorthandSelectorDouble(FName("@Last"), 0.1, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType EndPushInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector EndPushInputAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double EndPushConstant_DEPRECATED = 0.1;

#pragma endregion

	/** If enabled, value will relative to the size of the segment */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Relative", EditCondition="bPushEnd", EditConditionHides))
	bool bRelativeEnd = true;

	bool Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade);
	void Mutate(const int32 PointIndex, PCGExPaths::FSplineMeshSegment& InSegment);

#if WITH_EDITOR
	void ApplyDeprecation();
	/** Rewires the pre-shorthand override pins; call from the embedder's PCGExApplyDeprecationBeforeUpdatePins under the same version gate as ApplyDeprecation. */
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif

protected:
	TSharedPtr<PCGExDetails::TSettingValue<double>> StartAmount;
	TSharedPtr<PCGExDetails::TSettingValue<double>> EndAmount;
};
