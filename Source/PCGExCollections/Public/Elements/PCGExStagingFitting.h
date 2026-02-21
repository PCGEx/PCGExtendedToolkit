// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Fitting/PCGExFitting.h"
#include "Helpers/PCGExCollectionsHelpers.h"

#include "PCGExStagingFitting.generated.h"

class UStaticMesh;

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

UENUM()
enum class EPCGExFittingSource : uint8
{
	CollectionMap = 0 UMETA(DisplayName = "Collection Map", ToolTip="Use a collection map from a staging node to resolve entry bounds."),
	MeshAttribute = 1 UMETA(DisplayName = "Mesh Attribute", ToolTip="Read a static mesh path attribute and compute bounds from the mesh bounding box."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage prepare spawn proxy fitting justify variations", PCGExNodeLibraryDoc="staging/staging-fitting"))
class UPCGExStagingFittingSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(StagingFitting, "Staging : Fitting", "Apply fitting, justification and variations to staged points.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
#endif

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

protected:
	virtual bool SupportsDataStealing() const override { return true; }

	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get fitted.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

public:
	/** Where to read bounds from. Collection Map uses staged entry data; Mesh Attribute reads a static mesh path and uses its bounding box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFittingSource Source = EPCGExFittingSource::CollectionMap;

	/** The name of the attribute containing the static mesh path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Source == EPCGExFittingSource::MeshAttribute", EditConditionHides))
	FName AssetPathAttributeName = "AssetPath";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExScaleToFitDetails ScaleToFit;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExJustificationDetails Justification;

	/** Variations are only available in Collection Map mode (entries carry per-asset variation limits). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Source == EPCGExFittingSource::CollectionMap", EditConditionHides))
	FPCGExFittingVariationsDetails Variations;

	//** If enabled, filter output based on whether a staging has been applied or not (empty entry). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	bool bPruneEmptyPoints = true;

	/** Write the fitting translation offset to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteTranslation = false;

	/** Name of the FVector attribute to write fitting offset to.
	 * This is the translation added to the point transform according to fitting/justification rules.
	 * Mostly useful for offsetting spline meshes.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, EditCondition="bWriteTranslation"))
	FName TranslationAttributeName = FName("FittingOffset");
};

struct FPCGExStagingFittingContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingFittingElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;
	TSharedPtr<PCGEx::TAssetLoader<UStaticMesh>> StaticMeshLoader;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingFittingElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingFitting)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExStagingFitting
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingFittingContext, UPCGExStagingFittingSettings>
	{
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;
		TSharedPtr<TArray<PCGExValueHash>> MeshKeys;

		FPCGExFittingDetailsHandler FittingHandler;
		FPCGExFittingVariationsDetails Variations;

		TMap<PCGExValueHash, FBox> BoundsCache;

		TArray<int8> Mask;
		int32 NumInvalid = 0;

		TSharedPtr<PCGExData::TBuffer<FVector>> TranslationWriter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
		virtual void Write() override;
	};
}
