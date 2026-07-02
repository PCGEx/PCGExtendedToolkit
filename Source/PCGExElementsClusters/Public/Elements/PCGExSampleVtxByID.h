// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Math/PCGExMathAxis.h"
#include "Sampling/PCGExApplySamplingDetails.h"

#include "PCGExSampleVtxByID.generated.h"

namespace PCGExData
{
	class FMultiFacadePreloader;
}

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class IUnionBlender;
	class FUnionOpsManager;
	class FBlendOpsSchema;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="clusters/utilities/sample-vtx-by-id"))
class UPCGExSampleVtxByIDSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExSampleVtxByIDSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	PCGEX_NODE_INFOS(SampleVtxByID, "Sample : Vtx by ID", "Sample a cluster vtx by using a stored Vtx ID.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}
#endif

protected:
	virtual FName GetMainInputPin() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings

public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

	//~End UPCGExPointsProcessorSettings

	/** Name of the attribute that stores the vtx id (first 32 bits of the PCGEx/VData) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FName VtxIdSource = FName("VtxId");

	/** Whether and how to apply sampled result directly (not mutually exclusive with blending)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** The axis to align transform the look at vector to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Align"))
	EPCGExAxisAlign LookAtAxisAlign = EPCGExAxisAlign::Forward;

	/** Up vector source.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Use Up from..."))
	EPCGExInputValueType LookAtUpInput = EPCGExInputValueType::Constant;

	/** The attribute or property on selected source to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector (Attr)", EditCondition="LookAtUpInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector LookAtUpSource;

	/** The constant to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector", EditCondition="LookAtUpInput == EPCGExInputValueType::Constant", EditConditionHides))
	FVector LookAtUpConstant = FVector::UpVector;

	PCGEX_SETTING_VALUE_DECL(LookAtUp, FVector)

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasSuccesses = false;

	/** If enabled, add the specified tag to the output data if at least a single point has been sampled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasSuccesses"))
	FString HasSuccessesTag = TEXT("HasSuccesses");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoSuccesses = false;

	/** If enabled, add the specified tag to the output data if no points were sampled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoSuccesses"))
	FString HasNoSuccessesTag = TEXT("HasNoSuccesses");

	//

	/** If enabled, mark filtered out points as "failed". Otherwise, just skip the processing altogether. Only uncheck this if you want to ensure existing attribute values are preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bProcessFilteredOutAsFails = true;

	/** If enabled, points that failed to sample anything will be pruned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bPruneFailedSamples = false;
};

struct FPCGExSampleVtxByIDContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleVtxByIDElement;

	TSharedPtr<PCGExData::FMultiFacadePreloader> TargetsPreloader;

	TArray<TSharedRef<PCGExData::FFacade>> TargetFacades;
	TMap<uint32, uint64> VtxLookup; // Vtx ID :: PointIndex << IOIndex

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	// Blend configs resolved once in Boot and shared across processors -- per-processor
	// blender init only instantiates ops instead of re-enumerating target metadata.
	TSharedPtr<PCGExBlending::FBlendOpsSchema> BlendOpsSchema;

	FPCGExApplySamplingDetails ApplySampling;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleVtxByIDElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleVtxByID)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleVtxByID
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleVtxByIDContext, UPCGExSampleVtxByIDSettings>
	{
		TArray<int8> SamplingMask;

		FVector SafeUpVector = FVector::UpVector;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> LookAtUpGetter;
		TSharedPtr<PCGExData::TBuffer<int32>> VtxID32Getter;
		TSharedPtr<PCGExData::TBuffer<int64>> VtxID64Getter;

		FPCGExBlendingDetails BlendingDetails;

		TSharedPtr<PCGExBlending::FUnionOpsManager> UnionBlendOpsManager;
		TSharedPtr<PCGExBlending::IUnionBlender> DataBlender;

		int8 bAnySuccess = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
			DefaultPointFilterValue = true;
		}

		virtual ~FProcessor() override;

		void SamplingFailed(const int32 Index);

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void Write() override;

		virtual void Cleanup() override;
	};
}
