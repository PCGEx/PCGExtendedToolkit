// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"

#include "PCGExProperty.h"
#include "PCGExPropertyPinMarshal.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"
#include "Helpers/PCGExMemberPath.h"
#include "UObject/Stack.h"

namespace PCGExCollectionEntryBlueprintLibrary_Private
{
	const FPCGExAssetCollectionEntry* GetEntry(const UPCGExAssetCollection* Collection, int32 EntryIndex)
	{
		return Collection ? Collection->GetEntryRaw(EntryIndex).Entry : nullptr;
	}

	FPCGExAssetCollectionEntry* GetMutableEntry(UPCGExAssetCollection* Collection, int32 EntryIndex)
	{
		return Collection ? Collection->GetMutableEntryRaw(EntryIndex) : nullptr;
	}

	bool ReadInto(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		const FProperty* OutProp,
		void* OutMem)
	{
		if (!Collection || !OutProp || !OutMem)
		{
			return false;
		}

		const FPCGExEntryAccessResult Result = Collection->GetEntryRaw(EntryIndex);
		if (!Result)
		{
			return false;
		}

		const FInstancedStruct* Source = PCGExCollections::ResolveEntrySourceProperty(Result.Entry, Result.Host, PropertyName);
		if (!Source)
		{
			return false;
		}

		const FPCGExProperty* Prop = Source->GetPtr<FPCGExProperty>();
		if (!Prop)
		{
			return false;
		}

		return PCGExPropertyPinMarshal::TryWriteToPin(Prop, OutProp, OutMem);
	}

	// Resolve the writable override slot for (entry, property). Slots are kept parallel to the
	// collection schema by SyncPropertyOverridesToEntries -- never append here. A missing slot
	// means the property isn't part of the schema, which is an authoring error worth surfacing:
	// fail with a Blueprint runtime warning rather than silently returning false.
	FPCGExPropertyOverrideEntry* ResolveWritableOverride(UPCGExAssetCollection* Collection, int32 EntryIndex, FName PropertyName)
	{
		FPCGExAssetCollectionEntry* Entry = GetMutableEntry(Collection, EntryIndex);
		if (!Entry)
		{
			return nullptr;
		}

		FPCGExPropertyOverrideEntry* Slot = Entry->PropertyOverrides.FindEntryMutableByName(PropertyName);
		if (!Slot)
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Property '%s' is not part of the schema of collection '%s' -- entry override not written."),
					*PropertyName.ToString(), *GetNameSafe(Collection)),
				ELogVerbosity::Warning);
			return nullptr;
		}

		return Slot;
	}

	// Shared tail of every override-slot write: undo snapshot before the marshal, then enable + dirty only
	// when the value or enabled state changed, so a hook rewriting an identical value on a no-op rebuild
	// doesn't churn the package. Override writes don't feed the weight-sorted pick cache: no InvalidateCache.
	bool WriteOverrideSlot(UPCGExAssetCollection* Collection, FPCGExPropertyOverrideEntry* Slot, TFunctionRef<bool(FPCGExProperty*)> Write)
	{
		FPCGExProperty* Prop = Slot ? Slot->GetPropertyMutable() : nullptr;
		if (!Prop)
		{
			return false;
		}

		Collection->Modify(false);
		const FInstancedStruct Before = Slot->Value;
		if (!Write(Prop))
		{
			return false;
		}

		if (Slot->bEnabled && Slot->Value == Before)
		{
			return true;
		}

		Slot->bEnabled = true;
		(void)Collection->MarkPackageDirty();
		return true;
	}

	bool WriteFrom(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		const FProperty* InProp,
		const void* InMem)
	{
		if (!InProp || !InMem)
		{
			return false;
		}

		return WriteOverrideSlot(
			Collection, ResolveWritableOverride(Collection, EntryIndex, PropertyName),
			[InProp, InMem](FPCGExProperty* Prop) { return PCGExPropertyPinMarshal::TryReadFromPin(Prop, InProp, InMem); });
	}

	// Soft-path lookups for the well-typed Object/Class accessors; same rationale as the
	// component library: keep Object/Class pins out of the wildcard CustomStructureParam path.
	bool ReadEntrySoftPath(
		const UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		void* OutPath)
	{
		if (!Collection)
		{
			return false;
		}

		const FPCGExEntryAccessResult Result = Collection->GetEntryRaw(EntryIndex);
		if (!Result)
		{
			return false;
		}

		const FInstancedStruct* Source = PCGExCollections::ResolveEntrySourceProperty(Result.Entry, Result.Host, PropertyName);
		if (!Source)
		{
			return false;
		}

		const FPCGExProperty* Prop = Source->GetPtr<FPCGExProperty>();
		if (!Prop)
		{
			return false;
		}

		return Prop->TryWriteValue(PathType, OutPath);
	}

	bool WriteEntrySoftPath(
		UPCGExAssetCollection* Collection,
		int32 EntryIndex,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		const void* InPath)
	{
		return WriteOverrideSlot(
			Collection, ResolveWritableOverride(Collection, EntryIndex, PropertyName),
			[PathType, InPath](FPCGExProperty* Prop) { return Prop->TryReadValue(PathType, InPath); });
	}

	// --- Category tier ---

	const FPCGExProperty* ResolveCategoryProperty(const UPCGExAssetCollection* Collection, FName Category, FName PropertyName)
	{
		if (!Collection)
		{
			return nullptr;
		}
		const FInstancedStruct* Source = Collection->ResolveCategoryPropertySlot(Category, PropertyName, FPCGExProperty::StaticStruct());
		return Source ? Source->GetPtr<FPCGExProperty>() : nullptr;
	}

	bool ReadCategoryInto(
		const UPCGExAssetCollection* Collection,
		FName Category,
		FName PropertyName,
		const FProperty* OutProp,
		void* OutMem)
	{
		if (!OutProp || !OutMem)
		{
			return false;
		}
		const FPCGExProperty* Prop = ResolveCategoryProperty(Collection, Category, PropertyName);
		return Prop ? PCGExPropertyPinMarshal::TryWriteToPin(Prop, OutProp, OutMem) : false;
	}

	bool ReadCategorySoftPath(
		const UPCGExAssetCollection* Collection,
		FName Category,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		void* OutPath)
	{
		const FPCGExProperty* Prop = ResolveCategoryProperty(Collection, Category, PropertyName);
		return Prop ? Prop->TryWriteValue(PathType, OutPath) : false;
	}

	// Writable slot for (category, property). bMintRow mints a missing row (editor only, after the undo
	// snapshot); otherwise a missing row returns null and sets bOutNeedsRow so the caller can mint once it
	// holds a known-good value. The schema check runs first so a bad name never mints a row.
	FPCGExPropertyOverrideEntry* ResolveWritableCategoryOverride(UPCGExAssetCollection* Collection, FName Category, FName PropertyName, bool bMintRow, bool* bOutNeedsRow = nullptr)
	{
		if (!Collection)
		{
			return nullptr;
		}

		if (Category.IsNone())
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Category 'None' has no override row on collection '%s' -- category override not written."),
					*GetNameSafe(Collection)),
				ELogVerbosity::Warning);
			return nullptr;
		}

		if (!Collection->HasProperty(PropertyName))
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Property '%s' is not part of the schema of collection '%s' -- category override not written."),
					*PropertyName.ToString(), *GetNameSafe(Collection)),
				ELogVerbosity::Warning);
			return nullptr;
		}

		FPCGExCategoryOverrides* Row = Collection->FindCategoryOverridesRow(Category);
		if (!Row)
		{
			if (!bMintRow)
			{
				if (bOutNeedsRow)
				{
					*bOutNeedsRow = true;
				}
				return nullptr;
			}
#if WITH_EDITOR
			Collection->Modify();
			Row = Collection->EDITOR_FindOrAddCategoryOverrides(Category);
#else
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Collection '%s' has no override row for category '%s' and rows can only be minted in the editor -- category override not written."),
					*GetNameSafe(Collection), *Category.ToString()),
				ELogVerbosity::Warning);
			return nullptr;
#endif
		}

		FPCGExPropertyOverrideEntry* Slot = Row ? Row->PropertyOverrides.FindEntryMutableByName(PropertyName) : nullptr;
		if (!Slot)
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Category '%s' on collection '%s' has no override slot for property '%s' (row out of sync with the schema) -- category override not written."),
					*Category.ToString(), *GetNameSafe(Collection), *PropertyName.ToString()),
				ELogVerbosity::Warning);
			return nullptr;
		}

		return Slot;
	}

	// Category write: in place when the row exists; otherwise the value is marshalled into a scratch
	// copy of the schema default FIRST, so a failed conversion mints nothing, and the row is minted only
	// once the value is known good.
	bool WriteCategory(UPCGExAssetCollection* Collection, FName Category, FName PropertyName, TFunctionRef<bool(FPCGExProperty*)> Write)
	{
		bool bNeedsRow = false;
		if (FPCGExPropertyOverrideEntry* Slot = ResolveWritableCategoryOverride(Collection, Category, PropertyName, /*bMintRow=*/false, &bNeedsRow))
		{
			return WriteOverrideSlot(Collection, Slot, Write);
		}
		if (!bNeedsRow)
		{
			return false;
		}

#if WITH_EDITOR
		const FInstancedStruct* Prototype = Collection->CollectionProperties.GetPropertyByName(PropertyName);
		FInstancedStruct Scratch = Prototype ? *Prototype : FInstancedStruct();
		FPCGExProperty* ScratchProp = Scratch.GetMutablePtr<FPCGExProperty>();
		if (!ScratchProp || !Write(ScratchProp))
		{
			return false;
		}

		FPCGExPropertyOverrideEntry* Slot = ResolveWritableCategoryOverride(Collection, Category, PropertyName, /*bMintRow=*/true);
		if (!Slot)
		{
			return false;
		}

		Slot->Value = MoveTemp(Scratch);
		Slot->bEnabled = true;
		(void)Collection->MarkPackageDirty();
		return true;
#else
		// Rows can only be minted in the editor; the resolver reports it.
		(void)ResolveWritableCategoryOverride(Collection, Category, PropertyName, /*bMintRow=*/true);
		return false;
#endif
	}

	bool WriteCategoryFrom(
		UPCGExAssetCollection* Collection,
		FName Category,
		FName PropertyName,
		const FProperty* InProp,
		const void* InMem)
	{
		if (!InProp || !InMem)
		{
			return false;
		}

		return WriteCategory(
			Collection, Category, PropertyName,
			[InProp, InMem](FPCGExProperty* Prop) { return PCGExPropertyPinMarshal::TryReadFromPin(Prop, InProp, InMem); });
	}

	bool WriteCategorySoftPath(
		UPCGExAssetCollection* Collection,
		FName Category,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		const void* InPath)
	{
		return WriteCategory(
			Collection, Category, PropertyName,
			[PathType, InPath](FPCGExProperty* Prop) { return Prop->TryReadValue(PathType, InPath); });
	}

	// --- Collection tier ---

	const FPCGExProperty* ResolveCollectionProperty(const UPCGExAssetCollection* Collection, FName PropertyName)
	{
		if (!Collection)
		{
			return nullptr;
		}
		const FInstancedStruct* Source = Collection->CollectionProperties.GetPropertyByName(PropertyName);
		return Source ? Source->GetPtr<FPCGExProperty>() : nullptr;
	}

	bool ReadCollectionInto(
		const UPCGExAssetCollection* Collection,
		FName PropertyName,
		const FProperty* OutProp,
		void* OutMem)
	{
		if (!OutProp || !OutMem)
		{
			return false;
		}
		const FPCGExProperty* Prop = ResolveCollectionProperty(Collection, PropertyName);
		return Prop ? PCGExPropertyPinMarshal::TryWriteToPin(Prop, OutProp, OutMem) : false;
	}

	bool ReadCollectionSoftPath(
		const UPCGExAssetCollection* Collection,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		void* OutPath)
	{
		const FPCGExProperty* Prop = ResolveCollectionProperty(Collection, PropertyName);
		return Prop ? Prop->TryWriteValue(PathType, OutPath) : false;
	}

	// Writable default for PropertyName: the local schema entry, else the ImportOverrides slot standing in
	// for an imported asset's entry (the asset is never written through). ImportSlot is set only on the
	// imported path and enabled by the write; Holder is the property's FInstancedStruct, for before/after.
	struct FWritableCollectionDefault
	{
		FInstancedStruct* Holder = nullptr;
		FPCGExPropertyOverrideEntry* ImportSlot = nullptr;
	};

	FWritableCollectionDefault ResolveWritableCollectionDefault(UPCGExAssetCollection* Collection, FName PropertyName)
	{
		FWritableCollectionDefault Out;
		if (!Collection)
		{
			return Out;
		}

		if (FPCGExPropertySchema* Local = Collection->CollectionProperties.FindByNameMutable(PropertyName))
		{
			Out.Holder = &Local->Property;
		}
		// Schema-gated: a stale ImportOverrides slot (imported asset edited before this collection
		// reconciled) must not be resurrected by a write.
		else if (Collection->HasProperty(PropertyName))
		{
			if (FPCGExPropertyOverrideEntry* Slot = Collection->CollectionProperties.ImportOverrides.FindEntryMutableByName(PropertyName))
			{
				Out.ImportSlot = Slot;
				Out.Holder = &Slot->Value;
			}
		}

		if (!Out.Holder || !Out.Holder->IsValid())
		{
			Out.Holder = nullptr;
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Property '%s' is not part of the schema of collection '%s' -- collection default not written."),
					*PropertyName.ToString(), *GetNameSafe(Collection)),
				ELogVerbosity::Warning);
		}
		return Out;
	}

	// Defaults feed the registry prototypes the cache rebuild re-derives, so invalidate like the
	// editor's value-edit path does. Entries' disabled slots read through to the default on resolve.
	// Value-gated like the override tiers: an identical rewrite commits nothing.
	bool WriteCollectionDefault(UPCGExAssetCollection* Collection, FName PropertyName, TFunctionRef<bool(FPCGExProperty*)> Write)
	{
		const FWritableCollectionDefault Target = ResolveWritableCollectionDefault(Collection, PropertyName);
		FPCGExProperty* Prop = Target.Holder ? Target.Holder->GetMutablePtr<FPCGExProperty>() : nullptr;
		if (!Prop)
		{
			return false;
		}

		Collection->Modify(false);
		const FInstancedStruct Before = *Target.Holder;
		if (!Write(Prop))
		{
			return false;
		}

		const bool bAlreadyEnabled = !Target.ImportSlot || Target.ImportSlot->bEnabled;
		if (bAlreadyEnabled && *Target.Holder == Before)
		{
			return true;
		}

		if (Target.ImportSlot)
		{
			Target.ImportSlot->bEnabled = true;
		}
		(void)Collection->MarkPackageDirty();
		Collection->InvalidateCache();
		return true;
	}

	bool WriteCollectionFrom(
		UPCGExAssetCollection* Collection,
		FName PropertyName,
		const FProperty* InProp,
		const void* InMem)
	{
		if (!InProp || !InMem)
		{
			return false;
		}

		return WriteCollectionDefault(
			Collection, PropertyName,
			[InProp, InMem](FPCGExProperty* Prop) { return PCGExPropertyPinMarshal::TryReadFromPin(Prop, InProp, InMem); });
	}

	bool WriteCollectionSoftPath(
		UPCGExAssetCollection* Collection,
		FName PropertyName,
		EPCGMetadataTypes PathType,
		const void* InPath)
	{
		return WriteCollectionDefault(
			Collection, PropertyName,
			[PathType, InPath](FPCGExProperty* Prop) { return Prop->TryReadValue(PathType, InPath); });
	}

	// Dirty path for plain field setters: weight/category/tags feed the pick cache
	// (weights, categories) or tag queries, so invalidate alongside the undo snapshot.
	void CommitEntryFieldWrite(UPCGExAssetCollection* Collection)
	{
		Collection->Modify();
		(void)Collection->MarkPackageDirty();
		Collection->InvalidateCache();
	}

	// --- Generic reflected member access ---

	// Entry-struct layout for the collection's registered type. Instance TypeId first (exact
	// regardless of BP subclassing), class lookup second, base entry struct as final fallback
	// so base members stay reachable for unregistered custom types.
	const UStruct* ResolveEntryMemberRoot(const UPCGExAssetCollection* Collection)
	{
		const PCGExAssetCollection::FTypeRegistry& Registry = PCGExAssetCollection::FTypeRegistry::Get();

		PCGExAssetCollection::FTypeInfo TypeInfo;
		bool bFound = Registry.GetInfo(Collection->GetTypeId(), TypeInfo);
		if (!bFound || !TypeInfo.EntryStruct)
		{
			bFound = Registry.GetInfoByClass(Collection->GetClass(), TypeInfo);
		}

		if (bFound && TypeInfo.EntryStruct)
		{
			return TypeInfo.EntryStruct;
		}

		return FPCGExAssetCollectionEntry::StaticStruct();
	}

	// Per-entry refinement of ResolveEntryMemberRoot (heterogeneous hosts have no single
	// entry struct). Subcollection rows fall through to the collection root (legacy behavior).
	const UStruct* ResolveEntryMemberRootForEntry(const UPCGExAssetCollection* Collection, const FPCGExAssetCollectionEntry* Entry)
	{
		if (Entry)
		{
			if (const UScriptStruct* EntryStruct = PCGExAssetCollection::FTypeRegistry::Get().GetEntryStruct(Entry->GetTypeId()))
			{
				return EntryStruct;
			}
		}
		return ResolveEntryMemberRoot(Collection);
	}

	PCGExMemberPath::FResolvedMember ResolveEntryMember(UPCGExAssetCollection* Collection, int32 EntryIndex, FName MemberPath)
	{
		FPCGExAssetCollectionEntry* Entry = GetMutableEntry(Collection, EntryIndex);
		if (!Entry)
		{
			return PCGExMemberPath::FResolvedMember();
		}
		return PCGExMemberPath::Resolve(ResolveEntryMemberRootForEntry(Collection, Entry), Entry, MemberPath);
	}

	PCGExMemberPath::FResolvedMember ResolveEntryMemberConst(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName MemberPath)
	{
		// Read-only resolution; the resolver wants a mutable container but the address is
		// only ever read from on this path.
		const FPCGExAssetCollectionEntry* Entry = GetEntry(Collection, EntryIndex);
		if (!Entry)
		{
			return PCGExMemberPath::FResolvedMember();
		}
		return PCGExMemberPath::Resolve(ResolveEntryMemberRootForEntry(Collection, Entry), const_cast<FPCGExAssetCollectionEntry*>(Entry), MemberPath);
	}

	PCGExMemberPath::FResolvedMember ResolveCollectionMember(UPCGExAssetCollection* Collection, FName MemberPath)
	{
		if (!Collection)
		{
			return PCGExMemberPath::FResolvedMember();
		}
		return PCGExMemberPath::Resolve(Collection->GetClass(), Collection, MemberPath);
	}

	PCGExMemberPath::FResolvedMember ResolveCollectionMemberConst(const UPCGExAssetCollection* Collection, FName MemberPath)
	{
		if (!Collection)
		{
			return PCGExMemberPath::FResolvedMember();
		}
		return PCGExMemberPath::Resolve(Collection->GetClass(), const_cast<UPCGExAssetCollection*>(Collection), MemberPath);
	}

	void MemberPathMissWarning(const UPCGExAssetCollection* Collection, FName MemberPath)
	{
		FFrame::KismetExecutionMessage(
			*FString::Printf(
				TEXT("Member path '%s' could not be resolved on collection '%s'."),
				*MemberPath.ToString(), *GetNameSafe(Collection)),
			ELogVerbosity::Warning);
	}

	// Generic member writes can touch anything the pick cache consumes (Weight, Category,
	// material variants) -- blanket invalidate; the cache rebuilds lazily.
	void CommitMemberWrite(UPCGExAssetCollection* Collection)
	{
		Collection->Modify();
		(void)Collection->MarkPackageDirty();
		Collection->InvalidateCache();
	}

	// Exact-type copy between a member property and a Blueprint pin (either direction).
	// FBoolProperty pairs are special-cased: native bool vs bitfield bool pass SameType,
	// but CopyCompleteValue is FieldMask-based and corrupts across differing layouts.
	bool CopyMemberValue(
		const FProperty* SrcProp, const void* SrcMem,
		const FProperty* DstProp, void* DstMem,
		const UPCGExAssetCollection* Collection, FName MemberPath)
	{
		if (!SrcProp || !SrcMem || !DstProp || !DstMem)
		{
			return false;
		}

		const FBoolProperty* SrcBool = CastField<FBoolProperty>(SrcProp);
		const FBoolProperty* DstBool = CastField<FBoolProperty>(DstProp);
		if (SrcBool && DstBool)
		{
			DstBool->SetPropertyValue(DstMem, SrcBool->GetPropertyValue(SrcMem));
			return true;
		}

		if (!DstProp->SameType(SrcProp))
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Member '%s' on collection '%s' is of type '%s' but the connected pin is '%s'. Re-pick the member on the node to refresh the pin type."),
					*MemberPath.ToString(), *GetNameSafe(Collection),
					*SrcProp->GetCPPType(), *DstProp->GetCPPType()),
				ELogVerbosity::Warning);
			return false;
		}

		DstProp->CopyCompleteValue(DstMem, SrcMem);
		return true;
	}

	bool ReadMemberInto(
		const PCGExMemberPath::FResolvedMember& Member,
		const UPCGExAssetCollection* Collection, FName MemberPath,
		const FProperty* OutProp, void* OutMem)
	{
		if (!Member.IsValid())
		{
			MemberPathMissWarning(Collection, MemberPath);
			return false;
		}
		return CopyMemberValue(Member.Property, Member.Address, OutProp, OutMem, Collection, MemberPath);
	}

	bool WriteMemberFrom(
		const PCGExMemberPath::FResolvedMember& Member,
		UPCGExAssetCollection* Collection, FName MemberPath,
		const FProperty* InProp, const void* InMem)
	{
		if (!Member.IsValid())
		{
			MemberPathMissWarning(Collection, MemberPath);
			return false;
		}

		// Identical value: nothing to commit. Bool pairs compare through their own masks (see CopyMemberValue).
		const FBoolProperty* SrcBool = CastField<FBoolProperty>(InProp);
		const FBoolProperty* DstBool = CastField<FBoolProperty>(Member.Property);
		const bool bIdentical = (SrcBool && DstBool)
			? SrcBool->GetPropertyValue(InMem) == DstBool->GetPropertyValue(Member.Address)
			: Member.Property->SameType(InProp) && Member.Property->Identical(Member.Address, InMem);
		if (bIdentical)
		{
			return true;
		}

		if (!CopyMemberValue(InProp, InMem, Member.Property, Member.Address, Collection, MemberPath))
		{
			return false;
		}
		CommitMemberWrite(Collection);
		return true;
	}

	// Soft-reference flavor helpers. FSoftClassProperty derives from FSoftObjectProperty, so
	// the object flavor must explicitly reject class members and vice versa.
	const FSoftObjectProperty* ResolveSoftFlavor(
		const PCGExMemberPath::FResolvedMember& Member,
		const UPCGExAssetCollection* Collection, FName MemberPath, bool bClassFlavor)
	{
		if (!Member.IsValid())
		{
			MemberPathMissWarning(Collection, MemberPath);
			return nullptr;
		}

		const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Member.Property);
		const bool bIsClassProp = Member.Property->IsA<FSoftClassProperty>();
		if (!SoftProp || bIsClassProp != bClassFlavor)
		{
			FFrame::KismetExecutionMessage(
				*FString::Printf(
					TEXT("Member '%s' on collection '%s' is not a soft %s reference."),
					*MemberPath.ToString(), *GetNameSafe(Collection),
					bClassFlavor ? TEXT("class") : TEXT("object")),
				ELogVerbosity::Warning);
			return nullptr;
		}

		return SoftProp;
	}

	bool ReadMemberSoftPath(
		const PCGExMemberPath::FResolvedMember& Member,
		const UPCGExAssetCollection* Collection, FName MemberPath,
		UClass* ExpectedClass, bool bClassFlavor, FSoftObjectPath& OutPath)
	{
		const FSoftObjectProperty* SoftProp = ResolveSoftFlavor(Member, Collection, MemberPath, bClassFlavor);
		if (!SoftProp)
		{
			return false;
		}

		// Declared-class compatibility: the node stamps ExpectedClass from the member's own
		// class, so this only rejects stale/mismatched graphs.
		const UClass* DeclaredClass = bClassFlavor
			? CastFieldChecked<FSoftClassProperty>(SoftProp)->MetaClass
			: SoftProp->PropertyClass;
		if (ExpectedClass && DeclaredClass && !DeclaredClass->IsChildOf(ExpectedClass))
		{
			return false;
		}

		OutPath = SoftProp->GetPropertyValue(Member.Address).ToSoftObjectPath();
		return true;
	}

	bool WriteMemberSoftPath(
		const PCGExMemberPath::FResolvedMember& Member,
		UPCGExAssetCollection* Collection, FName MemberPath,
		bool bClassFlavor, const FSoftObjectPath& InPath)
	{
		const FSoftObjectProperty* SoftProp = ResolveSoftFlavor(Member, Collection, MemberPath, bClassFlavor);
		if (!SoftProp)
		{
			return false;
		}

		if (SoftProp->GetPropertyValue(Member.Address).ToSoftObjectPath() == InPath)
		{
			return true;
		}

		SoftProp->SetPropertyValue(Member.Address, FSoftObjectPtr(InPath));
		CommitMemberWrite(Collection);
		return true;
	}
}

bool UPCGExCollectionEntryBlueprintLibrary::TryGetEntryPropertyValue(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTryGetEntryPropertyValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FIntProperty, EntryIndex);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	// Wildcard OutValue: the K2 node wires its actual pin type here at compile time.
	// Read the FProperty descriptor + destination memory off the stack manually.
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::ReadInto(
			Collection, EntryIndex, PropertyName, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryPropertyOverride(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTrySetEntryPropertyOverride)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FIntProperty, EntryIndex);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::WriteFrom(
			Collection, EntryIndex, PropertyName, InProp, InMem);
	P_NATIVE_END;
}

UObject* UPCGExCollectionEntryBlueprintLibrary::TryGetEntryPropertyObject(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftObjectPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadEntrySoftPath(
		Collection, EntryIndex, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath))
	{
		return nullptr;
	}

	UObject* Resolved = PCGExPropertyPinMarshal::ResolveObjectFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryPropertyObject(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	UObject* NewObject)
{
	const FSoftObjectPath SoftPath(NewObject);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteEntrySoftPath(
		Collection, EntryIndex, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath);
}

TSubclassOf<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetEntryPropertyClass(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftClassPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadEntrySoftPath(
		Collection, EntryIndex, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath))
	{
		return nullptr;
	}

	UClass* Resolved = PCGExPropertyPinMarshal::ResolveClassFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryPropertyClass(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName PropertyName,
	UClass* NewClass)
{
	const FSoftClassPath SoftPath(NewClass);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteEntrySoftPath(
		Collection, EntryIndex, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath);
}

#pragma region Category tier

bool UPCGExCollectionEntryBlueprintLibrary::TryGetCategoryPropertyValue(
	const UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTryGetCategoryPropertyValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, Category);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::ReadCategoryInto(
			Collection, Category, PropertyName, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCategoryPropertyOverride(
	UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTrySetCategoryPropertyOverride)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, Category);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::WriteCategoryFrom(
			Collection, Category, PropertyName, InProp, InMem);
	P_NATIVE_END;
}

UObject* UPCGExCollectionEntryBlueprintLibrary::TryGetCategoryPropertyObject(
	const UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftObjectPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadCategorySoftPath(
		Collection, Category, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath))
	{
		return nullptr;
	}

	UObject* Resolved = PCGExPropertyPinMarshal::ResolveObjectFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCategoryPropertyObject(
	UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	UObject* NewObject)
{
	const FSoftObjectPath SoftPath(NewObject);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteCategorySoftPath(
		Collection, Category, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath);
}

TSubclassOf<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetCategoryPropertyClass(
	const UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftClassPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadCategorySoftPath(
		Collection, Category, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath))
	{
		return nullptr;
	}

	UClass* Resolved = PCGExPropertyPinMarshal::ResolveClassFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCategoryPropertyClass(
	UPCGExAssetCollection* Collection,
	FName Category,
	FName PropertyName,
	UClass* NewClass)
{
	const FSoftClassPath SoftPath(NewClass);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteCategorySoftPath(
		Collection, Category, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath);
}

#pragma endregion

#pragma region Collection tier

bool UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionPropertyValue(
	const UPCGExAssetCollection* Collection,
	FName PropertyName,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTryGetCollectionPropertyValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::ReadCollectionInto(
			Collection, PropertyName, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionPropertyDefault(
	UPCGExAssetCollection* Collection,
	FName PropertyName,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTrySetCollectionPropertyDefault)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::WriteCollectionFrom(
			Collection, PropertyName, InProp, InMem);
	P_NATIVE_END;
}

UObject* UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionPropertyObject(
	const UPCGExAssetCollection* Collection,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftObjectPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadCollectionSoftPath(
		Collection, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath))
	{
		return nullptr;
	}

	UObject* Resolved = PCGExPropertyPinMarshal::ResolveObjectFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionPropertyObject(
	UPCGExAssetCollection* Collection,
	FName PropertyName,
	UObject* NewObject)
{
	const FSoftObjectPath SoftPath(NewObject);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteCollectionSoftPath(
		Collection, PropertyName, EPCGMetadataTypes::SoftObjectPath, &SoftPath);
}

TSubclassOf<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionPropertyClass(
	const UPCGExAssetCollection* Collection,
	FName PropertyName,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	FSoftClassPath SoftPath;
	if (!PCGExCollectionEntryBlueprintLibrary_Private::ReadCollectionSoftPath(
		Collection, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath))
	{
		return nullptr;
	}

	UClass* Resolved = PCGExPropertyPinMarshal::ResolveClassFromPath(SoftPath, ExpectedClass);
	bSuccess = Resolved != nullptr;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionPropertyClass(
	UPCGExAssetCollection* Collection,
	FName PropertyName,
	UClass* NewClass)
{
	const FSoftClassPath SoftPath(NewClass);
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteCollectionSoftPath(
		Collection, PropertyName, EPCGMetadataTypes::SoftClassPath, &SoftPath);
}

#pragma endregion

int32 UPCGExCollectionEntryBlueprintLibrary::GetNumEntries(const UPCGExAssetCollection* Collection)
{
	return Collection ? Collection->NumEntries() : 0;
}

bool UPCGExCollectionEntryBlueprintLibrary::IsValidEntryIndex(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	return Collection ? Collection->IsValidIndex(EntryIndex) : false;
}

bool UPCGExCollectionEntryBlueprintLibrary::IsSubCollectionEntry(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->bIsSubCollection : false;
}

int32 UPCGExCollectionEntryBlueprintLibrary::GetEntryWeight(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Weight : 0;
}

bool UPCGExCollectionEntryBlueprintLibrary::SetEntryWeight(UPCGExAssetCollection* Collection, int32 EntryIndex, int32 NewWeight)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry)
	{
		return false;
	}

	const int32 ClampedWeight = FMath::Max(0, NewWeight);
	if (Entry->Weight == ClampedWeight)
	{
		return true;
	}

	Entry->Weight = ClampedWeight;
	PCGExCollectionEntryBlueprintLibrary_Private::CommitEntryFieldWrite(Collection);
	return true;
}

FName UPCGExCollectionEntryBlueprintLibrary::GetEntryCategory(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Category : NAME_None;
}

bool UPCGExCollectionEntryBlueprintLibrary::SetEntryCategory(UPCGExAssetCollection* Collection, int32 EntryIndex, FName NewCategory)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry)
	{
		return false;
	}

	if (Entry->Category == NewCategory)
	{
		return true;
	}

	Entry->Category = NewCategory;
	PCGExCollectionEntryBlueprintLibrary_Private::CommitEntryFieldWrite(Collection);
	return true;
}

TArray<FName> UPCGExCollectionEntryBlueprintLibrary::GetEntryTags(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Tags.Array() : TArray<FName>();
}

bool UPCGExCollectionEntryBlueprintLibrary::AddEntryTag(UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry || Tag.IsNone() || Entry->Tags.Contains(Tag))
	{
		return false;
	}

	Entry->Tags.Add(Tag);
	PCGExCollectionEntryBlueprintLibrary_Private::CommitEntryFieldWrite(Collection);
	return true;
}

bool UPCGExCollectionEntryBlueprintLibrary::RemoveEntryTag(UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry || Entry->Tags.Remove(Tag) == 0)
	{
		return false;
	}

	PCGExCollectionEntryBlueprintLibrary_Private::CommitEntryFieldWrite(Collection);
	return true;
}

bool UPCGExCollectionEntryBlueprintLibrary::EntryHasTag(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName Tag)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Tags.Contains(Tag) : false;
}

bool UPCGExCollectionEntryBlueprintLibrary::HasEntryPropertyOverride(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName PropertyName)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->PropertyOverrides.HasOverride(PropertyName) : false;
}

bool UPCGExCollectionEntryBlueprintLibrary::SetEntryPropertyOverrideEnabled(UPCGExAssetCollection* Collection, int32 EntryIndex, FName PropertyName, bool bEnabled)
{
	FPCGExPropertyOverrideEntry* Slot = PCGExCollectionEntryBlueprintLibrary_Private::ResolveWritableOverride(Collection, EntryIndex, PropertyName);
	if (!Slot)
	{
		return false;
	}

	if (Slot->bEnabled == bEnabled)
	{
		return true;
	}

	Slot->bEnabled = bEnabled;
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

bool UPCGExCollectionEntryBlueprintLibrary::HasCategoryPropertyOverride(const UPCGExAssetCollection* Collection, FName Category, FName PropertyName)
{
	const FPCGExPropertyOverrides* Layer = Collection ? Collection->FindCategoryOverrides(Category) : nullptr;
	return Layer ? Layer->HasOverride(PropertyName) : false;
}

bool UPCGExCollectionEntryBlueprintLibrary::SetCategoryPropertyOverrideEnabled(UPCGExAssetCollection* Collection, FName Category, FName PropertyName, bool bEnabled)
{
	if (!Collection || Category.IsNone() || !Collection->HasProperty(PropertyName))
	{
		return false;
	}

	// No row means every slot is already disabled -- nothing to mint for a disable.
	if (!bEnabled && !Collection->FindCategoryOverridesRow(Category))
	{
		return true;
	}

	FPCGExPropertyOverrideEntry* Slot = PCGExCollectionEntryBlueprintLibrary_Private::ResolveWritableCategoryOverride(Collection, Category, PropertyName, /*bMintRow=*/true);
	if (!Slot)
	{
		return false;
	}

	if (Slot->bEnabled == bEnabled)
	{
		return true;
	}

	Collection->Modify();
	Slot->bEnabled = bEnabled;
	(void)Collection->MarkPackageDirty();
	return true;
}

bool UPCGExCollectionEntryBlueprintLibrary::HasCategoryOverrides(const UPCGExAssetCollection* Collection, FName Category)
{
	const FPCGExPropertyOverrides* Layer = Collection ? Collection->FindCategoryOverrides(Category) : nullptr;
	return Layer ? Layer->GetEnabledCount() > 0 : false;
}

TArray<FName> UPCGExCollectionEntryBlueprintLibrary::GetOverriddenCategories(const UPCGExAssetCollection* Collection)
{
	TArray<FName> Out;
	if (!Collection)
	{
		return Out;
	}
	for (const FPCGExCategoryOverrides& Row : Collection->CategoryOverrides)
	{
		if (!Row.Category.IsNone() && Row.HasAnyEnabled())
		{
			Out.Add(Row.Category);
		}
	}
	return Out;
}

TArray<FName> UPCGExCollectionEntryBlueprintLibrary::GetUsedCategories(const UPCGExAssetCollection* Collection)
{
	TArray<FName> Out;
#if WITH_EDITOR
	if (Collection)
	{
		TSet<FName> Used;
		Collection->EDITOR_CollectUsedCategories(Used);
		Out = Used.Array();
	}
#endif
	return Out;
}

bool UPCGExCollectionEntryBlueprintLibrary::RemoveCategoryOverrides(UPCGExAssetCollection* Collection, FName Category)
{
#if WITH_EDITOR
	if (!Collection || !Collection->FindCategoryOverridesRow(Category))
	{
		return false;
	}

	Collection->Modify();
	const bool bRemoved = Collection->EDITOR_RemoveCategoryOverrides(Category);
	if (bRemoved)
	{
		(void)Collection->MarkPackageDirty();
	}
	return bRemoved;
#else
	return false;
#endif
}

int32 UPCGExCollectionEntryBlueprintLibrary::CleanupUnusedCategoryOverrides(UPCGExAssetCollection* Collection)
{
#if WITH_EDITOR
	if (!Collection)
	{
		return 0;
	}

	// Modify(false): the snapshot must precede the removal, but dirtying is conditional on it.
	Collection->Modify(false);
	const int32 NumRemoved = Collection->EDITOR_CleanupUnusedCategoryOverrides();
	if (NumRemoved > 0)
	{
		(void)Collection->MarkPackageDirty();
	}
	return NumRemoved;
#else
	return 0;
#endif
}

bool UPCGExCollectionEntryBlueprintLibrary::HasCollectionProperty(const UPCGExAssetCollection* Collection, FName PropertyName)
{
	return Collection ? Collection->HasProperty(PropertyName) : false;
}

bool UPCGExCollectionEntryBlueprintLibrary::IsCollectionPropertyImported(const UPCGExAssetCollection* Collection, FName PropertyName)
{
	if (!Collection || PropertyName.IsNone())
	{
		return false;
	}
	for (const FPCGExPropertySchema& Schema : Collection->CollectionProperties.Schemas)
	{
		if (Schema.Name == PropertyName)
		{
			return false;
		}
	}
	return Collection->CollectionProperties.FindByName(PropertyName) != nullptr;
}

TArray<FName> UPCGExCollectionEntryBlueprintLibrary::GetCollectionPropertyNames(const UPCGExAssetCollection* Collection)
{
	TArray<FName> Out;
	if (!Collection)
	{
		return Out;
	}
	for (const FInstancedStruct& Slot : Collection->CollectionProperties.BuildSchema())
	{
		if (const FPCGExProperty* Prop = Slot.GetPtr<FPCGExProperty>(); Prop && !Prop->PropertyName.IsNone())
		{
			Out.Add(Prop->PropertyName);
		}
	}
	return Out;
}

FSoftObjectPath UPCGExCollectionEntryBlueprintLibrary::GetEntryStagingPath(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Staging.Path : FSoftObjectPath();
}

FBox UPCGExCollectionEntryBlueprintLibrary::GetEntryStagingBounds(const UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	return Entry ? Entry->Staging.Bounds : FBox(ForceInit);
}

bool UPCGExCollectionEntryBlueprintLibrary::TryGetEntryMemberValue(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTryGetEntryMemberValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FIntProperty, EntryIndex);
	P_GET_PROPERTY(FNameProperty, MemberPath);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberInto(
			PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMemberConst(Collection, EntryIndex, MemberPath),
			Collection, MemberPath, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryMemberValue(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTrySetEntryMemberValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FIntProperty, EntryIndex);
	P_GET_PROPERTY(FNameProperty, MemberPath);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberFrom(
			PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMember(Collection, EntryIndex, MemberPath),
			Collection, MemberPath, InProp, InMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionMemberValue(
	const UPCGExAssetCollection* Collection,
	FName MemberPath,
	int32& OutValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTryGetCollectionMemberValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, MemberPath);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* OutProp = Stack.MostRecentProperty;
	void* OutMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberInto(
			PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMemberConst(Collection, MemberPath),
			Collection, MemberPath, OutProp, OutMem);
	P_NATIVE_END;
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionMemberValue(
	UPCGExAssetCollection* Collection,
	FName MemberPath,
	const int32& NewValue)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UPCGExCollectionEntryBlueprintLibrary::execTrySetCollectionMemberValue)
{
	P_GET_OBJECT(UPCGExAssetCollection, Collection);
	P_GET_PROPERTY(FNameProperty, MemberPath);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* InProp = Stack.MostRecentProperty;
	const void* InMem = Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberFrom(
			PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMember(Collection, MemberPath),
			Collection, MemberPath, InProp, InMem);
	P_NATIVE_END;
}

TSoftObjectPtr<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetEntryMemberSoftObject(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	FSoftObjectPath Path;
	bSuccess = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMemberConst(Collection, EntryIndex, MemberPath),
		Collection, MemberPath, *ExpectedClass, false, Path);
	return TSoftObjectPtr<UObject>(Path);
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryMemberSoftObject(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	TSoftObjectPtr<UObject> NewValue)
{
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMember(Collection, EntryIndex, MemberPath),
		Collection, MemberPath, false, NewValue.ToSoftObjectPath());
}

TSoftClassPtr<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetEntryMemberSoftClass(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	FSoftObjectPath Path;
	bSuccess = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMemberConst(Collection, EntryIndex, MemberPath),
		Collection, MemberPath, *ExpectedClass, true, Path);
	return TSoftClassPtr<UObject>(Path);
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetEntryMemberSoftClass(
	UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	FName MemberPath,
	TSoftClassPtr<UObject> NewValue)
{
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveEntryMember(Collection, EntryIndex, MemberPath),
		Collection, MemberPath, true, NewValue.ToSoftObjectPath());
}

TSoftObjectPtr<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionMemberSoftObject(
	const UPCGExAssetCollection* Collection,
	FName MemberPath,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	FSoftObjectPath Path;
	bSuccess = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMemberConst(Collection, MemberPath),
		Collection, MemberPath, *ExpectedClass, false, Path);
	return TSoftObjectPtr<UObject>(Path);
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionMemberSoftObject(
	UPCGExAssetCollection* Collection,
	FName MemberPath,
	TSoftObjectPtr<UObject> NewValue)
{
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMember(Collection, MemberPath),
		Collection, MemberPath, false, NewValue.ToSoftObjectPath());
}

TSoftClassPtr<UObject> UPCGExCollectionEntryBlueprintLibrary::TryGetCollectionMemberSoftClass(
	const UPCGExAssetCollection* Collection,
	FName MemberPath,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	FSoftObjectPath Path;
	bSuccess = PCGExCollectionEntryBlueprintLibrary_Private::ReadMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMemberConst(Collection, MemberPath),
		Collection, MemberPath, *ExpectedClass, true, Path);
	return TSoftClassPtr<UObject>(Path);
}

bool UPCGExCollectionEntryBlueprintLibrary::TrySetCollectionMemberSoftClass(
	UPCGExAssetCollection* Collection,
	FName MemberPath,
	TSoftClassPtr<UObject> NewValue)
{
	return PCGExCollectionEntryBlueprintLibrary_Private::WriteMemberSoftPath(
		PCGExCollectionEntryBlueprintLibrary_Private::ResolveCollectionMember(Collection, MemberPath),
		Collection, MemberPath, true, NewValue.ToSoftObjectPath());
}

FPCGExAssetGrammarDetails UPCGExCollectionEntryBlueprintLibrary::GetEntryGrammar(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	bSuccess = Entry != nullptr;
	return Entry ? Entry->AssetGrammar : FPCGExAssetGrammarDetails();
}

bool UPCGExCollectionEntryBlueprintLibrary::SetEntryGrammar(UPCGExAssetCollection* Collection, int32 EntryIndex, const FPCGExAssetGrammarDetails& NewGrammar)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry)
	{
		return false;
	}

	Entry->AssetGrammar = NewGrammar;
	// Grammar does not feed the pick cache -- undo snapshot + dirty only.
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

FPCGExAssetGrammarDetails UPCGExCollectionEntryBlueprintLibrary::GetEntryEffectiveGrammar(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess)
{
	bSuccess = false;
	if (!Collection)
	{
		return FPCGExAssetGrammarDetails();
	}

	const FPCGExEntryAccessResult Result = Collection->GetEntryRaw(EntryIndex);
	if (!Result)
	{
		return FPCGExAssetGrammarDetails();
	}

	const FPCGExAssetGrammarDetails* Effective = Result.Entry->GetEffectiveGrammar(Result.Host);
	bSuccess = Effective != nullptr;
	return Effective ? *Effective : FPCGExAssetGrammarDetails();
}

FPCGExFittingVariations UPCGExCollectionEntryBlueprintLibrary::GetEntryVariations(const UPCGExAssetCollection* Collection, int32 EntryIndex, bool& bSuccess)
{
	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	bSuccess = Entry != nullptr;
	return Entry ? Entry->Variations : FPCGExFittingVariations();
}

bool UPCGExCollectionEntryBlueprintLibrary::SetEntryVariations(UPCGExAssetCollection* Collection, int32 EntryIndex, const FPCGExFittingVariations& NewVariations)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	if (!Entry)
	{
		return false;
	}

	Entry->Variations = NewVariations;
	if (Entry->VariationMode == EPCGExEntryVariationMode::None)
	{
		// A write to an entry parked at None would otherwise be invisible -- promote to Local.
		// Global is left alone: writing while Global is already "parked values" behavior.
		Entry->VariationMode = EPCGExEntryVariationMode::Local;
	}
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

FPCGExAssetGrammarDetails UPCGExCollectionEntryBlueprintLibrary::GetCollectionGlobalGrammar(const UPCGExAssetCollection* Collection, bool& bSuccess)
{
	bSuccess = Collection != nullptr;
	return Collection ? Collection->GlobalAssetGrammar : FPCGExAssetGrammarDetails();
}

bool UPCGExCollectionEntryBlueprintLibrary::SetCollectionGlobalGrammar(UPCGExAssetCollection* Collection, const FPCGExAssetGrammarDetails& NewGrammar)
{
	if (!Collection)
	{
		return false;
	}

	Collection->GlobalAssetGrammar = NewGrammar;
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

FPCGExAssetGrammarDetails UPCGExCollectionEntryBlueprintLibrary::GetCollectionSubCollectionGrammar(const UPCGExAssetCollection* Collection, bool& bSuccess)
{
	bSuccess = Collection != nullptr;
	return Collection ? Collection->SubCollectionGrammar : FPCGExAssetGrammarDetails();
}

bool UPCGExCollectionEntryBlueprintLibrary::SetCollectionSubCollectionGrammar(UPCGExAssetCollection* Collection, const FPCGExAssetGrammarDetails& NewGrammar)
{
	if (!Collection)
	{
		return false;
	}

	Collection->SubCollectionGrammar = NewGrammar;
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

FPCGExFittingVariations UPCGExCollectionEntryBlueprintLibrary::GetCollectionGlobalVariations(const UPCGExAssetCollection* Collection, bool& bSuccess)
{
	bSuccess = Collection != nullptr;
	return Collection ? Collection->GlobalVariations : FPCGExFittingVariations();
}

bool UPCGExCollectionEntryBlueprintLibrary::SetCollectionGlobalVariations(UPCGExAssetCollection* Collection, const FPCGExFittingVariations& NewVariations)
{
	if (!Collection)
	{
		return false;
	}

	Collection->GlobalVariations = NewVariations;
	Collection->Modify();
	(void)Collection->MarkPackageDirty();
	return true;
}

UPCGExAssetCollection* UPCGExCollectionEntryBlueprintLibrary::GetEntrySubCollection(UPCGExAssetCollection* Collection, int32 EntryIndex)
{
	FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetMutableEntry(Collection, EntryIndex);
	return Entry ? Entry->GetSubCollection<UPCGExAssetCollection>() : nullptr;
}

UObject* UPCGExCollectionEntryBlueprintLibrary::LoadEntryAsset(
	const UPCGExAssetCollection* Collection,
	int32 EntryIndex,
	TSubclassOf<UObject> ExpectedClass,
	bool& bSuccess)
{
	bSuccess = false;

	const FPCGExAssetCollectionEntry* Entry = PCGExCollectionEntryBlueprintLibrary_Private::GetEntry(Collection, EntryIndex);
	if (!Entry || !Entry->Staging.Path.IsValid())
	{
		return nullptr;
	}

	UObject* Resolved = PCGExPropertyPinMarshal::ResolveObjectFromPath(Entry->Staging.Path, ExpectedClass);
	if (!Resolved)
	{
		return nullptr;
	}

	bSuccess = true;
	return Resolved;
}

bool UPCGExCollectionEntryBlueprintLibrary::RestageEntry(UPCGExAssetCollection* Collection, int32 EntryIndex)
{
#if WITH_EDITOR
	if (!Collection)
	{
		return false;
	}
	return Collection->EDITOR_RebuildEntryStaging(EntryIndex);
#else
	return false;
#endif
}

FName UPCGExCollectionEntryBlueprintLibrary::GetCollectionTypeId(const UPCGExAssetCollection* Collection)
{
	return Collection ? Collection->GetTypeId() : FName(NAME_None);
}
