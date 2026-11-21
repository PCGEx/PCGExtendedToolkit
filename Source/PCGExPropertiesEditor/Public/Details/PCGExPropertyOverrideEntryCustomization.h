// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IDetailPropertyRow.h"
#include "IPropertyTypeCustomization.h"
#include "PCGExPropertyRowHost.h"
#include "StructUtils/InstancedStruct.h"
#include "Styling/SlateTypes.h"
#include "UObject/WeakObjectPtr.h"

class FStructOnScope;
class IPropertyUtilities;
class UUserDefinedStruct;
class SWidget;
class UPCGExPropertyCollectionComponent;
struct FPCGExProperty;
struct FPCGExPropertyOverrideEntry;


/**
 * One override row: toggle, name label and value editor for an FPCGExPropertyOverrideEntry.
 *
 * A disabled row shows the value its host's consumers resolve to (IPCGExPropertyRowHost::EDITOR_ResolveInheritedValue),
 * off a copy this customization owns; the stored slot is shown only while enabled, or when nothing below declares the
 * property. The toggle lands the slot on that inherited value either way: the seed an enabled row starts from, the
 * mirror a disabled one keeps.
 *
 * Every widget and lambda built here reads through members, so their backing data outlives them: the detail panel
 * releases customizations only after their Slate subtree is torn down. InnerScope and NestedScope are non-owning
 * aliases of memory the panel (the slot) or this instance (the inherited copy) keeps alive for the session.
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

	/** The entry the handle edits (its first instance), null when unreachable. No allocation: read every paint. */
	FPCGExPropertyOverrideEntry* AccessEntry() const;
	/** Every entry the handle edits, one per selected object. */
	void ForEachEntry(TFunctionRef<void(FPCGExPropertyOverrideEntry&)> Visitor) const;
	const FPCGExProperty* AccessEntryProperty() const;
	FText GetEntryNameText() const;
	FText GetEntryTypeText() const;

	/** Where the rows sit in the host, rebuilt from the handles on each call: the panel may rebind its scope between them. */
	FPCGExPropertyRowSite BuildRowSite() const;

	/** What the entry resolves to while disabled; false when the host or the declaration cannot be reached. */
	bool ResolveInheritedValue(const FPCGExPropertyRowSite& Site, FInstancedStruct& Out) const;
	bool ResolveInheritedValue(FInstancedStruct& Out) const;

	/** On a component instance the EnabledOverrides set, which its bEnabled mirrors with a lag; bEnabled elsewhere. */
	bool IsOverrideEnabled(const FPCGExPropertyOverrideEntry& Entry) const;
	bool IsOverrideEnabled() const;
	ECheckBoxState GetToggleState() const;
	void OnToggleStateChanged(ECheckBoxState NewState);

	/**
	 * The raw edit: every entry lands on the inherited value (the seed an enabled row starts from, the mirror a disabled
	 * one keeps) and takes the flag. Runs inside a caller's transaction and pre/post notify bracket.
	 */
	void ApplyOverrideEnabled(bool bEnable);

	/** The toggle: ApplyOverrideEnabled under its own transaction and entry-handle notify bracket, then a rebuild. */
	void SetOverrideEnabled(bool bEnable);

	/** The value rows alias one memory, the inherited copy or the slot; only a rebuild swaps it. */
	void RequestRowsRebuild() const;

	/** The toggle checkbox, one widget for every host. */
	TSharedRef<SWidget> BuildOverrideToggleWidget();

	/** The row's name cell: the toggle, then the name and type label. */
	TSharedRef<SWidget> BuildNameContentWidget();

	/** Reset = defer to the Blueprint chain, i.e. disable. Installed everywhere: UE's default reset would blank the entry. */
	FResetToDefaultOverride MakeArchetypeResetOverride();
	bool IsResetToInheritedVisible(TSharedPtr<IPropertyHandle> Handle) const;
	void ResetToInherited(TSharedPtr<IPropertyHandle> Handle);

	TWeakPtr<IPropertyHandle> PropertyHandlePtr;
	TSharedPtr<IPropertyHandle> ValueHandlePtr;
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;

	// The inherited copy the value rows alias while the slot is disabled. Declared before the scopes that alias it.
	FInstancedStruct InheritedValue;
	TSharedPtr<FStructOnScope> InnerScope;

	// Aliases FPCGExProperty_Struct::Value memory; must outlive every row that captures it.
	TSharedPtr<FStructOnScope> NestedScope;

	FDelegateHandle UserDefinedStructReinstancedHandle;

	// The row host (IPCGExPropertyRowHost) these rows belong to: an outer, or the asset of the outer package when the
	// panel edits an outer-less copy (the collection grid). Templates included: a template resolves through its own chain.
	TWeakObjectPtr<UObject> WeakRowHost;

	// Set for EVERY non-template component outer, Instance-created included: its EnabledOverrides set is the toggle's truth.
	TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLiveComponent;

	// True only when the component has a Blueprint class chain to reset toward (CreationMethod != Instance). Gates the
	// reset arrow only: Instance-created components drive the set but have no chain.
	bool bComponentHasArchetypeChain = false;

	// First non-template outer. Drives the inline row's owner-change hook so external-struct edits dirty + signal PCG.
	TWeakObjectPtr<UObject> WeakOwner;
};
