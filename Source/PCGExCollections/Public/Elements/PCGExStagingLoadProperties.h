// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "PCGExPropertyWriter.h"
#include "Containers/PCGExScopedContainers.h"

#include "PCGExStagingLoadProperties.generated.h"

/**
 * Settings for the Staging Properties node.
 * Outputs property values from staged asset collection entries as point attributes.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage property attribute output", PCGExNodeLibraryDoc="staging/staging-load-properties"))
class UPCGExStagingLoadPropertiesSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(StagingLoadProperties, "Staging : Load Properties", "Output property values from staged entries as point attributes.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get properties.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override { return true; }

public:
	
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;
	
	/**
	 * Properties to output as point attributes.
	 * Property names must match properties defined in the source collection.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPropertyOutputSettings PropertyOutputSettings;
};

struct FPCGExStagingLoadPropertiesContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingLoadPropertiesElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;
	FPCGExPropertyOutputSettings PropertyOutputSettings;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingLoadPropertiesElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingLoadProperties)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExStagingLoadProperties
{
	/**
	 * Cached property resolution data for a single property across all unique entries.
	 * Pre-computed during Process() to avoid per-point resolution overhead.
	 */
	struct FPropertyCache
	{
		/** The writer instance (owns the output buffer) */
		FInstancedStruct Writer;

		/** Cached source property pointer per unique entry hash */
		TMap<uint64, const FPCGExProperty*> SourceByHash;

		/** Quick access to the writer's property */
		const FPCGExProperty* WriterPtr = nullptr;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingLoadPropertiesContext, UPCGExStagingLoadPropertiesSettings>
	{
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

		/** Pre-resolved property caches keyed by property name */
		TMap<FName, FPropertyCache> PropertyCaches;

		/** Unique entry hashes found in this point set */
		TSharedPtr<PCGExMT::TScopedSet<uint64>> ScopedUniqueEntryHashes;
		TSet<uint64> UniqueEntryHashes;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

	private:
		/** Pre-resolve properties for all unique hashes */
		void BuildPropertyCaches();
	};
}
