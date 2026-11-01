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
	 * Entry-tag gate of one swap rule, from constant FPCGExBaseTagFilterDetails clause values. The authored values travel
	 * in the table; Lists, their canonical parse (sorted, deduplicated), is what Passes, SameAs and the layer id read.
	 * Build gates through Make / FromConstants, which keep the two in sync. An empty gate lets every entry through.
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
	 * Material swaps per staging layer, carried as their own param data on Map pins next to the Collection Map
	 * (Labels::Tag_MaterialSwap...). A layer is a list of stages applied one after the other (chained Staging : Swap nodes);
	 * within a stage a slot is looked up once, so A->B with B->A exchanges the two, and a material's rules are tried in
	 * priority order: the first whose gate passes for the entry applies. An identity rule (A->A) keeps the material.
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

		/** Stage-wise merge, InOther's rules first: stage i merges with stage i, which is exact when one table's stages
		 *  extend the other's. Materials MergeStage reports as overruled go to OutConflicts. */
		void Append(const FMaterialSwapTable& InOther, TSet<FSoftObjectPath>* OutConflicts = nullptr);

		/** OutLayer becomes InLayer's stages followed by InStage (when it holds a rule). OutLayer's previous stages are
		 *  discarded, other layers kept. */
		void ChainLayer(FName InLayer, FName OutLayer, const FStage& InStage);

		/** Content identity of a layer, stable across sessions and platforms, blind to letter case; 0 when the layer has no
		 *  rule. */
		uint32 GetLayerId(FName InLayer) const;

		static bool IsTableData(const UPCGParamData* InParamData);

		/** Merges every table on the pin, later tables first. Warns once when rules overrule each other, and once when
		 *  tables are rejected for a mistyped column. */
		void UnpackPin(FPCGContext* InContext, FName InPinLabel);

		/**
		 * Merges one table, its rules first. Materials whose rules got overruled, by this table or within it (an earlier row
		 * wins), go to OutConflicts. Columns other than Match and Replace are optional (default layer, stage 0, no gate);
		 * stage values only order the stages. A column with another type than PackToDataset writes rejects the whole
		 * table (OutMistypedColumn names it): reading it as absent would drop rows, or turn a gate into no gate.
		 */
		EUnpackResult UnpackDataset(const UPCGParamData* InParamData, TSet<FSoftObjectPath>* OutConflicts = nullptr, FName* OutMistypedColumn = nullptr);

		/** Writes every rule, sorted by layer, stage and match path, rules in priority order: identical tables write
		 *  identical data. */
		void PackToDataset(UPCGParamData* OutParamData) const;

		/** InMaterial after every stage, for InEntry held by InHost (see FMaterialSwapGate::Passes). InMaterial itself when no
		 *  rule applies. */
		static FSoftObjectPath Resolve(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, const FSoftObjectPath& InMaterial);

		/**
		 * Swaps the descriptor's slots and overlay for InEntry held by InHost. A slot's current material is its override, else
		 * InMeshMaterials' entry; OverrideMaterials grows only to write a swap. Returns true when anything changed.
		 */
		static bool Apply(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, TConstArrayView<TSoftObjectPtr<UMaterialInterface>> InMeshMaterials, FSoftISMComponentDescriptor& InOutDescriptor);

	private:
		// Never holds an empty layer or an empty stage.
		TMap<FName, FStages> Layers;
	};
}
