// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExAttributesDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "UObject/Object.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExUberNoise.generated.h"

namespace PCGExBlending
{
	class FProxyDataBlender;
}

namespace PCGExNoise3D
{
	class FNoiseGenerator;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

namespace PCGExData
{
	class IBufferProxy;
}

UENUM()
enum class EPCGExUberNoiseMode : uint8
{
	New    = 0 UMETA(DisplayName = "New Attribute", ToolTip="Create new attribute"),
	Mutate = 1 UMETA(DisplayName = "Mutate Attribute", ToolTip="Blend noise with an existing attribute")
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="utilities/noise/uber-noise"))
class UPCGExUberNoiseSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(UberNoise, "Uber Noise", "Generate noise or mutate existing attribute using noises.", FName(GetDisplayName()));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Noise3D);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExUberNoiseMode Mode = EPCGExUberNoiseMode::New;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExUberNoiseMode::New", EditConditionHides))
	EPCGMetadataTypes OutputType = EPCGMetadataTypes::Double;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAttributeSourceToTargetDetails Attributes = FPCGExAttributeSourceToTargetDetails(FName(TEXT("$Density")));

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExUberNoiseMode::Mutate", EditConditionHides))
	EPCGExABBlendingType BlendMode = EPCGExABBlendingType::Add;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Mode == EPCGExUberNoiseMode::Mutate", EditConditionHides))
	FPCGExInputShorthandSelectorDouble SourceValueWeight = FPCGExInputShorthandSelectorDouble(FName("Weight"), 1, false);

#if WITH_EDITOR
	FString GetDisplayName() const;
#endif

private:
	friend class FPCGExUberNoiseElement;
};

struct FPCGExUberNoiseContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExUberNoiseElement;

	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExUberNoiseElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(UberNoise)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExUberNoise
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExUberNoiseContext, UPCGExUberNoiseSettings>
	{
		EPCGMetadataTypes UnderlyingType = EPCGMetadataTypes::Unknown;
		int32 NumFields = 0;

		TSharedPtr<PCGExBlending::FProxyDataBlender> Blender;
		TSharedPtr<PCGExData::IBufferProxy> NoiseBuffer;
		TSharedPtr<PCGExDetails::TSettingValue<double>> WeightBuffer;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
	};
}
