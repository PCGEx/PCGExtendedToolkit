// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExValueHashFilter.generated.h"

UENUM()
enum class EPCGExValueHashMode : uint8
{
	Merged     = 0 UMETA(DisplayName = "Merged", ToolTip="All input set will be merged into a single set."),
	Individual = 1 UMETA(DisplayName = "Individual", ToolTip="Input set are kept separated, and tested individually.")
};

UENUM()
enum class EPCGExValueHashSetInclusionMode : uint8
{
	Any = 0 UMETA(DisplayName = "Any", ToolTip="Value must be present in at least one set for the filter to pass."),
	All = 1 UMETA(DisplayName = "All", ToolTip="Value must be present in all input set for the filter to pass.")
};

namespace PCGExData
{
	class IBuffer;
}

USTRUCT(BlueprintType)
struct FPCGExValueHashFilterConfig
{
	GENERATED_BODY()

	FPCGExValueHashFilterConfig()
	{
	}

	/** How to process input sets */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExValueHashMode Mode = EPCGExValueHashMode::Merged;

	/** How to test against input sets */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Inclusion", EditCondition="Mode == EPCGExValueHashMode::Individual"))
	EPCGExValueHashSetInclusionMode Inclusion = EPCGExValueHashSetInclusionMode::Any;

	/** Operand A for testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName OperandA = FName("Value");

	/** Name of the attribute to read on sets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName SetAttributeName = NAME_None;

	/** Whether to invert the result of the filter */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExValueHashFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	TArray<TSet<uint32>> Hashes;

	UPROPERTY()
	FPCGExValueHashFilterConfig Config;

	virtual bool WantsPreparation(FPCGExContext* InContext) override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

protected:
	TArray<TSharedPtr<PCGExData::FFacade>> SetSources;
};

namespace PCGExPointFilter
{
	class FValueHashFilter final : public ISimpleFilter
	{
	public:
		explicit FValueHashFilter(const TObjectPtr<const UPCGExValueHashFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
			Hashes = &TypedFilterFactory->Hashes;
		}

		const TObjectPtr<const UPCGExValueHashFilterFactory> TypedFilterFactory;

		const TArray<TSet<PCGExValueHash>>* Hashes = nullptr;

		TSharedPtr<PCGExData::IBuffer> OperandA;
		bool bInvert = false;
		bool bAnyPass = true;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FValueHashFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-contains-hash"))
class UPCGExValueHashFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ValueHashFilterFactory, "Filter : Contains (Hash)", "Creates a filter definition that checks whether a given value hash is contained within a one or more set of values. Important note : this is a hash comparison, so it's highly type sensitive! Float 0 != Double 0", PCGEX_FACTORY_NAME_PRIORITY)
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExValueHashFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
