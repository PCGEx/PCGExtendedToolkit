// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExMaterialSwapTable.h"

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "PCGParamData.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Misc/StringBuilder.h"
#include "Selectors/PCGExSelectorTagFilter.h"

// Canonical keys are lowercased with FString::ToLower (TChar::ToLower is ASCII-only, so locale-free): names and paths
// compare case-insensitively, and runtime builds don't keep FName casing, so ToString's casing must never count.
namespace PCGExMaterialSwapTable
{
	void GetSortedMatches(const PCGExCollections::FMaterialSwapTable::FStage& InStage, TArray<FSoftObjectPath>& OutMatches)
	{
		OutMatches.Reset(InStage.Num());
		for (const TPair<FSoftObjectPath, TArray<PCGExCollections::FMaterialSwapRule>>& Pair : InStage)
		{
			OutMatches.Add(Pair.Key);
		}
		PCGExCollections::FMaterialSwapTable::SortCanonical(OutMatches);
	}

	// Length-prefixed, so field boundaries reach the hash: "AB" + "C" never reads as "A" + "BC".
	void AppendField(FStringBuilderBase& OutKey, const FString& InField)
	{
		const FString Canonical = InField.ToLower();
		OutKey.Appendf(TEXT("%d:"), Canonical.Len());
		OutKey << Canonical;
	}

	void AppendGate(FStringBuilderBase& OutKey, const PCGExCollections::FMaterialSwapGate& InGate)
	{
		if (InGate.IsEmpty())
		{
			OutKey.AppendChar(TEXT('~'));
			return;
		}

		// Every count is terminated, so its digits never run into the next length prefix.
		for (const TArray<FName>* List : {&InGate.Lists.RequireAll, &InGate.Lists.RequireAny, &InGate.Lists.Exclude})
		{
			OutKey.Appendf(TEXT("[%d;"), List->Num());
			for (const FName Tag : *List)
			{
				AppendField(OutKey, Tag.ToString());
			}
		}
		OutKey.Appendf(TEXT("|%d;"), static_cast<int32>(InGate.GetEffectiveTagSources()));
	}

	// The key GetStagesId hashes, counted and length-prefixed; empty stages add nothing. False when no stage holds a rule.
	bool BuildStagesKey(const PCGExCollections::FMaterialSwapTable::FStages& InStages, FStringBuilderBase& OutKey)
	{
		bool bAnyRule = false;
		TArray<FSoftObjectPath> Matches;
		for (const PCGExCollections::FMaterialSwapTable::FStage& Stage : InStages)
		{
			if (Stage.IsEmpty())
			{
				continue;
			}

			bAnyRule = true;
			OutKey.Appendf(TEXT("#%d;"), Stage.Num());

			GetSortedMatches(Stage, Matches);
			for (const FSoftObjectPath& Match : Matches)
			{
				const TArray<PCGExCollections::FMaterialSwapRule>& Rules = Stage.FindChecked(Match);
				AppendField(OutKey, Match.ToString());
				OutKey.Appendf(TEXT("(%d;"), Rules.Num());

				for (const PCGExCollections::FMaterialSwapRule& Rule : Rules)
				{
					AppendField(OutKey, Rule.Replacement.ToString());
					AppendGate(OutKey, Rule.Gate);
				}
			}
		}
		return bAnyRule;
	}

	// FName equality ignores case and LexicalLess sorts the same way, so equal tags end up next to each other.
	void CanonicalizeList(TArray<FName>& InOutTags)
	{
		InOutTags.Sort(FNameLexicalLess());
		for (int32 Index = InOutTags.Num() - 1; Index > 0; --Index)
		{
			if (InOutTags[Index] == InOutTags[Index - 1])
			{
				InOutTags.RemoveAt(Index);
			}
		}
	}

	// False when the column exists with another type: read as absent, it would drop rows or turn a gate into no gate.
	template <typename T>
	bool ReadColumn(const UPCGMetadata* InMetadata, const FName InName, const FPCGMetadataAttribute<T>*& OutColumn, FName& OutMistyped)
	{
		OutColumn = InMetadata->GetConstTypedAttribute<T>(InName);
		if (OutColumn || !InMetadata->HasAttribute(InName))
		{
			return true;
		}

		OutMistyped = InName;
		return false;
	}
}

namespace PCGExCollections
{
#pragma region FMaterialSwapGate

	FMaterialSwapGate FMaterialSwapGate::Make(const FName InRequireAll, const FName InRequireAny, const FName InExclude, const bool bInCommaSeparated, const uint8 InTagSources)
	{
		FMaterialSwapGate Gate;
		Gate.RequireAll = InRequireAll;
		Gate.RequireAny = InRequireAny;
		Gate.Exclude = InExclude;
		Gate.bCommaSeparated = bInCommaSeparated;
		Gate.TagSources = InTagSources;

		Gate.Lists.Parse(InRequireAll, InRequireAny, InExclude, bInCommaSeparated);
		PCGExMaterialSwapTable::CanonicalizeList(Gate.Lists.RequireAll);
		PCGExMaterialSwapTable::CanonicalizeList(Gate.Lists.RequireAny);
		PCGExMaterialSwapTable::CanonicalizeList(Gate.Lists.Exclude);
		return Gate;
	}

	FMaterialSwapGate FMaterialSwapGate::FromConstants(const FPCGExBaseTagFilterDetails& InFilter, int32& InOutNumAttributeClauses)
	{
		FName All;
		FName Any;
		FName Excluded;
		InOutNumAttributeClauses += InFilter.GetConstantClauses(All, Any, Excluded);
		return Make(All, Any, Excluded, InFilter.bParseCommaSeparatedLists, InFilter.TagSources);
	}

	bool FMaterialSwapGate::Passes(const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost) const
	{
		return Lists.IsEmpty() || Tags::Matches(InEntry, TagSources, Lists, InHost, true);
	}

	uint8 FMaterialSwapGate::GetEffectiveTagSources() const
	{
		return static_cast<uint8>(TagSources & static_cast<uint8>(EPCGExAssetTagInheritance::Asset | EPCGExAssetTagInheritance::Collection));
	}

	bool FMaterialSwapGate::SameAs(const FMaterialSwapGate& InOther) const
	{
		if (IsEmpty() || InOther.IsEmpty())
		{
			return IsEmpty() == InOther.IsEmpty();
		}

		return Lists.RequireAll == InOther.Lists.RequireAll
			&& Lists.RequireAny == InOther.Lists.RequireAny
			&& Lists.Exclude == InOther.Lists.Exclude
			&& GetEffectiveTagSources() == InOther.GetEffectiveTagSources();
	}

#pragma endregion

#pragma region FMaterialSwapTable

	FMaterialSwapTable::EAddResult FMaterialSwapTable::AddRule(FStage& InOutStage, const FSoftObjectPath& InMatch, const FMaterialSwapRule& InRule)
	{
		// Checked before FindOrAdd so an ignored row never leaves an empty rule list. An identity rule is kept: it keeps the
		// material for the entries it passes, ahead of the rules after it.
		if (InMatch.IsNull() || InRule.Replacement.IsNull())
		{
			return EAddResult::Ignored;
		}

		TArray<FMaterialSwapRule>& Rules = InOutStage.FindOrAdd(InMatch);
		for (const FMaterialSwapRule& Earlier : Rules)
		{
			// An ungated rule always applies, and a rule with the same gate applies whenever this one could.
			if (Earlier.Gate.IsEmpty() || Earlier.Gate.SameAs(InRule.Gate))
			{
				return Earlier.Replacement == InRule.Replacement ? EAddResult::Redundant : EAddResult::Shadowed;
			}
		}

		Rules.Add(InRule);
		return EAddResult::Added;
	}

	int32 FMaterialSwapTable::MergeStage(FStage& InOutStage, const FStage& InHigher, TSet<FSoftObjectPath>* OutConflicts)
	{
		check(&InOutStage != &InHigher);

		int32 NumConflicts = 0;
		for (const TPair<FSoftObjectPath, TArray<FMaterialSwapRule>>& Higher : InHigher)
		{
			TArray<FMaterialSwapRule> Lower;
			if (TArray<FMaterialSwapRule>* Existing = InOutStage.Find(Higher.Key))
			{
				Lower = MoveTemp(*Existing);
				InOutStage.Remove(Higher.Key);
			}

			// Re-added through AddRule so a hand-built stage can't smuggle null or unreachable rules in.
			for (const FMaterialSwapRule& Rule : Higher.Value)
			{
				AddRule(InOutStage, Higher.Key, Rule);
			}

			bool bOverruled = false;
			for (const FMaterialSwapRule& Rule : Lower)
			{
				bOverruled |= AddRule(InOutStage, Higher.Key, Rule) == EAddResult::Shadowed;
			}

			if (bOverruled)
			{
				NumConflicts++;
				if (OutConflicts) { OutConflicts->Add(Higher.Key); }
			}
		}
		return NumConflicts;
	}

	FMaterialSwapTable::FStages FMaterialSwapTable::Chain(const FStages& InUpstream, const FStage& InStage)
	{
		FStages Chained = InUpstream;

		FStage Sanitized;
		MergeStage(Sanitized, InStage);
		if (!Sanitized.IsEmpty())
		{
			Chained.Add(MoveTemp(Sanitized));
		}
		return Chained;
	}

	void FMaterialSwapTable::SortCanonical(TArray<FSoftObjectPath>& InOutPaths)
	{
		TArray<TPair<FString, FSoftObjectPath>> Keyed;
		Keyed.Reserve(InOutPaths.Num());
		for (FSoftObjectPath& Path : InOutPaths)
		{
			Keyed.Emplace(Path.ToString().ToLower(), MoveTemp(Path));
		}

		// Distinct paths differ ignoring case too (FSoftObjectPath equality), so this is a total order over them.
		Keyed.Sort([](const TPair<FString, FSoftObjectPath>& A, const TPair<FString, FSoftObjectPath>& B)
		{
			return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
		});

		for (int32 i = 0; i < Keyed.Num(); i++)
		{
			InOutPaths[i] = MoveTemp(Keyed[i].Value);
		}
	}

	uint32 FMaterialSwapTable::GetStagesId(const FStages& InStages)
	{
		// StrCrc32, not GetTypeHash: FName hashes differ between sessions, and spawned ISMs persist this value.
		TStringBuilder<1024> Key;
		if (!PCGExMaterialSwapTable::BuildStagesKey(InStages, Key))
		{
			return 0;
		}

		const uint32 Crc = FCrc::StrCrc32(Key.ToString());
		return Crc ? Crc : 1;
	}

	bool FMaterialSwapTable::SameStages(const FStages& InA, const FStages& InB)
	{
		TStringBuilder<1024> KeyA;
		TStringBuilder<1024> KeyB;
		PCGExMaterialSwapTable::BuildStagesKey(InA, KeyA);
		PCGExMaterialSwapTable::BuildStagesKey(InB, KeyB);
		return KeyA.ToView().Equals(KeyB.ToView(), ESearchCase::CaseSensitive);
	}

	FSoftObjectPath FMaterialSwapTable::Resolve(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, const FSoftObjectPath& InMaterial)
	{
		FSoftObjectPath Material = InMaterial;
		for (const FStage& Stage : InStages)
		{
			const TArray<FMaterialSwapRule>* Rules = Stage.Find(Material);
			if (!Rules)
			{
				continue;
			}

			for (const FMaterialSwapRule& Rule : *Rules)
			{
				if (Rule.Gate.Passes(InEntry, InHost))
				{
					Material = Rule.Replacement;
					break;
				}
			}
		}
		return Material;
	}

	bool FMaterialSwapTable::Apply(const FStages& InStages, const FPCGExAssetCollectionEntry& InEntry, const UPCGExAssetCollection* InHost, const TConstArrayView<TSoftObjectPtr<UMaterialInterface>> InMeshMaterials, FSoftISMComponentDescriptor& InOutDescriptor)
	{
		bool bChanged = false;

		TArray<TSoftObjectPtr<UMaterialInterface>>& SlotOverrides = InOutDescriptor.OverrideMaterials;
		const int32 NumSlots = FMath::Max(SlotOverrides.Num(), InMeshMaterials.Num());
		for (int32 i = 0; i < NumSlots; i++)
		{
			// A null override renders the mesh's own material, so that is the one to match.
			FSoftObjectPath Current;
			if (SlotOverrides.IsValidIndex(i) && !SlotOverrides[i].IsNull())
			{
				Current = SlotOverrides[i].ToSoftObjectPath();
			}
			else if (InMeshMaterials.IsValidIndex(i))
			{
				Current = InMeshMaterials[i].ToSoftObjectPath();
			}

			if (Current.IsNull())
			{
				continue;
			}

			const FSoftObjectPath Swapped = Resolve(InStages, InEntry, InHost, Current);
			if (Swapped == Current)
			{
				continue;
			}

			if (SlotOverrides.Num() <= i)
			{
				SlotOverrides.SetNum(i + 1);
			}
			SlotOverrides[i] = TSoftObjectPtr<UMaterialInterface>(Swapped);
			bChanged = true;
		}

		if (!InOutDescriptor.OverlayMaterial.IsNull())
		{
			const FSoftObjectPath Current = InOutDescriptor.OverlayMaterial.ToSoftObjectPath();
			const FSoftObjectPath Swapped = Resolve(InStages, InEntry, InHost, Current);
			if (Swapped != Current)
			{
				InOutDescriptor.OverlayMaterial = TSoftObjectPtr<UMaterialInterface>(Swapped);
				bChanged = true;
			}
		}

		return bChanged;
	}

#pragma endregion

#pragma region FMaterialSwapLibrary

	const FMaterialSwapLibrary::FStages* FMaterialSwapLibrary::Find(const FName InLayer, const uint32 InId) const
	{
		const TArray<FEntry>* Entries = InId ? Layers.Find(InLayer) : nullptr;
		if (!Entries)
		{
			return nullptr;
		}

		const FEntry* Entry = Entries->FindByPredicate([InId](const FEntry& Candidate) { return Candidate.Id == InId; });
		return Entry ? &Entry->Stages : nullptr;
	}

	uint32 FMaterialSwapLibrary::Add(const FName InLayer, const FStages& InStages)
	{
		// Re-added through MergeStage so a hand-built list can't smuggle unreachable rules or empty stages in.
		FStages Sanitized;
		for (const FMaterialSwapTable::FStage& Stage : InStages)
		{
			FMaterialSwapTable::FStage Clean;
			FMaterialSwapTable::MergeStage(Clean, Stage);
			if (!Clean.IsEmpty())
			{
				Sanitized.Add(MoveTemp(Clean));
			}
		}

		const uint32 Id = FMaterialSwapTable::GetStagesId(Sanitized);
		if (Id)
		{
			AddUnique(InLayer, Id, MoveTemp(Sanitized));
		}
		return Id;
	}

	void FMaterialSwapLibrary::RemoveLayer(const FName InLayer)
	{
		Layers.Remove(InLayer);
	}

	bool FMaterialSwapLibrary::IsTableData(const UPCGParamData* InParamData)
	{
		return InParamData && InParamData->Metadata
			&& InParamData->Metadata->HasAttribute(Labels::Tag_MaterialSwapMatch)
			&& InParamData->Metadata->HasAttribute(Labels::Tag_MaterialSwapReplace);
	}

	void FMaterialSwapLibrary::UnpackPin(FPCGContext* InContext, const FName InPinLabel)
	{
		TSet<FSoftObjectPath> Conflicts;
		int32 NumRejected = 0;
		FName FirstMistyped = NAME_None;
		const int32 NumCollisionsBefore = NumCollisions;

		for (const FPCGTaggedData& TaggedData : InContext->InputData.GetParamsByPin(InPinLabel))
		{
			FName Mistyped = NAME_None;
			if (UnpackDataset(Cast<UPCGParamData>(TaggedData.Data), &Conflicts, &Mistyped) == EUnpackResult::Mistyped && NumRejected++ == 0)
			{
				FirstMistyped = Mistyped;
			}
		}

		if (NumRejected > 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("{0} material swap table(s) on '{1}' ignored: column '{2}' does not have the type Staging : Swap writes."), FText::AsNumber(NumRejected), FText::FromName(InPinLabel), FText::FromName(FirstMistyped)));
		}

		if (!Conflicts.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Material swap rules on '{0}' disagree on {1} material(s) within their table: the first row wins."), FText::FromName(InPinLabel), FText::AsNumber(Conflicts.Num())));
		}

		if (NumCollisions > NumCollisionsBefore)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("{0} material swap table(s) on '{1}' share an id with a different table and were dropped: their points follow the other table's swaps."), FText::AsNumber(NumCollisions - NumCollisionsBefore), FText::FromName(InPinLabel)));
		}
	}

	FMaterialSwapLibrary::EUnpackResult FMaterialSwapLibrary::UnpackDataset(const UPCGParamData* InParamData, TSet<FSoftObjectPath>* OutConflicts, FName* OutMistypedColumn)
	{
		if (!IsTableData(InParamData))
		{
			return EUnpackResult::NotATable;
		}

		const UPCGMetadata* Metadata = InParamData->Metadata;

		const FPCGMetadataAttribute<FSoftObjectPath>* MatchAttribute = nullptr;
		const FPCGMetadataAttribute<FSoftObjectPath>* ReplaceAttribute = nullptr;
		const FPCGMetadataAttribute<FName>* LayerAttribute = nullptr;
		const FPCGMetadataAttribute<int32>* StageAttribute = nullptr;
		const FPCGMetadataAttribute<FName>* AllAttribute = nullptr;
		const FPCGMetadataAttribute<FName>* AnyAttribute = nullptr;
		const FPCGMetadataAttribute<FName>* ExcludeAttribute = nullptr;
		const FPCGMetadataAttribute<bool>* CommaAttribute = nullptr;
		const FPCGMetadataAttribute<int32>* SourcesAttribute = nullptr;
		const FPCGMetadataAttribute<int32>* TableAttribute = nullptr;

		FName Mistyped = NAME_None;
		const bool bReadable =
			PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapMatch, MatchAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapReplace, ReplaceAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapLayer, LayerAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapStage, StageAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTagsAll, AllAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTagsAny, AnyAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTagsExclude, ExcludeAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTagsCommaSeparated, CommaAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTagSources, SourcesAttribute, Mistyped)
			&& PCGExMaterialSwapTable::ReadColumn(Metadata, Labels::Tag_MaterialSwapTable, TableAttribute, Mistyped);

		if (!bReadable)
		{
			if (OutMistypedColumn) { *OutMistypedColumn = Mistyped; }
			return EUnpackResult::Mistyped;
		}

		// Present (IsTableData) and typed (ReadColumn).
		check(MatchAttribute && ReplaceAttribute);

		// Per table (layer, id), stage buckets in arrival order. Stage values only order the stages: they never size
		// anything, and distinct values never merge.
		using FTableKey = TPair<FName, uint32>;
		TMap<FTableKey, TMap<int32, FMaterialSwapTable::FStage>> Buckets;
		TArray<FTableKey> Arrival;

		const int64 NumRows = Metadata->GetItemCountForChild();
		for (int64 i = 0; i < NumRows; i++)
		{
			// No rule without both (AddRule would ignore it): Collection Map rows carry neither.
			const FSoftObjectPath Match = MatchAttribute->GetValueFromItemKey(i);
			FMaterialSwapRule Rule;
			Rule.Replacement = ReplaceAttribute->GetValueFromItemKey(i);
			if (Match.IsNull() || Rule.Replacement.IsNull())
			{
				continue;
			}

			Rule.Gate = FMaterialSwapGate::Make(
				AllAttribute ? AllAttribute->GetValueFromItemKey(i) : FName(NAME_None),
				AnyAttribute ? AnyAttribute->GetValueFromItemKey(i) : FName(NAME_None),
				ExcludeAttribute ? ExcludeAttribute->GetValueFromItemKey(i) : FName(NAME_None),
				CommaAttribute ? CommaAttribute->GetValueFromItemKey(i) : false,
				SourcesAttribute ? static_cast<uint8>(SourcesAttribute->GetValueFromItemKey(i)) : static_cast<uint8>(EPCGExAssetTagInheritance::Asset));

			const FTableKey TableKey(
				LayerAttribute ? LayerAttribute->GetValueFromItemKey(i) : FName(NAME_None),
				TableAttribute ? static_cast<uint32>(TableAttribute->GetValueFromItemKey(i)) : 0u);

			TMap<int32, FMaterialSwapTable::FStage>* StagesByValue = Buckets.Find(TableKey);
			if (!StagesByValue)
			{
				Arrival.Add(TableKey);
				StagesByValue = &Buckets.Add(TableKey);
			}

			const int32 StageValue = StageAttribute ? StageAttribute->GetValueFromItemKey(i) : 0;

			// Rows arrive in priority order: one an earlier row of its table shadows is a conflict.
			if (FMaterialSwapTable::AddRule(StagesByValue->FindOrAdd(StageValue), Match, Rule) == FMaterialSwapTable::EAddResult::Shadowed && OutConflicts)
			{
				OutConflicts->Add(Match);
			}
		}

		for (const FTableKey& TableKey : Arrival)
		{
			TMap<int32, FMaterialSwapTable::FStage>& StagesByValue = Buckets.FindChecked(TableKey);
			StagesByValue.KeySort(TLess<int32>());

			FStages Stages;
			for (TPair<int32, FMaterialSwapTable::FStage>& Stage : StagesByValue)
			{
				if (!Stage.Value.IsEmpty())
				{
					Stages.Add(MoveTemp(Stage.Value));
				}
			}

			// Rows without a table id (hand-made data) make one table per layer, keyed by its content.
			const uint32 Id = TableKey.Value ? TableKey.Value : FMaterialSwapTable::GetStagesId(Stages);
			if (Id && !Stages.IsEmpty())
			{
				AddUnique(TableKey.Key, Id, MoveTemp(Stages));
			}
		}

		return EUnpackResult::Unpacked;
	}

	void FMaterialSwapLibrary::PackToDataset(UPCGParamData* OutParamData) const
	{
		if (!OutParamData || !OutParamData->Metadata || Layers.IsEmpty())
		{
			return;
		}

		UPCGMetadata* Metadata = OutParamData->Metadata;
		FPCGMetadataAttribute<FSoftObjectPath>* MatchAttribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(Labels::Tag_MaterialSwapMatch, FSoftObjectPath(), false, true, true);
		FPCGMetadataAttribute<FSoftObjectPath>* ReplaceAttribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(Labels::Tag_MaterialSwapReplace, FSoftObjectPath(), false, true, true);
		FPCGMetadataAttribute<FName>* LayerAttribute = Metadata->FindOrCreateAttribute<FName>(Labels::Tag_MaterialSwapLayer, NAME_None, false, true, true);
		FPCGMetadataAttribute<int32>* StageAttribute = Metadata->FindOrCreateAttribute<int32>(Labels::Tag_MaterialSwapStage, 0, false, true, true);
		FPCGMetadataAttribute<FName>* AllAttribute = Metadata->FindOrCreateAttribute<FName>(Labels::Tag_MaterialSwapTagsAll, NAME_None, false, true, true);
		FPCGMetadataAttribute<FName>* AnyAttribute = Metadata->FindOrCreateAttribute<FName>(Labels::Tag_MaterialSwapTagsAny, NAME_None, false, true, true);
		FPCGMetadataAttribute<FName>* ExcludeAttribute = Metadata->FindOrCreateAttribute<FName>(Labels::Tag_MaterialSwapTagsExclude, NAME_None, false, true, true);
		FPCGMetadataAttribute<bool>* CommaAttribute = Metadata->FindOrCreateAttribute<bool>(Labels::Tag_MaterialSwapTagsCommaSeparated, false, false, true, true);
		FPCGMetadataAttribute<int32>* SourcesAttribute = Metadata->FindOrCreateAttribute<int32>(Labels::Tag_MaterialSwapTagSources, static_cast<int32>(EPCGExAssetTagInheritance::Asset), false, true, true);
		FPCGMetadataAttribute<int32>* TableAttribute = Metadata->FindOrCreateAttribute<int32>(Labels::Tag_MaterialSwapTable, 0, false, true, true);
		if (!MatchAttribute || !ReplaceAttribute || !LayerAttribute || !StageAttribute || !AllAttribute || !AnyAttribute || !ExcludeAttribute || !CommaAttribute || !SourcesAttribute || !TableAttribute)
		{
			return;
		}

		// FName equality ignores case, so layer keys are unique under this case-insensitive, session-stable order.
		TArray<FName> LayerNames;
		Layers.GetKeys(LayerNames);
		LayerNames.Sort(FNameLexicalLess());

		TArray<FSoftObjectPath> Matches;
		for (const FName Layer : LayerNames)
		{
			for (const FEntry& Entry : Layers.FindChecked(Layer))
			{
				for (int32 StageIndex = 0; StageIndex < Entry.Stages.Num(); StageIndex++)
				{
					const FMaterialSwapTable::FStage& Stage = Entry.Stages[StageIndex];
					PCGExMaterialSwapTable::GetSortedMatches(Stage, Matches);
					for (const FSoftObjectPath& Match : Matches)
					{
						for (const FMaterialSwapRule& Rule : Stage.FindChecked(Match))
						{
							const int64 Key = Metadata->AddEntry();
							MatchAttribute->SetValue(Key, Match);
							ReplaceAttribute->SetValue(Key, Rule.Replacement);
							LayerAttribute->SetValue(Key, Layer);
							StageAttribute->SetValue(Key, StageIndex);
							AllAttribute->SetValue(Key, Rule.Gate.RequireAll);
							AnyAttribute->SetValue(Key, Rule.Gate.RequireAny);
							ExcludeAttribute->SetValue(Key, Rule.Gate.Exclude);
							CommaAttribute->SetValue(Key, Rule.Gate.bCommaSeparated);
							SourcesAttribute->SetValue(Key, static_cast<int32>(Rule.Gate.TagSources));
							TableAttribute->SetValue(Key, static_cast<int32>(Entry.Id));
						}
					}
				}
			}
		}
	}

	void FMaterialSwapLibrary::ForwardInto(FPCGContext* InContext, UPCGParamData* InOutMap)
	{
		FMaterialSwapLibrary Incoming;
		Incoming.UnpackPin(InContext, Labels::SourceCollectionMapLabel);
		Incoming.PackToDataset(InOutMap);
	}

	void FMaterialSwapLibrary::AddUnique(const FName InLayer, const uint32 InId, FStages&& InStages)
	{
		TArray<FEntry>& Entries = Layers.FindOrAdd(InLayer);
		if (const FEntry* Held = Entries.FindByPredicate([InId](const FEntry& Candidate) { return Candidate.Id == InId; }))
		{
			if (!FMaterialSwapTable::SameStages(Held->Stages, InStages))
			{
				NumCollisions++;
			}
			return;
		}

		FEntry& Added = Entries.AddDefaulted_GetRef();
		Added.Id = InId;
		Added.Stages = MoveTemp(InStages);
	}

#pragma endregion

#pragma region FMaterialSwapChain

	bool FMaterialSwapChain::Restamps(const FMaterialSwapTable::FStage& InStage, const FName InLayer, const FName OutLayer)
	{
		return !InStage.IsEmpty() || InLayer != OutLayer;
	}

	void FMaterialSwapChain::Build(const FMaterialSwapLibrary& InIncoming, const FName InLayer, const FName OutLayer, const FMaterialSwapTable::FStage& InStage)
	{
		OutputLayer = OutLayer;
		Ids.Reset();
		Tables = FMaterialSwapLibrary();
		SlotByIncomingId.Reset();
		UnstampedSlot = INDEX_NONE;

		auto AddSlot = [&](const FMaterialSwapTable::FStages& InChained) -> int32
		{
			const uint32 Id = Tables.Add(OutputLayer, InChained);
			if (!Id)
			{
				return INDEX_NONE;
			}

			const int32 Existing = Ids.Find(Id);
			return Existing != INDEX_NONE ? Existing : Ids.Add(Id);
		};

		InIncoming.ForEachTable(InLayer, [&](const uint32 InId, const FMaterialSwapTable::FStages& InStages)
		{
			SlotByIncomingId.Add(InId, AddSlot(FMaterialSwapTable::Chain(InStages, InStage)));
		});

		UnstampedSlot = AddSlot(FMaterialSwapTable::Chain(FMaterialSwapTable::FStages(), InStage));
	}

	const FMaterialSwapTable::FStages* FMaterialSwapChain::GetStages(const int32 InSlot) const
	{
		return Ids.IsValidIndex(InSlot) ? Tables.Find(OutputLayer, Ids[InSlot]) : nullptr;
	}

	int32 FMaterialSwapChain::Remap(const uint32 InStamp, bool& bOutUnknown) const
	{
		bOutUnknown = false;
		if (!InStamp)
		{
			return UnstampedSlot;
		}

		if (const int32* Slot = SlotByIncomingId.Find(InStamp))
		{
			return *Slot;
		}

		bOutUnknown = true;
		return UnstampedSlot;
	}

	FMaterialSwapLibrary FMaterialSwapChain::Assemble(const FMaterialSwapLibrary& InIncoming, const bool bKeepIncoming, const TConstArrayView<bool> InUsed) const
	{
		FMaterialSwapLibrary Assembled = InIncoming;
		if (!bKeepIncoming)
		{
			Assembled.RemoveLayer(OutputLayer);
		}

		for (int32 Slot = 0; Slot < Ids.Num(); Slot++)
		{
			if (!InUsed.IsValidIndex(Slot) || !InUsed[Slot])
			{
				continue;
			}

			if (const FMaterialSwapTable::FStages* Stages = GetStages(Slot))
			{
				Assembled.Add(OutputLayer, *Stages);
			}
		}

		return Assembled;
	}

#pragma endregion
}
