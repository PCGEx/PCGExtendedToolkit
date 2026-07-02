// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "PCGExBlendAttributes.generated.h"

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class FBlendOpsManager;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/blending/uber-blend"))
class UPCGExBlendAttributesSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BlendAttributes, "Uber Blend", "[EXPERIMENTAL] One-stop node to combine multiple blends.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings


	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;
};

struct FPCGExBlendAttributesContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBlendAttributesElement;
	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBlendAttributesElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BlendAttributes)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBlendAttributes
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBlendAttributesContext, UPCGExBlendAttributesSettings>
	{
		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;

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
		virtual void Cleanup() override;
	};
}
