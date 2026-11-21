// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPointsProcessor.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExFlatProjection.generated.h"

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/transform/flat-projection"))
class UPCGExFlatProjectionSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FlatProjection, "Flat Projection", "Project points from their position in space to the XY plane.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Transform);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Whether this is a new projection or an old one*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bRestorePreviousProjection = false;

	/** The name of the attribute to write its index to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName AttributePrefix = "FlatProjection";


	/** Which position components from the stored transform should be applied to the point.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" ├─ Position", EditCondition="bRestorePreviousProjection", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 TransformPosition = 0;

	/** Which rotation components from the stored transform should be applied to the point.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" ├─ Rotation", EditCondition="bRestorePreviousProjection", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 TransformRotation = 0;

	/** Which scale components from the stored transform should be applied to the point.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Scale", EditCondition="bRestorePreviousProjection", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 TransformScale = 0;


	/** Whether this is a new projection or an old one*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bRestorePreviousProjection"))
	bool bSaveAttributeForRestore = true;

	/** Whether this is a new projection or an old one*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bRestorePreviousProjection"))
	bool bAlignLocalTransform = false;

	/** Projection settings for flattening points to the XY plane. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="!bRestorePreviousProjection", DisplayName="Projection"))
	FPCGExGeo2DProjectionDetails ProjectionDetails;
};

struct FPCGExFlatProjectionContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExFlatProjectionElement;

	FName CachedTransformAttributeName = NAME_None;

	TArray<int32> TrPosComponents;
	TArray<int32> TrRotComponents;
	TArray<int32> TrScaComponents;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExFlatProjectionElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FlatProjection)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFlatProjection
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExFlatProjectionContext, UPCGExFlatProjectionSettings>
	{
		bool bWriteAttribute = false;
		bool bInverseExistingProjection = false;
		bool bProjectLocalTransform = false;

		FPCGExGeo2DProjectionDetails ProjectionDetails;

		TSharedPtr<PCGExData::TBuffer<FTransform>> TransformWriter;
		TSharedPtr<PCGExData::TBuffer<FTransform>> TransformReader;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
