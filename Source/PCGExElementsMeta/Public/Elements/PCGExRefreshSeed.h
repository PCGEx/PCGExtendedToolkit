// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "PCGExRefreshSeed.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/modify/refresh-seed"))
class UPCGExRefreshSeedSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(RefreshSeed, "Refresh Seed", "Refresh point seed based on position.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Generic;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Base seed.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 Base = 0;
};

struct FPCGExRefreshSeedContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExRefreshSeedElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExRefreshSeedElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(RefreshSeed)

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExRefreshSeed
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExRefreshSeedContext, UPCGExRefreshSeedSettings>
	{
	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
	};
}
