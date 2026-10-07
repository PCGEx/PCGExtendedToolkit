// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UStruct;
struct FPropertyAndParent;

/**
 * Category-group property meta: a UPROPERTY carrying meta=(PCGExCategoryGroup="<id>[,<id>...]") belongs to
 * the listed groups; dotted ids nest ("Variations.Offset" under "Variations"). Hosts own the policy -- a
 * details view may show only one audience's groups (Valency panel tools) or hide the groups a user toggled
 * off (collection editor footer). Everything here is policy-free: parsing, resolution through the parent
 * chain, discovery, category-path gathering for custom rows, and tag validation.
 */
namespace PCGExCategoryGroups
{
	/** The UPROPERTY meta key. UPROPERTY macros cannot reference this constant: authors write the literal. */
	inline const FName MetaKey(TEXT("PCGExCategoryGroup"));

	/** Dotted ids nest: "Variations.Offset" is a child of "Variations". */
	inline constexpr TCHAR GroupSeparator = TEXT('.');

	/** Custom rows have no property to tag: FDetailWidgetRow::RowTag(MakeRowTag(Group)) joins them to a group. */
	PCGEXCOREEDITOR_API FName MakeRowTag(FName Group);
	PCGEXCOREEDITOR_API bool TryParseRowTag(FName RowName, FName& OutGroup);

	/** Groups listed on Property (comma-split, trimmed, deduplicated). Empty when untagged. */
	PCGEXCOREEDITOR_API void GetGroups(const FProperty& Property, TArray<FName>& OutGroups);
	PCGEXCOREEDITOR_API bool ListContains(const FProperty& Property, FName Group);

	/** Parent of a dotted id ("Variations" for "Variations.Offset"), None for a root id. */
	PCGEXCOREEDITOR_API FName GetParentGroup(FName Group);

	/** True when Group or one of its dotted ancestors satisfies Predicate. */
	PCGEXCOREEDITOR_API bool AnyInHierarchy(FName Group, TFunctionRef<bool(FName)> Predicate);

	/**
	 * True if Property is a struct, or a container of structs, one of whose DIRECT members lists Group.
	 * Deliberately not recursive: value types are embedded in unrelated hosts too, and deep tunneling
	 * would admit every such host.
	 */
	PCGEXCOREEDITOR_API bool HasTaggedDirectMember(const FProperty& Property, FName Group);

	/** The property when tagged, else the nearest tagged ancestor in the parent chain, else null. */
	PCGEXCOREEDITOR_API const FProperty* FindTaggedOwner(const FPropertyAndParent& PropertyAndParent);

	/** Rows over an external struct (FInstancedStruct children) have a chain root no UClass owns. */
	PCGEXCOREEDITOR_API bool IsExternalStructRow(const FPropertyAndParent& PropertyAndParent);

	/**
	 * Allow-list policy. Visible when the property lists Audience, a direct struct member does (nested
	 * surfacing: the host shell renders so its tagged member can), or, for a property with no list of its
	 * own, an ancestor is admitted; external-struct rows trust their host. An explicit list is the whole
	 * audience, ancestors never widen it. ExtraProperties admits untaggable engine properties by name.
	 */
	PCGEXCOREEDITOR_API bool IsVisibleForAudience(
		const FPropertyAndParent& PropertyAndParent, FName Audience, const TSet<FName>* ExtraProperties = nullptr);

	/**
	 * Deny-list policy. Untagged rows (no tagged owner in the chain) are visible; a tagged row stays
	 * visible while at least one of its groups is not hidden. IsGroupHidden owns the hierarchy rule.
	 */
	PCGEXCOREEDITOR_API bool IsVisibleUnlessHidden(
		const FPropertyAndParent& PropertyAndParent, TFunctionRef<bool(FName)> IsGroupHidden);

	/**
	 * Categories of admitted properties, split by how detail rows reference them: FullPaths holds real
	 * category names ("Valency|Spatial"), Segments the display names subcategory plumbing nodes report
	 * ("Spatial"). Named rows must only match full paths, or engine rows homed under broad parents leak in.
	 */
	struct PCGEXCOREEDITOR_API FCategoryPathSet
	{
		TSet<FName> FullPaths;
		TSet<FName> Segments;

		void Reset()
		{
			FullPaths.Reset();
			Segments.Reset();
		}

		/** Named rows (RowTag set) need a full-path parent; unnamed rows (subcategory plumbing, untagged
		 *  customization rows) may match either set. */
		bool IsCustomRowVisible(FName RowName, FName ParentName) const;
	};

	/** Categories of every property of Owner that Admit accepts. */
	PCGEXCOREEDITOR_API void GatherCategoryPaths(
		const UStruct* Owner, TFunctionRef<bool(const FProperty&)> Admit, FCategoryPathSet& OutCategories);

	/** GatherCategoryPaths with the IsVisibleForAudience admission, hosts of tagged members included. */
	PCGEXCOREEDITOR_API void GatherAudienceCategoryPaths(
		const UStruct* Owner, FName Audience, FCategoryPathSet& OutCategories, const TSet<FName>* ExtraProperties = nullptr);

	/** Every id tagged on Struct's properties, recursing into struct members and container inner types down
	 *  to MaxDepth; dotted ancestors are added too. */
	PCGEXCOREEDITOR_API void CollectGroups(const UStruct* Struct, TSet<FName>& OutGroups, int32 MaxDepth = 4);

	/** Non-shipping boot check: warns on tagged ids IsKnown rejects, over every UClass and UScriptStruct
	 *  OwnerFilter accepts. Catches literal drift between tags and the ids a host registers. */
	PCGEXCOREEDITOR_API void ValidateGroups(
		TFunctionRef<bool(const UStruct*)> OwnerFilter, TFunctionRef<bool(FName)> IsKnown, const TCHAR* Context);
}
