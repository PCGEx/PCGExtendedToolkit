// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExBoundsCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExFindPointOnBounds.generated.h"

class FPCGExComputeIOBounds;


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/analyze/find-point-on-bounds"))
class UPCGExFindPointOnBoundsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(FindPointOnBounds, "Find point on Bounds", "Find the closest point on the dataset bounds.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscAdd);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Data output mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointOnBoundsOutputMode OutputMode = EPCGExPointOnBoundsOutputMode::Merged;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bBestFitBounds = false;

	/** Whether to use best fit plane bounds, and which axis ordering should be used. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Use Best Fit bounds axis", EditCondition="bBestFitBounds"))
	EPCGExAxisOrder AxisOrder = EPCGExAxisOrder::YXZ;

	/** UVW position of the target within bounds. Attribute source is read from the @Data domain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="UVW"))
	FPCGExInputShorthandSelectorVector UVWValue = FPCGExInputShorthandSelectorVector(FName("@Data.UVW"), FVector::OneVector, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType UVWInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalUVW_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector UVW_DEPRECATED = FVector::OneVector;

#pragma endregion

	/** Offset to apply to the closest point, away from the bounds center. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Offset = 1;

	/** Meta filter settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

	/** Suppress warning about attribute type mismatches during merging. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bQuietAttributeMismatchWarning = false;

private:
	friend class FPCGExFindPointOnBoundsElement;
};

struct FPCGExFindPointOnBoundsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExFindPointOnBoundsElement;

	FPCGExCarryOverDetails CarryOverDetails;

	TArray<int32> BestIndices;
	TSharedPtr<PCGExData::FPointIO> MergedOut;
	TSharedPtr<PCGExData::FAttributesInfos> MergedAttributesInfos;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExFindPointOnBoundsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FindPointOnBounds)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFindPointOnBounds
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExFindPointOnBoundsContext, UPCGExFindPointOnBoundsSettings>
	{
		mutable FRWLock BestIndexLock;

		double BestDistance = TNumericLimits<double>::Max();
		FVector SearchPosition = FVector::ZeroVector;
		FVector BestPosition = FVector::ZeroVector;
		int32 BestIndex = -1;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};
}
