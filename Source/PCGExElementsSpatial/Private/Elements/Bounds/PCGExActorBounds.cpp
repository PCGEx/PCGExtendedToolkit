// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExActorBounds.h"

#include "PCGElement.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGNode.h"
#include "Algo/Sort.h"
#include "Components/PrimitiveComponent.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "PCGExLog.h"
#include "Elements/PCGActorSelector.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/WildcardString.h"

namespace PCGExActorBounds
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
	}
}

#pragma region FPCGExActorSelectionDetails

FPCGExActorSelectionDetails::FPCGExActorSelectionDetails()
	: ActorClass(AStaticMeshActor::StaticClass())
{
}

#if WITH_EDITOR
void FPCGExActorSelectionDetails::ApplyDeprecation(const UObject* InLogContext)
{
	// Clauses are trimmed comma lists now: a legacy tag holding a comma or edge whitespace no longer reads as itself.
	auto JoinLegacyTags = [InLogContext](const TArray<FName>& InTags)
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
		return PCGExActorBounds::Internal::JoinTags(InTags);
	};

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
			RequireAny = JoinLegacyTags(Tags_DEPRECATED);
			break;
		case EPCGExActorTagMatch::All:
			RequireAll = JoinLegacyTags(Tags_DEPRECATED);
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

	Exclude = JoinLegacyTags(SkipTags_DEPRECATED);
}
#endif

void FPCGExActorSelectionDetails::Init()
{
	const bool bRequireAllFits = PCGExActorBounds::Internal::ParseClause(RequireAll, RequireAllTags);
	const bool bRequireAnyFits = PCGExActorBounds::Internal::ParseClause(RequireAny, RequireAnyTags);
	const bool bExcludeFits = PCGExActorBounds::Internal::ParseClause(Exclude, ExcludeTags);
	bHasOverlongEntry = !bRequireAllFits || !bRequireAnyFits || !bExcludeFits;
}

bool FPCGExActorSelectionDetails::IsUsable(FText* OutWhyNot) const
{
	auto Fail = [OutWhyNot](const TCHAR* InWhyNot)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = FTEXT(InWhyNot);
		}
		return false;
	};

	// Not dropped and carried on with: leaving an entry out of Require All would gather more than was asked for.
	if (bHasOverlongEntry)
	{
		return Fail(TEXT("A tag list holds an entry longer than any actor tag can be; check that its tags are separated by commas. Nothing gathered."));
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

void FPCGExActorSelectionDetails::MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const
{
	// Every match carries each Require All tag, so one is enough; a literal tracks fewer actors than a pattern would.
	if (!RequireAllTags.IsEmpty())
	{
		const FName* Literal = RequireAllTags.FindByPredicate([](const FName& Tag) { return !PCGExActorBounds::Internal::HasWildcards(Tag); });
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
	if (bFilterByClass && ActorClass)
	{
		Parts.Add(ActorClass->GetName());
	}

	TArray<FName> ClauseTags;
	auto AddClause = [&](const TCHAR* InLabel, const FString& InList)
	{
		PCGExActorBounds::Internal::ParseClause(InList, ClauseTags);
		if (!ClauseTags.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("%s %s"), InLabel, *PCGExActorBounds::Internal::JoinTags(ClauseTags)));
		}
	};

	AddClause(TEXT("all:"), RequireAll);
	AddClause(TEXT("any:"), RequireAny);
	AddClause(TEXT("not:"), Exclude);

	return FString::Join(Parts, TEXT(" | "));
}

#pragma endregion

#pragma region FTagMatcher

namespace PCGExActorBounds
{
	FTagMatcher::FTagMatcher(const FPCGExActorSelectionDetails& InSelection)
	{
		const bool bAllowWildcards = InSelection.bAllowWildcards;
		auto IsPattern = [bAllowWildcards](const FName InTag) { return bAllowWildcards && Internal::HasWildcards(InTag); };

		// Require All first: it is the only clause that allocates bits, one per distinct tag or pattern.
		int32 NumExactRequired = 0;
		bool bHasRequiredPattern = false;
		for (const FName& Tag : InSelection.GetRequireAllTags())
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

		for (const FName& Tag : InSelection.GetRequireAnyTags())
		{
			const bool bPattern = IsPattern(Tag);
			const int32 Index = FindOrAddEntry(Tag, bPattern);
			(bPattern ? PatternContributions[Index] : ExactContributions[Index]).Flags |= FlagAny;
			bHasAny = true;
		}

		for (const FName& Tag : InSelection.GetExcludeTags())
		{
			const bool bPattern = IsPattern(Tag);
			const int32 Index = FindOrAddEntry(Tag, bPattern);
			FContribution& Entry = bPattern ? PatternContributions[Index] : ExactContributions[Index];
			Entry.Flags |= FlagExclude;
			if (Entry.BitsNum > 0 && Contradiction.IsNone())
			{
				Contradiction = Tag;
			}
		}

		// An exact Require All tag caught by an Exclude pattern excludes every match too, without sharing an entry.
		for (int32 i = 0; i < ExactTags.Num() && Contradiction.IsNone(); i++)
		{
			if (ExactContributions[i].BitsNum == 0)
			{
				continue;
			}

			const FString Required = ExactTags[i].ToString();
			for (int32 p = 0; p < Patterns.Num(); p++)
			{
				if ((PatternContributions[p].Flags & FlagExclude) && Internal::MatchesPattern(Patterns[p], Required))
				{
					Contradiction = ExactTags[i];
					break;
				}
			}
		}

		if (InSelection.bIgnorePCGSpawnedActors)
		{
			ExactContributions[FindOrAddEntry(PCGHelpers::DefaultPCGActorTag, false)].Flags |= FlagDrop;
		}

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

		// An actor tag equals at most one exact entry, so each exact Require All tag needs its own actor tag.
		MinActorTags = FMath::Max(NumExactRequired, (bHasRequiredPattern || bHasAny) ? 1 : 0);
		bEmpty = ExactTags.IsEmpty() && Patterns.IsEmpty();
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

	ETagVerdict FTagMatcher::Test(const TArray<FName>& InActorTags, const bool bResolveExcluded)
	{
		if (InActorTags.Num() < MinActorTags)
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

		for (const FName& Tag : InActorTags)
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

#pragma region FPCGExActorBoundsOutputDetails

bool FPCGExActorBoundsOutputDetails::IsUsable() const
{
	switch (BoundsSource)
	{
	case EPCGExActorBoundsSource::ActorSpace:
	case EPCGExActorBoundsSource::WorldAABB:
	case EPCGExActorBoundsSource::ActorSpaceLocal:
	case EPCGExActorBoundsSource::PerPrimitive:
		return true;
	default:
		return false;
	}
}

#pragma endregion

namespace PCGExActorBounds
{
	namespace Internal
	{
		/** True for every mode whose point carries the actor (or component) transform. Callers reject unknown values up front. */
		bool IsActorFramed(const EPCGExActorBoundsSource InSource)
		{
			switch (InSource)
			{
			case EPCGExActorBoundsSource::ActorSpace:
			case EPCGExActorBoundsSource::ActorSpaceLocal:
			case EPCGExActorBoundsSource::PerPrimitive:
				return true;
			case EPCGExActorBoundsSource::WorldAABB:
				return false;
			default:
				checkNoEntry();
				return false;
			}
		}

		/** True for primitives the settings exclude, and for those that take no space of their own: neither colliding nor visible in game. */
		bool ShouldSkipPrimitive(const UPrimitiveComponent* InComponent, const FPCGExActorBoundsOutputDetails& InDetails)
		{
			// An unregistered primitive has no render or physics state, and its Bounds may never have been computed.
			if (!InComponent->IsRegistered())
			{
				return true;
			}

			// PCG output must not feed back into its own inputs.
			if (InDetails.bIgnorePCGGeneratedComponents && InComponent->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
			{
				return true;
			}

			const AActor* Owner = InComponent->GetOwner();
			if (InDetails.bIgnoreEditorOnly && (InComponent->IsEditorOnly() || (Owner && Owner->IsEditorOnly())))
			{
				return true;
			}

			if (InComponent->IsCollisionEnabled())
			{
				return false;
			}

			// Game visibility, not editor visibility: editor helpers (sprites, arrows, frustums) are hidden in game.
			return !InComponent->IsVisible() || (Owner && Owner->IsHidden());
		}

		/** Relative to the largest world coordinate involved, so the test holds far from the origin. */
		constexpr double BoundsAgreementTolerance = 1e-5;

		/**
		 * Component-space box of a primitive. USceneComponent::CalcBounds is free to ignore the frame it is given, so the
		 * component's own local box is kept only when the cached world Bounds agree with it.
		 * Returns false when OutBox is the cached world box brought back into component space instead.
		 */
		bool ComponentSpaceBox(const UPrimitiveComponent* InComponent, const FBox& InWorldBox, FBox& OutBox)
		{
			const FTransform& ComponentTransform = InComponent->GetComponentTransform();
			const double MinScale = ComponentTransform.GetScale3D().GetAbsMin();
			const bool bCanInvert = MinScale > UE_KINDA_SMALL_NUMBER;
			OutBox = bCanInvert ? InWorldBox.InverseTransformBy(ComponentTransform) : FBox(FVector::ZeroVector, FVector::ZeroVector);

			const FBox Own = InComponent->CalcLocalBounds().GetBox();
			const double Tolerance = BoundsAgreementTolerance * FMath::Max3(1.0, InWorldBox.Min.GetAbsMax(), InWorldBox.Max.GetAbsMax());

			// The same answer in both frames means the frame was ignored; the brought-back box is exact either way.
			if (Own.Min.Equals(InWorldBox.Min, Tolerance) && Own.Max.Equals(InWorldBox.Max, Tolerance))
			{
				return false;
			}

			// Carried to world it must cover the cached box, and it must be no looser than that box brought back.
			if (!Own.TransformBy(ComponentTransform).ExpandBy(Tolerance).IsInsideOrOn(InWorldBox))
			{
				return false;
			}
			if (bCanInvert && !OutBox.ExpandBy(Tolerance / MinScale).IsInsideOrOn(Own))
			{
				return false;
			}

			OutBox = Own;
			return true;
		}

		/** Appends the single actor-level snapshot. Every mode but World AABB carries the actor transform. Returns the count appended (0 or 1). */
		int32 FinalizeSnapshot(const FTransform& InActorTransform, const FBox& InWorldBounds, const FBox& InLocalBounds, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots)
		{
			const bool bWorldAABB = !IsActorFramed(InDetails.BoundsSource);

			if (!InWorldBounds.IsValid)
			{
				if (InDetails.bOmitActorsWithoutBounds || (InCullBox && !InCullBox->IsInside(InActorTransform.GetLocation())))
				{
					return 0;
				}

				FSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
				Snapshot.Transform = bWorldAABB ? FTransform(InActorTransform.GetLocation()) : InActorTransform;
				Snapshot.LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
				return 1;
			}

			if (InCullBox && !InCullBox->Intersect(InWorldBounds))
			{
				return 0;
			}

			FSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
			if (bWorldAABB)
			{
				Snapshot.Transform = FTransform(InWorldBounds.GetCenter());
				const FVector Extent = InWorldBounds.GetExtent();
				Snapshot.LocalBounds = FBox(-Extent, Extent);
			}
			else
			{
				Snapshot.Transform = InActorTransform;
				Snapshot.LocalBounds = InLocalBounds;
			}

			return 1;
		}

		/** One point per primitive: component transform, own local bounds, culled individually. Returns the count appended. */
		int32 SnapshotPrimitives(const AActor* InActor, const FTransform& InActorTransform, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots)
		{
			const int32 StartNum = OutSnapshots.Num();
			bool bHasPrimitive = false;

			InActor->ForEachComponent<UPrimitiveComponent>(InDetails.bIncludeChildActors, [&](const UPrimitiveComponent* InPrimitive)
			{
				if (ShouldSkipPrimitive(InPrimitive, InDetails))
				{
					return;
				}

				bHasPrimitive = true;
				const FBox WorldBox = InPrimitive->Bounds.GetBox();
				if (InCullBox && !InCullBox->Intersect(WorldBox))
				{
					return;
				}

				FSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
				Snapshot.Transform = InPrimitive->GetComponentTransform();
				ComponentSpaceBox(InPrimitive, WorldBox, Snapshot.LocalBounds);
			});

			if (!bHasPrimitive)
			{
				// Nothing to split: same zero-extent point (or omission) as the other modes.
				return FinalizeSnapshot(InActorTransform, FBox(ForceInit), FBox(ForceInit), InDetails, InCullBox, OutSnapshots);
			}

			return OutSnapshots.Num() - StartNum;
		}
	}

	bool ResolveCull(FPCGExContext* InContext, const FName InBoundsPin, const bool bUnbounded, const bool bMustOverlapSelf, FCull& OutCull)
	{
		OutCull = FCull();

		if (!bUnbounded)
		{
			const TArray<FPCGTaggedData> BoundsInputs = InContext->InputData.GetInputsByPin(InBoundsPin);
			if (!BoundsInputs.IsEmpty())
			{
				if (const UPCGSpatialData* Spatial = Cast<UPCGSpatialData>(BoundsInputs[0].Data))
				{
					OutCull.InputBox = Spatial->GetBounds();
				}
			}

			if (!OutCull.InputBox.IsValid)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Bounds input carries no valid spatial bounds; nothing gathered. Enable Unbounded to gather every actor."));
				return false;
			}
		}

		OutCull.Box = OutCull.InputBox;

		if (bMustOverlapSelf)
		{
			const IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
			const FBox SelfBox = Source ? Source->GetExecutionState().GetBounds() : FBox(ForceInit);
			if (!SelfBox.IsValid)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Must Overlap Self is on but the execution source has no valid bounds; nothing gathered."));
				return false;
			}

			OutCull.Box = OutCull.InputBox.IsValid ? OutCull.InputBox.Overlap(SelfBox) : SelfBox;
			OutCull.bDisjoint = !OutCull.Box.IsValid;
		}

		return true;
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
	void AddStaticTrackedKeys(const UPCGSettings* InOwner, const FPCGExActorSelectionDetails& InSelection, const FName InBoundsPin, const bool bMustOverlapSelf, FPCGSelectionKeyToSettingsMap& OutKeysToSettings)
	{
		const UPCGNode* Node = Cast<const UPCGNode>(InOwner->GetOuter());
		if (Node && Node->IsInputPinConnected(InBoundsPin))
		{
			return;
		}

		FPCGExActorSelectionDetails Selection = InSelection;
		Selection.Init();

		TArray<FPCGSelectionKey> Keys;
		Selection.MakeTrackingKeys(Keys);
		for (FPCGSelectionKey& Key : Keys)
		{
			OutKeysToSettings.FindOrAdd(MoveTemp(Key)).Emplace(InOwner, bMustOverlapSelf);
		}
	}

	void RegisterDynamicTracking(FPCGExContext* InContext, const FPCGExActorSelectionDetails& InSelection, const FCull& InCull, const bool bMustOverlapSelf, const FName InSelectionProperty)
	{
		if (!InCull.InputBox.IsValid && !InContext->IsValueOverriden(InSelectionProperty))
		{
			return;
		}

		const bool bIsCulled = !InCull.InputBox.IsValid && bMustOverlapSelf;
		TArray<FPCGSelectionKey> Keys;
		InSelection.MakeTrackingKeys(Keys);
		for (FPCGSelectionKey& Key : Keys)
		{
			if (InCull.InputBox.IsValid)
			{
				Key.OptionalBounds.Add(InCull.InputBox);
			}
			FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(InContext, MoveTemp(Key), bIsCulled);
		}
	}
#endif

	int32 SnapshotActor(const AActor* InActor, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots)
	{
		check(InActor);

		const FTransform ActorTransform = InActor->GetActorTransform();

		bool bActorSpace = false;
		bool bLocalBounds = false;
		switch (InDetails.BoundsSource)
		{
		case EPCGExActorBoundsSource::PerPrimitive:
			return Internal::SnapshotPrimitives(InActor, ActorTransform, InDetails, InCullBox, OutSnapshots);
		case EPCGExActorBoundsSource::ActorSpace:
			bActorSpace = true;
			break;
		case EPCGExActorBoundsSource::ActorSpaceLocal:
			bLocalBounds = true;
			break;
		case EPCGExActorBoundsSource::WorldAABB:
			break;
		default:
			checkNoEntry();
			return 0;
		}

		// A zero scale has no inverse; such an actor has no extent either, so an empty local box is exact.
		const bool bCanInvert = ActorTransform.GetScale3D().GetAbsMin() > UE_KINDA_SMALL_NUMBER;
		// A full matrix inverse, once per actor and only in Local Bounds mode. FTransform composition (what
		// AActor::CalculateComponentsBoundingBoxInLocalSpace uses) cannot carry the shear a non-uniform actor scale
		// puts on rotated children and under-sizes their box; a matrix can.
		const FMatrix WorldToActor = (bLocalBounds && bCanInvert) ? ActorTransform.ToMatrixWithScale().Inverse() : FMatrix::Identity;

		FBox WorldBounds(ForceInit);
		FBox LocalBounds(ForceInit);

		InActor->ForEachComponent<UPrimitiveComponent>(InDetails.bIncludeChildActors, [&](const UPrimitiveComponent* InPrimitive)
		{
			if (Internal::ShouldSkipPrimitive(InPrimitive, InDetails))
			{
				return;
			}

			const FBox ComponentBox = InPrimitive->Bounds.GetBox();
			WorldBounds += ComponentBox;

			if (!bActorSpace && !bLocalBounds)
			{
				return;
			}

			if (!bCanInvert)
			{
				LocalBounds += FBox(FVector::ZeroVector, FVector::ZeroVector);
				return;
			}

			FBox ComponentLocalBox;
			if (bActorSpace || !Internal::ComponentSpaceBox(InPrimitive, ComponentBox, ComponentLocalBox))
			{
				// Per-component inverse of the cached world box is tighter than inverting the world union once.
				LocalBounds += ComponentBox.InverseTransformBy(ActorTransform);
			}
			else
			{
				// The component's own local box carried into actor space in one step: exact for unrotated components, one inflation otherwise.
				LocalBounds += ComponentLocalBox.TransformBy(InPrimitive->GetComponentTransform().ToMatrixWithScale() * WorldToActor);
			}
		});

		return Internal::FinalizeSnapshot(ActorTransform, WorldBounds, LocalBounds, InDetails, InCullBox, OutSnapshots);
	}

	int32 SnapshotBox(const FTransform& InActorTransform, const FBox& InWorldBounds, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots)
	{
		// A single world box is all a descriptor offers: every actor-framed mode derives its local box the same way.
		FBox LocalBounds(ForceInit);
		if (InWorldBounds.IsValid && Internal::IsActorFramed(InDetails.BoundsSource))
		{
			const bool bCanInvert = InActorTransform.GetScale3D().GetAbsMin() > UE_KINDA_SMALL_NUMBER;
			LocalBounds = bCanInvert ? InWorldBounds.InverseTransformBy(InActorTransform) : FBox(FVector::ZeroVector, FVector::ZeroVector);
		}

		return Internal::FinalizeSnapshot(InActorTransform, InWorldBounds, LocalBounds, InDetails, InCullBox, OutSnapshots);
	}

#pragma region FSweep

	FSweep::FSweep(const FPCGExActorSelectionDetails& InSelection, const FPCGExActorBoundsOutputDetails& InOutput, TArray<FSnapshot>& InKept)
		: Selection(InSelection), Output(InOutput), Kept(InKept), Tags(InSelection)
	{
	}

	TArray<FSnapshot>* FSweep::Route(const TArray<FName>& InActorTags)
	{
		if (Tags.IsEmpty())
		{
			return &Kept;
		}

		switch (Tags.Test(InActorTags, Discarded != nullptr))
		{
		case ETagVerdict::Keep:
			return &Kept;
		case ETagVerdict::Excluded:
			return Discarded;
		case ETagVerdict::Drop:
			return nullptr;
		default:
			checkNoEntry();
			return nullptr;
		}
	}

	template <typename ClassTestFn>
	void FSweep::AddActorImpl(const AActor* InActor, ClassTestFn&& InClassTest)
	{
		if (InActor == Self)
		{
			return;
		}

		// Routed before the bounds read: an excluded actor only costs a snapshot when it has a pin to go to.
		TArray<FSnapshot>* Target = Route(InActor->Tags);
		if (Target && InClassTest())
		{
			SnapshotActor(InActor, Output, CullBox, *Target);
		}
	}

	template <typename ClassTestFn>
	void FSweep::AddBoxImpl(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds, ClassTestFn&& InClassTest)
	{
		TArray<FSnapshot>* Target = Route(InActorTags);
		if (Target && InClassTest())
		{
			SnapshotBox(InActorTransform, InWorldBounds, Output, CullBox, *Target);
		}
	}

	void FSweep::AddActor(const AActor* InActor)
	{
		AddActorImpl(InActor, [] { return true; });
	}

	void FSweep::AddActor(const AActor* InActor, const TFunctionRef<bool()> InClassTest)
	{
		AddActorImpl(InActor, InClassTest);
	}

	void FSweep::AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds)
	{
		AddBoxImpl(InActorTags, InActorTransform, InWorldBounds, [] { return true; });
	}

	void FSweep::AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds, const TFunctionRef<bool()> InClassTest)
	{
		AddBoxImpl(InActorTags, InActorTransform, InWorldBounds, InClassTest);
	}

#pragma endregion

#pragma region Write

	namespace Internal
	{
		/** Location sits inline so most comparisons never touch a snapshot; Index resolves ties and drives the write. */
		struct FOrderEntry
		{
			FVector Location = FVector::ZeroVector;
			int32 Index = INDEX_NONE;
		};

		/** Orders snapshots that share a location on the rest of what gets written: equal means the same point. */
		bool IsLessAtSharedLocation(const FSnapshot& A, const FSnapshot& B)
		{
			const FQuat RA = A.Transform.GetRotation();
			const FQuat RB = B.Transform.GetRotation();
			const FVector SA = A.Transform.GetScale3D();
			const FVector SB = B.Transform.GetScale3D();

			const double VA[] = {RA.X, RA.Y, RA.Z, RA.W, SA.X, SA.Y, SA.Z, A.LocalBounds.Min.X, A.LocalBounds.Min.Y, A.LocalBounds.Min.Z, A.LocalBounds.Max.X, A.LocalBounds.Max.Y, A.LocalBounds.Max.Z};
			const double VB[] = {RB.X, RB.Y, RB.Z, RB.W, SB.X, SB.Y, SB.Z, B.LocalBounds.Min.X, B.LocalBounds.Min.Y, B.LocalBounds.Min.Z, B.LocalBounds.Max.X, B.LocalBounds.Max.Y, B.LocalBounds.Max.Z};

			for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(VA)); i++)
			{
				if (VA[i] != VB[i])
				{
					return VA[i] < VB[i];
				}
			}
			return false;
		}

		/**
		 * The write order, a function of the written values alone: whatever order the world was walked in, the same
		 * set of points comes out in the same order. Names play no part: they are not written, and need not be unique.
		 */
		void MakeWriteOrder(const TArray<FSnapshot>& InSnapshots, TArray<FOrderEntry>& OutOrder)
		{
			const int32 NumSnapshots = InSnapshots.Num();
			OutOrder.SetNum(NumSnapshots);
			for (int32 i = 0; i < NumSnapshots; i++)
			{
				OutOrder[i].Location = InSnapshots[i].Transform.GetLocation();
				OutOrder[i].Index = i;
			}

			Algo::Sort(OutOrder, [&InSnapshots](const FOrderEntry& A, const FOrderEntry& B)
			{
				if (A.Location.X != B.Location.X)
				{
					return A.Location.X < B.Location.X;
				}
				if (A.Location.Y != B.Location.Y)
				{
					return A.Location.Y < B.Location.Y;
				}
				if (A.Location.Z != B.Location.Z)
				{
					return A.Location.Z < B.Location.Z;
				}
				return IsLessAtSharedLocation(InSnapshots[A.Index], InSnapshots[B.Index]);
			});
		}
	}

	void WritePoints(UPCGBasePointData* InData, const TArray<FSnapshot>& InSnapshots)
	{
		check(InData);

		TArray<Internal::FOrderEntry> Order;
		Internal::MakeWriteOrder(InSnapshots, Order);

		const int32 NumPoints = InSnapshots.Num();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			InData, NumPoints,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed);

		TPCGValueRange<FTransform> Transforms = InData->GetTransformValueRange(false);
		TPCGValueRange<FVector> BoundsMin = InData->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> BoundsMax = InData->GetBoundsMaxValueRange(false);
		TPCGValueRange<int32> Seeds = InData->GetSeedValueRange(false);

		// Each index is written once, by one task; nothing shared is mutated.
		PCGExMT::ParallelOrSequential(NumPoints, [&](const int32 i)
		{
			const Internal::FOrderEntry& Entry = Order[i];
			const FSnapshot& Snapshot = InSnapshots[Entry.Index];

			Transforms[i] = Snapshot.Transform;
			BoundsMin[i] = Snapshot.LocalBounds.Min;
			BoundsMax[i] = Snapshot.LocalBounds.Max;
			Seeds[i] = PCGHelpers::ComputeSeed(static_cast<int>(Entry.Location.X), static_cast<int>(Entry.Location.Y), static_cast<int>(Entry.Location.Z));
		});
	}

#pragma endregion
}
