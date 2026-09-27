// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Elements/PCGProjectionParams.h"
#include "Sampling/PCGExApplySamplingDetails.h"

#include "PCGExProjection.generated.h"

class UPCGMetadata;
class UPCGSpatialData;

/**
 * Lightweight, parallel alternative to the stock Projection node.
 * Projects every point onto a single spatial target (landscape, surface, points, volume...) and applies the
 * projected transform / look-at per-component through FPCGExApplySamplingDetails.
 * Target attributes are forwarded on demand through a name filter; no color blend; supports data stealing.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/projection"))
class UPCGExProjectionSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExProjectionSettings();

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Projection, "Projection", "Projects points onto a spatial target and applies the projected transform per-component. Lightweight, parallel alternative to the stock Projection node : opt-in attribute forwarding, no color blend, supports data stealing.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

protected:
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/**
	 * Which components of the projected transform are applied to the point.
	 * Transform = the projected point transform (position on the target, rotation from the target normal/hit, target scale).
	 * LookAt = rotation looking from the original point toward its projected location (X forward).
	 * Defaults to position + rotation, like the stock node.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** If enabled, points the target rejects (outside the target, no hit...) are removed from the output. Otherwise they are left untouched. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bPruneFailedProjections = true;

	/** Write whether the projection was successful or not to a boolean attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSuccess = false;

	/** Name of the 'boolean' attribute to write projection success to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Success", PCG_Overridable, EditCondition="bWriteSuccess"))
	FName SuccessAttributeName = FName("bProjectionSuccess");

	/** Which of the attributes the target writes during projection (landscape layer weights, actor reference, point attributes...)
	 * are forwarded onto the projected points. Filtered-out and failed points keep their existing values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding", meta=(PCG_Overridable))
	FPCGExForwardDetails AttributesForwarding;

private:
	friend class FPCGExProjectionElement;
};

struct FPCGExProjectionContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExProjectionElement;

	/** Kept alive by InputData for the duration of the execution. */
	const UPCGSpatialData* ProjectionTarget = nullptr;

	FPCGProjectionParams ProjectionParams;
	FPCGExApplySamplingDetails ApplySampling;

	bool bWriteSuccess = false;
	bool bForwardAttributes = false;

	/** Target Elements attributes known upfront that pass the name filter; include-filter for the per-processor sampled metadata. */
	TSet<FName> ForwardedTargetAttributes;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExProjectionElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Projection)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExProjection
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExProjectionContext, UPCGExProjectionSettings>
	{
		/** 1 = keep, 0 = prune. Only allocated when pruning. */
		TArray<int8> ProjectionMask;
		TSharedPtr<PCGExData::TBuffer<bool>> SuccessWriter;

		/** Scratch metadata ProjectPoint writes into (context-managed, parented to the target); entries accumulate until CompleteWork. */
		UPCGMetadata* SampledMetadata = nullptr;
		/** Entry each point got, PCGInvalidEntryKey when filtered out or rejected; forwarded per scope at the end of ProcessPoints. */
		TArray<PCGMetadataEntryKey> SampledEntries;
		TSharedPtr<PCGExData::FDataForwardHandler> AttributesForward;

		bool bPrune = false;

		bool InitForwarding();

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
