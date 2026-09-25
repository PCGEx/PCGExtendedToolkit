// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bounds/PCGExActorBounds.h"

#include "PCGElement.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGNode.h"
#include "Algo/Sort.h"
#include "Components/BillboardComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Elements/PCGActorSelector.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/WildcardString.h"

#pragma region FPCGExActorSelectionDetails

FPCGExActorSelectionDetails::FPCGExActorSelectionDetails()
	: ActorClass(AActor::StaticClass())
{
}

void FPCGExActorSelectionDetails::Init()
{
	ExactTags.Reset();
	WildcardTags.Reset();

	for (const FName& Tag : Tags)
	{
		if (Tag.IsNone())
		{
			continue;
		}

		if (bAllowWildcards)
		{
			FString TagString = Tag.ToString();
			if (FWildcardString::ContainsWildcards(*TagString))
			{
				WildcardTags.Add(MoveTemp(TagString));
				continue;
			}
		}

		ExactTags.AddUnique(Tag);
	}
}

bool FPCGExActorSelectionDetails::IsUsable() const
{
	switch (Selection)
	{
	case EPCGExActorSelection::ByClass:
		return ActorClass != nullptr;
	case EPCGExActorSelection::ByTag:
		return !ExactTags.IsEmpty() || !WildcardTags.IsEmpty();
	default:
		checkNoEntry();
		return false;
	}
}

TSubclassOf<AActor> FPCGExActorSelectionDetails::GetIterationClass() const
{
	return (Selection == EPCGExActorSelection::ByClass && ActorClass) ? ActorClass : TSubclassOf<AActor>(AActor::StaticClass());
}

bool FPCGExActorSelectionDetails::MatchesClass(const AActor* InActor) const
{
	return Selection != EPCGExActorSelection::ByClass || InActor->IsA(ActorClass);
}

bool FPCGExActorSelectionDetails::MatchesTags(const TArray<FName>& InActorTags) const
{
	if (bIgnorePCGSpawnedActors && InActorTags.Contains(PCGHelpers::DefaultPCGActorTag))
	{
		return false;
	}

	if (Selection != EPCGExActorSelection::ByTag)
	{
		return true;
	}

	// Stock Get Actor Data matches wildcards case-insensitively; FName equality already is.
	auto AnyActorTagMatches = [&InActorTags](const FString& Pattern)
	{
		for (const FName& ActorTag : InActorTags)
		{
			TStringBuilder<NAME_SIZE> Builder;
			ActorTag.AppendString(Builder);
			if (FWildcardString::IsMatchSubstring(*Pattern, Builder.GetData(), Builder.GetData() + Builder.Len(), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	if (TagMatch == EPCGExActorTagMatch::Any)
	{
		for (const FName& Tag : ExactTags)
		{
			if (InActorTags.Contains(Tag))
			{
				return true;
			}
		}
		for (const FString& Pattern : WildcardTags)
		{
			if (AnyActorTagMatches(Pattern))
			{
				return true;
			}
		}
		return false;
	}

	for (const FName& Tag : ExactTags)
	{
		if (!InActorTags.Contains(Tag))
		{
			return false;
		}
	}
	for (const FString& Pattern : WildcardTags)
	{
		if (!AnyActorTagMatches(Pattern))
		{
			return false;
		}
	}
	return true;
}

void FPCGExActorSelectionDetails::MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const
{
	if (Selection == EPCGExActorSelection::ByClass)
	{
		if (ActorClass)
		{
			OutKeys.Emplace(TSubclassOf<UObject>(ActorClass));
		}
		return;
	}

	// The tracking key handles wildcard tags on its own; tags this node matches literally are simply over-tracked.
	for (const FName& Tag : Tags)
	{
		if (!Tag.IsNone())
		{
			OutKeys.Emplace(Tag);
		}
	}
}

FString FPCGExActorSelectionDetails::GetTitleInformation() const
{
	if (Selection == EPCGExActorSelection::ByClass)
	{
		return ActorClass ? ActorClass->GetName() : FString();
	}

	TArray<FString> TagStrings;
	for (const FName& Tag : Tags)
	{
		if (!Tag.IsNone())
		{
			TagStrings.Add(Tag.ToString());
		}
	}
	return FString::Join(TagStrings, TEXT(", "));
}

#pragma endregion

namespace PCGExActorBounds
{
	namespace Internal
	{
		bool ShouldSkipPrimitive(const UPrimitiveComponent* InComponent, const FPCGExActorBoundsOutputDetails& InDetails)
		{
			// Editor sprites are not geometry, and PCG output must not feed back into its own inputs.
			if (InComponent->IsA<UBillboardComponent>())
			{
				return true;
			}
			return InDetails.bIgnorePCGGeneratedComponents && InComponent->ComponentTags.Contains(PCGHelpers::DefaultPCGTag);
		}

		bool FinalizeSnapshot(const FTransform& InActorTransform, const FBox& InWorldBounds, const FBox& InLocalBounds, const FName InSortKey, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, FSnapshot& OutSnapshot)
		{
			if (!InWorldBounds.IsValid)
			{
				if (InDetails.bOmitActorsWithoutBounds)
				{
					return false;
				}

				OutSnapshot.Transform = InDetails.BoundsSource == EPCGExActorBoundsSource::WorldAABB ? FTransform(InActorTransform.GetLocation()) : InActorTransform;
				OutSnapshot.LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
				OutSnapshot.SortKey = InSortKey;
				return InCullBox == nullptr || InCullBox->IsInside(InActorTransform.GetLocation());
			}

			if (InCullBox && !InCullBox->Intersect(InWorldBounds))
			{
				return false;
			}

			OutSnapshot.SortKey = InSortKey;

			if (InDetails.BoundsSource == EPCGExActorBoundsSource::WorldAABB)
			{
				OutSnapshot.Transform = FTransform(InWorldBounds.GetCenter());
				const FVector Extent = InWorldBounds.GetExtent();
				OutSnapshot.LocalBounds = FBox(-Extent, Extent);
			}
			else
			{
				OutSnapshot.Transform = InActorTransform;
				OutSnapshot.LocalBounds = InLocalBounds;
			}

			return true;
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

		TArray<FPCGSelectionKey> Keys;
		InSelection.MakeTrackingKeys(Keys);
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

	bool SnapshotActor(const AActor* InActor, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, FSnapshot& OutSnapshot)
	{
		check(InActor);

		const FTransform ActorTransform = InActor->GetActorTransform();
		const bool bActorSpace = InDetails.BoundsSource == EPCGExActorBoundsSource::ActorSpace;
		// A zero scale has no inverse; such an actor has no extent either, so an empty local box is exact.
		const bool bCanInvert = ActorTransform.GetScale3D().GetAbsMin() > UE_KINDA_SMALL_NUMBER;

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

			// Per-component inverse is tighter than inverting the world union once.
			if (bActorSpace)
			{
				LocalBounds += bCanInvert ? ComponentBox.InverseTransformBy(ActorTransform) : FBox(FVector::ZeroVector, FVector::ZeroVector);
			}
		});

		return Internal::FinalizeSnapshot(ActorTransform, WorldBounds, LocalBounds, InActor->GetFName(), InDetails, InCullBox, OutSnapshot);
	}

	bool SnapshotBox(const FTransform& InActorTransform, const FBox& InWorldBounds, const FName InSortKey, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, FSnapshot& OutSnapshot)
	{
		FBox LocalBounds(ForceInit);
		if (InWorldBounds.IsValid && InDetails.BoundsSource == EPCGExActorBoundsSource::ActorSpace)
		{
			const bool bCanInvert = InActorTransform.GetScale3D().GetAbsMin() > UE_KINDA_SMALL_NUMBER;
			LocalBounds = bCanInvert ? InWorldBounds.InverseTransformBy(InActorTransform) : FBox(FVector::ZeroVector, FVector::ZeroVector);
		}

		return Internal::FinalizeSnapshot(InActorTransform, InWorldBounds, LocalBounds, InSortKey, InDetails, InCullBox, OutSnapshot);
	}

	void Sort(TArray<FSnapshot>& InOutSnapshots)
	{
		Algo::Sort(InOutSnapshots, [](const FSnapshot& A, const FSnapshot& B)
		{
			return A.SortKey.Compare(B.SortKey) < 0;
		});
	}

	void WritePoints(UPCGBasePointData* InData, const TArray<FSnapshot>& InSnapshots)
	{
		check(InData);

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
			const FSnapshot& Snapshot = InSnapshots[i];
			const FVector Location = Snapshot.Transform.GetLocation();

			Transforms[i] = Snapshot.Transform;
			BoundsMin[i] = Snapshot.LocalBounds.Min;
			BoundsMax[i] = Snapshot.LocalBounds.Max;
			Seeds[i] = PCGHelpers::ComputeSeed(static_cast<int>(Location.X), static_cast<int>(Location.Y), static_cast<int>(Location.Z));
		});
	}
}
