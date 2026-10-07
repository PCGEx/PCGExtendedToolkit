// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class UScriptStruct;
struct FPropertyAndParent;

/**
 * Footer filter groups of the collection editors: display info for PCGExCategoryGroup ids, discovery from
 * entry structs, and the hidden-state predicates every entry details view installs. Hidden state is
 * editor-wide (UPCGExCollectionsEditorSettings), by design: the toggles carry over between collections.
 */
namespace PCGExCollectionCategoryGroups
{
	/** Built-in ids. The entry UPROPERTY literals must match (see PCGExCategoryGroups::MetaKey). */
	namespace Ids
	{
		inline const FName Variations(TEXT("Variations"));
		inline const FName VariationsOffset(TEXT("Variations.Offset"));
		inline const FName VariationsRotation(TEXT("Variations.Rotation"));
		inline const FName VariationsScale(TEXT("Variations.Scale"));
		inline const FName Fitting(TEXT("Fitting"));
		inline const FName Tags(TEXT("Tags"));
		inline const FName Staging(TEXT("Staging"));
		inline const FName Grammar(TEXT("Grammar"));
		inline const FName Properties(TEXT("Properties"));
		inline const FName Materials(TEXT("Materials"));
		inline const FName Descriptors(TEXT("Descriptors"));
	}

	struct PCGEXCOLLECTIONSEDITOR_API FGroup
	{
		FName Id = NAME_None;
		FText Label;
		FText ToolTip;
		int32 SortOrder = 0;
	};

	/**
	 * Editor-wide display registry. Built-ins register from the module startup; out-of-module groups
	 * register from their editor module's StartupModule and MUST Unregister from ShutdownModule.
	 */
	class PCGEXCOLLECTIONSEDITOR_API FRegistry
	{
	public:
		static FRegistry& Get();

		/** Adds, or replaces the group with the same id. */
		void Register(const FGroup& Group);
		void Unregister(FName Id);
		bool Find(FName Id, FGroup& OutGroup) const;

		/** Registered ids whose dotted parent is Parent. */
		void GetChildren(FName Parent, TArray<FName>& OutIds) const;

	private:
		FRegistry() = default;

		mutable FRWLock RegistryLock;
		TMap<FName, FGroup> Groups;
	};

	PCGEXCOLLECTIONSEDITOR_API void RegisterBuiltInGroups();

	/**
	 * Groups a footer offers for these entry structs: every tagged id, its dotted ancestors and the
	 * registered children of each (sub-group rows have no property to discover), sorted by registry
	 * order. An unregistered id is still offered, labelled by its id, and logged once.
	 */
	PCGEXCOLLECTIONSEDITOR_API void DiscoverGroups(TConstArrayView<const UScriptStruct*> EntryStructs, TArray<FGroup>& OutGroups);

	/** Entry structs of every registered collection type: the scan for heterogeneous hosts. */
	PCGEXCOLLECTIONSEDITOR_API void GetAllRegisteredEntryStructs(TArray<const UScriptStruct*>& OutStructs);

	/** Hidden when the id or one of its dotted ancestors is in the settings' hidden set. */
	PCGEXCOLLECTIONSEDITOR_API bool IsGroupHidden(FName Group);

	/** Details-view delegates: untagged rows stay visible, tagged rows follow IsGroupHidden. */
	PCGEXCOLLECTIONSEDITOR_API bool IsPropertyVisible(const FPropertyAndParent& PropertyAndParent);
	PCGEXCOLLECTIONSEDITOR_API bool IsCustomRowVisible(FName RowName, FName ParentName);
}
