// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"

#include "Core/PCGExPathProcessor.h"
#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSplineMeshDetails.h"
#include "Math/PCGExMathAxis.h"
#include "Tangents/PCGExTangentsInstancedFactory.h"

#include "PCGExPathSplineMeshSimple.generated.h"

namespace PCGExMT
{
	class FTimeSlicedMainThreadLoop;
}

struct FPCGObjectPropertyOverrideDescription;

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/output/path-spline-mesh-simple"))
class UPCGExPathSplineMeshSimpleSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExPathSplineMeshSimpleSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(PathSplineMeshSimple, "Path : Spline Mesh (Simple)", "Create spline mesh components from paths.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spawner;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN(UPCGExPathProcessorSettings::GetNodeTitleColor());
	}

	virtual void PostInitProperties() override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

	/** How the asset gets selected */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType AssetType = EPCGExInputValueType::Attribute;

	/** The name of the attribute to write asset path to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Asset (Attr)", EditCondition="AssetType != EPCGExInputValueType::Constant", EditConditionHides))
	FName AssetPathAttributeName = "AssetPath";

	/** Constant static mesh .*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Asset", EditCondition="AssetType == EPCGExInputValueType::Constant", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bReadMaterialFromAttribute = false;

	/** The name of the attribute to write material path to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bReadMaterialFromAttribute"))
	FName MaterialAttributeName = "MaterialPath";

	/** The index of the slot to set the material to, if found.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Slot", EditCondition="bReadMaterialFromAttribute", EditConditionHides, HideEditConditionToggle))
	int32 MaterialSlotConstant = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Target Actor", meta = (PCG_Overridable))
	TSoftObjectPtr<AActor> TargetActor;

	/**  */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Target Actor", meta = (PCG_Overridable))
	//bool bPerSegmentTargetActor = false;

	/**  */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Target Actor", meta=(PCG_Overridable, EditCondition="bPerSegmentTargetActor", EditConditionHides))
	//FName TargetActorAttributeName;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bApplyCustomTangents_DEPRECATED = false;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ArriveTangentAttribute_DEPRECATED = "ArriveTangent";

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName LeaveTangentAttribute_DEPRECATED = "LeaveTangent";

#pragma endregion

	/** Per-point tangent settings. Can't be set if the spline is linear. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTangentsDetails Tangents;

	/** Start Offset (Vector 2 expected when read from an attribute) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mutations|Offsets", meta=(PCG_Overridable, DisplayName="Start Offset"))
	FPCGExInputShorthandNameVector2 StartOffsetValue = FPCGExInputShorthandNameVector2(FName("StartOffset"), FVector2D::ZeroVector, false);

	/** End Offset (Vector 2 expected when read from an attribute) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mutations|Offsets", meta=(PCG_Overridable, DisplayName="End Offset"))
	FPCGExInputShorthandNameVector2 EndOffsetValue = FPCGExInputShorthandNameVector2(FName("EndOffset"), FVector2D::ZeroVector, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType StartOffsetInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName StartOffsetAttribute_DEPRECATED = FName("StartOffset");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector2D StartOffset_DEPRECATED = FVector2D::ZeroVector;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType EndOffsetInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName EndOffsetAttribute_DEPRECATED = FName("EndOffset");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector2D EndOffset_DEPRECATED = FVector2D::ZeroVector;

#pragma endregion

	/** Push details */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mutations", meta=(PCG_Overridable, DisplayName="Expansion"))
	FPCGExSplineMeshMutationDetails MutationDetails;

	/** How the spline mesh up vector is determined. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExSplineMeshUpMode SplineMeshUpMode = EPCGExSplineMeshUpMode::Constant;

	/** Attribute to read the spline mesh up vector from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Spline Mesh Up Vector (Attr)", EditCondition="SplineMeshUpMode == EPCGExSplineMeshUpMode::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector SplineMeshUpVectorAttribute;

	/** Constant up vector for all spline mesh segments. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Spline Mesh Up Vector", EditCondition="SplineMeshUpMode == EPCGExSplineMeshUpMode::Constant", EditConditionHides))
	FVector SplineMeshUpVector = FVector::UpVector;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExMinimalAxis SplineMeshAxisConstant_DEPRECATED = EPCGExMinimalAxis::X;

#pragma endregion

	/** Default static mesh config applied to spline mesh components. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FPCGExStaticMeshComponentDescriptor StaticMeshDescriptor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGObjectPropertyOverrideDescription> PropertyOverrideDescriptions;

	/** Specify a list of functions to be called on the target actor after spline mesh creation. Functions need to be parameter-less and with "CallInEditor" flag enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay)
	TArray<FName> PostProcessFunctionNames;

protected:
	virtual bool IsCacheable() const override
	{
		return false;
	}
};

struct FPCGExPathSplineMeshSimpleContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathSplineMeshSimpleElement;

	TSharedPtr<PCGEx::TAssetLoader<UStaticMesh>> StaticMeshLoader;
	TSharedPtr<PCGEx::TAssetLoader<UMaterialInterface>> MaterialLoader;

	TObjectPtr<UStaticMesh> StaticMesh;

	FPCGExTangentsDetails Tangents;

	virtual void RegisterAssetDependencies() override;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathSplineMeshSimpleElement final : public FPCGExPathProcessorElement
{
public:
	// Generates artifacts
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return false;
	}

protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true)
	PCGEX_ELEMENT_CREATE_CONTEXT(PathSplineMeshSimple)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathSplineMeshSimple
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathSplineMeshSimpleContext, UPCGExPathSplineMeshSimpleSettings>
	{
	protected:
		bool bClosedLoop = false;
		bool bUseTags = false;
		bool bIsPreviewMode = false;
		int8 bHasValidSegments = false;

		FPCGExSplineMeshMutationDetails MutationDetails;

		int32 LastIndex = 0;
		TSharedPtr<PCGExTangents::FTangentsHandler> TangentsHandler;

		TSharedPtr<PCGExData::TBuffer<FVector>> UpGetter;
		TSharedPtr<PCGExDetails::TSettingValue<FVector2D>> StartOffset;
		TSharedPtr<PCGExDetails::TSettingValue<FVector2D>> EndOffset;

		TSharedPtr<TArray<PCGExValueHash>> MeshKeys;
		TSharedPtr<TArray<PCGExValueHash>> MaterialKeys;

		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;
		TArray<PCGExPaths::FSplineMeshSegment> Segments;
		TArray<TObjectPtr<UStaticMesh>> Meshes;
		TArray<TObjectPtr<UMaterialInterface>> Materials;

		TArray<FName> DataTags;
		AActor* TargetActor = nullptr;
		EObjectFlags ObjectFlags = RF_NoFlags;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void OnPointsProcessingComplete() override;
		void ProcessSegment(const int32 Index);

		virtual void CompleteWork() override;
	};
}
