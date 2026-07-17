// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"

#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExSettingsMacros.h"
#include "Details/PCGExSubdivisionDetails.h"
#include "Math/PCGExMath.h"

#include "PCGExSubdivide.generated.h"

class UPCGExSubPointsBlendInstancedFactory;
class FPCGExSubPointsBlendOperation;
/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-subdivide"))
class UPCGExSubdivideSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PathSubdivide, "Path : Subdivide", "Subdivide paths segments.");
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

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filter which segments will be subdivided.", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** Reference for computing the blending interpolation point point */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSubdivideMode SubdivideMethod = EPCGExSubdivideMode::Distance;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="SubdivideMethod != EPCGExSubdivideMode::Manhattan", EditConditionHides))
	EPCGExInputValueType AmountInput = EPCGExInputValueType::Constant;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Amount (Distance)", EditCondition="SubdivideMethod == EPCGExSubdivideMode::Distance && AmountInput == EPCGExInputValueType::Constant", EditConditionHides, ClampMin=0.1))
	double Distance = 10;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Amount (Count)", EditCondition="SubdivideMethod == EPCGExSubdivideMode::Count && AmountInput == EPCGExInputValueType::Constant", EditConditionHides, ClampMin=1))
	int32 Count = 10;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Amount (Attr)", EditCondition="SubdivideMethod != EPCGExSubdivideMode::Manhattan && AmountInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector SubdivisionAmount;

	PCGEX_SETTING_VALUE_DECL(SubdivisionAmount, double)
	
	/** How to round count based on distance subdivision. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="SubdivideMethod == EPCGExSubdivideMode::Distance"))
	EPCGExTruncateMode TruncateMethod = EPCGExTruncateMode::Floor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SubdivideMethod != EPCGExSubdivideMode::Manhattan && SubdivideMethod == EPCGExSubdivideMode::Distance", EditConditionHides))
	bool bRedistributeEvenly = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Manhattan", EditCondition="SubdivideMethod == EPCGExSubdivideMode::Manhattan", EditConditionHides))
	FPCGExManhattanDetails ManhattanDetails;


	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExSubPointsBlendInstancedFactory> Blending;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFlagSubPoints = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bFlagSubPoints"))
	FName SubPointFlagName = "IsSubPoint";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAlpha = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bWriteAlpha"))
	FName AlphaAttributeName = "Alpha";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, EditCondition="bWriteAlpha", EditConditionHides, HideEditConditionToggle))
	double DefaultAlpha = 1;
};

struct FPCGExSubdivideContext final : FPCGExPathProcessorContext
{
	friend class FPCGExSubdivideElement;

	UPCGExSubPointsBlendInstancedFactory* Blending = nullptr;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSubdivideElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Subdivide)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSubdivide
{
	struct FSubdivision
	{
		int32 NumSubdivisions = 0;

		int32 InStart = -1;
		int32 InEnd = -1;
		int32 OutStart = -1;
		int32 OutEnd = -1;

		double Dist = 0;

		double StepSize = 0;
		double StartOffset = 0;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSubdivideContext, UPCGExSubdivideSettings>
	{
		TArray<FSubdivision> Subdivisions;

		bool bClosedLoop = false;

		TSet<FName> ProtectedAttributes;
		TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending;

		TSharedPtr<PCGExData::TBuffer<bool>> FlagWriter;
		TSharedPtr<PCGExData::TBuffer<double>> AlphaWriter;

		TSharedPtr<PCGExDetails::TSettingValue<double>> AmountGetter;

		bool bIsManhattan = false;
		FPCGExManhattanDetails ManhattanDetails;
		TArray<TSharedPtr<TArray<FVector>>> ManhattanPoints;

		double ConstantAmount = 0;

		bool bUseCount = false;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool IsTrivial() const override
		{
			return false;
		} // Force non-trivial

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;

		virtual void Write() override;
	};
}
