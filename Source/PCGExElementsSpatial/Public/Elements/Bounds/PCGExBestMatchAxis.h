// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"


#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExDistancesDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Details/PCGExSettingsMacros.h"


#include "PCGExBestMatchAxis.generated.h"

namespace PCGExMatching
{
	class FTargetsHandler;
}

class FPCGExComputeIOBounds;

UENUM()
enum class EPCGExBestMatchAxisTargetMode : uint8
{
	Direction              = 0 UMETA(DisplayName = "Direction", ToolTip="Best match against a direction vector."),
	LookAtWorldPosition    = 1 UMETA(DisplayName = "Look at Position (World)", ToolTip="Best match against the look at vector toward a world position."),
	LookAtRelativePosition = 2 UMETA(DisplayName = "Look at Position (Relative)", ToolTip="Best match against the look at vector toward a relative position."),
	ClosestTarget          = 3 UMETA(DisplayName = "Look at Closest Target", ToolTip="Best match against the look at vector toward the closest target point.")
};

UCLASS(Hidden, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/transform/best-match-axis"))
class UPCGExBestMatchAxisSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(BestMatchAxis, "Best Match Axis", "Rotate a point or transform to closely match an input direction (or look at location) but preserve orthogonality.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Transform);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Drive the best match axis */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBestMatchAxisTargetMode Mode = EPCGExBestMatchAxisTargetMode::Direction;


	/** The value to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExBestMatchAxisTargetMode::ClosestTarget", EditConditionHides))
	FPCGExInputShorthandSelectorVector Match = FPCGExInputShorthandSelectorVector(FName("@Last"), FVector::UpVector, true);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MatchInput_DEPRECATED = EPCGExInputValueType::Attribute;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector MatchSource_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector MatchConstant_DEPRECATED = FVector::UpVector;

#pragma endregion

	// TODO : Support attribute mutation such as transform, rotator, vector
	// TODO : Auto-pick axis based on unsigned dot product (so we only mutate where it make the most meaingful)

	/** If enabled, allows you to filter out which targets get sampled by which data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExBestMatchAxisTargetMode::ClosestTarget", EditConditionHides))
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Sampling);

	/** Distance method to be used for source & target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExBestMatchAxisTargetMode::ClosestTarget", EditConditionHides))
	FPCGExDistanceDetails DistanceDetails;

private:
	friend class FPCGExBestMatchAxisElement;
};

struct FPCGExBestMatchAxisContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBestMatchAxisElement;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	int32 NumMaxTargets = 0;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBestMatchAxisElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BestMatchAxis)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBestMatchAxis
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBestMatchAxisContext, UPCGExBestMatchAxisSettings>
	{
		TSet<const UPCGData*> IgnoreList;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> MatchGetter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
	};
}
