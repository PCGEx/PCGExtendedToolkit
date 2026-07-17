// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendOpFactory.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "UObject/Object.h"

#include "Factories/PCGExFactoryProvider.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExBlendOpFactoryProvider.generated.h"

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExBlending
{
	class FProxyDataBlender;
}

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Blending", meta=(PCGExNodeLibraryDoc="metadata/blending/blendop"))
class PCGEXBLENDING_API UPCGExBlendOpFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(BlendOp, "BlendOp", "Creates a single Blend Operation node, to be used with the Attribute Blender.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(BlendOp);
	}


	virtual bool CanUserEditTitle() const override
	{
		return false;
	}

	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	//PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), true)

	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoBlendOp)

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

	virtual FName GetMainOutputPin() const override
	{
		return PCGExBlending::Labels::OutputBlendingLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Priority.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	int32 Priority = 0;

	/** Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAttributeBlendConfig Config;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
