// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IDetailPropertyRow.h"
#include "IPropertyTypeCustomization.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FStructOnScope;
class IPropertyUtilities;
class UUserDefinedStruct;
class SWidget;
class UPCGExPropertyCollectionComponent;


/**
 * Customizes FPCGExPropertyOverrideEntry to show PropertyName label + Value widget.
 * Uses dynamic text for PropertyName to update when schema changes.
 *
 * Lifetime-critical state (InnerScope, ValueHandlePtr, EnabledHandlePtr) is held as
 * members so every widget / lambda created in CustomizeHeader / CustomizeChildren is
 * guaranteed to outlive its backing data by being transitively owned by this
 * customization instance. The detail panel releases customizations only after its
 * Slate subtree is torn down, which makes the lifetime coupling structural rather
 * than relying on ref-count ordering between the detail tree and child widgets.
 *
 * InnerScope is intentionally non-owning (aliases FInstancedStruct::GetMutableMemory)
 * so edits flow directly into the outer struct scope the detail panel already owns.
 * The inner type is pinned by the collection schema for the lifetime of the detail
 * session, so the aliased pointer is stable.
 */
class FPCGExPropertyOverrideEntryCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual ~FPCGExPropertyOverrideEntryCustomization() override;

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	void OnUserDefinedStructReinstanced(const UUserDefinedStruct& Struct);

	const struct FPCGExProperty* AccessEntryProperty() const;
	FText GetEntryNameText() const;
	FText GetEntryTypeText() const;

	TWeakPtr<IPropertyHandle> PropertyHandlePtr;
	TSharedPtr<IPropertyHandle> ValueHandlePtr;
	TSharedPtr<IPropertyHandle> EnabledHandlePtr;
	TSharedPtr<FStructOnScope> InnerScope;

	// Aliases FPCGExProperty_Struct::Value memory; must outlive every row that captures it.
	TSharedPtr<FStructOnScope> NestedScope;

	FDelegateHandle UserDefinedStructReinstancedHandle;
	// Captured at CustomizeHeader (inline external-structure rows expose no outers, so the reset
	// delegates can't re-derive it). Set for EVERY non-template component outer, Instance-created
	// included -- it drives the authoritative EnabledOverrides TSet the toggle checkbox writes.
	// Null for non-component owners (Tuple node, level exporter, asset rows).
	TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLiveComponent;

	// True only when the component has a BP archetype/class chain to reset toward
	// (CreationMethod != Instance). Gates the reset arrow ONLY -- kept separate from
	// WeakLiveComponent because Instance-created components drive the TSet but have no chain.
	bool bComponentHasArchetypeChain = false;

	// First non-template outer captured at CustomizeHeader. Covers component AND asset-collection
	// owners; drives the inline row's owner-change hook so external-struct edits dirty + signal PCG.
	TWeakObjectPtr<UObject> WeakOwner;

	int32 CachedOverrideIndex = INDEX_NONE;

	// Asset-default resolved once at CustomizeHeader (imports tree is stable for the detail
	// session). Invalid = "no asset default exists" -- TryGetResetSource relies on the distinction.
	FInstancedStruct CachedAssetDefaultValue;

	// Defensive-only: a type-mismatch reset reallocates and would invalidate InnerScope's alias.
	// Same-type copy preserves the alias and needs no refresh, which is the common case under
	// ReconcileImportOverrides.
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;

	FResetToDefaultOverride MakeArchetypeResetOverride() const;

	/**
	 * Build the toggle widget for the override's enable flag. In component context, returns a
	 * custom SCheckBox that writes EnabledOverrides directly via SetOverrideEnabled -- bypasses
	 * UE's property edit chain so CDO->instance propagation never fires for inspector toggles.
	 * Non-component owners get the standard handle-bound widget.
	 */
	TSharedRef<SWidget> BuildOverrideToggleWidget() const;

	/** Read the entry's PropertyName at the current OverrideIndex on the owning component. */
	FName GetOverrideEntryName() const;
};
