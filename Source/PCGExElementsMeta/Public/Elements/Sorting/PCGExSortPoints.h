// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"


#include "Core/PCGExPointsProcessor.h"
#include "Sorting/PCGExSortingCommon.h"
#include "Sorting/PCGExSortingDetails.h"

#include "PCGExSortPoints.generated.h"

namespace PCGExSorting
{
	class FSorter;
}

UCLASS(Abstract, MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc")
class UPCGExSortPointsBaseSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

	//~Begin UPCGSettings
protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Controls the order in which points will be ordered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const;

private:
	friend class FPCGExSortPointsBaseElement;
};

UCLASS(MinimalAPI, BlueprintType, Hidden, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="misc/sort-points"))
class UPCGExSortPointsSettings : public UPCGExSortPointsBaseSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SortPointsStatic, "Sort Points (Static)", "Sort the source points according to specific rules.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Generic;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	//~Begin UObject interface
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	/** Ordered list of attribute to check to sort over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, TitleProperty="{TitlePropertyName}"))
	TArray<FPCGExSortRuleConfig> Rules = {FPCGExSortRuleConfig{}};

	virtual bool GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const override;

private:
	friend class FPCGExSortPointsBaseElement;
};

struct FPCGExSortPointsContext final : FPCGExPointsProcessorContext
{
protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSortPointsBaseElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SortPoints)

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSortPoints
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSortPointsContext, UPCGExSortPointsBaseSettings>
	{
		TSharedPtr<PCGExSorting::FSorter> Sorter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
