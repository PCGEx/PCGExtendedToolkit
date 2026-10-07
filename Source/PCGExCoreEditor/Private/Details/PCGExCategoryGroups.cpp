// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExCategoryGroups.h"

#include "PCGExLog.h"
#include "PropertyEditorDelegates.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace PCGExCategoryGroups
{
	namespace Internal
	{
		constexpr const TCHAR* RowTagPrefix = TEXT("PCGExCategoryGroup:");

		/** Containers resolve to their element type; everything else is itself. */
		const FProperty* ElementOf(const FProperty& Property)
		{
			if (const FArrayProperty* AsArray = CastField<FArrayProperty>(&Property))
			{
				return AsArray->Inner;
			}
			if (const FSetProperty* AsSet = CastField<FSetProperty>(&Property))
			{
				return AsSet->ElementProp;
			}
			if (const FMapProperty* AsMap = CastField<FMapProperty>(&Property))
			{
				return AsMap->ValueProp;
			}
			return &Property;
		}

		void CollectGroupsRecursive(const UStruct* Struct, TSet<FName>& OutGroups, const int32 Depth, TSet<const UStruct*>& Visited)
		{
			if (!Struct || Depth < 0)
			{
				return;
			}

			bool bAlreadyVisited = false;
			Visited.Add(Struct, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				return;
			}

			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				TArray<FName> Groups;
				GetGroups(**It, Groups);
				for (FName Group : Groups)
				{
					for (; !Group.IsNone(); Group = GetParentGroup(Group))
					{
						OutGroups.Add(Group);
					}
				}

				if (const FStructProperty* AsStruct = CastField<FStructProperty>(ElementOf(**It)))
				{
					CollectGroupsRecursive(AsStruct->Struct, OutGroups, Depth - 1, Visited);
				}
			}
		}
	}
}

FName PCGExCategoryGroups::MakeRowTag(const FName Group)
{
	return FName(*(FString(Internal::RowTagPrefix) + Group.ToString()));
}

bool PCGExCategoryGroups::TryParseRowTag(const FName RowName, FName& OutGroup)
{
	if (RowName.IsNone())
	{
		return false;
	}

	const FString RowStr = RowName.ToString();
	if (!RowStr.StartsWith(Internal::RowTagPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}

	OutGroup = FName(*RowStr.Mid(FCString::Strlen(Internal::RowTagPrefix)));
	return !OutGroup.IsNone();
}

void PCGExCategoryGroups::GetGroups(const FProperty& Property, TArray<FName>& OutGroups)
{
	const FString* Value = Property.FindMetaData(MetaKey);
	if (!Value)
	{
		return;
	}

	TArray<FString> Ids;
	Value->ParseIntoArray(Ids, TEXT(","));
	for (FString& Id : Ids)
	{
		Id.TrimStartAndEndInline();
		if (!Id.IsEmpty())
		{
			OutGroups.AddUnique(FName(*Id));
		}
	}
}

bool PCGExCategoryGroups::ListContains(const FProperty& Property, const FName Group)
{
	if (Group.IsNone())
	{
		return false;
	}

	TArray<FName> Groups;
	GetGroups(Property, Groups);
	return Groups.Contains(Group);
}

FName PCGExCategoryGroups::GetParentGroup(const FName Group)
{
	const FString Str = Group.ToString();
	int32 Index = INDEX_NONE;
	if (!Str.FindLastChar(GroupSeparator, Index) || Index <= 0)
	{
		return NAME_None;
	}
	return FName(*Str.Left(Index));
}

bool PCGExCategoryGroups::AnyInHierarchy(FName Group, TFunctionRef<bool(FName)> Predicate)
{
	while (!Group.IsNone())
	{
		if (Predicate(Group))
		{
			return true;
		}
		Group = GetParentGroup(Group);
	}
	return false;
}

bool PCGExCategoryGroups::HasTaggedDirectMember(const FProperty& Property, const FName Group)
{
	const FStructProperty* AsStruct = CastField<FStructProperty>(Internal::ElementOf(Property));
	if (!AsStruct || !AsStruct->Struct)
	{
		return false;
	}

	for (TFieldIterator<FProperty> It(AsStruct->Struct); It; ++It)
	{
		if (ListContains(**It, Group))
		{
			return true;
		}
	}
	return false;
}

const FProperty* PCGExCategoryGroups::FindTaggedOwner(const FPropertyAndParent& PropertyAndParent)
{
	if (PropertyAndParent.Property.HasMetaData(MetaKey))
	{
		return &PropertyAndParent.Property;
	}

	for (const FProperty* Parent : PropertyAndParent.ParentProperties)
	{
		if (Parent && Parent->HasMetaData(MetaKey))
		{
			return Parent;
		}
	}
	return nullptr;
}

bool PCGExCategoryGroups::IsExternalStructRow(const FPropertyAndParent& PropertyAndParent)
{
	const FProperty* Root = PropertyAndParent.ParentProperties.Num() > 0
		? PropertyAndParent.ParentProperties.Last()
		: &PropertyAndParent.Property;
	return Root && Root->GetOwnerClass() == nullptr;
}

bool PCGExCategoryGroups::IsVisibleForAudience(
	const FPropertyAndParent& PropertyAndParent, const FName Audience, const TSet<FName>* ExtraProperties)
{
	if (Audience.IsNone())
	{
		return false;
	}

	const auto Admits = [Audience, ExtraProperties](const FProperty& Property)
	{
		return ListContains(Property, Audience)
			|| (ExtraProperties && ExtraProperties->Contains(Property.GetFName()));
	};

	if (Admits(PropertyAndParent.Property))
	{
		return true;
	}

	// Host shell of a tagged member, checked BEFORE the audience rejection below: the host's own list must
	// not strip the shell its member needs.
	if (HasTaggedDirectMember(PropertyAndParent.Property, Audience))
	{
		return true;
	}

	// An explicit list is the whole audience: ancestors never rescue it.
	if (PropertyAndParent.Property.HasMetaData(MetaKey))
	{
		return false;
	}

	for (const FProperty* Parent : PropertyAndParent.ParentProperties)
	{
		if (Parent && Admits(*Parent))
		{
			return true;
		}
	}

	return IsExternalStructRow(PropertyAndParent);
}

bool PCGExCategoryGroups::IsVisibleUnlessHidden(
	const FPropertyAndParent& PropertyAndParent, TFunctionRef<bool(FName)> IsGroupHidden)
{
	const FProperty* Owner = FindTaggedOwner(PropertyAndParent);
	if (!Owner)
	{
		return true;
	}

	TArray<FName> Groups;
	GetGroups(*Owner, Groups);
	if (Groups.IsEmpty())
	{
		return true;
	}

	for (const FName Group : Groups)
	{
		if (!IsGroupHidden(Group))
		{
			return true;
		}
	}
	return false;
}

bool PCGExCategoryGroups::FCategoryPathSet::IsCustomRowVisible(const FName RowName, const FName ParentName) const
{
	if (!RowName.IsNone())
	{
		return FullPaths.Contains(ParentName);
	}
	return FullPaths.Contains(ParentName) || Segments.Contains(ParentName);
}

void PCGExCategoryGroups::GatherCategoryPaths(
	const UStruct* Owner, TFunctionRef<bool(const FProperty&)> Admit, FCategoryPathSet& OutCategories)
{
	if (!Owner)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropIt(Owner); PropIt; ++PropIt)
	{
		if (!Admit(**PropIt))
		{
			continue;
		}

		const FString CategoryPath = PropIt->GetMetaData(TEXT("Category"));
		if (CategoryPath.IsEmpty())
		{
			continue;
		}

		OutCategories.FullPaths.Add(FName(*CategoryPath));

		TArray<FString> Segments;
		CategoryPath.ParseIntoArray(Segments, TEXT("|"));
		for (const FString& Segment : Segments)
		{
			OutCategories.Segments.Add(FName(*Segment.TrimStartAndEnd()));
		}
	}
}

void PCGExCategoryGroups::GatherAudienceCategoryPaths(
	const UStruct* Owner, const FName Audience, FCategoryPathSet& OutCategories, const TSet<FName>* ExtraProperties)
{
	if (Audience.IsNone())
	{
		return;
	}

	GatherCategoryPaths(
		Owner,
		[Audience, ExtraProperties](const FProperty& Property)
		{
			// Hosts of tagged members count too: their (sub)category must render for the member to appear.
			return ListContains(Property, Audience)
				|| (ExtraProperties && ExtraProperties->Contains(Property.GetFName()))
				|| HasTaggedDirectMember(Property, Audience);
		},
		OutCategories);
}

void PCGExCategoryGroups::CollectGroups(const UStruct* Struct, TSet<FName>& OutGroups, const int32 MaxDepth)
{
	TSet<const UStruct*> Visited;
	Internal::CollectGroupsRecursive(Struct, OutGroups, MaxDepth, Visited);
}

void PCGExCategoryGroups::ValidateGroups(
	TFunctionRef<bool(const UStruct*)> OwnerFilter, TFunctionRef<bool(FName)> IsKnown, const TCHAR* Context)
{
#if !UE_BUILD_SHIPPING
	const auto ValidateOwner = [&IsKnown, Context](const UStruct* Owner)
	{
		for (TFieldIterator<FProperty> PropIt(Owner, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			TArray<FName> Groups;
			GetGroups(**PropIt, Groups);
			for (const FName Group : Groups)
			{
				if (!IsKnown(Group))
				{
					UE_LOG(LogPCGEx, Warning, TEXT("[%s] %s.%s: %s value '%s' matches no registered id."),
					       Context, *Owner->GetName(), *PropIt->GetName(), *MetaKey.ToString(), *Group.ToString());
				}
			}
		}
	};

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (OwnerFilter(*It))
		{
			ValidateOwner(*It);
		}
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (OwnerFilter(*It))
		{
			ValidateOwner(*It);
		}
	}
#endif
}
