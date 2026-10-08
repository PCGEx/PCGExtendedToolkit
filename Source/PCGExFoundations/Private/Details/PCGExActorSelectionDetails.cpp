// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExActorSelectionDetails.h"

#include "EngineUtils.h"
#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "PCGExLog.h"
#include "PCGExVersion.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Core/PCGExContext.h"
#include "Elements/PCGActorSelector.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/WildcardString.h"

namespace PCGExActorSelection
{
	namespace Internal
	{
		/** Past this many exact tags a hash lookup beats a linear FName scan. */
		constexpr int32 ExactLinearScanMax = 8;

		/**
		 * Trimmed tags of one comma-separated clause; blanks, None and duplicates (case-insensitive) dropped.
		 * Returns false when an entry was too long to be a tag; FName asserts on those, so they are left out.
		 */
		bool ParseClause(const FString& InList, TArray<FName>& OutTags)
		{
			OutTags.Reset();

			bool bAllEntriesFit = true;
			for (const FString& Entry : PCGExArrayHelpers::GetStringArrayFromCommaSeparatedList(InList))
			{
				if (Entry.Len() >= NAME_SIZE)
				{
					bAllEntriesFit = false;
					continue;
				}

				const FName Tag(*Entry);
				if (!Tag.IsNone())
				{
					OutTags.AddUnique(Tag);
				}
			}
			return bAllEntriesFit;
		}

		FString JoinTags(const TArray<FName>& InTags)
		{
			TArray<FString> TagStrings;
			for (const FName& Tag : InTags)
			{
				if (!Tag.IsNone())
				{
					TagStrings.Add(Tag.ToString());
				}
			}
			return FString::Join(TagStrings, TEXT(", "));
		}

		bool HasWildcards(const FName InTag)
		{
			return FWildcardString::ContainsWildcards(*InTag.ToString());
		}

		// Stock Get Actor Data matches wildcards case-insensitively; FName equality already is.
		bool MatchesPattern(const FString& InPattern, const FStringView InTag)
		{
			return FWildcardString::IsMatchSubstring(*InPattern, InTag.GetData(), InTag.GetData() + InTag.Len(), ESearchCase::IgnoreCase);
		}

		/** Appends "label: tags" for each non-empty clause. Parses on its own. */
		void AppendClauseTitles(const FString& InRequireAll, const FString& InRequireAny, const FString& InExclude, TArray<FString>& OutParts)
		{
			TArray<FName> ClauseTags;
			auto AddClause = [&](const TCHAR* InLabel, const FString& InList)
			{
				ParseClause(InList, ClauseTags);
				if (!ClauseTags.IsEmpty())
				{
					OutParts.Add(FString::Printf(TEXT("%s %s"), InLabel, *JoinTags(ClauseTags)));
				}
			};

			AddClause(TEXT("all:"), InRequireAll);
			AddClause(TEXT("any:"), InRequireAny);
			AddClause(TEXT("not:"), InExclude);
		}

		const TCHAR* ScopeTitle(const EPCGExActorScope InScope)
		{
			switch (InScope)
			{
			case EPCGExActorScope::World:
				return TEXT("");
			case EPCGExActorScope::Self:
				return TEXT("self");
			case EPCGExActorScope::Parent:
				return TEXT("parent");
			case EPCGExActorScope::Root:
				return TEXT("root");
			case EPCGExActorScope::Original:
				return TEXT("original");
			default:
				return TEXT("?");
			}
		}

		const TCHAR* OverlongEntryWarning = TEXT("A tag list holds an entry longer than any actor tag can be; check that its tags are separated by commas. Nothing gathered.");

#if WITH_EDITOR
		/** True when the stock selector's tag or class criteria were tested (PCGActorSelector::FilterRequired). */
		bool LegacyFilterRequired(const FPCGActorSelectorSettings& InLegacy)
		{
			return (InLegacy.ActorFilter == EPCGActorFilter::AllWorldActors || InLegacy.bIncludeChildren) && !InLegacy.bDisableFilter;
		}

		/** Legacy tag migration: the comma-list format cannot carry commas or edge whitespace verbatim. */
		FString JoinLegacyTags(const TArray<FName>& InTags, const UObject* InLogContext)
		{
			for (const FName& Tag : InTags)
			{
				const FString TagString = Tag.ToString();
				if (TagString.Contains(TEXT(",")))
				{
					UE_LOG(LogPCGEx, Warning, TEXT("%s: tag '%s' contains a comma and now reads as several tags."), *GetPathNameSafe(InLogContext), *TagString);
				}
				else if (TagString.TrimStartAndEnd().Len() != TagString.Len())
				{
					UE_LOG(LogPCGEx, Warning, TEXT("%s: tag '%s' starts or ends with whitespace, which is now trimmed; it no longer matches the same actors."), *GetPathNameSafe(InLogContext), *TagString);
				}
			}
			return JoinTags(InTags);
		}
#endif
	}
}

#pragma region FPCGExTagFilterDetails

#if WITH_EDITOR
void FPCGExTagFilterDetails::ApplyDeprecation(const FPCGComponentSelectorSettings& InLegacy, const UObject* InLogContext)
{
	switch (InLegacy.ComponentSelection)
	{
	case EPCGComponentSelection::ByTag:
		if (!InLegacy.ComponentSelectionTag.IsNone())
		{
			RequireAny = PCGExActorSelection::Internal::JoinLegacyTags({InLegacy.ComponentSelectionTag}, InLogContext);
			bAllowWildcards |= PCGExActorSelection::Internal::HasWildcards(InLegacy.ComponentSelectionTag);
		}
		break;
	case EPCGComponentSelection::ByClass:
		// A class selection has no tag counterpart; the host decides what its component class filter becomes.
		break;
	default:
		UE_LOG(LogPCGEx, Error, TEXT("%s: unknown legacy component selection mode %d; no tag filter was migrated."), *GetPathNameSafe(InLogContext), static_cast<int32>(InLegacy.ComponentSelection));
		break;
	}
}
#endif

void FPCGExTagFilterDetails::Init()
{
	const bool bRequireAllFits = PCGExActorSelection::Internal::ParseClause(RequireAll, RequireAllTags);
	const bool bRequireAnyFits = PCGExActorSelection::Internal::ParseClause(RequireAny, RequireAnyTags);
	const bool bExcludeFits = PCGExActorSelection::Internal::ParseClause(Exclude, ExcludeTags);
	bHasOverlongEntry = !bRequireAllFits || !bRequireAnyFits || !bExcludeFits;
}

bool FPCGExTagFilterDetails::IsUsable(FText* OutWhyNot) const
{
	if (bHasOverlongEntry)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = FText::FromString(PCGExActorSelection::Internal::OverlongEntryWarning);
		}
		return false;
	}
	return true;
}

PCGExActorSelection::FTagClauseView FPCGExTagFilterDetails::GetClauseView(const FName InDropTag) const
{
	return PCGExActorSelection::FTagClauseView{RequireAllTags, RequireAnyTags, ExcludeTags, bAllowWildcards, InDropTag};
}

FString FPCGExTagFilterDetails::GetTitleInformation() const
{
	TArray<FString> Parts;
	PCGExActorSelection::Internal::AppendClauseTitles(RequireAll, RequireAny, Exclude, Parts);
	return FString::Join(Parts, TEXT(" | "));
}

#pragma endregion

#pragma region FPCGExActorSelectionDetails

FPCGExActorSelectionDetails::FPCGExActorSelectionDetails()
	: ActorClass(AStaticMeshActor::StaticClass())
{
}

#if WITH_EDITOR
void FPCGExActorSelectionDetails::ApplyDeprecation(const UObject* InLogContext)
{
	using namespace PCGExActorSelection::Internal;

	switch (Selection_DEPRECATED)
	{
	case EPCGExActorSelection::ByClass:
		bFilterByClass = true;
		break;
	case EPCGExActorSelection::ByTag:
		bFilterByClass = false;
		switch (TagMatch_DEPRECATED)
		{
		case EPCGExActorTagMatch::Any:
			RequireAny = JoinLegacyTags(Tags_DEPRECATED, InLogContext);
			break;
		case EPCGExActorTagMatch::All:
			RequireAll = JoinLegacyTags(Tags_DEPRECATED, InLogContext);
			break;
		default:
			UE_LOG(LogPCGEx, Error, TEXT("%s: unknown legacy tag match mode %d; the selection tags were not migrated."), *GetPathNameSafe(InLogContext), static_cast<int32>(TagMatch_DEPRECATED));
			break;
		}
		break;
	default:
		UE_LOG(LogPCGEx, Error, TEXT("%s: unknown legacy selection mode %d; only the skip tags were migrated."), *GetPathNameSafe(InLogContext), static_cast<int32>(Selection_DEPRECATED));
		break;
	}

	Exclude = JoinLegacyTags(SkipTags_DEPRECATED, InLogContext);
}

void FPCGExActorSelectionDetails::ApplyDeprecation(const FPCGActorSelectorSettings& InLegacy, const UObject* InLogContext)
{
	using namespace PCGExActorSelection::Internal;

	const FString Owner = GetPathNameSafe(InLogContext);

	switch (InLegacy.ActorFilter)
	{
	case EPCGActorFilter::AllWorldActors:
		Scope = EPCGExActorScope::World;
		break;
	case EPCGActorFilter::Self:
		Scope = EPCGExActorScope::Self;
		break;
	case EPCGActorFilter::Parent:
		Scope = EPCGExActorScope::Parent;
		break;
	case EPCGActorFilter::Root:
		Scope = EPCGExActorScope::Root;
		break;
	case EPCGActorFilter::Original:
		Scope = EPCGExActorScope::Original;
		break;
	case EPCGActorFilter::FromInput:
		Scope = EPCGExActorScope::World;
		UE_LOG(LogPCGEx, Warning, TEXT("%s: the legacy selector read actors from an input, which this selection cannot; it now sweeps the world."), *Owner);
		break;
	default:
		UE_LOG(LogPCGEx, Error, TEXT("%s: unknown legacy actor filter %d; the scope stays World."), *Owner, static_cast<int32>(InLegacy.ActorFilter));
		break;
	}

	bIncludeChildren = InLegacy.bIncludeChildren;
	bMustOverlapSelf = InLegacy.bMustOverlapSelf;

	// Stock never skipped PCG-spawned actors; new selections do.
	bIgnorePCGSpawnedActors = false;

	// Carried as-is: inert outside World, live again if the scope is later set to World.
	bIgnoreSelf = InLegacy.bIgnoreSelfAndChildren;
	if (bIgnoreSelf && Scope != EPCGExActorScope::World)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("%s: the legacy selector ignored self inside a self-relative scope and selected nothing; the scope actor is now selected."), *Owner);
	}

	const bool bCriteriaApplied = LegacyFilterRequired(InLegacy);

	if (InLegacy.bDisableFilter && (InLegacy.ActorFilter == EPCGActorFilter::AllWorldActors || InLegacy.bIncludeChildren))
	{
		// Stock with a disabled filter selected every actor; a class filter on AActor says the same here.
		bFilterByClass = true;
		ActorClass = AActor::StaticClass();
	}
	else
	{
		switch (InLegacy.ActorSelection)
		{
		case EPCGActorSelection::ByClass:
			bFilterByClass = true;
			ActorClass = InLegacy.ActorSelectionClass;
			if (!ActorClass && bCriteriaApplied)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("%s: the legacy selector filtered by class without a class and selected nothing; it still does."), *Owner);
			}
			break;
		case EPCGActorSelection::ByTag:
			bFilterByClass = false;
			if (!InLegacy.ActorSelectionTag.IsNone())
			{
				RequireAny = JoinLegacyTags({InLegacy.ActorSelectionTag}, InLogContext);
				bAllowWildcards |= HasWildcards(InLegacy.ActorSelectionTag);
			}
			else if (bCriteriaApplied)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("%s: the legacy selector filtered by tag without a tag and selected nothing; it still does."), *Owner);
			}
			break;
		default:
			UE_LOG(LogPCGEx, Warning, TEXT("%s: legacy actor selection mode %d has no counterpart; no class or tag criteria were migrated."), *Owner, static_cast<int32>(InLegacy.ActorSelection));
			break;
		}
	}

	if (bCriteriaApplied && !InLegacy.bSelectMultiple)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("%s: the legacy selector stopped at the first matching actor; every matching actor is now selected."), *Owner);
	}

	if (InLegacy.IncludeIsolatedActors != EPCGIsolatedActorIncludeFlag::Ignore)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("%s: isolated actors are no longer included in or substituted for the selection."), *Owner);
	}
}
#endif

void FPCGExActorSelectionDetails::Init()
{
	const bool bRequireAllFits = PCGExActorSelection::Internal::ParseClause(RequireAll, RequireAllTags);
	const bool bRequireAnyFits = PCGExActorSelection::Internal::ParseClause(RequireAny, RequireAnyTags);
	const bool bExcludeFits = PCGExActorSelection::Internal::ParseClause(Exclude, ExcludeTags);
	bHasOverlongEntry = !bRequireAllFits || !bRequireAnyFits || !bExcludeFits;
}

bool FPCGExActorSelectionDetails::IsUsable(FText* OutWhyNot) const
{
	auto Fail = [OutWhyNot](const TCHAR* InWhyNot)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = FText::FromString(InWhyNot);
		}
		return false;
	};

	switch (Scope)
	{
	case EPCGExActorScope::World:
	case EPCGExActorScope::Self:
	case EPCGExActorScope::Parent:
	case EPCGExActorScope::Root:
	case EPCGExActorScope::Original:
		break;
	default:
		return Fail(TEXT("Scope holds a value this version does not know; pick it again. Nothing gathered."));
	}

	if (!AppliesCriteria())
	{
		return true;
	}

	// Not dropped and carried on with: leaving an entry out of Require All would gather more than was asked for.
	if (bHasOverlongEntry)
	{
		return Fail(PCGExActorSelection::Internal::OverlongEntryWarning);
	}

	if (bFilterByClass)
	{
		return ActorClass ? true : Fail(TEXT("Filter By Class is on but no actor class is set; nothing gathered."));
	}

	if (RequireAllTags.IsEmpty() && RequireAnyTags.IsEmpty())
	{
		return Fail(TEXT("Nothing selects actors: turn on the class filter, or set Require All or Require Any tags. Exclude alone only narrows."));
	}

	return true;
}

TSubclassOf<AActor> FPCGExActorSelectionDetails::GetIterationClass() const
{
	return (bFilterByClass && ActorClass) ? ActorClass : TSubclassOf<AActor>(AActor::StaticClass());
}

PCGExActorSelection::FTagClauseView FPCGExActorSelectionDetails::GetClauseView() const
{
	return PCGExActorSelection::FTagClauseView{RequireAllTags, RequireAnyTags, ExcludeTags, bAllowWildcards, bIgnorePCGSpawnedActors ? PCGHelpers::DefaultPCGActorTag : FName(NAME_None)};
}

void FPCGExActorSelectionDetails::MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const
{
	// Nothing is gathered, so no actor edit can change the output.
	if (!IsUsable())
	{
		return;
	}

	// A self-relative scope tracks through the execution source, like the stock selector's own key.
	switch (Scope)
	{
	case EPCGExActorScope::World:
		break;
	case EPCGExActorScope::Self:
		OutKeys.Emplace(EPCGActorFilter::Self);
		return;
	case EPCGExActorScope::Parent:
		OutKeys.Emplace(EPCGActorFilter::Parent);
		return;
	case EPCGExActorScope::Root:
		OutKeys.Emplace(EPCGActorFilter::Root);
		return;
	case EPCGExActorScope::Original:
		OutKeys.Emplace(EPCGActorFilter::Original);
		return;
	default:
		checkNoEntry(); // IsUsable rejects unknown scopes first.
		return;
	}

	// Every match carries each Require All tag, so one is enough; a literal tracks fewer actors than a pattern would.
	if (!RequireAllTags.IsEmpty())
	{
		const FName* Literal = RequireAllTags.FindByPredicate([](const FName& Tag) { return !PCGExActorSelection::Internal::HasWildcards(Tag); });
		OutKeys.Emplace(Literal ? *Literal : RequireAllTags[0]);
		return;
	}

	// Tag keys match wildcards and removed tags on their own (FPCGSelectionKey::IsMatching).
	if (!RequireAnyTags.IsEmpty())
	{
		for (const FName& Tag : RequireAnyTags)
		{
			OutKeys.Emplace(Tag);
		}
		return;
	}

	if (bFilterByClass && ActorClass)
	{
		OutKeys.Emplace(TSubclassOf<UObject>(ActorClass));
	}
}

FString FPCGExActorSelectionDetails::GetTitleInformation() const
{
	TArray<FString> Parts;

	if (Scope != EPCGExActorScope::World)
	{
		Parts.Add(bIncludeChildren ? FString::Printf(TEXT("%s +children"), PCGExActorSelection::Internal::ScopeTitle(Scope)) : FString(PCGExActorSelection::Internal::ScopeTitle(Scope)));
	}

	if (AppliesCriteria())
	{
		if (bFilterByClass && ActorClass)
		{
			Parts.Add(ActorClass->GetName());
		}
		PCGExActorSelection::Internal::AppendClauseTitles(RequireAll, RequireAny, Exclude, Parts);
	}

	return FString::Join(Parts, TEXT(" | "));
}

#pragma endregion

#pragma region FTagMatcher

namespace PCGExActorSelection
{
	FTagMatcher::FTagMatcher(const FPCGExActorSelectionDetails& InSelection)
		: FTagMatcher(InSelection.GetClauseView())
	{
	}

	FTagMatcher::FTagMatcher(const FPCGExTagFilterDetails& InFilter, const FName InDropTag)
		: FTagMatcher(InFilter.GetClauseView(InDropTag))
	{
	}

	FTagMatcher::FTagMatcher(const FTagClauseView& InClauses)
	{
		const bool bAllowWildcards = InClauses.bAllowWildcards;
		auto IsPattern = [bAllowWildcards](const FName InTag) { return bAllowWildcards && Internal::HasWildcards(InTag); };

		// Require All first: it is the only clause that allocates bits, one per distinct tag or pattern.
		int32 NumExactRequired = 0;
		bool bHasRequiredPattern = false;
		for (const FName& Tag : InClauses.RequireAll)
		{
			const bool bPattern = IsPattern(Tag);
			const int32 Index = FindOrAddEntry(Tag, bPattern);
			FContribution& Entry = bPattern ? PatternContributions[Index] : ExactContributions[Index];
			if (Entry.BitsNum > 0)
			{
				continue;
			}

			Entry.BitsStart = Bits.Num();
			Entry.BitsNum = 1;
			Bits.Add(NumRequiredBits++);

			if (bPattern) { bHasRequiredPattern = true; }
			else { NumExactRequired++; }
		}

		for (const FName& Tag : InClauses.RequireAny)
		{
			const bool bPattern = IsPattern(Tag);
			const int32 Index = FindOrAddEntry(Tag, bPattern);
			(bPattern ? PatternContributions[Index] : ExactContributions[Index]).Flags |= FlagAny;
			bHasAny = true;
		}

		for (const FName& Tag : InClauses.Exclude)
		{
			const bool bPattern = IsPattern(Tag);
			const int32 Index = FindOrAddEntry(Tag, bPattern);
			(bPattern ? PatternContributions[Index] : ExactContributions[Index]).Flags |= FlagExclude;
		}

		if (!InClauses.DropTag.IsNone())
		{
			ExactContributions[FindOrAddEntry(InClauses.DropTag, false)].Flags |= FlagDrop;
		}

		Contradiction = FindContradiction(InClauses.RequireAny);

		if (ExactTags.Num() > Internal::ExactLinearScanMax)
		{
			ExactIndex.Reserve(ExactTags.Num());
			for (int32 i = 0; i < ExactTags.Num(); i++)
			{
				ExactIndex.Add(ExactTags[i], i);
			}
		}

		const int32 NumWords = (NumRequiredBits + 63) / 64;
		RequiredWords.SetNumZeroed(NumWords);
		SeenWords.SetNumZeroed(NumWords);
		for (int32 Bit = 0; Bit < NumRequiredBits; Bit++)
		{
			RequiredWords[Bit >> 6] |= uint64(1) << (Bit & 63);
		}

		// A tag equals at most one exact entry, so each exact Require All tag needs its own tag in the list.
		MinTags = FMath::Max(NumExactRequired, (bHasRequiredPattern || bHasAny) ? 1 : 0);
		bEmpty = ExactTags.IsEmpty() && Patterns.IsEmpty();
	}

	FName FTagMatcher::FindContradiction(const TArray<FName>& InRequireAnyTags) const
	{
		// Excluded by its own entry (Exclude, or the drop tag), or caught by an Exclude pattern.
		auto IsExactExcluded = [this](const int32 Index)
		{
			if (ExactContributions[Index].Flags & (FlagExclude | FlagDrop))
			{
				return true;
			}

			const FString Tag = ExactTags[Index].ToString();
			for (int32 p = 0; p < Patterns.Num(); p++)
			{
				if ((PatternContributions[p].Flags & FlagExclude) && Internal::MatchesPattern(Patterns[p], Tag))
				{
					return true;
				}
			}
			return false;
		};

		bool bEveryAnyExcluded = bHasAny;

		for (int32 i = 0; i < ExactTags.Num(); i++)
		{
			const FContribution& Entry = ExactContributions[i];
			const bool bRequired = Entry.BitsNum > 0;
			const bool bAny = (Entry.Flags & FlagAny) != 0;
			if (!bRequired && !bAny)
			{
				continue;
			}

			const bool bExcluded = IsExactExcluded(i);
			if (bRequired && bExcluded)
			{
				return ExactTags[i];
			}
			if (bAny && !bExcluded)
			{
				bEveryAnyExcluded = false;
			}
		}

		// A pattern is only known to be excluded when Exclude holds the very same pattern.
		for (int32 p = 0; p < Patterns.Num(); p++)
		{
			const FContribution& Entry = PatternContributions[p];
			const bool bExcluded = (Entry.Flags & FlagExclude) != 0;
			if (Entry.BitsNum > 0 && bExcluded)
			{
				return FName(*Patterns[p]);
			}
			if ((Entry.Flags & FlagAny) && !bExcluded)
			{
				bEveryAnyExcluded = false;
			}
		}

		return bEveryAnyExcluded ? InRequireAnyTags[0] : FName(NAME_None);
	}

	int32 FTagMatcher::FindOrAddEntry(const FName InTag, const bool bPattern)
	{
		if (bPattern)
		{
			// FString equality ignores case, like the pattern match itself.
			const FString Pattern = InTag.ToString();
			int32 Index = Patterns.IndexOfByKey(Pattern);
			if (Index == INDEX_NONE)
			{
				Index = Patterns.Add(Pattern);
				PatternContributions.AddDefaulted();
			}
			return Index;
		}

		int32 Index = ExactTags.IndexOfByKey(InTag);
		if (Index == INDEX_NONE)
		{
			Index = ExactTags.Add(InTag);
			ExactContributions.AddDefaulted();
		}
		return Index;
	}

	const FTagMatcher::FContribution* FTagMatcher::FindExact(const FName InTag) const
	{
		if (!ExactIndex.IsEmpty())
		{
			const int32* Index = ExactIndex.Find(InTag);
			return Index ? &ExactContributions[*Index] : nullptr;
		}

		const int32 NumExact = ExactTags.Num();
		for (int32 i = 0; i < NumExact; i++)
		{
			if (ExactTags[i] == InTag)
			{
				return &ExactContributions[i];
			}
		}
		return nullptr;
	}

	const FTagMatcher::FContribution* FTagMatcher::Classify(const FName InTag)
	{
		if (Patterns.IsEmpty())
		{
			return FindExact(InTag);
		}

		// The returned pointer lives in Memo: valid until the next Classify adds an entry.
		if (const FContribution* Known = Memo.Find(InTag))
		{
			return (Known->Flags || Known->BitsNum) ? Known : nullptr;
		}

		FContribution Merged;
		Merged.BitsStart = Bits.Num();

		auto Merge = [this, &Merged](const FContribution& InContribution)
		{
			Merged.Flags |= InContribution.Flags;
			for (int32 i = 0; i < InContribution.BitsNum; i++)
			{
				// Copied out first: TArray::Add rejects a reference into its own storage.
				const int32 Bit = Bits[InContribution.BitsStart + i];
				Bits.Add(Bit);
			}
		};

		if (const FContribution* Exact = FindExact(InTag))
		{
			Merge(*Exact);
		}

		TStringBuilder<NAME_SIZE> Builder;
		InTag.AppendString(Builder);
		for (int32 i = 0; i < Patterns.Num(); i++)
		{
			if (Internal::MatchesPattern(Patterns[i], Builder.ToView()))
			{
				Merge(PatternContributions[i]);
			}
		}

		Merged.BitsNum = Bits.Num() - Merged.BitsStart;
		const FContribution& Stored = Memo.Add(InTag, Merged);
		return (Stored.Flags || Stored.BitsNum) ? &Stored : nullptr;
	}

	ETagVerdict FTagMatcher::Test(const TArray<FName>& InTags, const bool bResolveExcluded)
	{
		if (InTags.Num() < MinTags)
		{
			return ETagVerdict::Drop;
		}

		const int32 NumWords = SeenWords.Num();
		if (NumWords > 0)
		{
			FMemory::Memzero(SeenWords.GetData(), NumWords * sizeof(uint64));
		}

		bool bAnyHit = !bHasAny;
		bool bExcluded = false;

		for (const FName& Tag : InTags)
		{
			const FContribution* Contribution = Classify(Tag);
			if (!Contribution)
			{
				continue;
			}

			const uint8 Flags = Contribution->Flags;
			if (Flags & FlagDrop)
			{
				return ETagVerdict::Drop;
			}
			if (Flags & FlagExclude)
			{
				if (!bResolveExcluded)
				{
					return ETagVerdict::Drop;
				}
				bExcluded = true;
			}
			if (Flags & FlagAny)
			{
				bAnyHit = true;
			}

			const int32 BitsEnd = Contribution->BitsStart + Contribution->BitsNum;
			for (int32 i = Contribution->BitsStart; i < BitsEnd; i++)
			{
				const int32 Bit = Bits[i];
				SeenWords[Bit >> 6] |= uint64(1) << (Bit & 63);
			}
		}

		if (!bAnyHit)
		{
			return ETagVerdict::Drop;
		}

		for (int32 i = 0; i < NumWords; i++)
		{
			if (SeenWords[i] != RequiredWords[i])
			{
				return ETagVerdict::Drop;
			}
		}

		return bExcluded ? ETagVerdict::Excluded : ETagVerdict::Keep;
	}
}

#pragma endregion

#pragma region Resolution

namespace PCGExActorSelection
{
	AActor* ResolveSelf(const FPCGExContext* InContext)
	{
#if PCGEX_ENGINE_VERSION >= 508
		const IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
		return Source ? Source->GetExecutionState().GetTypedTarget<AActor>() : nullptr;
#else
		const UPCGComponent* Component = InContext->GetComponent();
		return Component ? Component->GetOwner() : nullptr;
#endif
	}

	AActor* ResolveOriginal(const FPCGExContext* InContext)
	{
#if PCGEX_ENGINE_VERSION >= 508
		const IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
		const IPCGGraphExecutionSource* Original = Source ? Source->GetExecutionState().GetOriginalSource() : nullptr;
		return Original ? Original->GetExecutionState().GetTypedTarget<AActor>() : nullptr;
#else
		const UPCGComponent* Component = InContext->GetComponent();
		const UPCGComponent* Original = Component ? Component->GetOriginalComponent() : nullptr;
		return Original ? Original->GetOwner() : nullptr;
#endif
	}

	bool IsSelfOrChildOfSelf(const AActor* InActor, const AActor* InSelf)
	{
		if (!InSelf)
		{
			return false;
		}

		for (const AActor* Current = InActor; Current; Current = Current->GetParentActor())
		{
			if (Current == InSelf)
			{
				return true;
			}
		}
		return false;
	}

	bool FQuery::Make(const FPCGExActorSelectionDetails& InSelection, FPCGExContext* InContext, FQuery& OutQuery, const bool bApplyOverlap)
	{
		check(IsInGameThread());

		OutQuery = FQuery();
		OutQuery.Selection = &InSelection;
		OutQuery.Context = InContext;
		OutQuery.bApplyOverlap = bApplyOverlap;

		OutQuery.World = InContext->GetWorld();
		if (!OutQuery.World)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("No world to select actors from."));
			return false;
		}

		if (FText WhyNot; !InSelection.IsUsable(&WhyNot))
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, WhyNot);
			return false;
		}

		if (InSelection.bMustOverlapSelf)
		{
			const IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
			OutQuery.SelfBounds = Source ? Source->GetExecutionState().GetBounds() : FBox(ForceInit);
			if (!OutQuery.SelfBounds.IsValid)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Must Overlap Self is on but the execution source has no valid bounds; nothing selected."));
				return false;
			}
		}

		if (InSelection.Scope == EPCGExActorScope::World && InSelection.bIgnoreSelf)
		{
			OutQuery.Self = ResolveSelf(InContext);
		}

		if (InSelection.AppliesCriteria())
		{
			OutQuery.Tags.Emplace(InSelection.GetClauseView());
		}

		return true;
	}

	void FQuery::GatherScopeActors(TArray<AActor*>& OutActors) const
	{
		AActor* SelfActor = ResolveSelf(Context);

		switch (Selection->Scope)
		{
		case EPCGExActorScope::Self:
			if (SelfActor)
			{
				OutActors.Add(SelfActor);
			}
			break;
		case EPCGExActorScope::Parent:
			if (SelfActor)
			{
				AActor* Parent = SelfActor->GetParentActor();
				OutActors.Add(Parent ? Parent : SelfActor);
			}
			break;
		case EPCGExActorScope::Root:
			for (AActor* Current = SelfActor; Current; Current = Current->GetParentActor())
			{
				if (!Current->GetParentActor())
				{
					OutActors.Add(Current);
					break;
				}
			}
			break;
		case EPCGExActorScope::Original:
			if (AActor* Original = ResolveOriginal(Context))
			{
				OutActors.Add(Original);
			}
			else if (SelfActor)
			{
				OutActors.Add(SelfActor);
			}
			break;
		default:
			checkNoEntry();
			break;
		}

		if (Selection->bIncludeChildren)
		{
			const int32 NumScopeActors = OutActors.Num();
			for (int32 i = 0; i < NumScopeActors; i++)
			{
				OutActors[i]->GetAttachedActors(OutActors, /*bResetArray=*/false, /*bRecursivelyIncludeAttachedActors=*/true);
			}
		}
	}

	void FQuery::ForEachCandidate(TFunctionRef<void(AActor*)> InVisit)
	{
		check(Selection && World);

		if (Selection->Scope == EPCGExActorScope::World)
		{
			for (TActorIterator<AActor> It(World, Selection->GetIterationClass()); It; ++It)
			{
				AActor* Actor = *It;
				if (Self && IsSelfOrChildOfSelf(Actor, Self))
				{
					continue;
				}
				InVisit(Actor);
			}
			return;
		}

		TArray<AActor*> ScopeActors;
		GatherScopeActors(ScopeActors);

		const UClass* ClassFilter = (Selection->AppliesCriteria() && Selection->bFilterByClass) ? Selection->ActorClass.Get() : nullptr;
		for (AActor* Actor : ScopeActors)
		{
			if (!Actor || (ClassFilter && !Actor->IsA(ClassFilter)))
			{
				continue;
			}
			InVisit(Actor);
		}
	}

	bool FQuery::Overlaps(const AActor* InActor) const
	{
		if (!bApplyOverlap || !Selection->bMustOverlapSelf)
		{
			return true;
		}

#if PCGEX_ENGINE_VERSION >= 508
		const FBox ActorBounds = PCGHelpers::GetGridBounds(InActor, Context->ExecutionSource.Get());
#else
		const FBox ActorBounds = PCGHelpers::GetGridBounds(InActor, Context->GetComponent());
#endif
		return ActorBounds.IsValid && SelfBounds.Intersect(ActorBounds);
	}

	void FQuery::Run(TFunctionRef<void(AActor*)> InKeep)
	{
		ForEachCandidate([this, &InKeep](AActor* Actor)
		{
			// Tags first: an FName scan is far cheaper than the actor bounds the overlap test needs.
			if (Tags && !Tags->IsEmpty() && Tags->Test(Actor->Tags, false) != ETagVerdict::Keep)
			{
				return;
			}
			if (!Overlaps(Actor))
			{
				return;
			}
			InKeep(Actor);
		});
	}

	void CombineSelfBoundsCrc(const FPCGGetDependenciesCrcParams& InParams, const bool bMustOverlapSelf, FPCGCrc& InOutCrc)
	{
		if (!bMustOverlapSelf || !InParams.ExecutionSource)
		{
			return;
		}

		if (const UPCGData* SelfData = InParams.ExecutionSource->GetExecutionState().GetSelfData())
		{
			InOutCrc.Combine(SelfData->GetOrComputeCrc(/*bFullDataCrc=*/false));
		}
	}

#if WITH_EDITOR
	void RegisterDynamicTracking(FPCGExContext* InContext, const FPCGExActorSelectionDetails& InSelection, const bool bIsCulled, const FName InSelectionProperty)
	{
		if (!InContext->IsValueOverriden(InSelectionProperty))
		{
			return;
		}

		TArray<FPCGSelectionKey> Keys;
		InSelection.MakeTrackingKeys(Keys);
		for (FPCGSelectionKey& Key : Keys)
		{
			FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(InContext, MoveTemp(Key), bIsCulled);
		}
	}
#endif
}

#pragma endregion
