// Copyright 2026 Timothé Lapetite and contributors

// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExPointsProcessor.h"

#include "Components/PrimitiveComponent.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExCollisionDetails.h"
#include "Materials/MaterialInterface.h"
#include "Sampling/PCGExApplySamplingDetails.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExSampleNearestSurface.generated.h"

#define PCGEX_FOREACH_FIELD_NEARESTSURFACE(MACRO)\
MACRO(Success, bool, false)\
MACRO(Location, FVector, FVector::ZeroVector)\
MACRO(LookAt, FVector, FVector::OneVector)\
MACRO(Normal, FVector, FVector::OneVector)\
MACRO(IsInside, bool, false)\
MACRO(Distance, double, 0)\
MACRO(ActorReference, FSoftObjectPath, FSoftObjectPath())\
MACRO(PhysMat, FSoftObjectPath, FSoftObjectPath())

class AActor;
class UWorld;
class UPCGExPointFilterFactoryData;

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

/**
 * Use PCGExSampling to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/nearest/sample-nearest-surface"))
class UPCGExSampleNearestSurfaceSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(SampleNearestSurface, "Sample : Nearest Surface", "Find the closest point on the nearest collidable surface.");

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

	/** Surface source */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	EPCGExSurfaceSource SurfaceSource = EPCGExSurfaceSource::ActorReferences;

	/** Name of the attribute that contains a path to an actor in the level, usually from a GetActorData PCG Node in point mode.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable, EditCondition="SurfaceSource == EPCGExSurfaceSource::ActorReferences", EditConditionHides))
	FName ActorReference = FName("ActorReference");

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double MaxDistance_DEPRECATED = 1000;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bUseLocalMaxDistance_DEPRECATED = false;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalMaxDistance_DEPRECATED;

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDoubleAbs Distance = FPCGExInputShorthandSelectorDoubleAbs(FName("Distance"), 1000, false);


	/** Whether and how to apply sampled result directly (not mutually exclusive with output)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** Write whether the sampling was successful or not to a boolean attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSuccess = false;

	/** Name of the 'boolean' attribute to write sampling success to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Success", PCG_Overridable, EditCondition="bWriteSuccess"))
	FName SuccessAttributeName = FName("bSamplingSuccess");

	/** Write the sample location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLocation = false;

	/** Name of the 'vector' attribute to write sampled Location to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Location", PCG_Overridable, EditCondition="bWriteLocation"))
	FName LocationAttributeName = FName("NearestLocation");

	/** Write the sample "look at" direction from the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLookAt = false;

	/** Name of the 'vector' attribute to write sampled LookAt to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="LookAt", PCG_Overridable, EditCondition="bWriteLookAt"))
	FName LookAtAttributeName = FName("NearestLookAt");


	/** Write the sampled normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNormal = false;

	/** Name of the 'vector' attribute to write sampled Normal to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Normal", PCG_Overridable, EditCondition="bWriteNormal"))
	FName NormalAttributeName = FName("NearestNormal");


	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDistance = false;

	/** Name of the 'double' attribute to write sampled distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Distance", PCG_Overridable, EditCondition="bWriteDistance"))
	FName DistanceAttributeName = FName("NearestDistance");

	/** Whether to output normalized distance or not*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Normalized", EditCondition="bWriteDistance", EditConditionHides, HideEditConditionToggle))
	bool bOutputNormalizedDistance = false;

	/** Whether to do a OneMinus on the normalized distance value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" │ └─ OneMinus", EditCondition="bWriteDistance && bOutputNormalizedDistance", EditConditionHides, HideEditConditionToggle))
	bool bOutputOneMinusDistance = false;

	/** Scale factor applied to the distance output; allows to easily invert it using -1 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Scale", EditCondition="bWriteDistance", EditConditionHides, HideEditConditionToggle))
	double DistanceScale = 1;

	/** Write the inside/outside status of the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsInside = false;

	/** Name of the 'bool' attribute to write sampled point inside or outside the collision.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="IsInside", PCG_Overridable, EditCondition="bWriteIsInside"))
	FName IsInsideAttributeName = FName("IsInside");

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteActorReference = false;

	/** Name of the 'string' attribute to write actor reference to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="ActorReference", PCG_Overridable, EditCondition="bWriteActorReference"))
	FName ActorReferenceAttributeName = FName("ActorReference");

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePhysMat = false;

	/** Name of the 'string' attribute to write actor reference to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="PhysMat", PCG_Overridable, EditCondition="bWritePhysMat"))
	FName PhysMatAttributeName = FName("PhysMat");

	/** Which actor reference points attributes to forward on points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="SurfaceSource == EPCGExSurfaceSource::ActorReferences", EditConditionHides))
	FPCGExForwardDetails AttributesForwarding;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExCollisionDetails CollisionSettings;

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasSuccesses = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasSuccesses"))
	FString HasSuccessesTag = TEXT("HasSuccesses");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoSuccesses = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoSuccesses"))
	FString HasNoSuccessesTag = TEXT("HasNoSuccesses");

	//

	/** If enabled, mark filtered out points as "failed". Otherwise, just skip the processing altogether. Only uncheck this if you want to ensure existing attribute values are preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bProcessFilteredOutAsFails = true;

	/** If enabled, points that failed to sample anything will be pruned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bPruneFailedSamples = false;

	/** Consider points that are inside as failed samples. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable), AdvancedDisplay)
	bool bProcessInsideAsFailedSamples = false;

	/** Consider points that are outside as failed samples. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable), AdvancedDisplay)
	bool bProcessOutsideAsFailedSamples = false;
};

struct FPCGExSampleNearestSurfaceContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleNearestSurfaceElement;

	TSharedPtr<PCGExData::FFacade> ActorReferenceDataFacade;

	FPCGExCollisionDetails CollisionSettings;

	FPCGExApplySamplingDetails ApplySampling;

	bool bUseInclude = false;
	TMap<AActor*, int32> IncludedActors;
	TArray<UPrimitiveComponent*> IncludedPrimitives;

	PCGEX_FOREACH_FIELD_NEARESTSURFACE(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleNearestSurfaceElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleNearestSurface)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleNearestSurface
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleNearestSurfaceContext, UPCGExSampleNearestSurfaceSettings>
	{
		TArray<int8> SamplingMask;

		TSharedPtr<PCGExData::FDataForwardHandler> SurfacesForward;

		TSharedPtr<PCGExDetails::TSettingValue<double>> DistanceGetter;
		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxDistanceValue;
		double MaxSampledDistance = 0;

		PCGEX_FOREACH_FIELD_NEARESTSURFACE(PCGEX_OUTPUT_DECL)

		int8 bAnySuccess = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void OnPointsProcessingComplete() override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
