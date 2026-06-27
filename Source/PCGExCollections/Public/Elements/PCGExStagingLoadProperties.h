// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExAssetGrammar.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Helpers/PCGExCollectionsHelpers.h"

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

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Sampler;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get properties.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/**
	 * Properties to output as point attributes.
	 * Property names must match properties defined in the source collection.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPropertyOutputSettings PropertyOutputSettings;

	/** If enabled, write each entry's collection Tags joined into a string attribute on the point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteEntryTags = false;

	// --- Entry Data ---

	/** Write the entry's asset path (Staging.Path). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAssetPath = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteAssetPath"))
	FName AssetPathAttributeName = FName("AssetPath");

	/** Write the entry's authored weight. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteWeight = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteWeight"))
	FName WeightAttributeName = FName("Weight");

	/** Write the entry's category. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCategory = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteCategory"))
	FName CategoryAttributeName = FName("Category");

	/** Write the entry's staging bounds extents. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteExtents = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteExtents"))
	FName ExtentsAttributeName = FName("Extents");

	/** Write the entry's staging bounds minimum. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMin = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteBoundsMin"))
	FName BoundsMinAttributeName = FName("BoundsMin");

	/** Write the entry's staging bounds maximum. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMax = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteBoundsMax"))
	FName BoundsMaxAttributeName = FName("BoundsMax");

	/** Write the host collection's type id (e.g. Mesh, Actor, Level, PCGDataAsset, Niagara). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCollectionType = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteCollectionType"))
	FName CollectionTypeAttributeName = FName("CollectionType");

	/** Output attribute name for the joined entry tags. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteEntryTags"))
	FName EntryTagsAttributeName = FName("EntryTags");

	/** Separator inserted between tags when joining. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Entry Data", meta=(PCG_Overridable, EditCondition="bWriteEntryTags", EditConditionHides, HideEditConditionToggle))
	FString EntryTagsSeparator = TEXT(",");

	// --- Grammar Data ---

	/**
	 * Which subdivision axes to emit per row. Bits are intersected with each entry's own Axes
	 * bitmask at write time, so axes the entry doesn't enable produce no output for that row.
	 * When exactly one axis ends up being emitted across all rows, the _X/_Y/_Z suffix is
	 * dropped from per-axis attribute names (Size, Scalable) for the legacy single-axis shape --
	 * unless bAlwaysSuffixAxes is set.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExCollections.EPCGExGrammarAxes"))
	uint8 OutputAxes = static_cast<uint8>(EPCGExGrammarAxes::X);

	/** Always append _X/_Y/_Z to per-axis attribute names, even when only one axis is emitted.
	 *  Use this when downstream graphs expect suffixed names regardless of axis count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable))
	bool bAlwaysSuffixAxes = false;

	/** Write the entry's resolved grammar Symbol. Resolved once per unique entry via the entry's effective grammar. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSymbol = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, EditCondition="bWriteSymbol"))
	FName SymbolAttributeName = FName("Symbol");

	/** Write the entry's resolved grammar Size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSize = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, EditCondition="bWriteSize"))
	FName SizeAttributeName = FName("Size");

	/** Write the entry's resolved grammar Scalable flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteScalable = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, EditCondition="bWriteScalable"))
	FName ScalableAttributeName = FName("Scalable");

	/** Write the entry's resolved grammar DebugColor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDebugColor = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Grammar Data", meta=(PCG_Overridable, EditCondition="bWriteDebugColor"))
	FName DebugColorAttributeName = FName("DebugColor");
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

	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()
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

		/** Optional joined entry-tags writer + per-hash cache (only allocated when bWriteEntryTags). */
		TSharedPtr<PCGExData::TBuffer<FString>> EntryTagsWriter;
		TMap<uint64, FString> EntryTagsByHash;

		/** Per-field writers + value-by-hash caches. Only allocated when the corresponding toggle is on. */
#define PCGEX_LOAD_PROP_FIELD_DECL(_NAME, _TYPE, _DEFAULT, _GETTER) \
		TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Writer; \
		TMap<uint64, _TYPE> _NAME##ByHash;

		/** Per-axis grammar writers + caches. Index = axis bit (0=X, 1=Y, 2=Z); only slots whose
		 *  bit appears in (entry Axes & Settings->OutputAxes) get allocated. ActiveAxes lists the
		 *  populated slot indices so the per-point write loop avoids 2-of-3 null checks in the
		 *  single-axis common case. */
#define PCGEX_LOAD_PROP_PERAXIS_DECL(_NAME, _TYPE, _DEFAULT, _GETTER) \
		TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Writer[3]; \
		TMap<uint64, _TYPE> _NAME##ByHash[3]; \
		TArray<int32, TInlineAllocator<3>> _NAME##ActiveAxes;

#define PCGEX_FOREACH_ENTRY_DATA_FIELD(MACRO)\
MACRO(AssetPath, FSoftObjectPath, FSoftObjectPath(), Entry->Staging.Path)\
MACRO(Weight, int32, 0, Entry->Weight)\
MACRO(Category, FName, NAME_None, Entry->Category)\
MACRO(Extents, FVector, FVector::ZeroVector, Entry->Staging.Bounds.GetExtent())\
MACRO(BoundsMin, FVector, FVector::ZeroVector, Entry->Staging.Bounds.Min)\
MACRO(BoundsMax, FVector, FVector::ZeroVector, Entry->Staging.Bounds.Max)\
MACRO(CollectionType, FName, NAME_None, Host ? Host->GetTypeId() : NAME_None)

// Symbol/DebugColor are axis-invariant -- one writer + cache each, sourced from the effective
// grammar pointer (not a per-axis Module).
#define PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(MACRO)\
MACRO(Symbol, FName, NAME_None, Grammar->Symbol)\
MACRO(DebugColor, FVector4, FVector4(1, 1, 1, 1), FVector4(Grammar->DebugColor))

// Per-axis grammar attributes. Getter expressions assume a local `Module` (FPCGSubdivisionSubmodule)
// populated by FixLeaf/FixSubCollection at the call site.
#define PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(MACRO)\
MACRO(Size, double, 0.0, Module.Size)\
MACRO(Scalable, bool, true, Module.bScalable)

		PCGEX_FOREACH_ENTRY_DATA_FIELD(PCGEX_LOAD_PROP_FIELD_DECL)
		PCGEX_FOREACH_GRAMMAR_SHARED_FIELD(PCGEX_LOAD_PROP_FIELD_DECL)
		PCGEX_FOREACH_GRAMMAR_PERAXIS_FIELD(PCGEX_LOAD_PROP_PERAXIS_DECL)

#undef PCGEX_LOAD_PROP_FIELD_DECL
#undef PCGEX_LOAD_PROP_PERAXIS_DECL

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

		/** Pre-join entry tags per unique hash (only when Settings->bWriteEntryTags is on). */
		void BuildEntryTagsCache();

		/** Allocate enabled entry-data and grammar-data writers + per-hash value caches. */
		void BuildEntryFieldsCache();

		/** True if any entry-data or grammar-data writer was allocated. */
		bool HasAnyEntryFieldWriter() const;
	};
}
