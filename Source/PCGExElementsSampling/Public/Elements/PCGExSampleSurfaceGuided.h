// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"

#include "Core/PCGExPointsProcessor.h"
#include "Data/External/PCGExMesh.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Factories/PCGExFactories.h"

#include "Details/PCGExCollisionDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathAxis.h"
#include "Sampling/PCGExApplySamplingDetails.h"
#include "Sampling/PCGExSamplingCommon.h"


#include "PCGExSampleSurfaceGuided.generated.h"


#define PCGEX_FOREACH_FIELD_SURFACEGUIDED(MACRO)\
MACRO(Success, bool, false)\
MACRO(Location, FVector, FVector::ZeroVector)\
MACRO(LookAt, FVector, FVector::OneVector)\
MACRO(Normal, FVector, FVector::OneVector)\
MACRO(Distance, double, 0)\
MACRO(IsInside, bool, false)\
MACRO(UVCoords, FVector2D, FVector2D::ZeroVector)\
MACRO(FaceIndex, int32, -1)\
MACRO(ActorReference, FSoftObjectPath, FSoftObjectPath())\
MACRO(HitComponentReference, FSoftObjectPath, FSoftObjectPath())\
MACRO(PhysMat, FSoftObjectPath, FSoftObjectPath())\
MACRO(RenderMat, FSoftObjectPath, FSoftObjectPath())

class UPCGExTexParamFactoryData;

namespace PCGExTexture
{
	class FLookup;
}


UENUM()
enum class EPCGExTraceSampleDistanceInput : uint8
{
	DirectionLength = 0 UMETA(DisplayName = "Direction Length", ToolTip="..."),
	Constant        = 1 UMETA(DisplayName = "Constant", ToolTip="Constant"),
	Attribute       = 2 UMETA(DisplayName = "Attribute", ToolTip="Attribute"),
};

class UPCGExPointFilterFactoryData;

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;

	template <typename T>
	class TScopedArray;
}

/**
 * Use PCGExSampling to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="sampling/nearest/sample-line-trace"))
class UPCGExSampleSurfaceGuidedSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExSampleSurfaceGuidedSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(SampleSurfaceGuided, "Sample : Line Trace", "Find the collision point on the nearest collidable surface in a given direction.");

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

	/** The origin of the trace */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Origin;

	/** The direction to use for the trace */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Direction;

	/** Invert the trace direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable, DisplayName=" └─ Invert"))
	bool bInvertDirection = false;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExTraceSampleDistanceInput DistanceInput_DEPRECATED = EPCGExTraceSampleDistanceInput::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double MaxDistance_DEPRECATED = 1000;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector LocalMaxDistance_DEPRECATED;

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDoubleAbs Distance = FPCGExInputShorthandSelectorDoubleAbs(FName("Distance"), 1000, false);


	/** Whether and how to apply sampled result directly (not mutually exclusive with output)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** How hit transform rotation should be constructed. First value used is the impact normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEnum))
	EPCGExMakeRotAxis RotationConstruction = EPCGExMakeRotAxis::Z;

	/** Second value used for constructing rotation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Cross Axis"))
	FPCGExInputShorthandSelectorDirection CrossAxis = FPCGExInputShorthandSelectorDirection(FString("$Rotation.Forward"), PCGEX_CORE_SETTINGS.WorldForward, true);

	/** Write whether the sampling was sucessful or not to a boolean attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteSuccess = false;

	/** Name of the 'boolean' attribute to write sampling success to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Success", PCG_Overridable, EditCondition="bWriteSuccess"))
	FName SuccessAttributeName = FName("bSamplingSuccess");

	/** Write the sample location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLocation = false;

	/** Name of the 'vector' attribute to write sampled Location to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Location", PCG_Overridable, EditCondition="bWriteLocation"))
	FName LocationAttributeName = FName("TracedLocation");

	/** Write the sample "look at" direction from the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLookAt = false;

	/** Name of the 'vector' attribute to write sampled LookAt to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="LookAt", PCG_Overridable, EditCondition="bWriteLookAt"))
	FName LookAtAttributeName = FName("TracedLookAt");

	/** Write the sampled normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNormal = false;

	/** Name of the 'vector' attribute to write sampled Normal to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Normal", PCG_Overridable, EditCondition="bWriteNormal"))
	FName NormalAttributeName = FName("TracedNormal");

	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDistance = false;

	/** Name of the 'double' attribute to write sampled distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Distance", PCG_Overridable, EditCondition="bWriteDistance"))
	FName DistanceAttributeName = FName("TracedDistance");

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

	/** Write UV coordinates of the hit surface to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteUVCoords = false;

	/** Create an attribute for UV Coordinates of the surface hit.
	 * Note: Will only work in complex traces and must have 'Project Settings->Physics->Support UV From Hit Results' set to true. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="UV Coords", PCG_Overridable, EditCondition="bWriteUVCoords"))
	FName UVCoordsAttributeName = FName("UVCoords");

	/** This UV Channel will be selected when retrieving UV Coordinates from a raycast query. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, DisplayName=" └─ UV Channel", EditCondition = "bWriteUVCoords", EditConditionHides, HideEditConditionToggle))
	int32 UVChannel = 0;

	/** Write the index of the hit triangle to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteFaceIndex = false;

	/** Create an attribute for index of the hit face.
	 * Note: Will only work in complex traces. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Face Index", PCG_Overridable, EditCondition="bWriteFaceIndex"))
	FName FaceIndexAttributeName = FName("FaceIndex");

	/** Whether to attempt to compute the vertex color and write it to the point $Color */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable))
	bool bWriteVertexColor = false;

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteActorReference = false;

	/** Name of the 'string' attribute to write actor reference to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="ActorReference", PCG_Overridable, EditCondition="bWriteActorReference"))
	FName ActorReferenceAttributeName = FName("ActorReference");

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteHitComponentReference = false;

	/** Name of the 'string' attribute to write actor reference to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="HitComponent", PCG_Overridable, EditCondition="bWriteHitComponentReference"))
	FName HitComponentReferenceAttributeName = FName("HitComponent");

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePhysMat = false;

	/** Name of the 'string' attribute to write actor reference to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="PhysMat", PCG_Overridable, EditCondition="bWritePhysMat"))
	FName PhysMatAttributeName = FName("PhysMat");

	/** Write the actor reference hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRenderMat = false;

	/** Create an attribute for the render material. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(DisplayName="RenderMat", PCG_Overridable, EditCondition="bWriteRenderMat"))
	FName RenderMatAttributeName = FName("RenderMat");

	/** The index of the render material when it is queried from the hit. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta = (PCG_Overridable, DisplayName=" ├─ Material Index", EditCondition = "bWriteRenderMat", EditConditionHides, HideEditConditionToggle))
	int32 RenderMaterialIndex = 0;

	/** Whether to extract texture parameters */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta = (PCG_Overridable, DisplayName=" └─ Texture Parameters", EditCondition = "bWriteRenderMat", EditConditionHides, HideEditConditionToggle))
	bool bExtractTextureParameters = false;

	/** Which actor reference points attributes to forward on points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output (Actor Data)", meta=(EditCondition="SurfaceSource == EPCGExSurfaceSource::ActorReferences", EditConditionHides))
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

	/** Suppress warnings about UV query settings not being enabled in project settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietUVSettingsWarning = false;
};

struct FPCGExSampleSurfaceGuidedContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleSurfaceGuidedElement;

	TSharedPtr<PCGExData::FFacade> ActorReferenceDataFacade;

	bool bSupportsUVQuery = false;
	bool bUseInclude = false;
	bool bExtractTextureParams = false;

	TMap<AActor*, int32> IncludedActors;
	TArray<UPrimitiveComponent*> IncludedPrimitives;

	FPCGExCollisionDetails CollisionSettings;

	FPCGExApplySamplingDetails ApplySampling;

	TArray<TObjectPtr<const UPCGExTexParamFactoryData>> TexParamsFactories;

	PCGEX_FOREACH_FIELD_SURFACEGUIDED(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleSurfaceGuidedElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleSurfaceGuided)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleSurfaceGuided
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleSurfaceGuidedContext, UPCGExSampleSurfaceGuidedSettings>
	{
		TArray<int8> SamplingMask;

		TSharedPtr<PCGExData::FDataForwardHandler> SurfacesForward;

		TSharedPtr<PCGExDetails::TSettingValue<double>> DistanceGetter;
		TSharedPtr<PCGExData::TBuffer<FVector>> DirectionGetter;
		TSharedPtr<PCGExData::TBuffer<FVector>> OriginGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> CrossAxis;
		TSharedPtr<PCGExDetails::TSettingValue<double>> SphereRadiusGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> BoxHalfExtentsGetter;

		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxDistanceValue;
		double MaxSampledDistance = 0;

		TSharedPtr<PCGExTexture::FLookup> TexParamLookup;

		TArray<int32> FaceIndex;
		TArray<int32> MeshIndex;
		TArray<FVector> HitLocation;
		TArray<PCGExMesh::FMeshData> MeshData;
		TSharedPtr<PCGExMT::TScopedArray<const UStaticMesh*>> ScopedMeshes;

		PCGEX_FOREACH_FIELD_SURFACEGUIDED(PCGEX_OUTPUT_DECL)

		int8 bAnySuccess = 0;
		UWorld* World = nullptr;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;

		void ProcessTraceResult(const PCGExMT::FScope& Scope, const FHitResult& HitResult, const int32 Index, const FVector& Origin, const FVector& Direction, PCGExData::FMutablePoint& MutablePoint);

		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		void GetVertexColorAtHit(const int32 Index, FVector4& OutColor) const;
		virtual void OnPointsProcessingComplete() override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
