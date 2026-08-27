// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"


#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExPointsToBounds.generated.h"

namespace PCGExBlending
{
	class FMetadataBlender;
}

namespace PCGExMath
{
	struct FBestFitPlane;
}

UENUM()
enum class EPCGExPointsToBoundsOutputMode : uint8
{
	Collapse  = 0 UMETA(DisplayName = "Collapse", Tooltip="Collapse point set to a single point with the blended properties of the whole."),
	WriteData = 1 UMETA(DisplayName = "Write Data", Tooltip="Leave points unaffected and write the results to the data domain instead."),
};


USTRUCT(BlueprintType)
struct FPCGExPointsToBoundsDataDetails
{
	GENERATED_BODY()

	FPCGExPointsToBoundsDataDetails()
	{
	}

	/** Write the computed transform to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTransform = false;

	/** Attribute name for the transform output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteTransform"))
	FName TransformAttributeName = FName("@Data.Transform");

	/** Write the blended density to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDensity = true;

	/** Attribute name for the density output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteDensity"))
	FName DensityAttributeName = FName("@Data.Density");

	/** Write the bounds minimum corner to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMin = true;

	/** Attribute name for the bounds minimum output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBoundsMin"))
	FName BoundsMinAttributeName = FName("@Data.BoundsMin");

	/** Write the bounds maximum corner to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMax = true;

	/** Attribute name for the bounds maximum output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBoundsMax"))
	FName BoundsMaxAttributeName = FName("@Data.BoundsMax");

	/** Write the blended color to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteColor = true;

	/** Attribute name for the color output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteColor"))
	FName ColorAttributeName = FName("@Data.Color");

	/** Write the blended steepness to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSteepness = true;

	/** Attribute name for the steepness output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteSteepness"))
	FName SteepnessAttributeName = FName("@Data.Steepness");

	/** Write the best-fit plane transform to a data domain attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBestFitPlane = true;

	/** Attribute name for the best-fit plane transform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBestFitPlane"))
	FName BestFitPlaneAttributeName = FName("@Data.BestFitPlane");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Axis Order", EditCondition="bWriteBestFitPlane", EditConditionHides, HideEditConditionToggle))
	EPCGExAxisOrder AxisOrder = EPCGExAxisOrder::XYZ;

	void Output(const UPCGBasePointData* InBoundsData, UPCGBasePointData* OutData, const TArray<FPCGAttributeIdentifier>& AttributeIdentifiers, PCGExMath::FBestFitPlane& Plane) const;
	void OutputInverse(const UPCGBasePointData* InPoints, UPCGBasePointData* OutData, const TArray<FPCGAttributeIdentifier>& AttributeIdentifiers, PCGExMath::FBestFitPlane& Plane) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/modify/points-to-bounds"))
class UPCGExPointsToBoundsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PointsToBounds, "Points to Bounds", "Merge points group to a single point representing their bounds.");

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

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Output Object Oriented Bounds. This only accounts for positions and ignores point bounds. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputOrientedBoundingBox = false;

	/** If enabled, use precise min box fit. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Precise Fit", EditCondition="bOutputOrientedBoundingBox", EditConditionHides))
	bool bUseMinBoxFit = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Axis Order", EditCondition="bOutputOrientedBoundingBox", EditConditionHides))
	EPCGExAxisOrder AxisOrder = EPCGExAxisOrder::XYZ;

	/** Which point bounds should be used */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** How to reduce data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointsToBoundsOutputMode OutputMode = EPCGExPointsToBoundsOutputMode::Collapse;

	/** Bound point is the result of its contents */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bBlendProperties = true;

	/** Defines how fused point properties and attributes are merged into the final point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bBlendProperties"))
	FPCGExBlendingDetails BlendingSettings = FPCGExBlendingDetails(EPCGExBlendingType::Average, EPCGExBlendingType::None);

	/** Which data to write. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExPointsToBoundsDataDetails DataDetails;

	/** Write point counts */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bWritePointsCount = false;

	/** Attribute to write points count to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bWritePointsCount"))
	FName PointsCountAttributeName = "@Data.MergedPointsCount";

private:
	friend class FPCGExPointsToBoundsElement;
};

struct FPCGExPointsToBoundsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExPointsToBoundsElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPointsToBoundsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PointsToBounds)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPointsToBounds
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPointsToBoundsContext, UPCGExPointsToBoundsSettings>
	{
		PCGExMath::FBestFitPlane BestFitPlane;
		TSharedPtr<PCGExData::FPointIO> OutputIO;
		TSharedPtr<PCGExData::FFacade> OutputFacade;
		TArray<FPCGAttributeIdentifier> BlendedAttributes;
		TSharedPtr<PCGExBlending::FMetadataBlender> MetadataBlender;
		FBox Bounds;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
