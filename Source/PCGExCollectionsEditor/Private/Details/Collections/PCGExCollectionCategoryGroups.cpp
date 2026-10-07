// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionCategoryGroups.h"

#include "PCGExCollectionsEditorSettings.h"
#include "PCGExLog.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Details/PCGExCategoryGroups.h"

#pragma region FRegistry

PCGExCollectionCategoryGroups::FRegistry& PCGExCollectionCategoryGroups::FRegistry::Get()
{
	static FRegistry Instance;
	return Instance;
}

void PCGExCollectionCategoryGroups::FRegistry::Register(const FGroup& Group)
{
	if (Group.Id.IsNone())
	{
		return;
	}

	FWriteScopeLock Lock(RegistryLock);
	Groups.Add(Group.Id, Group);
}

void PCGExCollectionCategoryGroups::FRegistry::Unregister(const FName Id)
{
	FWriteScopeLock Lock(RegistryLock);
	Groups.Remove(Id);
}

bool PCGExCollectionCategoryGroups::FRegistry::Find(const FName Id, FGroup& OutGroup) const
{
	FReadScopeLock Lock(RegistryLock);
	if (const FGroup* Found = Groups.Find(Id))
	{
		OutGroup = *Found;
		return true;
	}
	return false;
}

void PCGExCollectionCategoryGroups::FRegistry::GetChildren(const FName Parent, TArray<FName>& OutIds) const
{
	FReadScopeLock Lock(RegistryLock);
	for (const TPair<FName, FGroup>& Pair : Groups)
	{
		if (PCGExCategoryGroups::GetParentGroup(Pair.Key) == Parent)
		{
			OutIds.Add(Pair.Key);
		}
	}
}

#pragma endregion

void PCGExCollectionCategoryGroups::RegisterBuiltInGroups()
{
	FRegistry& Registry = FRegistry::Get();
	int32 Order = 0;
	const auto Add = [&Registry, &Order](const FName Id, const TCHAR* Label, const TCHAR* ToolTip)
	{
		Registry.Register(FGroup{Id, FText::FromString(Label), FText::FromString(ToolTip), Order++});
	};

	Add(Ids::Variations, TEXT("Variations"), TEXT("Show/hide Variations"));
	Add(Ids::VariationsOffset, TEXT("Var : Offset"), TEXT("Show/hide Variations : Offset"));
	Add(Ids::VariationsRotation, TEXT("Var : Rot"), TEXT("Show/hide Variations : Rotation"));
	Add(Ids::VariationsScale, TEXT("Var : Scale"), TEXT("Show/hide Variations : Scale"));
	Add(Ids::Fitting, TEXT("Fitting"), TEXT("Show/hide Fitting overrides"));
	Add(Ids::Tags, TEXT("Tags"), TEXT("Show/hide Tags"));
	Add(Ids::Staging, TEXT("Staging"), TEXT("Show/hide Staging"));
	Add(Ids::Grammar, TEXT("Grammar"), TEXT("Show/hide Grammar"));
	Add(Ids::Properties, TEXT("Properties"), TEXT("Show/hide Property Overrides"));
	Add(Ids::Materials, TEXT("Materials"), TEXT("Show/hide Materials"));
	Add(Ids::Descriptors, TEXT("Descriptors"), TEXT("Show/hide Descriptors"));
}

void PCGExCollectionCategoryGroups::DiscoverGroups(const TConstArrayView<const UScriptStruct*> EntryStructs, TArray<FGroup>& OutGroups)
{
	TSet<FName> Ids;
	for (const UScriptStruct* Struct : EntryStructs)
	{
		PCGExCategoryGroups::CollectGroups(Struct, Ids);
	}

	// Registered children of every discovered group: sub-group rows have no property to discover.
	TArray<FName> Pending = Ids.Array();
	while (!Pending.IsEmpty())
	{
		TArray<FName> Children;
		FRegistry::Get().GetChildren(Pending.Pop(EAllowShrinking::No), Children);
		for (const FName Child : Children)
		{
			bool bAlreadyIn = false;
			Ids.Add(Child, &bAlreadyIn);
			if (!bAlreadyIn)
			{
				Pending.Add(Child);
			}
		}
	}

	static TSet<FName> WarnedIds;
	for (const FName Id : Ids)
	{
		FGroup Group;
		if (!FRegistry::Get().Find(Id, Group))
		{
			Group.Id = Id;
			Group.Label = FText::FromName(Id);
			Group.ToolTip = FText::Format(INVTEXT("Show/hide {0} (unregistered group)"), FText::FromName(Id));
			Group.SortOrder = MAX_int32;

			bool bWarned = false;
			WarnedIds.Add(Id, &bWarned);
			if (!bWarned)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("PCGExCategoryGroup '%s' is tagged on an entry property but not registered with PCGExCollectionCategoryGroups::FRegistry."), *Id.ToString());
			}
		}
		OutGroups.Add(MoveTemp(Group));
	}

	OutGroups.Sort([](const FGroup& A, const FGroup& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder : A.Id.LexicalLess(B.Id);
	});
}

void PCGExCollectionCategoryGroups::GetAllRegisteredEntryStructs(TArray<const UScriptStruct*>& OutStructs)
{
	PCGExAssetCollection::FTypeRegistry::Get().ForEach([&OutStructs](const PCGExAssetCollection::FTypeInfo& Info)
	{
		if (Info.EntryStruct)
		{
			OutStructs.AddUnique(Info.EntryStruct);
		}
	});
}

bool PCGExCollectionCategoryGroups::IsGroupHidden(const FName Group)
{
	const UPCGExCollectionsEditorSettings* Settings = GetDefault<UPCGExCollectionsEditorSettings>();
	return PCGExCategoryGroups::AnyInHierarchy(Group, [Settings](const FName Id) { return Settings->HiddenCategoryGroups.Contains(Id); });
}

bool PCGExCollectionCategoryGroups::IsPropertyVisible(const FPropertyAndParent& PropertyAndParent)
{
	return PCGExCategoryGroups::IsVisibleUnlessHidden(PropertyAndParent, [](const FName Group) { return IsGroupHidden(Group); });
}

bool PCGExCollectionCategoryGroups::IsCustomRowVisible(const FName RowName, const FName ParentName)
{
	FName Group;
	return !PCGExCategoryGroups::TryParseRowTag(RowName, Group) || !IsGroupHidden(Group);
}
