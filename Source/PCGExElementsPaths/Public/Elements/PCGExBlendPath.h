// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExBlendPath.generated.h"

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class FBlendOpsManager;
}

class UPCGExSubPointsBlendInstancedFactory;

UENUM()
enum class EPCGExPathBlendMode : uint8
{
	Full   = 0 UMETA(DisplayName = "Start to End", ToolTip="Blend properties & attributes of all path' points from start point to last point"),
	Switch = 1 UMETA(DisplayName = "Switch", ToolTip="Switch between pruning/non-pruning based on filters"),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-blend"))
class UPCGExBlendPathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExBlendPathSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(BlendPath, "Path : Blend", "Blend path individual points between its start and end points.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBlendOver BlendOver = EPCGExBlendOver::Distance;

	/** Fixed lerp value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="BlendOver == EPCGExBlendOver::Fixed", EditConditionHides))
	FPCGExInputShorthandSelectorDoubleAbs Lerp = FPCGExInputShorthandSelectorDoubleAbs(FName("@Last"), 0.5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType LerpInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LerpAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double LerpConstant_DEPRECATED = 0.5;

#pragma endregion

	/** Blending settings used to smooth attributes.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExBlendingDetails BlendingSettings = FPCGExBlendingDetails(EPCGExBlendingType::Lerp, EPCGExBlendingType::None);

	/** If enabled, will apply blending to othe first point. Can be useful with some blendmodes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bBlendFirstPoint = false;

	/** If enabled, will apply blending to the last  point. Can be useful with some blendmodes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bBlendLastPoint = false;
};

struct FPCGExBlendPathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExBlendPathElement;
	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBlendPathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BlendPath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBlendPath
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBlendPathContext, UPCGExBlendPathSettings>
	{
		int32 MaxIndex = 0;

		PCGExPaths::FPathMetrics Metrics;

		TSharedPtr<PCGExDetails::TSettingValue<double>> LerpGetter;
		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;

		int32 Start = -1;
		int32 End = -1;

		TArray<double> Length;

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
