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
#include "Elements/PCGActorSelector.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/WildcardString.h"

namespace PCGExActorBounds
{
	namespace Internal
	{
		// Stock Get Actor Data matches wildcards case-insensitively; FName equality already is.
		bool AnyActorTagMatchesPattern(const TArray<FName>& InActorTags, const FString& InPattern)
		{
			for (const FName& ActorTag : InActorTags)
			{
				TStringBuilder<NAME_SIZE> Builder;
				ActorTag.AppendString(Builder);
				if (FWildcardString::IsMatchSubstring(*InPattern, Builder.GetData(), Builder.GetData() + Builder.Len(), ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
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
	}
}

#pragma region FPCGExActorTagSet

void FPCGExActorTagSet::Init(const TArray<FName>& InTags, const bool bAllowWildcards)
{
	Exact.Reset();
	Wildcards.Reset();

	for (const FName& Tag : InTags)
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
				Wildcards.Add(MoveTemp(TagString));
				continue;
			}
		}

		Exact.AddUnique(Tag);
	}
}

bool FPCGExActorTagSet::MatchesAny(const TArray<FName>& InActorTags) const
{
	for (const FName& Tag : Exact)
	{
		if (InActorTags.Contains(Tag))
		{
			return true;
		}
	}
	for (const FString& Pattern : Wildcards)
	{
		if (PCGExActorBounds::Internal::AnyActorTagMatchesPattern(InActorTags, Pattern))
		{
			return true;
		}
	}
	return false;
}

bool FPCGExActorTagSet::MatchesAll(const TArray<FName>& InActorTags) const
{
	for (const FName& Tag : Exact)
	{
		if (!InActorTags.Contains(Tag))
		{
			return false;
		}
	}
	for (const FString& Pattern : Wildcards)
	{
		if (!PCGExActorBounds::Internal::AnyActorTagMatchesPattern(InActorTags, Pattern))
		{
			return false;
		}
	}
	return true;
}

#pragma endregion

#pragma region FPCGExActorSelectionDetails

FPCGExActorSelectionDetails::FPCGExActorSelectionDetails()
	: ActorClass(AActor::StaticClass())
{
}

void FPCGExActorSelectionDetails::Init()
{
	Select.Init(Tags, bAllowWildcards);
	Skip.Init(SkipTags, bAllowWildcards);
}

bool FPCGExActorSelectionDetails::IsUsable() const
{
	switch (Selection)
	{
	case EPCGExActorSelection::ByClass:
		return ActorClass != nullptr;
	case EPCGExActorSelection::ByTag:
		return !Select.IsEmpty();
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

	return TagMatch == EPCGExActorTagMatch::Any ? Select.MatchesAny(InActorTags) : Select.MatchesAll(InActorTags);
}

void FPCGExActorSelectionDetails::MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const
{
	if (Selection == EPCGExActorSelection::ByClass)
	{
		if (ActorClass)
		{
			OutKeys.Emplace(TSubclassOf<UObject>(ActorClass));
		}
	}
	else
	{
		// The tracking key handles wildcard tags on its own; tags this node matches literally are simply over-tracked.
		for (const FName& Tag : Tags)
		{
			if (!Tag.IsNone())
			{
				OutKeys.Emplace(Tag);
			}
		}
	}
}

FString FPCGExActorSelectionDetails::GetTitleInformation() const
{
	FString Title = Selection == EPCGExActorSelection::ByClass ? (ActorClass ? ActorClass->GetName() : FString()) : PCGExActorBounds::Internal::JoinTags(Tags);

	const FString SkipTitle = PCGExActorBounds::Internal::JoinTags(SkipTags);
	if (!SkipTitle.IsEmpty())
	{
		Title += TEXT(" | skip ") + SkipTitle;
	}

	return Title;
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
		: Selection(InSelection), Output(InOutput), Kept(InKept)
	{
	}

	TArray<FSnapshot>* FSweep::Route(const TArray<FName>& InActorTags)
	{
		if (!Selection.MatchesTags(InActorTags))
		{
			return nullptr;
		}
		if (Selection.HasSkipTags() && Selection.ShouldSkip(InActorTags))
		{
			return Discarded;
		}
		return &Kept;
	}

	void FSweep::AddActor(const AActor* InActor)
	{
		if (InActor == Self || !Selection.MatchesClass(InActor))
		{
			return;
		}

		// Routed before the bounds read: a skipped actor only costs a snapshot when it has a pin to go to.
		if (TArray<FSnapshot>* Target = Route(InActor->Tags))
		{
			SnapshotActor(InActor, Output, CullBox, *Target);
		}
	}

	void FSweep::AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds)
	{
		if (TArray<FSnapshot>* Target = Route(InActorTags))
		{
			SnapshotBox(InActorTransform, InWorldBounds, Output, CullBox, *Target);
		}
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
