// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyCollectionComponent.h"

#include "PCGExPropertiesCustomVersion.h"
#include "PCGExPropertySchemaAsset.h"

#if WITH_EDITOR
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/UObjectIterator.h"
#endif

#if WITH_EDITOR
// Mirror SyncFromArchetype's matching policy: HeaderId primary, Name fallback.
// HeaderId precedence is load-bearing -- a renamed entry must still match by id, not
// shadow a sibling with the same Name.
namespace PCGExPropertyCollectionComponent
{
	template <typename TSchemaArray>
	static auto FindSchemaByIdentity(TSchemaArray& Schemas, int32 HeaderId, FName Name) -> decltype(&Schemas[0])
	{
		if (HeaderId != 0)
		{
			for (auto& S : Schemas)
			{
				if (S.HeaderId == HeaderId)
				{
					return &S;
				}
			}
		}
		if (!Name.IsNone())
		{
			for (auto& S : Schemas)
			{
				if (S.Name == Name)
				{
					return &S;
				}
			}
		}
		return nullptr;
	}
}
#endif // WITH_EDITOR

#pragma region UPCGExPropertyCollectionComponent

UPCGExPropertyCollectionComponent::UPCGExPropertyCollectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPCGExPropertyCollectionComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	if (IsTemplate())
	{
		return;
	}

	// Strip any bEnabled flags + TSet entries inherited from the CDO at construction. Override
	// toggles are instance-owned; resolution falls back through the BP chain to surface the
	// CDO/asset value when the instance hasn't toggled.
	for (FPCGExPropertyOverrideEntry& Entry : Properties.ImportOverrides.Overrides)
	{
		Entry.bEnabled = false;
	}
	EnabledOverrides.Empty();
}

void UPCGExPropertyCollectionComponent::PostLoad()
{
	Super::PostLoad();

	if (IsTemplate())
	{
		// Templates author overrides through per-entry bEnabled alone; the TSet is per-instance
		// state. Older builds synced the TSet on template toggle edits, and that baked set is
		// what in-parity instances inherited through archetype delta on every package load (the
		// CDO->instance toggle bleed). Clearing is deterministic, so no dirty flag is needed;
		// the next blueprint save persists the empty set.
		EnabledOverrides.Empty();
		return;
	}

#if WITH_EDITOR
	if (GetLinkerCustomVersion(FPCGExPropertiesCustomVersion::GUID) < FPCGExPropertiesCustomVersion::InstanceOwnedOverrideToggles)
	{
		// Legacy package: the TSet was delta-serialized against the archetype, so saved-in-parity
		// state re-resolved to the archetype's CURRENT set at this load -- poisoned and authored
		// entries are indistinguishable. Rebuild from the bEnabled-vs-archetype diff instead:
		// template bEnabled is authored data (never mutated at load), making it the only
		// ordering-stable signal regardless of template/instance PostLoad order; an instance bool
		// that diverges from its archetype was tag-serialized and is therefore trustworthy.
		// Known one-time loss: an instance toggle authored identical to the CDO's own toggle
		// reads as parity and un-pins (falls back to chain resolution -- same resolved value
		// today); re-toggling re-authors it under the always-serialized format.
		EnabledOverrides.Reset();

		const UPCGExPropertyCollectionComponent* Archetype = Cast<UPCGExPropertyCollectionComponent>(GetArchetype());
		const bool bHasArchetype = Archetype && Archetype != this;

		const TArray<FPCGExPropertyOverrideEntry>& OwnEntries = Properties.ImportOverrides.Overrides;
		for (int32 Index = 0; Index < OwnEntries.Num(); ++Index)
		{
			const FPCGExPropertyOverrideEntry& Entry = OwnEntries[Index];
			if (!Entry.bEnabled)
			{
				continue;
			}

			const FName Name = Entry.GetPropertyName();
			if (Name.IsNone())
			{
				continue;
			}

			// Match by PropertyName first, same-index second -- Overrides arrays are kept
			// parallel with the resolved imports schema by construction (same fallback policy
			// as the instance-data capture path).
			const FPCGExPropertyOverrideEntry* ArchEntry = nullptr;
			if (bHasArchetype)
			{
				for (const FPCGExPropertyOverrideEntry& Candidate : Archetype->Properties.ImportOverrides.Overrides)
				{
					if (Candidate.GetPropertyName() == Name)
					{
						ArchEntry = &Candidate;
						break;
					}
				}
				if (!ArchEntry && Archetype->Properties.ImportOverrides.Overrides.IsValidIndex(Index))
				{
					ArchEntry = &Archetype->Properties.ImportOverrides.Overrides[Index];
				}
			}

			if (!ArchEntry || !ArchEntry->bEnabled)
			{
				EnabledOverrides.Add(Name);
			}
		}
	}

	// Re-derive the UI mirror from the authoritative TSet. Tagged delta may have left bEnabled
	// holding the template's current value wherever instance and template were in parity at save
	// time -- without this, a CDO toggle flip would still read as enabled on untouched instances.
	SyncBEnabledFromOverrideSet();
#endif
}

void UPCGExPropertyCollectionComponent::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FPCGExPropertiesCustomVersion::GUID);

	Super::Serialize(Ar);

	// Persistent binary packages only. Transactions and in-memory copies (duplication, CPFUO
	// during BP reinstancing) round-trip the TSet correctly through the tagged UPROPERTY pass:
	// with template TSets pinned empty (PostLoad), an instance set is either empty-matching-empty
	// (reconstructs identically from the archetype) or divergent (tag-serialized), so false
	// parity is impossible there. Text formats keep the tagged representation only.
	if (!Ar.IsPersistent() || Ar.IsTransacting() || Ar.IsTextFormat())
	{
		return;
	}

	// Written unconditionally (templates included -- theirs is empty): the tagged pass omits the
	// TSet whenever it matches the archetype, and omitted state re-resolves to the archetype's
	// CURRENT value on the next load, silently following CDO toggle changes. This copy pins the
	// authored per-instance state to the package. The UPROPERTY pass still runs for backward
	// compat with pre-InstanceOwnedOverrideToggles packages.
	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FPCGExPropertiesCustomVersion::GUID) >= FPCGExPropertiesCustomVersion::InstanceOwnedOverrideToggles)
		{
			Ar << EnabledOverrides;
		}
	}
	else if (Ar.IsSaving())
	{
		Ar << EnabledOverrides;
	}
}

void UPCGExPropertyCollectionComponent::SetOverrideEnabled(FName PropertyName, bool bEnabled)
{
	if (PropertyName.IsNone())
	{
		return;
	}

	// Templates author overrides through bEnabled alone; the TSet is per-instance state and
	// stays empty on templates (see PostLoad). A template TSet would leak into in-parity
	// instances through archetype delta at load.
	if (!IsTemplate())
	{
		if (bEnabled)
		{
			EnabledOverrides.Add(PropertyName);
		}
		else
		{
			EnabledOverrides.Remove(PropertyName);
		}
	}

	if (FPCGExPropertyOverrideEntry* Entry = Properties.ImportOverrides.FindEntryMutableByName(PropertyName))
	{
		Entry->bEnabled = bEnabled;
	}
}

void UPCGExPropertyCollectionComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	// Repair instances whose schema drifted from their Blueprint's after a CDO edit -- UE's
	// per-property propagation drops FInstancedStruct type info through arrays. Skip templates
	// and Instance-created components (the latter own their schema; archetype is the empty CDO).
	if (IsTemplate() || CreationMethod == EComponentCreationMethod::Instance)
	{
		return;
	}

	const UPCGExPropertyCollectionComponent* Archetype = Cast<UPCGExPropertyCollectionComponent>(GetArchetype());
	if (!Archetype || Archetype == this)
	{
		return;
	}

	Properties.SyncFromArchetype(Archetype->Properties);
#endif
}

#if WITH_EDITOR
const UPCGExPropertyCollectionComponent* UPCGExPropertyCollectionComponent::FindSCSTemplateInClass(const UClass* Cls, FName ComponentName)
{
	const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Cls);
	if (!BPClass)
	{
		return nullptr;
	}
	USimpleConstructionScript* SCS = BPClass->SimpleConstructionScript;
	if (!SCS)
	{
		return nullptr;
	}
	const USCS_Node* Node = SCS->FindSCSNode(ComponentName);
	if (!Node)
	{
		return nullptr;
	}
	return Cast<UPCGExPropertyCollectionComponent>(Node->ComponentTemplate);
}

void UPCGExPropertyCollectionComponent::SyncBEnabledFromOverrideSet()
{
	// Templates never derive bEnabled: theirs is the authored override layer that chain
	// resolution reads directly, and their TSet is pinned empty (PostLoad) -- deriving here
	// would wipe authored CDO toggles (e.g. when a CDO actor is passed through
	// ExtractSchemaFromActor as a donor).
	if (IsTemplate())
	{
		return;
	}

	for (FPCGExPropertyOverrideEntry& Entry : Properties.ImportOverrides.Overrides)
	{
		const FName Name = Entry.GetPropertyName();
		Entry.bEnabled = !Name.IsNone() && EnabledOverrides.Contains(Name);
	}
}

void UPCGExPropertyCollectionComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Only chain edits whose leaf is FPCGExPropertyOverrideEntry::bEnabled are relevant.
	const FProperty* Leaf = PropertyChangedEvent.Property;
	const bool bIsBEnabledEdit =
		Leaf &&
		Leaf->GetFName() == GET_MEMBER_NAME_CHECKED(FPCGExPropertyOverrideEntry, bEnabled) &&
		Leaf->GetOwnerStruct() == FPCGExPropertyOverrideEntry::StaticStruct();

	// CRITICAL -- bEnabled is a derived mirror and must NEVER be folded back into the TSet.
	// UObject::PostEditChangeChainProperty (Obj.cpp:600-641, PropagatePostEditChange:827)
	// forwards this exact chain event to every archetype instance when a template is edited,
	// AFTER the details panel raw-imported the new bEnabled into in-parity instances. So this
	// override runs both for genuine edits on this object and as propagation fallout on every
	// instance of an edited template -- and a mirror->authority sync here converts the CDO's
	// toggle into instance-authored state (the recurring "instance toggles follow the base
	// class" bug). Re-derive the mirror from the authoritative TSet instead: propagated
	// clobbers self-heal at notify time, before Super's RerunConstructionScripts can capture
	// them into instance data. The TSet is written exclusively through SetOverrideEnabled.
	// No-op on templates (guard in the sync): their bEnabled IS the authored layer.
	if (bIsBEnabledEdit)
	{
		SyncBEnabledFromOverrideSet();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// On CDO/template edits Super propagates to dependents and clobbers their bEnabled. Restore
	// each non-template's bEnabled from its own (untouched) TSet. No archetype-chain filter:
	// BP editor paths (CDO subobject vs SCS template vs ICH template) put the propagation target
	// and live-edit target in parallel branches, so IsBasedOnArchetype(this) silently misses real
	// dependents. Re-syncing untouched components is a harmless no-op.
	if (!bIsBEnabledEdit || !IsTemplate())
	{
		return;
	}
	for (TObjectIterator<UPCGExPropertyCollectionComponent> It; It; ++It)
	{
		UPCGExPropertyCollectionComponent* Dependent = *It;
		if (Dependent == this)
		{
			continue;
		}
		if (Dependent->IsTemplate())
		{
			continue;
		}

		Dependent->SyncBEnabledFromOverrideSet();
	}
}

#endif

namespace PCGExPropertyCollectionComponent
{
	// Shared chain construction used by BuildResolvedSchema and BuildInheritedSchema.
	// Editor walks SCS templates; cooked walks GetArchetype(). Skips Self so the same routine
	// works for both "include own overrides at the head" and "exclude own overrides" callers --
	// the caller decides via the bIncludeOwnOverrides flag on BuildSchema below.
	static void BuildOverrideChain(
		const UPCGExPropertyCollectionComponent* Self,
		TArray<const FPCGExPropertyOverrides*, TInlineAllocator<4>>& OutChain)
	{
#if WITH_EDITOR
		if (const AActor* Owner = Self->GetOwner())
		{
			for (const UClass* Cls = Owner->GetClass(); Cls; Cls = Cls->GetSuperClass())
			{
				const UPCGExPropertyCollectionComponent* Template =
					UPCGExPropertyCollectionComponent::FindSCSTemplateInClass(Cls, Self->GetFName());
				if (!Template || Template == Self)
				{
					continue;
				}
				OutChain.Add(&Template->Properties.ImportOverrides);
			}
		}
#else
		for (const UObject* Arch = Self->GetArchetype(); Arch && Arch != Self; Arch = Arch->GetArchetype())
		{
			if (const UPCGExPropertyCollectionComponent* Comp = Cast<UPCGExPropertyCollectionComponent>(Arch))
			{
				OutChain.Add(&Comp->Properties.ImportOverrides);
			}
			if (Arch->GetArchetype() == Arch)
			{
				break;
			}
		}
#endif
	}
}

TArray<FInstancedStruct> UPCGExPropertyCollectionComponent::BuildResolvedSchema() const
{
	TArray<const FPCGExPropertyOverrides*, TInlineAllocator<4>> Chain;
	PCGExPropertyCollectionComponent::BuildOverrideChain(this, Chain);
	return Properties.BuildSchema(MakeArrayView(Chain));
}

TArray<FInstancedStruct> UPCGExPropertyCollectionComponent::BuildInheritedSchema() const
{
	// Same chain as BuildResolvedSchema, but pass bIncludeOwnOverrides=false so this instance's
	// ImportOverrides does NOT participate -- the resulting view is "what would this actor
	// surface if it didn't override anything", i.e., the BP CDO's chain-resolved value (or asset
	// default at the bottom if no CDO authored anything for the property).
	TArray<const FPCGExPropertyOverrides*, TInlineAllocator<4>> Chain;
	PCGExPropertyCollectionComponent::BuildOverrideChain(this, Chain);
	return Properties.BuildSchema(MakeArrayView(Chain), /*bIncludeOwnOverrides=*/false);
}

void UPCGExPropertyCollectionComponent::PreparePropertyValues_Implementation()
{
	// Default body: BP subclass overrides replace this; the delegate path serves BP authors
	// who BindEvent on an instance instead of subclassing.
	OnPreparePropertyValues.Broadcast(this);
}

TStructOnScope<FActorComponentInstanceData> UPCGExPropertyCollectionComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> InstanceData =
		MakeStructOnScope<FActorComponentInstanceData, FPCGExPropertyCollectionInstanceData>(this);

	// Fall back to the base path when we have nothing to restore -- avoids stashing an empty
	// override list on components that don't diverge from their CDO. In cooked builds the
	// captured-divergence fields don't exist, so ContainsData() falls through to Super::ContainsData
	// and we always end up taking the base path here.
	if (!InstanceData.Cast<FPCGExPropertyCollectionInstanceData>()->ContainsData())
	{
		InstanceData = Super::GetComponentInstanceData();
	}
	return InstanceData;
}

#pragma endregion

#pragma region FPCGExPropertyCollectionInstanceData

FPCGExPropertyCollectionInstanceData::FPCGExPropertyCollectionInstanceData(const UPCGExPropertyCollectionComponent* SourceComponent)
	: FActorComponentInstanceData(SourceComponent)
{
#if WITH_EDITOR
	if (!SourceComponent)
	{
		return;
	}

	// Capture EnabledOverrides unconditionally -- the TSet IS the per-instance authored state
	// (even when empty) and OnComponentCreated wipes it on every reconstruction. Done before the
	// archetype-required captures so orphaned-archetype cases still get the TSet replayed.
	CapturedEnabledOverrides = SourceComponent->EnabledOverrides;
	bHasCapturedEnabledOverrides = true;

	// Local schemas: standard diff-against-CDO. Import overrides: hybrid (see per-entry comment).
	const UPCGExPropertyCollectionComponent* Archetype = Cast<UPCGExPropertyCollectionComponent>(SourceComponent->GetArchetype());
	if (!Archetype || Archetype == SourceComponent)
	{
		return;
	}

	const FPCGExPropertySchemaCollection& InstanceCol = SourceComponent->GetProperties();
	const FPCGExPropertySchemaCollection& ArchCol = Archetype->Properties;

	for (const FPCGExPropertySchema& InstanceSchema : InstanceCol.Schemas)
	{
		// No identity = nothing useful to match on at apply time; skip rather than capture an
		// orphan that can never be replayed.
		if (InstanceSchema.HeaderId == 0 && InstanceSchema.Name.IsNone())
		{
			continue;
		}

		const FPCGExPropertySchema* MatchedArch = PCGExPropertyCollectionComponent::FindSchemaByIdentity(
			ArchCol.Schemas, InstanceSchema.HeaderId, InstanceSchema.Name);

		// No archetype match: this entry exists only on the instance. Treat as authored and
		// capture (rare in practice: SyncFromArchetype drops these on OnRegister, but a fresh
		// instance-created component might briefly carry such state before its first sync).
		const bool bDiffers = !MatchedArch || (InstanceSchema.Property != MatchedArch->Property);
		if (!bDiffers)
		{
			continue;
		}

		FPCGExCapturedSchemaOverride& Captured = DivergentSchemas.AddDefaulted_GetRef();
		Captured.HeaderId = InstanceSchema.HeaderId;
		Captured.Name = InstanceSchema.Name;
		Captured.Property = InstanceSchema.Property;
	}

	// Precompute archetype lookup by PropertyName once -- the per-instance match loop would
	// otherwise be O(K*M) for K instance entries against M archetype entries.
	TMap<FName, int32> ArchIndexByName;
	ArchIndexByName.Reserve(ArchCol.ImportOverrides.Overrides.Num());
	for (int32 i = 0; i < ArchCol.ImportOverrides.Overrides.Num(); ++i)
	{
		const FName ArchName = ArchCol.ImportOverrides.Overrides[i].GetPropertyName();
		if (!ArchName.IsNone())
		{
			ArchIndexByName.Add(ArchName, i);
		}
	}

	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCol.ImportOverrides.Overrides.Num(); ++InstanceIndex)
	{
		const FPCGExPropertyOverrideEntry& InstanceEntry = InstanceCol.ImportOverrides.Overrides[InstanceIndex];
		const FName PropertyName = InstanceEntry.GetPropertyName();

		// Match by PropertyName; same-index is the fallback because Overrides is structurally
		// aligned with the resolved imports schema by construction.
		const FPCGExPropertyOverrideEntry* MatchedArch = nullptr;
		if (!PropertyName.IsNone())
		{
			if (const int32* Found = ArchIndexByName.Find(PropertyName))
			{
				MatchedArch = &ArchCol.ImportOverrides.Overrides[*Found];
			}
		}
		if (!MatchedArch && ArchCol.ImportOverrides.Overrides.IsValidIndex(InstanceIndex))
		{
			MatchedArch = &ArchCol.ImportOverrides.Overrides[InstanceIndex];
		}

		// bEnabled is instance-owned (OnComponentCreated wipes it on every reconstruction), so any
		// bEnabled=true must be preserved regardless of CDO match -- otherwise toggling on while
		// the CDO matches would round-trip to false on reconstruct. Value is captured on diff even
		// when disabled, so a toggle-off/toggle-on cycle preserves the previously-authored value
		// (inspector value is iteration scratch space).
		const bool bValueDiffers = MatchedArch && MatchedArch->Value != InstanceEntry.Value;
		if (!InstanceEntry.bEnabled && !bValueDiffers)
		{
			continue;
		}

		FPCGExCapturedImportOverride& Captured = DivergentImportOverrides.AddDefaulted_GetRef();
		Captured.PropertyName = PropertyName;
		Captured.SourceIndex = InstanceIndex;
		Captured.bEnabled = InstanceEntry.bEnabled;
		Captured.Value = InstanceEntry.Value;
	}
#endif // WITH_EDITOR
}

bool FPCGExPropertyCollectionInstanceData::ContainsData() const
{
#if WITH_EDITOR
	return Super::ContainsData() || !DivergentSchemas.IsEmpty() || !DivergentImportOverrides.IsEmpty() || bHasCapturedEnabledOverrides;
#else
	return Super::ContainsData();
#endif
}

void FPCGExPropertyCollectionInstanceData::ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase)
{
	Super::ApplyToComponent(Component, CacheApplyPhase);
#if WITH_EDITOR
	UPCGExPropertyCollectionComponent* Typed = Cast<UPCGExPropertyCollectionComponent>(Component);
	if (!Typed)
	{
		return;
	}

	// Replayed twice per construction run (idempotent): pre-UCS so a construction script reads instance
	// values, post-UCS so inspector-authored divergences win over CS writes. See the struct comment.
	if (CacheApplyPhase != ECacheApplyPhase::PostSimpleConstructionScript &&
		CacheApplyPhase != ECacheApplyPhase::PostUserConstructionScript)
	{
		return;
	}

	FPCGExPropertySchemaCollection& Live = Typed->GetPropertiesMutable();

	for (const FPCGExCapturedSchemaOverride& Captured : DivergentSchemas)
	{
		FPCGExPropertySchema* Match = PCGExPropertyCollectionComponent::FindSchemaByIdentity(
			Live.Schemas, Captured.HeaderId, Captured.Name);
		if (!Match)
		{
			// Schema was removed from the CDO since capture; drop the override.
			continue;
		}

		// Type compatibility: if the CDO retyped this property since capture, the captured
		// FInstancedStruct holds a struct of the old type. Mirror SyncFromArchetype's "type
		// change resets value" rule -- prefer the CDO default by skipping the overwrite.
		const UScriptStruct* CapturedType = Captured.Property.GetScriptStruct();
		const UScriptStruct* MatchType = Match->Property.GetScriptStruct();
		if (!CapturedType || CapturedType != MatchType)
		{
			continue;
		}

		Match->Property = Captured.Property;
		Match->SyncPropertyName(); // Refresh inner PropertyName / HeaderId cache.
	}

	for (const FPCGExCapturedImportOverride& Captured : DivergentImportOverrides)
	{
		// Find the live entry: by PropertyName first; fall back to SourceIndex (parallel-array
		// invariant) when the captured PropertyName was missing at snapshot time. Without the
		// index fallback, a capture taken with broken inner+outer identity could never replay.
		FPCGExPropertyOverrideEntry* Match = nullptr;
		if (!Captured.PropertyName.IsNone())
		{
			for (FPCGExPropertyOverrideEntry& Entry : Live.ImportOverrides.Overrides)
			{
				if (Entry.GetPropertyName() == Captured.PropertyName)
				{
					Match = &Entry;
					break;
				}
			}
		}
		if (!Match && Live.ImportOverrides.Overrides.IsValidIndex(Captured.SourceIndex))
		{
			Match = &Live.ImportOverrides.Overrides[Captured.SourceIndex];
		}
		if (!Match)
		{
			// Imported entry no longer resolved (asset removed / renamed); drop.
			continue;
		}

		// bEnabled survives even when the captured Value lost its inner FInstancedStruct content
		// at snapshot time, so a plain "toggle this override on" replays correctly.
		Match->bEnabled = Captured.bEnabled;

		// Only restore Value when types match; a type-blanked capture would push the live entry
		// back into the bad state.
		const UScriptStruct* CapturedType = Captured.Value.GetScriptStruct();
		const UScriptStruct* MatchType = Match->Value.GetScriptStruct();
		if (CapturedType && CapturedType == MatchType)
		{
			Match->Value = Captured.Value;
		}
	}

	// Brings ImportOverrides.Overrides into alignment with the resolved imports tree -- handles
	// the case where a referenced asset's schema list drifted out-of-band since the BP CDO last
	// reconciled. Neither OnRegister (Schemas-only) nor the replay loop above (in-place writes)
	// fixes that drift.
	Live.ReconcileImportOverrides();

	// Restore TSet wholesale and re-derive bEnabled from it. Must run after ReconcileImportOverrides
	// (which can resize the array) so the bEnabled sync targets the post-reconcile entry set.
	if (bHasCapturedEnabledOverrides)
	{
		Typed->EnabledOverrides = CapturedEnabledOverrides;
		Typed->SyncBEnabledFromOverrideSet();
	}
#endif // WITH_EDITOR
}

void FPCGExPropertyCollectionInstanceData::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
#if WITH_EDITOR
	// FComponentInstanceDataCache doesn't auto-collect references from instance data structs
	// (see its header comment), so we walk every captured FInstancedStruct and forward to its
	// AddStructReferencedObjects -- keeps any UObject refs held by inner property structs
	// alive while CapturedProperties is in flight between capture and apply.
	for (FPCGExCapturedSchemaOverride& Captured : DivergentSchemas)
	{
		Captured.Property.AddStructReferencedObjects(Collector);
	}
	for (FPCGExCapturedImportOverride& Captured : DivergentImportOverrides)
	{
		Captured.Value.AddStructReferencedObjects(Collector);
	}
#endif
}

#pragma endregion
