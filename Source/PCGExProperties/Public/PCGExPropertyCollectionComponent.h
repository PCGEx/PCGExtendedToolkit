// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ComponentInstanceDataCache.h"
#include "PCGExProperty.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

#include "PCGExPropertyCollectionComponent.generated.h"

class UPCGExPropertyCollectionComponent;
struct FPropertyChangedChainEvent;

// The capture/apply path solves the BP-recompile instance-wipe bug, which is an editor-only
// problem. Cooked builds don't recompile BPs at runtime, so there's no reinstance gap to
// preserve state across. Struct definitions are kept un-gated (UHT rejects WITH_EDITOR
// around UPROPERTYs), and only the captured-divergence UPROPERTY fields inside the
// FActorComponentInstanceData subclass are WITH_EDITORONLY_DATA. The InstanceData methods
// gate their bodies with WITH_EDITOR so cooked builds fall through to base no-op behavior.

/**
 * A single instance-level divergence from the archetype's local Schemas, keyed by stable
 * identity (HeaderId primary, Name fallback for legacy / pre-sync entries).
 *
 * "Identity" mirrors FPCGExPropertySchemaCollection::SyncFromArchetype's matching policy --
 * HeaderId is the editor-only stable key that survives renames, reorders, and type changes;
 * Name is the fallback for entries that pre-date the HeaderId scheme.
 *
 * Only constructed inside the editor-only capture path; in cooked builds the type exists
 * but has no live instances.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExCapturedSchemaOverride
{
	GENERATED_BODY()

	UPROPERTY()
	int32 HeaderId = 0;

	UPROPERTY()
	FName Name = NAME_None;

	UPROPERTY()
	FInstancedStruct Property;
};

/**
 * A single instance-level divergence from the archetype's ImportOverrides.Overrides.
 *
 * Identity is PropertyName (primary). Index is captured as a parallel-array fallback for the
 * case where the live instance's outer-and-inner identity were both missing at snapshot time
 * (UE's per-property delta can blank inner FInstancedStruct content; outer identity may not
 * have been migrated onto the CDO yet). The Overrides array is structurally parallel with the
 * imports-only resolved schema by construction, so same-index pairing is safe when no other
 * signal survives.
 *
 * Only constructed inside the editor-only capture path; in cooked builds the type exists
 * but has no live instances.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExCapturedImportOverride
{
	GENERATED_BODY()

	UPROPERTY()
	FName PropertyName = NAME_None;

	UPROPERTY()
	int32 SourceIndex = INDEX_NONE;

	UPROPERTY()
	bool bEnabled = false;

	UPROPERTY()
	FInstancedStruct Value;
};

/**
 * Instance data preserved across Blueprint reinstancing for UPCGExPropertyCollectionComponent.
 *
 * UE's default per-instance UPROPERTY delta serialization is unreliable for the shape
 * FPCGExPropertySchemaCollection has -- TArray<FStruct> where each entry's Property is an
 * FInstancedStruct. Across a BP recompile, the captured delta either drops the polymorphic
 * inner-struct content or fails to re-apply it, leaving instances with CDO defaults and
 * silently wiping user-authored values.
 *
 * Diff-against-archetype capture: at GetComponentInstanceData time, walk the live collection
 * and capture only entries whose value differs from the matching archetype entry. This
 * matches UE's standard "instance-authored = differs from CDO" semantics that the broken
 * default delta path is supposed to provide (and would, if FInstancedStruct-in-TArray
 * round-tripped correctly). The instance-data cache holds these captured entries as live
 * in-memory structs (not as serialized bytes), so FInstancedStruct content survives the
 * reinstance gap intact.
 *
 * ApplyToComponent walks the captured divergences and overwrites the matching entries on
 * the freshly reinstanced (and structure-synced) component in the PostUserConstructionScript
 * phase. Effect: inspector edits win over CS writes for the same field, while CS writes that
 * match the CDO are not captured (so changing CS logic on the CDO between recompiles works
 * for any field the instance hasn't diverged on). The inner property's per-field reset arrow
 * is the un-stick mechanism -- resetting to CDO restores parity, the entry is no longer
 * captured next recompile, and CS regains control of it.
 *
 * The captured-divergence UPROPERTY arrays are WITH_EDITORONLY_DATA -- cooked builds don't
 * carry them, and the method bodies are WITH_EDITOR-gated to fall through to base behavior.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExPropertyCollectionInstanceData : public FActorComponentInstanceData
{
	GENERATED_BODY()

	FPCGExPropertyCollectionInstanceData() = default;
	explicit FPCGExPropertyCollectionInstanceData(const UPCGExPropertyCollectionComponent* SourceComponent);

#if WITH_EDITORONLY_DATA
	/** Instance-level divergences from the archetype's local Schemas. */
	UPROPERTY()
	TArray<FPCGExCapturedSchemaOverride> DivergentSchemas;

	/** Instance-level divergences from the archetype's ImportOverrides.Overrides. */
	UPROPERTY()
	TArray<FPCGExCapturedImportOverride> DivergentImportOverrides;

	/**
	 * Snapshot of the instance's EnabledOverrides TSet. Captured unconditionally (the TSet IS
	 * the authored state; even empty is meaningful) and replayed wholesale on apply.
	 */
	UPROPERTY()
	TSet<FName> CapturedEnabledOverrides;

	UPROPERTY()
	bool bHasCapturedEnabledOverrides = false;
#endif

	virtual bool ContainsData() const override;
	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
};

/**
 * Actor component for attaching property collections to any actor.
 * Runtime-compatible - can be used on any actor, not just editor cages.
 *
 * Valency scans for these on cages/patterns during compilation.
 * Other systems can scan for them on spawned actors at runtime.
 *
 * This is the bridge between the property system and the actor world:
 * place this component on any actor, define properties in the Details panel,
 * and any PCGEx system that scans for properties will find them.
 *
 * Custom property types defined in any module will appear in the schema
 * collection's FInstancedStruct picker automatically.
 */
UCLASS(ClassGroup = "PCGEx", meta = (BlueprintSpawnableComponent, DisplayName = "PCGEx Property Collection", PCGExNodeLibraryDoc="metadata/keys/property-collection-component"))
class PCGEXPROPERTIES_API UPCGExPropertyCollectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCGExPropertyCollectionComponent();

	virtual void OnRegister() override;

	// Fresh non-CDO instances start with every ImportOverride disabled regardless of what the
	// CDO authored -- the toggle is purely instance state. Resolution falls back through the BP
	// class chain so the CDO's authored override stays effective on un-toggled instances.
	// Does NOT fire on load: saved/authored instances are handled by PostLoad instead.
	virtual void OnComponentCreated() override;

	/**
	 * Load-time invariant enforcement for override-toggle state:
	 * - Templates: EnabledOverrides is cleared. Templates author overrides through bEnabled
	 *   alone; a non-empty template TSet is legacy poison (older builds synced it on CDO toggle
	 *   edits) and is what in-parity instances inherited through archetype delta at every
	 *   package load -- the root of the CDO->instance toggle bleed.
	 * - Instances (editor): packages older than InstanceOwnedOverrideToggles rebuild the TSet
	 *   from the bEnabled-vs-archetype diff (the only ordering-stable authored signal in legacy
	 *   data; template bEnabled is authored and never mutated at load). Then bEnabled is
	 *   re-derived from the TSet so the mirror never carries archetype-inherited state.
	 */
	virtual void PostLoad() override;

	/**
	 * Serializes EnabledOverrides unconditionally into persistent packages (custom-version
	 * gated) on top of the regular UPROPERTY pass. Tagged delta serialization omits the TSet
	 * whenever it matches the archetype, and omitted state re-resolves to the archetype's
	 * CURRENT value on the next load -- i.e. per-instance toggles silently follow CDO toggle
	 * changes. The unconditional copy pins the authored per-instance state to the package.
	 */
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	// On chain-leaf bEnabled events, re-derive the bEnabled mirror from the authoritative TSet
	// (never the inverse -- see SyncBEnabledFromOverrideSet). This both rejects UE's
	// CDO->instance propagation fallout (UObject::PostEditChangeChainProperty forwards template
	// chain edits to archetype instances after raw-importing the new value into in-parity ones)
	// and normalizes any non-authoritative bEnabled write. On template edits, additionally walk
	// dependent instances after Super and restore their bEnabled from each instance's own TSet
	// as a backstop for raw-import paths that carry no notify.
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Capture the live Properties collection into FPCGExPropertyCollectionInstanceData so it
	 * survives Blueprint reinstancing. See the struct comment for why we can't rely on UE's
	 * default per-instance delta path. The override is declared always (UFUNCTION
	 * declarations can't be conditionally compiled) but its capture logic is WITH_EDITOR-only;
	 * in cooked builds the captured-state struct has no editor-only fields populated and
	 * ContainsData() returns false, so this falls back to Super::GetComponentInstanceData().
	 */
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;

	/**
	 * Property collection with schema definitions and default values.
	 * These compile into runtime property data during cage/pattern builds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Properties")
	FPCGExPropertySchemaCollection Properties;

	/**
	 * Authoritative per-instance "which import overrides are enabled" set.
	 *
	 * The matching bEnabled on each FPCGExPropertyOverrideEntry is a DERIVED mirror that UE's
	 * propagation machinery overwrites on CDO edits (raw value import + forwarded
	 * PostEditChangeChainProperty on every archetype instance -- neither can be blocked).
	 * The TSet survives because UE's edit chain targets the nested bool, not this top-level
	 * field. Data flows one way only: TSet -> bEnabled (SyncBEnabledFromOverrideSet); the TSet
	 * itself is written exclusively through SetOverrideEnabled, the instance-data restore, and
	 * the PostLoad legacy migration.
	 *
	 * Invariants (enforced by PostLoad, Serialize and the IsTemplate guards in the sync paths):
	 * - Templates never own a TSet: theirs is always empty. Template bEnabled is the authored
	 *   override layer that chain resolution reads directly; a template TSet would leak into
	 *   in-parity instances through archetype delta at load.
	 * - Instances never inherit the TSet through archetype delta: Serialize writes it into
	 *   persistent packages unconditionally, so a saved-in-parity instance can no longer adopt
	 *   the template's toggle state on its next load.
	 *
	 * Empty on fresh instances (OnComponentCreated); captured/restored across BP reinstancing
	 * via FPCGExPropertyCollectionInstanceData.
	 *
	 * Scoped to this component intentionally -- other consumers of FPCGExPropertyOverrides
	 * (Tuple node, level exporter) aren't subject to CDO->instance propagation and use bEnabled
	 * directly.
	 */
	UPROPERTY()
	TSet<FName> EnabledOverrides;

#if WITH_EDITOR
	// Re-derive every entry's bEnabled from EnabledOverrides. Use after CDO->instance propagation
	// may have clobbered bEnabled, or after restoring the TSet from instance data. No-op on
	// templates: their bEnabled is authored data and must never be derived.
	//
	// There is intentionally NO inverse (bEnabled -> TSet): UObject::PostEditChangeChainProperty
	// forwards template chain edits to archetype instances, so any mirror->authority fold runs
	// on propagation fallout and bakes the CDO's toggle into instance-authored state. The TSet
	// is written exclusively through SetOverrideEnabled (and the PostLoad legacy migration).
	void SyncBEnabledFromOverrideSet();
#endif

	/**
	 * Toggle an import-override entry atomically: writes the TSet (authoritative) and mirrors
	 * into the matching entry's bEnabled (UI). Single entry point for activation paths
	 * (notably SetProperty K2 thunks auto-enabling on write). No-op for unknown / None names.
	 * Local schemas have no toggle; this only affects ImportOverrides entries.
	 * On templates only bEnabled is written: templates author through bEnabled and their TSet
	 * stays empty (see PostLoad).
	 */
	void SetOverrideEnabled(FName PropertyName, bool bEnabled);

	/**
	 * Dynamic multicast delegate fired alongside PreparePropertyValues. BP authors can BindEvent
	 * to this on any instance of the component without needing to subclass -- useful when a
	 * level-placed actor wants to compute schema values from world state.
	 *
	 * The bound function runs inside the same snapshot/restore window as PreparePropertyValues,
	 * so writes through SetProperty are export-time-only.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPCGExOnPreparePropertyValues, UPCGExPropertyCollectionComponent*, CollectionComponent);

	UPROPERTY(BlueprintAssignable, Category = "Properties", meta=(DisplayName="On Prepare Property Values"))
	FPCGExOnPreparePropertyValues OnPreparePropertyValues;

	/**
	 * Fires immediately before the component's authored schema is read for level / actor
	 * export (ExtractSchemaFromActor). BP override (subclass) and OnPreparePropertyValues
	 * (instance bind) both flow through here -- the C++ default body broadcasts the delegate
	 * so a single chokepoint serves both hook styles.
	 *
	 * Writes through SetProperty during this event are visible to the exporter but reverted
	 * on the live component afterwards, so they don't get captured as instance-authored state
	 * by the BP recompile machinery. Intended workaround for the "construction-script writes
	 * become sticky" limitation: if a value needs to be derived (from world position, owning
	 * actor state, world-state queries, etc.), set it here instead of in the construction
	 * script and it stays out of the inspector-authored set.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Properties", meta=(DisplayName="On Prepare Property Values"))
	void PreparePropertyValues();

	/**
	 * Get the property collection.
	 * @return Reference to the property schema collection
	 */
	const FPCGExPropertySchemaCollection& GetProperties() const
	{
		return Properties;
	}

	/**
	 * Get the property collection (mutable).
	 * @return Reference to the property schema collection
	 */
	FPCGExPropertySchemaCollection& GetPropertiesMutable()
	{
		return Properties;
	}

	/**
	 * Resolve the schema with the BP class chain as fallback override layers. Instance's own
	 * ImportOverrides leads, then ancestors in inheritance order, then the asset's authored
	 * value. First layer with bEnabled=true wins per entry.
	 *
	 * Editor walks SCS templates via FindSCSTemplateInClass (the GetArchetype() path hits the
	 * FInstancedStruct-on-CDO-subobject duplication issue). Cooked walks GetArchetype() directly
	 * -- the subobject duplicate is safe there.
	 */
	TArray<FInstancedStruct> BuildResolvedSchema() const;

	/**
	 * Same chain construction as BuildResolvedSchema, but does NOT include this instance's own
	 * ImportOverrides. Returns "what value would this actor surface if it didn't author any
	 * per-instance overrides?" -- i.e., the BP class chain / CDO view, with asset defaults at
	 * the bottom.
	 *
	 * Used by collection ingestion paths to compute the "common-ancestor" collection default
	 * across contributing actors: when all unique BP classes agree on the inherited value for a
	 * property, the collection's default snaps to that value rather than to whichever actor's
	 * per-instance override happened to be iterated first.
	 */
	TArray<FInstancedStruct> BuildInheritedSchema() const;

	/**
	 * Asset-default view: schemas walked with NO override chain whatsoever (no instance overrides,
	 * no BP class chain). For imported schema entries this returns the asset's authored default;
	 * for locals it returns the schema's in-place value. Used as the fallback when contributing
	 * BP classes disagree at the CDO level (least-specific common point in the chain).
	 */
	TArray<FInstancedStruct> BuildAssetDefaultSchema() const
	{
		return Properties.BuildSchema({}, /*bIncludeOwnOverrides=*/false);
	}

#if WITH_EDITOR
	/**
	 * Find the SCS-authored template for the component named ComponentName on Cls's CDO.
	 *
	 * Walks UClass -> UBlueprintGeneratedClass -> SimpleConstructionScript -> FindSCSNode.
	 * Returns nullptr for native / non-BP classes and classes that don't author this
	 * component. Used by editor customizations to traverse a component instance's BP
	 * class chain, resolving each parent layer's stored archetype state directly.
	 *
	 * GetArchetype() can't substitute here: it returns the CDO actor's component
	 * subobject, which is a duplicate of the SCS template, and per UE's
	 * FInstancedStruct-in-TArray issues the duplicate can lose inner Value content.
	 * The SCS template is the authored source -- BP editor writes flow into it
	 * directly, so it reads correctly even when the duplicate path is broken.
	 */
	static const UPCGExPropertyCollectionComponent* FindSCSTemplateInClass(const UClass* Cls, FName ComponentName);
#endif

	/**
	 * Locate the property-collection component on an actor with copy-paste resilience.
	 *
	 * FindComponentByClass walks the runtime-registered OwnedComponents set. Level-editor
	 * copy-paste of an actor that carries an instance component sometimes produces a
	 * duplicate whose component is present in the persisted InstanceComponents array but
	 * never registered into OwnedComponents -- making FindComponentByClass return null
	 * even though the data is on the actor. Fallback walks the persisted list so callers
	 * (the actor-collection scan path, the level-exporter mesh path, and any future
	 * downstream consumer) all see the same set of components.
	 *
	 * Const-on-input mirrors AActor::FindComponentByClass: walking the component set is a
	 * read on the actor; the returned component is mutable because callers commonly need
	 * to call mutating accessors on it.
	 */
	static UPCGExPropertyCollectionComponent* FindOnActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (UPCGExPropertyCollectionComponent* Comp = Actor->FindComponentByClass<UPCGExPropertyCollectionComponent>())
		{
			return Comp;
		}
		for (UActorComponent* IC : Actor->GetInstanceComponents())
		{
			if (UPCGExPropertyCollectionComponent* Candidate = Cast<UPCGExPropertyCollectionComponent>(IC))
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	/**
	 * Extract the authored schema from a donor actor's property-collection component, with
	 * SyncPropertyName run first so every inner property carries up-to-date PropertyName +
	 * HeaderId. Returns empty when the actor lacks a component.
	 *
	 * Fires PreparePropertyValues before reading (BP override computes values dynamically).
	 * Per-schema inner Property values are snapshotted before the event and restored after,
	 * so writes inside the event are export-time-only.
	 *
	 * Snapshot is NARROW (only Schemas[i].Property) rather than the whole collection: a wholesale
	 * swap would reallocate every FInstancedStruct in ImportOverrides too, dangling any inspector
	 * widget holding an FStructOnScope alias into that memory (the enum picker is one such).
	 */
	static TArray<FInstancedStruct> ExtractSchemaFromActor(const AActor* Actor)
	{
		UPCGExPropertyCollectionComponent* Comp = FindOnActor(Actor);
		if (!Comp)
		{
			return {};
		}

		TArray<FInstancedStruct> SchemaPropertySnapshot;
		SchemaPropertySnapshot.Reserve(Comp->Properties.Schemas.Num());
		for (const FPCGExPropertySchema& Schema : Comp->Properties.Schemas)
		{
			SchemaPropertySnapshot.Add(Schema.Property);
		}

#if WITH_EDITOR
		// EnabledOverrides (TSet) is the authoritative signal; per-entry bEnabled is the UI mirror
		// UE per-property delta can clobber. Without this sync, an instance whose user toggled an
		// override off can still resolve to its cached Value (stuck bEnabled=true) and leak that
		// value through the chain walk into downstream ingestion.
		Comp->SyncBEnabledFromOverrideSet();
#endif

		Comp->PreparePropertyValues();

		for (FPCGExPropertySchema& Schema : Comp->Properties.Schemas)
		{
			Schema.SyncPropertyName();
		}
		TArray<FInstancedStruct> Result = Comp->BuildResolvedSchema();

		const int32 RestoreCount = FMath::Min(SchemaPropertySnapshot.Num(), Comp->Properties.Schemas.Num());
		for (int32 i = 0; i < RestoreCount; ++i)
		{
			Comp->Properties.Schemas[i].Property = SchemaPropertySnapshot[i];
		}
		return Result;
	}
};
