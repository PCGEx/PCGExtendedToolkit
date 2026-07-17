// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertySchemaCollectionCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExPropertySchemaAsset.h"
#include "PropertyHandle.h"
#include "Details/PCGExEditorCustomizationUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/TransactionObjectEvent.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


namespace PCGExPropertySchemaCollectionCustomization
{
	// Resolve the reset target for a local schema entry by walking Owner's BP class chain
	// from Live up to the topmost ancestor that authored this component, returning the
	// closest ancestor whose Schemas[SchemaIndex].Property differs from Live's current value.
	//
	// Mental model: "reset to what my parent class set this to." A child BP that overrides
	// the schema entry naturally wins over a more distant ancestor because the walk hits it
	// first; a child that didn't override falls through to the layer that did. Same shape as
	// FPCGExPropertyOverrideEntryCustomization's BP-chain walk for ImportOverrides --
	// keeping the two reset behaviours symmetric.
	//
	// Returns false (no reset target) when:
	// - Live has no owning actor (data-asset / Tuple / etc -- not a BP-instance context).
	// - Live isn't actually in a BP class chain (native-only / template / Instance-created).
	// - No ancestor diverges from Live (entry is already at parent default).
	bool TryGetLocalSchemaResetSource(
		const UPCGExPropertyCollectionComponent& Live,
		int32 SchemaIndex,
		FInstancedStruct& OutValue)
	{
		if (!Live.Properties.Schemas.IsValidIndex(SchemaIndex))
		{
			return false;
		}
		const FInstancedStruct& LiveProp = Live.Properties.Schemas[SchemaIndex].Property;

		const AActor* Owner = Live.GetOwner();
		if (!Owner)
		{
			return false;
		}

		for (UClass* Cls = Owner->GetClass(); Cls; Cls = Cls->GetSuperClass())
		{
			const UPCGExPropertyCollectionComponent* Template =
				UPCGExPropertyCollectionComponent::FindSCSTemplateInClass(Cls, Live.GetFName());
			if (!Template || Template == &Live)
			{
				continue;
			}
			if (!Template->Properties.Schemas.IsValidIndex(SchemaIndex))
			{
				continue;
			}

			const FInstancedStruct& AncestorProp = Template->Properties.Schemas[SchemaIndex].Property;
			if (AncestorProp != LiveProp)
			{
				OutValue = AncestorProp;
				return true;
			}
		}
		return false;
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExPropertySchemaCollectionCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertySchemaCollectionCustomization());
}

FPCGExPropertySchemaCollectionCustomization::~FPCGExPropertySchemaCollectionCustomization()
{
	UnsubscribeImportedAssets();
	if (ObjectTransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(ObjectTransactedHandle);
		ObjectTransactedHandle.Reset();
	}
}

void FPCGExPropertySchemaCollectionCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Store utilities and handles
	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	PropertyHandlePtr = PropertyHandle;

	// Detect instance mode: the outer object is a UPCGExPropertyCollectionComponent that was
	// inherited from a Blueprint class (SCS/UCS/Native), not added directly to the actor instance.
	// Components added per-instance (CreationMethod == Instance) own their own schema and should
	// retain full editing; only inherited components should lock the schema and redirect the user
	// to the Blueprint. Other users of FPCGExPropertySchemaCollection (Tuple nodes, data assets,
	// etc.) won't cast to the component type, so they are unaffected.
	bIsInstanceMode = false;
	WeakLiveComponent.Reset();
	WeakHostObject.Reset();
	if (TSharedPtr<IPropertyUtilities> Utils = WeakPropertyUtilities.Pin())
	{
		for (const TWeakObjectPtr<UObject>& ObjPtr : Utils->GetSelectedObjects())
		{
			UObject* Obj = ObjPtr.Get();
			if (!Obj)
			{
				continue;
			}
			if (!WeakHostObject.IsValid())
			{
				WeakHostObject = Obj;
			}
			if (UPCGExPropertyCollectionComponent* Comp = Cast<UPCGExPropertyCollectionComponent>(Obj))
			{
				if (!Comp->IsTemplate() && Comp->CreationMethod != EComponentCreationMethod::Instance)
				{
					bIsInstanceMode = true;
					WeakLiveComponent = Comp;
					// No archetype capture here: ApplyLocalSchemaResetOverride walks the BP
					// class chain dynamically at reset time, so a stale-at-customize-time
					// archetype reference would just trail the live BP CDO state.
					break;
				}
			}
		}
	}

	// Bind once. UE undo can reallocate inner FInstancedStruct memory in ImportOverrides.Overrides,
	// dangling the FStructOnScope aliases held by per-entry customizations. ForceRefresh from
	// the handler rebuilds them.
	if (!ObjectTransactedHandle.IsValid())
	{
		ObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(
			this, &FPCGExPropertySchemaCollectionCustomization::OnObjectTransacted);
	}

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertySchemaCollectionCustomization::GetHeaderText)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
}

FText FPCGExPropertySchemaCollectionCustomization::GetHeaderText() const
{
	// Called per Slate redraw -- avoid the full Resolve walk by serving a cache populated by
	// CustomizeChildren. First call (before the section is expanded) falls through to compute.
	if (CachedResolvedCount < 0)
	{
		CachedResolvedCount = 0;
		if (TSharedPtr<IPropertyHandle> Handle = PropertyHandlePtr.Pin())
		{
			TArray<void*> RawData;
			Handle->AccessRawData(RawData);
			if (!RawData.IsEmpty() && RawData[0])
			{
				TArray<FPCGExPropertyResolved> Resolved;
				static_cast<const FPCGExPropertySchemaCollection*>(RawData[0])->Resolve(Resolved);
				CachedResolvedCount = Resolved.Num();
			}
		}
	}

	return FText::FromString(FString::Printf(TEXT("%d %s"), CachedResolvedCount, CachedResolvedCount == 1 ? TEXT("property") : TEXT("properties")));
}

void FPCGExPropertySchemaCollectionCustomization::OnSchemasArrayChanged()
{
	TSharedPtr<IPropertyHandle> PropertyHandle = PropertyHandlePtr.Pin();
	if (!PropertyHandle.IsValid())
	{
		return;
	}

	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return;
	}

	FPCGExPropertySchemaCollection* Collection = static_cast<FPCGExPropertySchemaCollection*>(RawData[0]);
	for (FPCGExPropertySchema& Schema : Collection->Schemas)
	{
		Schema.SyncPropertyName();
	}
	ReconcileImportOverridesPreservingLiveRows(*Collection);

	CachedResolvedCount = -1;
	// Deferred rebuild. RequestRefresh only re-lays-out existing rows (never re-runs this customization
	// -> stale list); a synchronous ForceRefresh from inside a change notification would re-enter.
	if (TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
	{
		PropertyUtilities->RequestForceRefresh();
	}
}

void FPCGExPropertySchemaCollectionCustomization::OnImportedSchemasArrayChanged()
{
	TSharedPtr<IPropertyHandle> PropertyHandle = PropertyHandlePtr.Pin();
	if (!PropertyHandle.IsValid())
	{
		return;
	}

	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	if (!RawData.IsEmpty() && RawData[0])
	{
		ReconcileImportOverridesPreservingLiveRows(*static_cast<FPCGExPropertySchemaCollection*>(RawData[0]));
	}

	CachedResolvedCount = -1;
	// Deferred rebuild -- see OnSchemasArrayChanged.
	if (TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
	{
		PropertyUtilities->RequestForceRefresh();
	}
}

void FPCGExPropertySchemaCollectionCustomization::OnImportedAssetChanged(UPCGExPropertySchemaAsset* /*ChangedAsset*/)
{
	ReconcileAndNotify();
}

void FPCGExPropertySchemaCollectionCustomization::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	// Global delegate -- filter by host identity, and only act on undo/redo (the path that
	// bypasses every other refresh hook).
	if (Object != WeakHostObject.Get() || Event.GetEventType() != ETransactionObjectEventType::UndoRedo)
	{
		return;
	}
	if (TSharedPtr<IPropertyUtilities> Utils = WeakPropertyUtilities.Pin())
	{
		Utils->ForceRefresh();
	}
}

void FPCGExPropertySchemaCollectionCustomization::ReconcileAndNotify()
{
	TSharedPtr<IPropertyHandle> PropertyHandle = PropertyHandlePtr.Pin();
	if (!PropertyHandle.IsValid())
	{
		return;
	}

	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	if (!RawData.IsEmpty() && RawData[0])
	{
		ReconcileImportOverridesPreservingLiveRows(*static_cast<FPCGExPropertySchemaCollection*>(RawData[0]));
	}

	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet); // mark host dirty to re-serialise

	// Fires from an imported asset's broadcast -> deferred rebuild (synchronous ForceRefresh would
	// re-enter mid-broadcast; RequestRefresh wouldn't re-run this customization -> stale).
	CachedResolvedCount = -1;
	if (TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
	{
		PropertyUtilities->RequestForceRefresh();
	}
}

void FPCGExPropertySchemaCollectionCustomization::ReconcileImportOverridesPreservingLiveRows(FPCGExPropertySchemaCollection& Collection)
{
	// Mounted rows alias each Overrides[i].Value's inner memory via non-owning FStructOnScope and repaint
	// from it, so reconciling in place (SyncToSchema's slow path frees it) is a use-after-free until the
	// deferred rebuild tears them down. Park the entries -- a TArray move leaves their inner allocations
	// put -- and reconcile a copy instead. Stays in the caller's transaction, so undo is unaffected.
	TArray<FPCGExPropertyOverrideEntry>& Parked = ParkedOverrideBuffers.AddDefaulted_GetRef();
	Parked = MoveTemp(Collection.ImportOverrides.Overrides);
	Collection.ImportOverrides.Overrides = Parked; // copy back for the reconcile to reallocate
	Collection.ReconcileImportOverrides();
}

void FPCGExPropertySchemaCollectionCustomization::EmitSectionHeader(IDetailChildrenBuilder& ChildBuilder, const FString& Title) const
{
	// Left padding of -16 preserves the ZoneGraph-style offset that aligns section labels
	// with the property tree's gutter rather than the value column.
	ChildBuilder.AddCustomRow(FText::FromString(Title))
	            .WholeRowContent()
	[
		SNew(SBox)
		.Padding(FMargin(-16, 4, 0, 4))
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Title.ToUpper()))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
		]
	];
}

void FPCGExPropertySchemaCollectionCustomization::EmitImportSections(IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> CollectionHandle, const TArray<FPCGExPropertyResolved>& Resolved)
{
	TSharedPtr<IPropertyHandle> ImportOverridesHandle = CollectionHandle->GetChildHandle(TEXT("ImportOverrides"));
	if (!ImportOverridesHandle.IsValid())
	{
		return;
	}
	TSharedPtr<IPropertyHandle> OverridesArrayHandle = ImportOverridesHandle->GetChildHandle(TEXT("Overrides"));
	if (!OverridesArrayHandle.IsValid())
	{
		return;
	}

	// Force the detail panel to build property nodes for the ImportOverrides subtree by
	// adding the parent as a collapsed row. Without this, per-instance UPROPERTY delta
	// tracking can fail for writes through the entry handles below -- UE only builds the
	// node tree for branches reached via AddProperty, and writes through unbuilt nodes
	// silently revert on instances (CDO/archetype propagation re-reads stale memory).
	ChildBuilder.AddProperty(ImportOverridesHandle.ToSharedRef()).Visibility(EVisibility::Collapsed);

	int32 ImportIndex = 0;
	UPCGExPropertySchemaAsset* CurrentSection = nullptr;
	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		if (!Entry.OwningAsset)
		{
			continue;
		}

		if (Entry.OwningAsset != CurrentSection)
		{
			CurrentSection = Entry.OwningAsset;
			EmitSectionHeader(ChildBuilder, CurrentSection->GetName());
		}

		TSharedPtr<IPropertyHandle> EntryHandle = OverridesArrayHandle->GetChildHandle(static_cast<uint32>(ImportIndex));
		if (EntryHandle.IsValid())
		{
			IDetailPropertyRow& Row = ChildBuilder.AddProperty(EntryHandle.ToSharedRef());
			Row.ShowPropertyButtons(false);
			// Reset-to-archetype on import overrides is handled by FPCGExPropertyOverrideEntryCustomization
			// (the type customization that owns the actual visible row). Applying it here on the
			// outer entry row in addition would trigger UE's duplicate-handler ensure.
		}
		++ImportIndex;
	}
}

void FPCGExPropertySchemaCollectionCustomization::SubscribeToImportedAssets()
{
	UnsubscribeImportedAssets();

	// Walk the full ImportedSchemas tree (BFS, cycle-safe) and subscribe to every reachable
	// asset -- including ones that don't contribute surviving entries to Resolved (empty
	// imports, fully name-shadowed). Otherwise their later additions broadcast into the void.
	TSharedPtr<IPropertyHandle> Handle = PropertyHandlePtr.Pin();
	if (!Handle.IsValid())
	{
		return;
	}
	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return;
	}
	const FPCGExPropertySchemaCollection* RootCollection = static_cast<const FPCGExPropertySchemaCollection*>(RawData[0]);

	TSet<UPCGExPropertySchemaAsset*> Unique;
	TArray<const FPCGExPropertySchemaCollection*> Frontier;
	Frontier.Add(RootCollection);
	while (!Frontier.IsEmpty())
	{
		const FPCGExPropertySchemaCollection* Current = Frontier.Pop(EAllowShrinking::No);
		for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Current->ImportedSchemas)
		{
			UPCGExPropertySchemaAsset* Asset = AssetPtr.Get();
			if (!Asset)
			{
				continue;
			}
			bool bAlreadySeen = false;
			Unique.Add(Asset, &bAlreadySeen);
			if (!bAlreadySeen)
			{
				Frontier.Add(&Asset->Collection);
			}
		}
	}

	for (UPCGExPropertySchemaAsset* Asset : Unique)
	{
		FDelegateHandle DelHandle = Asset->OnSchemaAssetChanged.AddSP(
			this, &FPCGExPropertySchemaCollectionCustomization::OnImportedAssetChanged);
		AssetDelegateHandles.Emplace(TWeakObjectPtr<UPCGExPropertySchemaAsset>(Asset), DelHandle);
	}
}

void FPCGExPropertySchemaCollectionCustomization::UnsubscribeImportedAssets()
{
	for (TPair<TWeakObjectPtr<UPCGExPropertySchemaAsset>, FDelegateHandle>& Pair : AssetDelegateHandles)
	{
		if (UPCGExPropertySchemaAsset* Asset = Pair.Key.Get())
		{
			Asset->OnSchemaAssetChanged.Remove(Pair.Value);
		}
	}
	AssetDelegateHandles.Reset();
}

void FPCGExPropertySchemaCollectionCustomization::ApplyLocalSchemaResetOverride(IDetailPropertyRow& Row, int32 SchemaIndex)
{
	// IsVisible / Handler use captured weak pointers, not Handle->GetOuterObjects.
	// External-structure handles (the inline rendering path uses one) don't expose the owning
	// component via outers, so the old approach silently hid the arrow for inline rows.
	// FPCGExProperty subclasses carry meta=(NoResetToDefault) on their inner Value field
	// to suppress UE's broken per-field arrow, so the arrow on this row is the only one users
	// see -- one click resets the whole entry to the BP-chain parent's value.
	//
	// Reset target is resolved dynamically by walking Owner's BP class chain (see
	// TryGetLocalSchemaResetSource). This mirrors the import-override reset behaviour
	// (MakeArchetypeResetOverride) and is the correct semantics when a child BP overrides a
	// schema entry authored on a parent BP -- the previous "direct archetype only" behaviour
	// missed the chain entirely for components inherited from parent classes.
	const TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakLive = WeakLiveComponent;

	auto IsVisible = FIsResetToDefaultVisible::CreateLambda(
		[WeakLive, SchemaIndex](TSharedPtr<IPropertyHandle>) -> bool
		{
			const UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
			if (!Live)
			{
				return false;
			}
			FInstancedStruct Ignored;
			return PCGExPropertySchemaCollectionCustomization::TryGetLocalSchemaResetSource(*Live, SchemaIndex, Ignored);
		});

	auto Handler = FResetToDefaultHandler::CreateLambda(
		[WeakLive, SchemaIndex](TSharedPtr<IPropertyHandle> Handle)
		{
			UPCGExPropertyCollectionComponent* Live = WeakLive.Get();
			if (!Live || !Live->Properties.Schemas.IsValidIndex(SchemaIndex))
			{
				return;
			}

			FInstancedStruct ResetValue;
			if (!PCGExPropertySchemaCollectionCustomization::TryGetLocalSchemaResetSource(*Live, SchemaIndex, ResetValue))
			{
				return;
			}

			Live->Modify();
			Live->Properties.Schemas[SchemaIndex].Property = ResetValue;
			Live->Properties.Schemas[SchemaIndex].SyncPropertyName();
			if (Handle.IsValid())
			{
				Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}
		});

	Row.OverrideResetToDefault(FResetToDefaultOverride::Create(IsVisible, Handler, /*bPropagateToChildren*/ true));
}


bool FPCGExPropertySchemaCollectionCustomization::TryRenderFlatInline(
	IDetailChildrenBuilder& ChildBuilder,
	TSharedRef<IPropertyHandle> ElementHandle)
{
	// One raw-data access for the whole schema -- derive Property, Name, and type from it
	TArray<void*> ElemRaw;
	ElementHandle->AccessRawData(ElemRaw);
	if (ElemRaw.IsEmpty() || !ElemRaw[0])
	{
		return false;
	}

	FPCGExPropertySchema* Schema = static_cast<FPCGExPropertySchema*>(ElemRaw[0]);
	if (!Schema->Property.IsValid())
	{
		return false;
	}

	UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(Schema->Property.GetScriptStruct());
	if (!InnerStruct || !InnerStruct->HasMetaData(TEXT("PCGExInlineValue")))
	{
		return false;
	}

	uint8* StructMemory = Schema->Property.GetMutableMemory();
	if (!StructMemory)
	{
		return false;
	}

	FString TypeName;
	if (const FPCGExProperty* Prop = Schema->GetProperty())
	{
		TypeName = Prop->GetTypeName().ToString();
	}
	const FText LabelText = FText::FromString(
		FString::Printf(TEXT("%s (%s)"), *Schema->Name.ToString(), *TypeName));

	// Non-owning scope: memory is owned by the live component instance
	TSharedPtr<FStructOnScope> Scope = MakeShared<FStructOnScope>(InnerStruct, StructMemory);
	InstanceScopes.Add(Scope);

	const TSharedRef<SWidget> NameContent = SNew(STextBlock)
		.Text(LabelText)
		.Font(IDetailLayoutBuilder::GetDetailFont());

	IDetailPropertyRow* Row = FPCGExInlineWidgetRegistry::AddCompactValueRow(
		ChildBuilder, Scope.ToSharedRef(), InnerStruct, NameContent);
	if (!Row)
	{
		return false;
	}

	if (bIsInstanceMode)
	{
		ApplyLocalSchemaResetOverride(*Row, ElementHandle->GetIndexInArray());
		PCGExEditorCustomizationUtils::HookOwnerChangeOnHandleChanged(Row->GetPropertyHandle(), WeakLiveComponent);
	}
	return true;
}

void FPCGExPropertySchemaCollectionCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> SchemasArrayHandle = PropertyHandle->GetChildHandle(TEXT("Schemas"));
	if (!SchemasArrayHandle.IsValid())
	{
		return;
	}

	SchemasArrayHandlePtr = SchemasArrayHandle;

	if (bIsInstanceMode)
	{
		// Schema structure is locked -- only values are editable. ReadOnlySchema on the array
		// handle propagates to children so the fallback path (complex types, delegated to
		// FPCGExPropertySchemaCustomization) enters its value-only rendering automatically.
		SchemasArrayHandle->SetInstanceMetaData(FName(TEXT("ReadOnlySchema")), TEXT("true"));

		// Reset scopes from the previous layout pass. The detail panel tears down the Slate
		// subtree before calling CustomizeChildren again, so clearing here is safe.
		InstanceScopes.Reset();

		uint32 NumElements = 0;
		SchemasArrayHandle->GetNumChildren(NumElements);

		for (uint32 i = 0; i < NumElements; ++i)
		{
			TSharedPtr<IPropertyHandle> ElementHandle = SchemasArrayHandle->GetChildHandle(i);
			if (!ElementHandle.IsValid())
			{
				continue;
			}

			if (!TryRenderFlatInline(ChildBuilder, ElementHandle.ToSharedRef()))
			{
				// Complex or unknown type: delegate to FPCGExPropertySchemaCustomization,
				// which sees ReadOnlySchema on the parent handle and renders value-only.
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
				Row.ShowPropertyButtons(false);
				ApplyLocalSchemaResetOverride(Row, ElementHandle->GetIndexInArray());
			}
		}
	}
	else
	{
		// Array-shape only -- per-element value edits are handled by inner customizations and
		// the host object's PostEditChangeProperty. SetOnChildPropertyValueChanged is avoided:
		// it propagates through inner FInstancedStruct value edits and would tear down the
		// active widget mid-drag.
		SchemasArrayHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCollectionCustomization::OnSchemasArrayChanged));

		ChildBuilder.AddProperty(SchemasArrayHandle.ToSharedRef());
	}

	// Resolve once and share with EmitImportSections, SubscribeToImportedAssets, and the header
	// count cache. Reconcile only on shape divergence (e.g. CDO gained an import since this
	// instance was saved) -- the no-op write would otherwise dirty unedited instances on inspection.
	TArray<FPCGExPropertyResolved> Resolved;
	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	FPCGExPropertySchemaCollection* Collection = (RawData.IsEmpty() || !RawData[0])
		? nullptr
		: static_cast<FPCGExPropertySchemaCollection*>(RawData[0]);
	if (Collection)
	{
		Collection->Resolve(Resolved);

		// Asset shape may have drifted since this collection was last reconciled. Asset-side
		// PostLoad handles the "stale-on-load" case (UPCGExPropertySchemaAsset::PostLoad runs
		// SyncAllSchemas + ReconcileImportOverrides), so this check primarily covers in-memory
		// drift: an imported asset changed between this collection's load and now, but this
		// customization wasn't open to catch the OnSchemaAssetChanged broadcast. Also covers
		// non-asset hosts (Tuple node settings, level-instance component collections, etc.)
		// where there's no PostLoad self-heal. Conditional rather than unconditional because
		// an unconditional Reconcile would dirty unedited instances on every inspection.
		//
		// Drift comes in two shapes:
		// 1. Count drift: imported assets gained/lost entries since last reconcile.
		// 2. Type drift: imported assets retyped an entry (same Name/HeaderId, different
		//    UScriptStruct). This collection's cached Overrides[i].Value still carries the
		//    old type, so the OverrideEntry customization would render with stale type info
		//    even though the resolved Source has the new type. Caught by per-entry type
		//    comparison below; SyncToSchema's "type changed" branch rebuilds Value with the
		//    schema default while preserving bEnabled.
		int32 ExpectedImportCount = 0;
		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (Entry.OwningAsset)
			{
				++ExpectedImportCount;
			}
		}
		bool bNeedsReconcile = (Collection->ImportOverrides.Overrides.Num() != ExpectedImportCount);

		if (!bNeedsReconcile)
		{
			int32 ImportIndex = 0;
			for (const FPCGExPropertyResolved& Entry : Resolved)
			{
				if (!Entry.OwningAsset || !Entry.Source)
				{
					continue;
				}
				if (Collection->ImportOverrides.Overrides.IsValidIndex(ImportIndex))
				{
					const UScriptStruct* ResolvedType = Entry.Source->Property.GetScriptStruct();
					const UScriptStruct* OverrideType = Collection->ImportOverrides.Overrides[ImportIndex].Value.GetScriptStruct();
					if (ResolvedType != OverrideType)
					{
						bNeedsReconcile = true;
						break;
					}
				}
				++ImportIndex;
			}
		}

		if (bNeedsReconcile)
		{
			// Reconcile reallocates ImportOverrides.Overrides, so the OverrideValue pointers
			// cached in Resolved entries are now stale -- rebuild Resolved against the new state.
			Collection->ReconcileImportOverrides(Resolved);
			Collection->Resolve(Resolved);

			// Reconcile grew Overrides via raw pointer, so the new entries have no child handles in
			// this already-built node tree and EmitImportSections drops them this pass. RequestRefresh
			// is unreliable mid layout-build (see ReconcileAndNotify); a deferred ForceRefresh rebuilds
			// after this build unwinds and re-emits every row. Converges: the rebuilt pass finds no drift.
			if (TSharedPtr<IPropertyUtilities> Utils = WeakPropertyUtilities.Pin())
			{
				// Bound to Utils itself: drained by Utils after the build unwinds, no-ops if it's gone.
				Utils->EnqueueDeferredAction(FSimpleDelegate::CreateSP(Utils.ToSharedRef(), &IPropertyUtilities::ForceRefresh));
			}
		}

		EmitImportSections(ChildBuilder, PropertyHandle, Resolved);
	}

	if (!bIsInstanceMode)
	{
		// ImportedSchemas array editor (managing the imports list itself) -- structural,
		// not exposed in instance mode where the schema shape is locked.
		if (TSharedPtr<IPropertyHandle> ImportedSchemasHandle = PropertyHandle->GetChildHandle(TEXT("ImportedSchemas")))
		{
			ImportedSchemasHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCollectionCustomization::OnImportedSchemasArrayChanged));
			ChildBuilder.AddProperty(ImportedSchemasHandle.ToSharedRef());
		}
	}

	if (Collection)
	{
		SubscribeToImportedAssets();
		CachedResolvedCount = Resolved.Num();
	}
}
