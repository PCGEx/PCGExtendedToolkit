// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyOverrideEntryCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PCGExPropertyType_Struct.h"
#include "PropertyHandle.h"
#include "StructUtilsDelegates.h"
#include "Details/PCGExPropertyLabelRow.h"
#include "StructUtils/UserDefinedStruct.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PropertyHandle.h"
#include "Details/PCGExEditorCustomizationUtils.h"
#include "Details/PCGExPropertyLabelRow.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

// Forward declarations for the file-local helper namespace (defined further down).
// CustomizeHeader calls TryResolveAssetDefault to populate CachedAssetDefaultValue once
// per detail-panel rebuild.
namespace PCGExPropertyOverrideEntryCustomization
{
	bool TryResolveAssetDefault(
		const UPCGExPropertyCollectionComponent& Comp,
		int32 OverrideIndex,
		bool& OutEnabled,
		FInstancedStruct& OutValue);
}

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyOverrideEntryCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyOverrideEntryCustomization());
}

FPCGExPropertyOverrideEntryCustomization::~FPCGExPropertyOverrideEntryCustomization()
{
	if (UserDefinedStructReinstancedHandle.IsValid())
	{
		UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Remove(UserDefinedStructReinstancedHandle);
	}
}

void FPCGExPropertyOverrideEntryCustomization::OnUserDefinedStructReinstanced(const UUserDefinedStruct& Struct)
{
	// UDS reinstance: UScriptStruct* survives but GetStructureSize() may change, leaving the
	// FInstancedStruct allocation too small. Reinit drops payload but prevents OOB on next edit.
	if (!ValueHandlePtr.IsValid())
	{
		return;
	}

	TArray<void*> RawData;
	ValueHandlePtr->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return;
	}

	FInstancedStruct* OuterInstance = static_cast<FInstancedStruct*>(RawData[0]);
	if (!OuterInstance->IsValid())
	{
		return;
	}
	if (OuterInstance->GetScriptStruct() != FPCGExProperty_Struct::StaticStruct())
	{
		return;
	}

	FPCGExProperty_Struct* StructProp = reinterpret_cast<FPCGExProperty_Struct*>(OuterInstance->GetMutableMemory());
	if (StructProp->Value.GetScriptStruct() != &Struct)
	{
		return;
	}

	StructProp->Value.InitializeAs(&Struct);

	// Deferred refresh avoids tearing down the widget tree mid-event-handler stack.
	if (TSharedPtr<IPropertyUtilities> Util = WeakPropertyUtilities.Pin())
	{
		Util->RequestRefresh();
	}
}

const FPCGExProperty* FPCGExPropertyOverrideEntryCustomization::AccessEntryProperty() const
{
	if (!PropertyHandlePtr.IsValid())
	{
		return nullptr;
	}
	TArray<void*> RawData;
	PropertyHandlePtr.Pin()->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return nullptr;
	}
	const FPCGExPropertyOverrideEntry* Entry = static_cast<FPCGExPropertyOverrideEntry*>(RawData[0]);
	if (!Entry || !Entry->Value.IsValid())
	{
		return nullptr;
	}
	return Entry->Value.GetPtr<FPCGExProperty>();
}

FText FPCGExPropertyOverrideEntryCustomization::GetEntryNameText() const
{
	const FPCGExProperty* Prop = AccessEntryProperty();
	return Prop ? FText::FromName(Prop->PropertyName) : FText::FromString(TEXT("None"));
}

FText FPCGExPropertyOverrideEntryCustomization::GetEntryTypeText() const
{
	const FPCGExProperty* Prop = AccessEntryProperty();
	return Prop ? FText::FromName(Prop->GetDisplayTypeName()) : FText::FromString(TEXT("Unknown"));
}

FName FPCGExPropertyOverrideEntryCustomization::GetOverrideEntryName() const
{
	const UPCGExPropertyCollectionComponent* Live = WeakLiveComponent.Get();
	if (!Live)
	{
		return NAME_None;
	}
	if (!Live->Properties.ImportOverrides.Overrides.IsValidIndex(CachedOverrideIndex))
	{
		return NAME_None;
	}
	return Live->Properties.ImportOverrides.Overrides[CachedOverrideIndex].GetPropertyName();
}

TSharedRef<SWidget> FPCGExPropertyOverrideEntryCustomization::BuildOverrideToggleWidget() const
{
	// Non-component owners (Tuple node, level exporter rows): standard handle-bound checkbox.
	if (!WeakLiveComponent.IsValid())
	{
		return EnabledHandlePtr.IsValid()
			? EnabledHandlePtr->CreatePropertyValueWidget()
			: SNullWidget::NullWidget;
	}

	// Component owners: custom SCheckBox that writes the TSet directly via SetOverrideEnabled,
	// bypassing UE's edit chain -- so PostEditChangeChainProperty (whose SyncBEnabledFromOverrideSet
	// would revert a bEnabled write from the empty set in the same notify) never fires for toggles.
	const TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLive = WeakLiveComponent;
	const int32 OverrideIndex = CachedOverrideIndex;

	auto IsChecked = [WeakLive, OverrideIndex]() -> ECheckBoxState
	{
		const UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
		if (!Live)
		{
			return ECheckBoxState::Undetermined;
		}
		if (!Live->Properties.ImportOverrides.Overrides.IsValidIndex(OverrideIndex))
		{
			return ECheckBoxState::Undetermined;
		}

		const FName Name = Live->Properties.ImportOverrides.Overrides[OverrideIndex].GetPropertyName();
		if (Name.IsNone())
		{
			return ECheckBoxState::Undetermined;
		}
		return Live->EnabledOverrides.Contains(Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	};

	auto OnCheckStateChanged = [WeakLive, OverrideIndex](ECheckBoxState NewState)
	{
		UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
		if (!Live)
		{
			return;
		}
		if (!Live->Properties.ImportOverrides.Overrides.IsValidIndex(OverrideIndex))
		{
			return;
		}

		const FName Name = Live->Properties.ImportOverrides.Overrides[OverrideIndex].GetPropertyName();
		if (Name.IsNone())
		{
			return;
		}

		Live->Modify();
		Live->SetOverrideEnabled(Name, NewState == ECheckBoxState::Checked);
	};

	return SNew(SCheckBox)
		.IsChecked_Lambda(IsChecked)
		.OnCheckStateChanged_Lambda(OnCheckStateChanged);
}

void FPCGExPropertyOverrideEntryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Capture the entry + its well-known child handles as members. Every lambda / widget
	// created below (and in CustomizeChildren) reads through these members, so their
	// backing nodes stay reachable for the full lifetime of this customization.
	PropertyHandlePtr = PropertyHandle;
	ValueHandlePtr = PropertyHandle->GetChildHandle(TEXT("Value"));
	EnabledHandlePtr = PropertyHandle->GetChildHandle(TEXT("bEnabled"));
	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	// The reset handler walks the BP class chain DYNAMICALLY per paint (CDO state can change while
	// the detail panel is open). The asset-default tail of that chain is stable for the customization
	// lifetime and is cached here once.
	WeakLiveComponent.Reset();
	WeakOwner.Reset();
	bComponentHasArchetypeChain = false;
	CachedOverrideIndex = PropertyHandle->GetIndexInArray();
	CachedAssetDefaultValue.Reset();

	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);
	for (UObject* Outer : OuterObjects)
	{
		if (!Outer || Outer->IsTemplate())
		{
			continue;
		}

		if (!WeakOwner.IsValid())
		{
			WeakOwner = Outer;
		}

		if (UPCGExPropertyCollectionComponent* Comp = Cast<UPCGExPropertyCollectionComponent>(Outer))
		{
			// Bind the live component for EVERY non-template component (Instance-created included):
			// its EnabledOverrides TSet is the authoritative toggle store the custom checkbox writes.
			WeakLiveComponent = Comp;

			// The reset arrow only means something with a BP archetype/class chain to defer to;
			// Instance-created components have none, so it (and its asset-default cache) stays off.
			bComponentHasArchetypeChain = (Comp->CreationMethod != EComponentCreationMethod::Instance);
			if (bComponentHasArchetypeChain)
			{
				// Invalid result after this means "no asset default for this entry" -- distinct
				// from "not yet resolved" and load-bearing for TryGetResetSource.
				bool _IgnoredEnabled = false;
				PCGExPropertyOverrideEntryCustomization::TryResolveAssetDefault(
					*Comp, CachedOverrideIndex, _IgnoredEnabled, CachedAssetDefaultValue);
			}
			break;
		}
	}

	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	UserDefinedStructReinstancedHandle = UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.AddSP(
		this, &FPCGExPropertyOverrideEntryCustomization::OnUserDefinedStructReinstanced);

	// Check if this is an inline type (schema-driven, stable for the detail session)
	bool bShouldInline = false;
	if (ValueHandlePtr.IsValid())
	{
		TArray<void*> RawData;
		ValueHandlePtr->AccessRawData(RawData);
		if (!RawData.IsEmpty() && RawData[0])
		{
			FInstancedStruct* Instance = static_cast<FInstancedStruct*>(RawData[0]);
			if (Instance && Instance->IsValid())
			{
				UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(Instance->GetScriptStruct());
				if (InnerStruct)
				{
					bShouldInline = InnerStruct->HasMetaData(TEXT("PCGExInlineValue"));
				}
			}
		}
	}

	// For complex (non-inline) types, show checkbox + label in header
	// For simple (inline) types, header stays empty (everything in CustomizeChildren)
	if (!bShouldInline)
	{
		HeaderRow
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					BuildOverrideToggleWidget()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					PCGExPropertyLabelRow::Build(
						TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertyOverrideEntryCustomization::GetEntryNameText)),
						TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertyOverrideEntryCustomization::GetEntryTypeText)))
				]
			];

		// Complex types put their content on HeaderRow; the reset arrow lives on this row.
		// Inline types leave HeaderRow empty and their inner Value row inherits
		// NoResetToDefault from the FPCGExProperty subclass's Value UPROPERTY meta,
		// suppressing the arrow there directly -- so no override needed on that path.
		//
		// Always install the override here, regardless of whether we have a component.
		// In component context (BP-inherited instance), MakeArchetypeResetOverride's
		// IsVisible walks the BP class chain to detect divergence. In NON-component
		// contexts (asset-hosted entries, schema-definition rows, etc.) WeakLive is
		// null and IsVisible returns false -- which is exactly what we want, because
		// UE's default reset on the outer FPCGExPropertyOverrideEntry struct would
		// clear the FInstancedStruct shape (name=None, type=Unknown), destroying the
		// entry. The override suppresses that arrow.
		HeaderRow.OverrideResetToDefault(MakeArchetypeResetOverride());
	}
}

namespace PCGExPropertyOverrideEntryCustomization
{
	// Resolve the imported asset's authored (Value) for the entry at OverrideIndex within the
	// component's ImportOverrides.Overrides. Walks the imports tree and grabs the same-index
	// imported entry's Source->Property. Returns false when the resolve doesn't reach that
	// index (e.g. asset removed since last reconcile).
	bool TryResolveAssetDefault(
		const UPCGExPropertyCollectionComponent& Comp,
		int32 OverrideIndex,
		bool& OutEnabled,
		FInstancedStruct& OutValue)
	{
		TArray<FPCGExPropertyResolved> Resolved;
		Comp.Properties.Resolve(Resolved);

		int32 ImportSeen = 0;
		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (!Entry.OwningAsset || !Entry.Source)
			{
				continue;
			}
			if (ImportSeen == OverrideIndex)
			{
				OutEnabled = false; // Asset-default means "no override active"
				OutValue = Entry.Source->Property;
				return true;
			}
			++ImportSeen;
		}
		return false;
	}

	// (bEnabled, Value) the entry should reset to. Walks Owner->GetClass() up the BP
	// inheritance chain; the first SCS template carrying an enabled override at
	// OverrideIndex supplies (bEnabled=true, Value=A.Value). When no layer in the chain
	// has an enabled override (or the entry isn't owned by a BP-instance actor at all),
	// the imported asset's authored Property becomes the target with bEnabled=false.
	//
	// Net effect: reset always lands on the entry's *effective* value as if the instance
	// had deferred to its parent chain -- never on a stale "disabled but still stored"
	// slot. A direct parent override wins over deeper ancestors which in turn win over
	// the asset default. Asset-default is the deepest fallback and is always available
	// for entries that exist as ImportOverrides.
	//
	// CachedAssetDefault: pointer to a stable asset-default value resolved once at
	// CustomizeHeader. Non-null and valid -> use it (skips the per-paint Resolve walk).
	// Non-null and invalid -> no asset default exists for this entry (e.g. asset removed
	// since the cache was built), so the chain walk is the only signal. Null -> fall
	// back to a live Resolve walk (defensive; not expected on the normal path).
	bool TryGetResetSource(
		const UPCGExPropertyCollectionComponent& Live,
		int32 OverrideIndex,
		const FInstancedStruct* CachedAssetDefault,
		bool& OutEnabled,
		FInstancedStruct& OutValue)
	{
		if (const AActor* Owner = Live.GetOwner())
		{
			for (UClass* Cls = Owner->GetClass(); Cls; Cls = Cls->GetSuperClass())
			{
				const UPCGExPropertyCollectionComponent* Template = UPCGExPropertyCollectionComponent::FindSCSTemplateInClass(Cls, Live.GetFName());
				if (!Template || Template == &Live)
				{
					continue;
				}
				if (!Template->Properties.ImportOverrides.Overrides.IsValidIndex(OverrideIndex))
				{
					continue;
				}

				const FPCGExPropertyOverrideEntry& A = Template->Properties.ImportOverrides.Overrides[OverrideIndex];
				if (A.bEnabled)
				{
					OutEnabled = true;
					OutValue = A.Value;
					return true;
				}
			}
		}

		if (CachedAssetDefault)
		{
			if (!CachedAssetDefault->IsValid())
			{
				return false;
			}
			OutEnabled = false;
			OutValue = *CachedAssetDefault;
			return true;
		}

		return TryResolveAssetDefault(Live, OverrideIndex, OutEnabled, OutValue);
	}
}

FResetToDefaultOverride FPCGExPropertyOverrideEntryCustomization::MakeArchetypeResetOverride() const
{
	const TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLive = WeakLiveComponent;
	const TWeakPtr<IPropertyUtilities> WeakUtils = WeakPropertyUtilities;
	const int32 OverrideIndex = CachedOverrideIndex;

	// No BP chain (Instance-created / non-component): IsVisible returns false so no arrow shows, but
	// the override stays installed to suppress UE's default reset (which would blank the entry shape).
	const bool bHasChain = bComponentHasArchetypeChain;

	// Capture the cached asset default by value so the lambda stays self-contained -- the
	// pointer passed into TryGetResetSource targets this captured copy so the helper sees
	// "non-null, valid" (use cache) or "non-null, invalid" (no asset default exists)
	// rather than falling back to a live Resolve walk.
	const FInstancedStruct CachedAssetDefault = CachedAssetDefaultValue;

	auto IsVisible = FIsResetToDefaultVisible::CreateLambda(
		[WeakLive, OverrideIndex, CachedAssetDefault, bHasChain](TSharedPtr<IPropertyHandle>) -> bool
		{
			if (!bHasChain)
			{
				return false;
			}
			const UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
			if (!Live || !Live->Properties.ImportOverrides.Overrides.IsValidIndex(OverrideIndex))
			{
				return false;
			}

			bool SrcEnabled = false;
			FInstancedStruct SrcValue;
			if (!PCGExPropertyOverrideEntryCustomization::TryGetResetSource(*Live, OverrideIndex, &CachedAssetDefault, SrcEnabled, SrcValue))
			{
				return false;
			}

			// Authored = present in the TSet (the authoritative signal); bEnabled alone can hold
			// transient propagation-clobber state and must not drive the arrow.
			const FPCGExPropertyOverrideEntry& L = Live->Properties.ImportOverrides.Overrides[OverrideIndex];
			const FName EntryName = L.GetPropertyName();
			const bool bInstanceAuthored = !EntryName.IsNone() && Live->EnabledOverrides.Contains(EntryName);
			return bInstanceAuthored || L.Value != SrcValue;
		});

	auto Handler = FResetToDefaultHandler::CreateLambda(
		[WeakLive, WeakUtils, OverrideIndex, CachedAssetDefault, bHasChain](TSharedPtr<IPropertyHandle> Handle)
		{
			if (!bHasChain)
			{
				return;
			}
			UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
			if (!Live || !Live->Properties.ImportOverrides.Overrides.IsValidIndex(OverrideIndex))
			{
				return;
			}

			bool SrcEnabled = false;
			FInstancedStruct SrcValue;
			if (!PCGExPropertyOverrideEntryCustomization::TryGetResetSource(*Live, OverrideIndex, &CachedAssetDefault, SrcEnabled, SrcValue))
			{
				return;
			}

			Live->Modify();
			FPCGExPropertyOverrideEntry& L = Live->Properties.ImportOverrides.Overrides[OverrideIndex];

			// Reset = defer to the chain: un-author the instance toggle (TSet is the
			// authoritative signal) and land the stored value on the chain's effective value.
			// Writing bEnabled = SrcEnabled directly would diverge the mirror from the TSet and
			// read as instance-authored state to every TSet consumer (instance-data capture, the
			// post-CDO-edit repair walk, the export-time sync) -- the chain already surfaces the
			// parent's enabled override without the instance pinning it.
			Live->SetOverrideEnabled(L.GetPropertyName(), false);
			L.bEnabled = false; // Direct mirror write covers entries with broken identity (None name).

			// Copy-assign (not move): same-type copy preserves the destination memory pointer
			// that InnerScope aliases. Move-assign would Reset() first, invalidating the alias.
			// ReconcileImportOverrides keeps types aligned so same-type is the common case;
			// the refresh below is a defensive fallback for the type-mismatch reallocation path.
			L.Value = SrcValue;
			if (Handle.IsValid())
			{
				Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}

			if (TSharedPtr<IPropertyUtilities> Utils = WeakUtils.Pin())
			{
				Utils->RequestRefresh();
			}
		});

	return FResetToDefaultOverride::Create(IsVisible, Handler, /*bPropagateToChildren*/ true);
}

void FPCGExPropertyOverrideEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// CustomizeHeader is always called first and populates the member handles,
	// but recover gracefully if we are somehow entered cold.
	if (!ValueHandlePtr.IsValid())
	{
		ValueHandlePtr = PropertyHandle->GetChildHandle(TEXT("Value"));
	}
	if (!EnabledHandlePtr.IsValid())
	{
		EnabledHandlePtr = PropertyHandle->GetChildHandle(TEXT("bEnabled"));
	}

	if (!ValueHandlePtr.IsValid())
	{
		return;
	}

	// Access raw data to get the FInstancedStruct
	TArray<void*> RawData;
	ValueHandlePtr->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return;
	}

	FInstancedStruct* Instance = static_cast<FInstancedStruct*>(RawData[0]);
	if (!Instance || !Instance->IsValid())
	{
		return;
	}

	UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(Instance->GetScriptStruct());
	if (!InnerStruct)
	{
		return;
	}

	uint8* StructMemory = Instance->GetMutableMemory();
	if (!StructMemory)
	{
		return;
	}

	// Check if this type should be inlined
	const bool bShouldInline = InnerStruct->HasMetaData(TEXT("PCGExInlineValue"));

	// Hold the inner scope on the customization so it is guaranteed to outlive every
	// widget / lambda that will reference it below. The detail panel tears down the
	// Slate subtree before releasing its customization instances, so this member
	// chain (customization -> InnerScope -> StructMemory) gives a structural, not
	// probabilistic, lifetime guarantee.
	// Non-owning ctor is correct: StructMemory is owned by the outer FStructOnScope
	// the grid view passes into SetStructureData, which the detail panel keeps alive
	// for the session, and the inner type is pinned by the collection schema.
	InnerScope = MakeShared<FStructOnScope>(InnerStruct, StructMemory);

	// In component context, read the TSet directly (the authoritative signal -- bEnabled may
	// briefly disagree post-propagation until the PostEdit safety-net resyncs it). Other owners
	// fall back to the property handle.
	TWeakPtr<IPropertyHandle> WeakEnabledHandle = EnabledHandlePtr;
	const TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLiveForEnabled = WeakLiveComponent;
	const int32 EnabledQueryIndex = CachedOverrideIndex;
	TAttribute<bool> IsEnabledAttr = TAttribute<bool>::Create([WeakLiveForEnabled, EnabledQueryIndex, WeakEnabledHandle]()
	{
		if (const UPCGExPropertyCollectionComponent* Live = WeakLiveForEnabled.Get())
		{
			if (Live->Properties.ImportOverrides.Overrides.IsValidIndex(EnabledQueryIndex))
			{
				const FName Name = Live->Properties.ImportOverrides.Overrides[EnabledQueryIndex].GetPropertyName();
				return !Name.IsNone() && Live->EnabledOverrides.Contains(Name);
			}
			return false;
		}
		if (TSharedPtr<IPropertyHandle> Handle = WeakEnabledHandle.Pin())
		{
			bool bEnabled = false;
			Handle->GetValue(bEnabled);
			return bEnabled;
		}
		return true;
	});

	if (bShouldInline)
	{
		const TSharedRef<SWidget> NameContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				BuildOverrideToggleWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				PCGExPropertyLabelRow::Build(
					TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertyOverrideEntryCustomization::GetEntryNameText)),
					TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertyOverrideEntryCustomization::GetEntryTypeText)),
					/*bShowSeparator=*/false)
			];

		IDetailPropertyRow* InnerRow = FPCGExInlineWidgetRegistry::AddCompactValueRow(
			ChildBuilder, InnerScope.ToSharedRef(), InnerStruct, NameContent, IsEnabledAttr);
		if (InnerRow)
		{
			// Reset arrow walks the BP class chain -- only for BP-inherited owners. Instance-created
			// components have none; their inner Value row keeps its NoResetToDefault meta.
			if (bComponentHasArchetypeChain)
			{
				InnerRow->OverrideResetToDefault(MakeArchetypeResetOverride());
			}
			if (WeakOwner.IsValid())
			{
				PCGExEditorCustomizationUtils::HookOwnerChangeOnHandleChanged(InnerRow->GetPropertyHandle(), WeakOwner);
			}
		}
	}
	else if (InnerStruct == FPCGExProperty_Struct::StaticStruct())
	{
		// Bypass the FInstancedStruct wrapper row: walk Value's inner struct as siblings of the
		// override header. Type is schema-pinned in override context so no combo is needed.
		FPCGExProperty_Struct* StructProp = reinterpret_cast<FPCGExProperty_Struct*>(StructMemory);
		const UScriptStruct* InnerInnerStruct = StructProp->Value.GetScriptStruct();
		uint8* InnerInnerMemory = StructProp->Value.GetMutableMemory();
		if (InnerInnerStruct && InnerInnerMemory)
		{
			NestedScope = MakeShared<FStructOnScope>(InnerInnerStruct, InnerInnerMemory);
			for (TFieldIterator<FProperty> It(InnerInnerStruct); It; ++It)
			{
				// CPF_Edit filter: AddExternalStructureProperty returns null on non-editable
				// fields and the dereference would crash. Arbitrary user structs may include
				// VisibleAnywhere/no-spec fields, unlike the FPCGExProperty_* set.
				if (!It->HasAnyPropertyFlags(CPF_Edit))
				{
					continue;
				}
				if (IDetailPropertyRow* RowPtr = ChildBuilder.AddExternalStructureProperty(NestedScope.ToSharedRef(), It->GetFName()))
				{
					RowPtr->IsEnabled(IsEnabledAttr);
				}
			}
		}
		else
		{
			// No struct type picked yet (or override is stale) -- show the default FInstancedStruct
			// row so the user can pick a type from the combo.
			FPCGExInlineWidgetRegistry::AddComplexValueRows(ChildBuilder, InnerScope.ToSharedRef(), InnerStruct, IsEnabledAttr);
		}
	}
	else
	{
		FPCGExInlineWidgetRegistry::AddComplexValueRows(
			ChildBuilder, InnerScope.ToSharedRef(), InnerStruct, IsEnabledAttr, WeakOwner);
	}
}
