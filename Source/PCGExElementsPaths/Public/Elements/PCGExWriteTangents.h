// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "PCGExWriteTangents.generated.h"

class UPCGExTangentsInstancedFactory;
class FPCGExTangentsOperation;
/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/analyze/path-write-tangents"))
class UPCGExWriteTangentsSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

	UPCGExWriteTangentsSettings(const FObjectInitializer& ObjectInitializer);

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PathWriteTangents, "Path : Write Tangents", "Computes & writes points tangents.");
#endif

#if WITH_EDITORONLY_DATA
	// UObject interface
	virtual void PostInitProperties() override;
	// End of UObject interface
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	virtual FName GetPointFilterPin() const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName ArriveName = "ArriveTangent";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName LeaveName = "LeaveTangent";

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> Tangents;

	/** Optional module for the start point specifically */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, DisplayName=" ├─ Start Override", ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> StartTangents;

	/** Optional module for the end point specifically */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, DisplayName=" └─ End Override", ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> EndTangents;


	/** Scale applied to the arrive tangent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Scaling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorVector ArriveScale = FPCGExInputShorthandSelectorVector(FString(TEXT("$Scale")), FVector::OneVector, false);

	/** Scale applied to the leave tangent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Scaling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorVector LeaveScale = FPCGExInputShorthandSelectorVector(FString(TEXT("$Scale")), FVector::OneVector, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ArriveScaleInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector ArriveScaleAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double ArriveScaleConstant_DEPRECATED = 1;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType LeaveScaleInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LeaveScaleAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double LeaveScaleConstant_DEPRECATED = 1;

#pragma endregion
};

struct FPCGExWriteTangentsContext final : FPCGExPathProcessorContext
{
	friend class FPCGExWriteTangentsElement;

	UPCGExTangentsInstancedFactory* Tangents = nullptr;
	UPCGExTangentsInstancedFactory* StartTangents = nullptr;
	UPCGExTangentsInstancedFactory* EndTangents = nullptr;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExWriteTangentsElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WriteTangents)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWriteTangents
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExWriteTangentsContext, UPCGExWriteTangentsSettings>
	{
		bool bClosedLoop = false;
		int32 LastIndex = 0;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ArriveScaleReader;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> LeaveScaleReader;

		TSharedPtr<PCGExData::TBuffer<FVector>> ArriveWriter;
		TSharedPtr<PCGExData::TBuffer<FVector>> LeaveWriter;

		TSharedPtr<FPCGExTangentsOperation> Tangents;
		TSharedPtr<FPCGExTangentsOperation> StartTangents;
		TSharedPtr<FPCGExTangentsOperation> EndTangents;

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
