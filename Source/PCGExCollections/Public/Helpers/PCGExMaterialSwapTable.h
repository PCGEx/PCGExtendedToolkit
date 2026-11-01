// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "PCGExCollectionsCommon.h"
#include "Core/PCGExCollectionTags.h"

class UMaterialInterface;
class UPCGParamData;
struct FPCGContext;
struct FSoftISMComponentDescriptor;
struct FPCGExAssetCollectionEntry;
struct FPCGExBaseTagFilterDetails;

namespace PCGExCollections
{
	/**
	 * Entry-tag gate of one swap rule. The authored clause values travel in the table; Lists, their canonical parse
	 * (sorted, deduplicated), is what Passes, SameAs and the layer id read, so build gates through Make / FromConstants.
	 * An empty gate passes every entry.
	 */
	struct PCGEXCOLLECTIONS_API FMaterialSwapGate
	{
		FName RequireAll = NAME_None;
		FName RequireAny = NAME_None;
		FName Exclude = NAME_None;
		bool bCommaSeparated = false;
		uint8 TagSources = static_cast<uint8>(EPCGExAssetTagInheritance::Asset);

		Tags::FTagLists Lists;

		static FMaterialSwapGate Make(FName InRequireAll, FName InRequireAny, FName InExclude, bool bInCommaSeparated, uint8 InTagSources);

		/** Constant clause values of InFilter. Clauses set to Attribute read nothing here and are counted. */
		static FMaterialSwapGate FromConstants(const FPCGExBaseTagFilterDetails& InFilter, int32& InOutNumAttributeClauses);

		bool IsEmpty() const { return Lists.IsEmpty(); }

		/** InHost is the collection holding InEntry (null when unknown): the Collection tag source reads its tags. */
		bool Passes(const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost) const;

		/** Tag sources the tag walk reads (Asset, Collection); other bits change nothing. */
		uint8 GetEffectiveTagSources() const;

		/** Same canonical lists and effective tag sources, so gates that test alike compare equal; every empty gate equals
		 *  every other. */
		bool SameAs(const FMaterialSwapGate& InOther) const;
	};

	/** One candidate replacement for a matched material. */
	struct FMaterialSwapRule
	{
		FSoftObjectPath Replacement;
		FMaterialSwapGate Gate;
	};

	/** One swap as authored: the material it matches and the rule applied to it. */
	struct FMaterialSwapRow
	{
		FSoftObjectPath Match;
		FMaterialSwapRule Rule;
	};

	/**
	 * One material swap table: per staging layer, a list of stages applied in sequence (chained Staging : Swap nodes). Within
	 * a stage a slot is looked up once (A->B with B->A exchanges them) and a material's rules are tried in order: the first
	 * whose gate passes applies, A->A keeps A. FMaterialSwapLibrary carries tables on Map pins.
	 */
	class PCGEXCOLLECTIONS_API FMaterialSwapTable
	{
		friend class FMaterialSwapLibrary;

	public:
		/** Rules per matched material, in priority order. Never holds an empty rule list. */
		using FStage = TMap<FSoftObjectPath, TArray<FMaterialSwapRule>>;
		using FStages = TArray<FStage>;

		enum class EAddResult : uint8
		{
			Added,     // Reachable: appended at the lowest priority.
			Ignored,   // Null match or replacement.
			Redundant, // An earlier rule that always wins over this one gives the same replacement.
			Shadowed,  // Same, with a different replacement: this rule could never apply.
		};

		/** Appends InRule at the lowest priority of InMatch, unless an earlier rule makes it unreachable (an ungated rule, or
		 *  one with the same gate). */
		static EAddResult AddRule(FStage& InOutStage, const FSoftObjectPath& InMatch, const FMaterialSwapRule& InRule);

		/** Puts InHigher's rules ahead of InOutStage's, material by material. Returns how many of InOutStage's materials lost
		 *  a rule to a higher one naming another replacement (ungated, or with the same gate); OutConflicts collects them. */
		static int32 MergeStage(FStage& InOutStage, const FStage& InHigher, TSet<FSoftObjectPath>* OutConflicts = nullptr);

		bool IsEmpty() const { return Layers.IsEmpty(); }

		const FStages* Find(const FName InLayer) const { return Layers.Find(InLayer); }

		/** Stage-wise merge behind this table's rules: stage i merges with stage i, which is exact when one table's stages
		 *  extend the other's. Materials whose InOther rules lose to one already held go to OutConflicts. */
		void Append(const FMaterialSwapTable& InOther, TSet<FSoftObjectPath>* OutConflicts = nullptr);

		/** InUpstream followed by InStage (when it holds a rule): what a point that passed both swaps follows. */
		static FStages Chain(const FStages& InUpstream, const FStage& InStage);

		/** OutLayer becomes Chain(InLayer's stages, InStage). OutLayer's previous stages are discarded, other layers kept. */
		void ChainLayer(FName InLayer, FName OutLayer, const FStage& InStage);

		/** The table's canonical path order: lowercased, so neither a path's casing nor the build's moves it. */
		static void SortCanonical(TArray<FSoftObjectPath>& InOutPaths);

		/** Content identity of a stage list, stable across sessions and platforms, blind to letter case; 0 when it holds no
		 *  rule. A point's stamp holds the id of the table it follows. */
		static uint32 GetStagesId(const FStages& InStages);

		/** GetStagesId of InLayer's stages; 0 when the layer has no rule. */
		uint32 GetLayerId(FName InLayer) const;

		/** InMaterial after every stage, for InEntry held by InHost (see FMaterialSwapGate::Passes). InMaterial itself when no
		 *  rule applies. */
		static FSoftObjectPath Resolve(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, const FSoftObjectPath& InMaterial);

		/** Swaps the descriptor's slots and overlay for InEntry held by InHost. A slot's current material is its override,
		 *  else InMeshMaterials'; OverrideMaterials only grows. True when anything changed. */
		static bool Apply(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, TConstArrayView<TSoftObjectPtr<UMaterialInterface>> InMeshMaterials, FSoftISMComponentDescriptor& InOutDescriptor);

	private:
		// Never holds an empty layer or an empty stage.
		TMap<FName, FStages> Layers;
	};

	/**
	 * Every material swap table a Map pin carries, kept apart by staging layer and content id: a point's stamp names the one
	 * it follows (each Staging : Swap it passed chained its own stage onto the table the point followed before). Rows carry
	 * their table's id and share the Collection Map's param data.
	 */
	class PCGEXCOLLECTIONS_API FMaterialSwapLibrary
	{
	public:
		using FStages = FMaterialSwapTable::FStages;

		enum class EUnpackResult : uint8
		{
			Merged,    // Table rows, merged (possibly without a single reachable row).
			NotATable, // No Match + Replace columns: some other param data.
			Mistyped,  // A column with another type than PackToDataset writes: nothing merged.
		};

		bool IsEmpty() const { return Layers.IsEmpty(); }

		/** InLayer's table InId, null when none is held (0 never names one). */
		const FStages* Find(FName InLayer, uint32 InId) const;

		/** Adds InStages as a table of InLayer, keyed by its content id, and returns the id; a table already held under that
		 *  id is kept. 0, and nothing added, when InStages holds no rule. */
		uint32 Add(FName InLayer, const FStages& InStages);

		/** Adds every layer of InTable (see the other overload). */
		void Add(const FMaterialSwapTable& InTable);

		/** Calls InFn(Id, Stages) for each table of InLayer, in arrival order. */
		template <typename FnT>
		void ForEachTable(const FName InLayer, FnT&& InFn) const
		{
			if (const TArray<FEntry>* Entries = Layers.Find(InLayer))
			{
				for (const FEntry& Entry : *Entries)
				{
					InFn(Entry.Id, Entry.Stages);
				}
			}
		}

		/** Every table merged into one, earlier tables first: a union view, never what a point follows. Materials whose
		 *  rules lose to an earlier table's go to OutConflicts. */
		FMaterialSwapTable Merge(TSet<FSoftObjectPath>* OutConflicts = nullptr) const;

		static bool IsTableData(const UPCGParamData* InParamData);

		/** Unpacks every table on the pin. Warns once when rows shadow each other within a table, and once when sets are
		 *  rejected for a mistyped column. */
		void UnpackPin(FPCGContext* InContext, FName InPinLabel);

		/**
		 * Unpacks InParamData's tables, grouped by layer and table id; rows without an id make one table per layer, keyed by
		 * its content. Rows without a Match or Replace are skipped: Collection Map rows share the param data. Rows shadowed
		 * within their table go to OutConflicts. A column of another type than PackToDataset writes rejects the whole set
		 * (OutMistypedColumn names it) rather than dropping rows or ungating a rule.
		 */
		EUnpackResult UnpackDataset(const UPCGParamData* InParamData, TSet<FSoftObjectPath>* OutConflicts = nullptr, FName* OutMistypedColumn = nullptr);

		/** Appends every table as rows of OutParamData tagged with its id: layers sorted, tables in arrival order, rows by
		 *  stage and match path, rules in priority order. Rows already there (the Collection Map's) are kept. */
		void PackToDataset(UPCGParamData* OutParamData) const;

		/** Unpacks the tables on the input Map pin and appends their rows, ids kept, to InOutMap, the node's output Map. */
		static void ForwardInto(FPCGContext* InContext, UPCGParamData* InOutMap);

	private:
		struct FEntry
		{
			uint32 Id = 0;
			FStages Stages;
		};

		// Per layer, tables in arrival order under unique ids. Never holds an empty layer.
		TMap<FName, TArray<FEntry>> Layers;

		void AddUnique(FName InLayer, uint32 InId, FStages&& InStages);
	};
}
