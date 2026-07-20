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

	// Enable the freshly-written override and dirty the owning collection. Override writes
	// don't feed the weight-sorted pick cache, so no InvalidateCache here.
	void CommitOverrideWrite(UPCGExAssetCollection* Collection, FPCGExPropertyOverrideEntry* Slot)
	{
		Slot->bEnabled = true;
		Collection->Modify();
		(void)Collection->MarkPackageDirty();
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

		FPCGExPropertyOverrideEntry* Slot = ResolveWritableOverride(Collection, EntryIndex, PropertyName);
		if (!Slot)
		{
			return false;
		}

		FPCGExProperty* Prop = Slot->GetPropertyMutable();
		if (!Prop)
		{
			return false;
		}

		if (!PCGExPropertyPinMarshal::TryReadFromPin(Prop, InProp, InMem))
		{
			return false;
		}

		CommitOverrideWrite(Collection, Slot);
		return true;
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
		FPCGExPropertyOverrideEntry* Slot = ResolveWritableOverride(Collection, EntryIndex, PropertyName);
		if (!Slot)
		{
			return false;
		}

		FPCGExProperty* Prop = Slot->GetPropertyMutable();
		if (!Prop)
		{
			return false;
		}

		if (!Prop->TryReadValue(PathType, InPath))
		{
			return false;
		}

		CommitOverrideWrite(Collection, Slot);
		return true;
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

	UObject* Resolved = SoftPath.ResolveObject();
	if (!Resolved)
	{
		Resolved = SoftPath.TryLoad();
	}
	if (!Resolved)
	{
		return nullptr;
	}
	if (*ExpectedClass && !Resolved->IsA(ExpectedClass))
	{
		return nullptr;
	}

	bSuccess = true;
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

	UClass* Resolved = Cast<UClass>(SoftPath.ResolveObject());
	if (!Resolved)
	{
		Resolved = SoftPath.TryLoadClass<UObject>();
	}
	if (!Resolved)
	{
		return nullptr;
	}
	if (*ExpectedClass && !Resolved->IsChildOf(ExpectedClass))
	{
		return nullptr;
	}

	bSuccess = true;
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

	UObject* Resolved = Entry->Staging.Path.ResolveObject();
	if (!Resolved)
	{
		Resolved = Entry->Staging.Path.TryLoad();
	}
	if (!Resolved)
	{
		return nullptr;
	}
	if (*ExpectedClass && !Resolved->IsA(ExpectedClass))
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
