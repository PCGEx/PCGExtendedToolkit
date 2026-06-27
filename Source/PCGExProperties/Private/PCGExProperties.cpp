// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExProperties.h"
#include "PCGExLog.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
#include "PCGExPropertyTypes.h"

#if WITH_EDITOR
void FPCGExPropertiesModule::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
	// No special editor registration needed for properties module
	IPCGExModuleInterface::RegisterToEditor(InStyle);
}
#endif

PCGEX_IMPLEMENT_MODULE(FPCGExPropertiesModule, PCGExProperties)

#pragma region FPCGExPropertySchema

FPCGExPropertySchema::FPCGExPropertySchema()
{
#if WITH_EDITOR
	Property.InitializeAs<FPCGExProperty_Float>();
#endif
}

#pragma endregion

#pragma region FPCGExPropertySchemaCollection

namespace PCGExPropertySchemaResolve
{
	// Depth-first walk: locals first, then ImportedSchemas in array order.
	// Seen / Visited are accumulated across the whole walk to enforce first-wins dedup
	// (by Name) and cycle detection (by asset pointer). Override layers apply only to
	// imported entries -- locals are edited in-place and never read overrides.
	static void Walk(
		const FPCGExPropertySchemaCollection& Collection,
		UPCGExPropertySchemaAsset* OwningAsset,
		TConstArrayView<const FPCGExPropertyOverrides*> OverrideChain,
		TArray<FPCGExPropertyResolved>& Out,
		TSet<FName>& Seen,
		TSet<const UPCGExPropertySchemaAsset*>& Visited)
	{
		for (int32 i = 0; i < Collection.Schemas.Num(); ++i)
		{
			const FPCGExPropertySchema& Schema = Collection.Schemas[i];
			if (!Schema.IsValid())
			{
				continue;
			}

			bool bAlreadySeen = false;
			Seen.Add(Schema.Name, &bAlreadySeen);
			if (bAlreadySeen)
			{
				continue;
			}

			const FInstancedStruct* Override = nullptr;
			if (OwningAsset)
			{
				for (const FPCGExPropertyOverrides* Layer : OverrideChain)
				{
					if (!Layer)
					{
						continue;
					}
					if (const FInstancedStruct* Found = Layer->GetOverride(Schema.Name))
					{
						Override = Found;
						break;
					}
				}
			}

			Out.Emplace(&Schema, OwningAsset, i, Override);
		}

		for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Collection.ImportedSchemas)
		{
			UPCGExPropertySchemaAsset* Asset = AssetPtr.Get();
			if (!Asset)
			{
				continue;
			}

			bool bAlreadyVisited = false;
			Visited.Add(Asset, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				UE_LOG(LogPCGEx, Warning,
				       TEXT("PCGExPropertySchemaCollection: cyclic or duplicate schema asset import skipped: %s"),
				       *Asset->GetPathName());
				continue;
			}

			Walk(Asset->Collection, Asset, OverrideChain, Out, Seen, Visited);
		}
	}

	// FindByName equivalent of Walk -- early-exits on the first matching name without allocating
	// the full resolved list. Visited prevents infinite recursion through cyclic imports.
	static const FPCGExPropertySchema* Find(
		const FPCGExPropertySchemaCollection& Collection,
		FName PropertyName,
		TSet<const UPCGExPropertySchemaAsset*>& Visited)
	{
		for (const FPCGExPropertySchema& Schema : Collection.Schemas)
		{
			if (Schema.Name == PropertyName)
			{
				return &Schema;
			}
		}

		for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Collection.ImportedSchemas)
		{
			UPCGExPropertySchemaAsset* Asset = AssetPtr.Get();
			if (!Asset)
			{
				continue;
			}

			bool bAlreadyVisited = false;
			Visited.Add(Asset, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				continue;
			}

			if (const FPCGExPropertySchema* Found = Find(Asset->Collection, PropertyName, Visited))
			{
				return Found;
			}
		}

		return nullptr;
	}
}

void FPCGExPropertySchemaCollection::Resolve(TArray<FPCGExPropertyResolved>& Out, TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain, bool bIncludeOwnOverrides) const
{
	Out.Reset();

	if (ImportedSchemas.IsEmpty())
	{
		Out.Reserve(Schemas.Num());
		for (int32 i = 0; i < Schemas.Num(); ++i)
		{
			const FPCGExPropertySchema& Schema = Schemas[i];
			if (Schema.IsValid())
			{
				Out.Emplace(&Schema, nullptr, i);
			}
		}
		return;
	}

	// This collection's own ImportOverrides leads the chain when bIncludeOwnOverrides is true;
	// fallback layers come after. When false, only the fallback layers participate -- callers use
	// this to extract the "no instance overrides" view (asset/CDO defaults only).
	TArray<const FPCGExPropertyOverrides*, TInlineAllocator<4>> Chain;
	Chain.Reserve((bIncludeOwnOverrides ? 1 : 0) + FallbackChain.Num());
	if (bIncludeOwnOverrides)
	{
		Chain.Add(&ImportOverrides);
	}
	for (const FPCGExPropertyOverrides* Layer : FallbackChain)
	{
		Chain.Add(Layer);
	}

	TSet<FName> Seen;
	TSet<const UPCGExPropertySchemaAsset*> Visited;
	PCGExPropertySchemaResolve::Walk(*this, nullptr, MakeArrayView(Chain), Out, Seen, Visited);
}

const FPCGExPropertySchema* FPCGExPropertySchemaCollection::FindByName(FName PropertyName) const
{
	if (PropertyName.IsNone())
	{
		return nullptr;
	}

	if (ImportedSchemas.IsEmpty())
	{
		for (const FPCGExPropertySchema& Schema : Schemas)
		{
			if (Schema.Name == PropertyName)
			{
				return &Schema;
			}
		}
		return nullptr;
	}

	TSet<const UPCGExPropertySchemaAsset*> Visited;
	return PCGExPropertySchemaResolve::Find(*this, PropertyName, Visited);
}

const FInstancedStruct* FPCGExPropertySchemaCollection::GetPropertyByName(FName PropertyName) const
{
	if (PropertyName.IsNone())
	{
		return nullptr;
	}

	for (const FPCGExPropertySchema& Schema : Schemas)
	{
		if (Schema.Name == PropertyName)
		{
			return &Schema.Property;
		}
	}

	if (const FInstancedStruct* Override = ImportOverrides.GetOverride(PropertyName))
	{
		return Override;
	}

	// Imports-only walk -- locals already checked. Each asset's FindByName handles its
	// own subtree cycles; sibling-cycle duplication is harmless (bounded by Visited).
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : ImportedSchemas)
	{
		if (const UPCGExPropertySchemaAsset* Asset = AssetPtr.Get())
		{
			if (const FPCGExPropertySchema* Schema = Asset->Collection.FindByName(PropertyName))
			{
				return &Schema->Property;
			}
		}
	}
	return nullptr;
}

FPCGExPropertySchema* FPCGExPropertySchemaCollection::FindByNameMutable(FName PropertyName)
{
	if (PropertyName.IsNone())
	{
		return nullptr;
	}
	for (FPCGExPropertySchema& Schema : Schemas)
	{
		if (Schema.Name == PropertyName)
		{
			return &Schema;
		}
	}
	return nullptr;
}

TArray<FInstancedStruct> FPCGExPropertySchemaCollection::BuildSchema(TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain, bool bIncludeOwnOverrides) const
{
	TArray<FPCGExPropertyResolved> Resolved;
	Resolve(Resolved, FallbackChain, bIncludeOwnOverrides);

	TArray<FInstancedStruct> Result;
	Result.Reserve(Resolved.Num());
	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		Result.Add(Entry.GetEffectiveProperty());
	}
	return Result;
}

bool FPCGExPropertySchemaCollection::ValidateUniqueNames(TArray<FName>& OutDuplicates) const
{
	OutDuplicates.Empty();
	TSet<FName> Seen;

	for (const FPCGExPropertySchema& Schema : Schemas)
	{
		if (Schema.Name.IsNone())
		{
			continue;
		}

		bool bAlreadyInSet = false;
		Seen.Add(Schema.Name, &bAlreadyInSet);

		if (bAlreadyInSet)
		{
			OutDuplicates.AddUnique(Schema.Name);
		}
	}

	return OutDuplicates.IsEmpty();
}

void FPCGExPropertySchemaCollection::SyncAllSchemas()
{
	// Discarded; in the typical (no collision) case the array never allocates.
	TArray<FPCGExHeaderIdRemap> Unused;
	SyncAllSchemas(Unused);
}

void FPCGExPropertySchemaCollection::SyncAllSchemas(TArray<FPCGExHeaderIdRemap>& OutRemaps)
{
#if WITH_EDITOR
	TSet<int32> SeenHeaderIds;
	SeenHeaderIds.Reserve(Schemas.Num());

	auto GenerateUniqueHeaderId = [&SeenHeaderIds]() -> int32
	{
		int32 NewId = 0;
		do { NewId = GetTypeHash(FGuid::NewGuid()); }
		while (NewId == 0 || SeenHeaderIds.Contains(NewId));
		return NewId;
	};

	for (FPCGExPropertySchema& Schema : Schemas)
	{
		// Copy-paste preserves the source's HeaderId; without this catch, SyncToSchema's
		// IndexByHeaderId aliases the duplicates and one side's overrides get silently dropped.
		bool bAlreadySeen = false;
		if (Schema.HeaderId != 0)
		{
			SeenHeaderIds.Add(Schema.HeaderId, &bAlreadySeen);
		}
		if (Schema.HeaderId == 0 || bAlreadySeen)
		{
			const int32 OldId = Schema.HeaderId;
			Schema.HeaderId = GenerateUniqueHeaderId();
			SeenHeaderIds.Add(Schema.HeaderId);

			// Zero-bootstraps never appeared in saved overrides, so there is nothing to remap.
			if (bAlreadySeen)
			{
				OutRemaps.Emplace(OldId, Schema.HeaderId, Schema.Name);
			}
		}
		Schema.SyncPropertyName();
	}
#else
	for (FPCGExPropertySchema& Schema : Schemas)
	{
		Schema.SyncPropertyName();
	}
#endif
}

void FPCGExPropertySchemaCollection::SyncAllSchemasAndRemap(TFunctionRef<void(TConstArrayView<FPCGExHeaderIdRemap>)> ApplyRemap)
{
	TArray<FPCGExHeaderIdRemap> Remaps;
	SyncAllSchemas(Remaps);
	if (!Remaps.IsEmpty())
	{
		ApplyRemap(Remaps);
	}
}

void FPCGExPropertySchemaCollection::ApplyToOverrides(FPCGExPropertyOverrides& Overrides) const
{
	const TArray<FInstancedStruct> Schema = BuildSchema();
	Overrides.SyncToSchema(Schema);
}

void FPCGExPropertySchemaCollection::ApplyToOverrides(TArray<FPCGExPropertyOverrides>& OverridesArray) const
{
	const TArray<FInstancedStruct> Schema = BuildSchema();
	for (FPCGExPropertyOverrides& Overrides : OverridesArray)
	{
		Overrides.SyncToSchema(Schema);
	}
}

bool FPCGExPropertySchemaCollection::ReconcileImportOverrides()
{
	TArray<FPCGExPropertyResolved> Resolved;
	Resolve(Resolved);
	return ReconcileImportOverrides(Resolved);
}

bool FPCGExPropertySchemaCollection::ReconcileImportOverrides(const TArray<FPCGExPropertyResolved>& Resolved)
{
	check(IsInGameThread());

	TArray<FInstancedStruct> ImportedOnlySchema;
	ImportedOnlySchema.Reserve(Resolved.Num());
	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		if (!Entry.OwningAsset)
		{
			continue;
		}

		// Feed a local copy of Source->Property patched with the canonical name/HeaderId from
		// Schema. The asset's cached Property->PropertyName can be stale (no PostLoad sync), and
		// SyncToSchema both copies it into new entries and overwrites existing entries with it --
		// a stale empty cache would silently wipe every override's PropertyName to None.
		// GetEffectiveProperty would feed the override back into itself; Source->Property is the
		// canonical schema, patched here to guarantee the name truth.
		FInstancedStruct Patched = Entry.Source->Property;
		if (FPCGExProperty* Prop = Patched.GetMutablePtr<FPCGExProperty>())
		{
			Prop->PropertyName = Entry.Source->Name;
#if WITH_EDITOR
			Prop->HeaderId = Entry.Source->HeaderId;
#endif
		}
		ImportedOnlySchema.Add(MoveTemp(Patched));
	}

	return ImportOverrides.SyncToSchema(ImportedOnlySchema);
}

void FPCGExPropertySchemaCollection::SyncFromArchetype(const FPCGExPropertySchemaCollection& Archetype)
{
#if WITH_EDITOR
	// Fast path: structure already mirrors the archetype. Hits on every register after the first sync.
	if (Schemas.Num() == Archetype.Schemas.Num())
	{
		bool bStructureMatches = true;
		for (int32 i = 0; i < Schemas.Num(); ++i)
		{
			const FPCGExProperty* MyProp = Schemas[i].GetProperty();
			const FPCGExProperty* ArchProp = Archetype.Schemas[i].GetProperty();
			if (!MyProp || !ArchProp ||
				Schemas[i].Property.GetScriptStruct() != Archetype.Schemas[i].Property.GetScriptStruct() ||
				MyProp->HeaderId != ArchProp->HeaderId)
			{
				bStructureMatches = false;
				break;
			}
		}

		if (bStructureMatches)
		{
			return;
		}
	}

	TArray<FPCGExPropertySchema> OldSchemas = MoveTemp(Schemas);

	TMap<int32, int32> OldIndexByInnerHeaderId;
	OldIndexByInnerHeaderId.Reserve(OldSchemas.Num());
	for (int32 i = 0; i < OldSchemas.Num(); ++i)
	{
		if (const FPCGExProperty* Prop = OldSchemas[i].GetProperty())
		{
			if (Prop->HeaderId != 0)
			{
				OldIndexByInnerHeaderId.Add(Prop->HeaderId, i);
			}
		}
	}

	// Built lazily on the first HeaderId miss -- covers legacy instances saved before the
	// ctor change, when HeaderIds were random and won't match the CDO's
	TMap<FName, int32> OldIndexByName;
	bool bNameMapBuilt = false;

	Schemas.Reset(Archetype.Schemas.Num());

	for (const FPCGExPropertySchema& ArchetypeSchema : Archetype.Schemas)
	{
		FPCGExPropertySchema& NewSchema = Schemas.AddDefaulted_GetRef();
		NewSchema.HeaderId = ArchetypeSchema.HeaderId;
		NewSchema.Name = ArchetypeSchema.Name;

		int32 ExistingIndex = INDEX_NONE;
		if (const FPCGExProperty* ArchProp = ArchetypeSchema.GetProperty())
		{
			if (ArchProp->HeaderId != 0)
			{
				if (const int32* Found = OldIndexByInnerHeaderId.Find(ArchProp->HeaderId))
				{
					ExistingIndex = *Found;
				}
			}
		}

		if (ExistingIndex == INDEX_NONE && !ArchetypeSchema.Name.IsNone())
		{
			if (!bNameMapBuilt)
			{
				bNameMapBuilt = true;
				OldIndexByName.Reserve(OldSchemas.Num());
				for (int32 i = 0; i < OldSchemas.Num(); ++i)
				{
					if (!OldSchemas[i].Name.IsNone())
					{
						OldIndexByName.Add(OldSchemas[i].Name, i);
					}
				}
			}
			if (const int32* Found = OldIndexByName.Find(ArchetypeSchema.Name))
			{
				ExistingIndex = *Found;
			}
		}

		FPCGExPropertySchema* Existing = OldSchemas.IsValidIndex(ExistingIndex) ? &OldSchemas[ExistingIndex] : nullptr;

		if (Existing && Existing->Property.GetScriptStruct() == ArchetypeSchema.Property.GetScriptStruct())
		{
			NewSchema.Property = MoveTemp(Existing->Property);
		}
		else
		{
			NewSchema.Property = ArchetypeSchema.Property;
		}

		NewSchema.SyncPropertyName();
	}
#endif
}

#pragma endregion

#pragma region FPCGExPropertyOverrides

// ============================================================================
// SyncToSchema - The heart of the override system
// ============================================================================
//
// This method rebuilds the Overrides array to match a new schema while preserving
// user state (enabled flags, values) through schema changes. It uses HeaderId
// (editor-only) as the stable key for matching.
//
// The algorithm:
// 1. Save old overrides and index them by HeaderId
// 2. For each schema property:
//    a. Try to find an existing override with matching HeaderId
//    b. If found AND same type: keep value + bEnabled, update PropertyName
//    c. If found but type changed: use schema default value, keep bEnabled
//    d. If not found (new property): use schema default, set bEnabled=false
// 3. Result: Overrides array is parallel with Schema array
//
// At runtime (no WITH_EDITOR), the HeaderId path is skipped and all overrides
// are rebuilt from schema defaults.

bool FPCGExPropertyOverrides::SyncToSchema(const TArray<FInstancedStruct>& Schema)
{
#if WITH_EDITOR
	// Fast path: structure matches entry-for-entry. Refresh in place; the slow path below
	// reallocates Overrides, dangling FStructOnScope aliases held by per-entry detail
	// customizations (causes scrambled values, undo-time access violations). Reconcile
	// usually arrives with no structural change, so this is the hot path.
	if (Overrides.Num() == Schema.Num())
	{
		bool bStructureMatches = true;
		for (int32 i = 0; i < Schema.Num(); ++i)
		{
			const FPCGExProperty* SchemaProp = Schema[i].GetPtr<FPCGExProperty>();
			if (!SchemaProp ||
				Overrides[i].Value.GetScriptStruct() != Schema[i].GetScriptStruct() ||
				Overrides[i].HeaderId != SchemaProp->HeaderId)
			{
				bStructureMatches = false;
				break;
			}
		}
		if (bStructureMatches)
		{
			bool bChanged = false;
			for (int32 i = 0; i < Schema.Num(); ++i)
			{
				const FPCGExProperty* SchemaProp = Schema[i].GetPtr<FPCGExProperty>();
				if (Overrides[i].PropertyName != SchemaProp->PropertyName)
				{
					Overrides[i].PropertyName = SchemaProp->PropertyName;
					bChanged = true;
				}
				if (FPCGExProperty* MyProp = Overrides[i].GetPropertyMutable())
				{
					if (MyProp->PropertyName != SchemaProp->PropertyName)
					{
						MyProp->PropertyName = SchemaProp->PropertyName;
						bChanged = true;
					}
					if (MyProp->SyncStructuralFromSchema(*SchemaProp))
					{
						bChanged = true;
					}
				}
			}
			return bChanged;
		}
	}
#endif

	// Match existing entries to the new schema by OUTER identity (HeaderId primary, PropertyName
	// fallback). Outer identity lives on FPCGExPropertyOverrideEntry directly, not inside Value's
	// FInstancedStruct, so it survives UE's broken per-property delta propagation.
	TArray<FPCGExPropertyOverrideEntry> OldOverrides = MoveTemp(Overrides);

#if WITH_EDITOR
	// One-shot inner→outer migration for legacy data, then build identity maps in the same
	// pass. Idempotent: skips entries that already carry outer identity. The CDO is the trigger
	// -- once any reconcile fires for a CDO, its entries acquire outer identity here, and UE
	// per-property delta propagates the outer scalars (FName, int32) to instances correctly
	// on the next CDO edit.
	TMap<int32, int32> IndexByHeaderId;
	TMap<FName, int32> IndexByName;
	for (int32 i = 0; i < OldOverrides.Num(); ++i)
	{
		FPCGExPropertyOverrideEntry& Entry = OldOverrides[i];
		if (Entry.HeaderId == 0 || Entry.PropertyName.IsNone())
		{
			if (const FPCGExProperty* Prop = Entry.Value.GetPtr<FPCGExProperty>())
			{
				if (Entry.HeaderId == 0)
				{
					Entry.HeaderId = Prop->HeaderId;
				}
				if (Entry.PropertyName.IsNone())
				{
					Entry.PropertyName = Prop->PropertyName;
				}
			}
		}
		// First-wins on both maps so legacy collided overrides resolve correctly through the
		// Name fallback below; identical to last-wins when identities are unique.
		if (Entry.HeaderId != 0 && !IndexByHeaderId.Contains(Entry.HeaderId))
		{
			IndexByHeaderId.Add(Entry.HeaderId, i);
		}
		if (!Entry.PropertyName.IsNone() && !IndexByName.Contains(Entry.PropertyName))
		{
			IndexByName.Add(Entry.PropertyName, i);
		}
	}
#endif

	Overrides.Reset(Schema.Num());

	for (int32 SchemaIndex = 0; SchemaIndex < Schema.Num(); ++SchemaIndex)
	{
		const FInstancedStruct& SchemaProp = Schema[SchemaIndex];
		const FPCGExProperty* SchemaData = SchemaProp.GetPtr<FPCGExProperty>();
		if (!SchemaData)
		{
			continue;
		}

		FPCGExPropertyOverrideEntry& NewEntry = Overrides.AddDefaulted_GetRef();
#if WITH_EDITORONLY_DATA
		// Outer identity cache (editor-only). Cooked path skips this; the inner
		// FPCGExProperty::PropertyName carried inside SchemaProp is sufficient at runtime.
		NewEntry.PropertyName = SchemaData->PropertyName;
		NewEntry.HeaderId = SchemaData->HeaderId;
#endif

		FPCGExPropertyOverrideEntry* Existing = nullptr;

#if WITH_EDITOR
		if (SchemaData->HeaderId != 0)
		{
			if (const int32* Found = IndexByHeaderId.Find(SchemaData->HeaderId))
			{
				Existing = &OldOverrides[*Found];
			}
		}
		if (!Existing && !SchemaData->PropertyName.IsNone())
		{
			if (const int32* Found = IndexByName.Find(SchemaData->PropertyName))
			{
				Existing = &OldOverrides[*Found];
			}
		}

		// Last-resort parallel-index fallback. The Overrides array is structurally parallel to
		// the imports-only schema by construction (every reconcile writes them in lockstep), so
		// when an entry has no usable outer OR inner identity -- which happens when UE's
		// per-property delta blanked the inner FInstancedStruct AND outer identity hadn't been
		// migrated onto the CDO yet -- the same-index slot is the only signal left. Critical
		// for preserving bEnabled toggles on instances of pre-fix authored data.
		//
		// Gated on the candidate ALSO being identity-less: an entry that still carries
		// HeaderId/Name but didn't match any schema slot is a removed property, not a
		// propagation casualty -- routing it here would misread "remove + add" as "rename"
		// and silently preserve a stale value (and stale inner HeaderId) under the new name.
		if (!Existing && OldOverrides.IsValidIndex(SchemaIndex))
		{
			FPCGExPropertyOverrideEntry& Candidate = OldOverrides[SchemaIndex];
			if (Candidate.HeaderId == 0 && Candidate.PropertyName.IsNone())
			{
				Existing = &Candidate;
			}
		}
#endif

		if (Existing)
		{
			NewEntry.bEnabled = Existing->bEnabled;

			if (Existing->Value.IsValid() && Existing->Value.GetScriptStruct() == SchemaProp.GetScriptStruct())
			{
				// Same type - preserve value, refresh inner PropertyName / structural fields from schema
				NewEntry.Value = MoveTemp(Existing->Value);
				if (FPCGExProperty* Prop = NewEntry.GetPropertyMutable())
				{
					Prop->PropertyName = SchemaData->PropertyName;
					Prop->SyncStructuralFromSchema(*SchemaData);
				}
			}
			else
			{
				// Type changed, or inner Value lost to broken FInstancedStruct propagation.
				// Take schema default; bEnabled (preserved above) carries the user's authoring intent.
				NewEntry.Value = SchemaProp;
			}
		}
		else
		{
			NewEntry.Value = SchemaProp;
			NewEntry.bEnabled = false;
		}
	}

	// Slow path always rebuilds storage -- the relocated backing memory is itself an
	// observable change regardless of bitwise content equality.
	return true;
}

void FPCGExPropertyOverrides::ApplyHeaderIdRemap(TConstArrayView<FPCGExHeaderIdRemap> Remaps)
{
#if WITH_EDITOR
	if (Remaps.IsEmpty())
	{
		return;
	}

	for (FPCGExPropertyOverrideEntry& Entry : Overrides)
	{
		// Match on (OldId, Name): OldId alone aliases multiple schema entries pre-regeneration,
		// so Name is required to pin the remap to the specific entry whose HeaderId changed.
		for (const FPCGExHeaderIdRemap& Remap : Remaps)
		{
			if (Entry.HeaderId == Remap.OldId && Entry.PropertyName == Remap.Name)
			{
				Entry.HeaderId = Remap.NewId;
				if (FPCGExProperty* Prop = Entry.GetPropertyMutable())
				{
					Prop->HeaderId = Remap.NewId;
				}
				break;
			}
		}
	}
#endif
}

const FInstancedStruct* FPCGExPropertyOverrides::GetOverride(FName PropertyName) const
{
	for (const FPCGExPropertyOverrideEntry& Entry : Overrides)
	{
		if (Entry.bEnabled && Entry.GetPropertyName() == PropertyName)
		{
			return &Entry.Value;
		}
	}
	return nullptr;
}

#pragma endregion

#pragma region PCGExProperties soft object path walk

namespace PCGExPropertySoftPaths
{
	// Visited tracks UPCGExPropertySchemaAsset pointers so an A->B->A import cycle
	// terminates instead of looping. Peer to PCGExPropertySchemaResolve::Walk; distinct
	// because Resolve dedups locals/imports by Name (first-wins) and walks override
	// layers as a chain -- both wrong here, which must surface every soft path
	// regardless of shadowing.
	static void WalkCollection(
		const FPCGExPropertySchemaCollection& Collection,
		TSet<const UPCGExPropertySchemaAsset*>& Visited,
		TSet<FSoftObjectPath>& OutPaths)
	{
		for (const FPCGExPropertySchema& Schema : Collection.Schemas)
		{
			if (const FPCGExProperty* Prop = Schema.GetProperty())
			{
				Prop->GatherSoftObjectPaths(OutPaths);
			}
		}

		PCGExProperties::GatherSoftObjectPaths(Collection.ImportOverrides, OutPaths);

		// Imported schema assets are auto-cooked via hard refs, but we still walk into
		// each one's Collection to surface its leaf soft paths.
		for (const TObjectPtr<UPCGExPropertySchemaAsset>& Asset : Collection.ImportedSchemas)
		{
			if (!Asset) { continue; }
			bool bAlreadyVisited = false;
			Visited.Add(Asset.Get(), &bAlreadyVisited);
			if (bAlreadyVisited) { continue; }
			WalkCollection(Asset->Collection, Visited, OutPaths);
		}
	}
}

namespace PCGExProperties
{
	void GatherSoftObjectPaths(const FPCGExPropertyOverrides& Overrides, TSet<FSoftObjectPath>& OutPaths)
	{
		for (const FPCGExPropertyOverrideEntry& Entry : Overrides.Overrides)
		{
			if (!Entry.bEnabled) { continue; }
			if (const FPCGExProperty* Prop = Entry.GetProperty())
			{
				Prop->GatherSoftObjectPaths(OutPaths);
			}
		}
	}

	void GatherSoftObjectPaths(const FPCGExPropertySchemaCollection& Collection, TSet<FSoftObjectPath>& OutPaths)
	{
		TSet<const UPCGExPropertySchemaAsset*> Visited;
		PCGExPropertySoftPaths::WalkCollection(Collection, Visited, OutPaths);
	}
}

#pragma endregion
