// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "UObject/StructOnScope.h"
#include "PCGExProperty.h" // FPCGExPropertyOverrideEntry (ParkedOverrideBuffers member)

class IDetailPropertyRow;
class IPropertyUtilities;
class FResetToDefaultOverride;
class FTransactionObjectEvent;
class UPCGExPropertySchemaAsset;
struct FPCGExPropertyResolved;
struct FPCGExPropertySchemaCollection;

/**
 * Customizes FPCGExPropertySchemaCollection to:
 * - Show dynamic header with schema count
 * - Trigger refresh when schemas change (add/remove/reorder/type change)
 * - Sync PropertyName and HeaderId when array changes
 *
 * Instance mode (UPCGExPropertyCollectionComponent on a non-template actor):
 * - Schema structure is locked (no add/remove/reorder)
 * - Each entry shows name as a read-only label; only the value is editable
 * - Edit the Blueprint to change the schema definition
 */
class FPCGExPropertySchemaCollectionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual ~FPCGExPropertySchemaCollectionCustomization() override;

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	FText GetHeaderText() const;

	/** Called when Schemas array changes - syncs PropertyNames, reconciles imports, broadcasts refresh */
	void OnSchemasArrayChanged();

	/** Called when ImportedSchemas array changes -- imports tree shape changed, reconcile + refresh */
	void OnImportedSchemasArrayChanged();

	/** Called when a referenced UPCGExPropertySchemaAsset broadcasts OnSchemaAssetChanged */
	void OnImportedAssetChanged(UPCGExPropertySchemaAsset* ChangedAsset);

	/** ForceRefresh on undo/redo for our host -- UE transaction restore can reallocate
	 *  FInstancedStruct memory in ImportOverrides.Overrides, dangling per-entry FStructOnScope aliases. */
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);

	/**
	 * Render a single schema element as a flat single-row inline value editor.
	 * Returns false if the schema isn't a PCGExInlineValue type or any precondition fails,
	 * letting the caller fall back to FPCGExPropertySchemaCustomization's read-only rendering.
	 */
	bool TryRenderFlatInline(class IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> ElementHandle);

	/** Emit a small uppercase gray section header row -- matches the ZoneGraph customization style. */
	void EmitSectionHeader(class IDetailChildrenBuilder& ChildBuilder, const FString& Title) const;

	/**
	 * Emit one section header per imported asset and one row per override entry. Assumes the
	 * caller has already reconciled ImportOverrides so its Overrides array is parallel with
	 * the imports-only slice of Resolved.
	 */
	void EmitImportSections(class IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> CollectionHandle, const TArray<FPCGExPropertyResolved>& Resolved);

	/**
	 * Mutate the underlying collection: call ReconcileImportOverrides, then ask outer object's
	 * PostEditChangeProperty pipeline to run (NotifyPostChange). Safe to call from change handlers.
	 */
	void ReconcileAndNotify();

	/** ReconcileImportOverrides without freeing memory the still-mounted rows alias (see .cpp). */
	void ReconcileImportOverridesPreservingLiveRows(FPCGExPropertySchemaCollection& Collection);

	/** Subscribe to OnSchemaAssetChanged on every asset reachable through ImportedSchemas (BFS). */
	void SubscribeToImportedAssets();

	/** Remove all subscriptions registered through SubscribeToImportedAssets. Idempotent. */
	void UnsubscribeImportedAssets();

	/**
	 * Replace UE's default reset-to-default arrow on Row with one that copies the archetype's
	 * same-index local schema entry onto this collection's entry.
	 *
	 * Local-schema rows are built directly here (TryRenderFlatInline / fallback), so this
	 * customization owns the row and can attach the override directly. Import-override rows
	 * are built by FPCGExPropertyOverrideEntryCustomization, which handles its own reset.
	 *
	 * Only meaningful in bIsInstanceMode (the only context where an archetype exists). Caller
	 * is responsible for the gate.
	 */
	void ApplyLocalSchemaResetOverride(IDetailPropertyRow& Row, int32 SchemaIndex);

	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;
	TWeakPtr<IPropertyHandle> PropertyHandlePtr;
	TWeakPtr<IPropertyHandle> SchemasArrayHandlePtr;

	/** Outer host of the collection; used to filter OnObjectTransacted to events we own. */
	TWeakObjectPtr<UObject> WeakHostObject;

	/** Handle for the OnObjectTransacted binding -- removed in destructor. */
	FDelegateHandle ObjectTransactedHandle;

	/** True when the outer object is a non-template UPCGExPropertyCollectionComponent instance */
	bool bIsInstanceMode = false;

	/**
	 * Captured at CustomizeHeader for the instance-mode reset-to-archetype delegates.
	 *
	 * The handle UE passes to the IsVisible / Handler callbacks belongs to the row UE is
	 * rendering -- which, for inline-rendered FInstancedStruct content, is an external
	 * FStructOnScope row whose GetOuterObjects() returns nothing. Re-deriving the component
	 * from the handle therefore can't work for inline rows; capturing once at customization
	 * time and passing by-value into the lambdas does work for every row type.
	 *
	 * The archetype side of the reset (BP-chain walk) is resolved dynamically per paint
	 * inside ApplyLocalSchemaResetOverride's lambdas -- see TryGetLocalSchemaResetSource --
	 * so no separate WeakArchetypeComponent is cached here.
	 */
	TWeakObjectPtr<class UPCGExPropertyCollectionComponent> WeakLiveComponent;

	/**
	 * Struct scopes for inline flat rows in instance mode.
	 * Each FStructOnScope wraps the inner property struct memory (non-owning) and must outlive
	 * the widgets that reference it. Populated in CustomizeChildren; reset each rebuild.
	 */
	TArray<TSharedPtr<FStructOnScope>> InstanceScopes;

	/** Active OnSchemaAssetChanged subscriptions. Cleared on customization teardown / rebuild. */
	TArray<TPair<TWeakObjectPtr<UPCGExPropertySchemaAsset>, FDelegateHandle>> AssetDelegateHandles;

	/** Old override buffers kept alive so rows aliasing their memory outlive a reallocating reconcile;
	 *  each dies with this customization instance, after the rebuild disconnects those rows. */
	TArray<TArray<FPCGExPropertyOverrideEntry>> ParkedOverrideBuffers;

	/** Cached resolved entry count for GetHeaderText (called per Slate redraw). -1 means unset. */
	mutable int32 CachedResolvedCount = -1;
};
