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
	 * Material swaps per staging layer, written as rows of the Map pin's param data beside the Collection Map rows. A layer
	 * is a list of stages applied in sequence (chained Staging : Swap nodes); within a stage a slot is looked up once (A->B
	 * with B->A exchanges them) and a material's rules are tried in order: the first whose gate passes applies, A->A keeps A.
	 * Earlier rules win everywhere (earlier rows, earlier tables), so merging tables into one param data changes nothing.
	 */
	class PCGEXCOLLECTIONS_API FMaterialSwapTable
	{
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

		enum class EUnpackResult : uint8
		{
			Merged,    // A table, merged (possibly without a single reachable row).
			NotATable, // No Match + Replace columns: some other param data.
			Mistyped,  // A column with another type than PackToDataset writes: nothing merged.
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

		/** OutLayer becomes InLayer's stages followed by InStage (when it holds a rule). OutLayer's previous stages are
		 *  discarded, other layers kept. */
		void ChainLayer(FName InLayer, FName OutLayer, const FStage& InStage);

		/** The table's canonical path order: lowercased, so neither a path's casing nor the build's moves it. */
		static void SortCanonical(TArray<FSoftObjectPath>& InOutPaths);

		/** Content identity of a layer, stable across sessions and platforms, blind to letter case; 0 when the layer has no
		 *  rule. */
		uint32 GetLayerId(FName InLayer) const;

		static bool IsTableData(const UPCGParamData* InParamData);

		/** Merges every table on the pin, earlier tables first. Warns once when rules overrule each other, and once when
		 *  tables are rejected for a mistyped column. */
		void UnpackPin(FPCGContext* InContext, FName InPinLabel);

		/**
		 * Merges one table behind the rules already held; materials whose rules lose (to an earlier table or row) go to
		 * OutConflicts. Rows without a Match or Replace are skipped: Collection Map rows share the param data. Columns
		 * beyond Match and Replace are optional. A column of another type than PackToDataset writes rejects the whole table
		 * (OutMistypedColumn names it) rather than dropping rows or ungating a rule.
		 */
		EUnpackResult UnpackDataset(const UPCGParamData* InParamData, TSet<FSoftObjectPath>* OutConflicts = nullptr, FName* OutMistypedColumn = nullptr);

		/** Appends every rule as rows of OutParamData, sorted by layer, stage and match path, rules in priority order:
		 *  identical tables write identical rows. Other rows already there (the Collection Map's) are kept. */
		void PackToDataset(UPCGParamData* OutParamData) const;

		/** Merges the tables on the input Map pin and appends their rows to InOutMap, the node's output Map. */
		static void ForwardInto(FPCGContext* InContext, UPCGParamData* InOutMap);

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
}
