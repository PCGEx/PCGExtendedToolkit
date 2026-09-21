// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPtr.h"

class IPropertyHandle;
class SWidget;
class UPCGExAssetCollection;

/**
 * Inline editor for an FPCGExCollectionEntryRef handle: collection asset box, lock toggle, and an entry
 * dropdown with a live thumbnail. Every FPCGExCollectionEntryRef in the editor renders through it -- the
 * struct customization (FPCGExCollectionEntryRefCustomization) for bare struct properties, the inline widget
 * registry for FPCGExProperty_CollectionEntry::Value.
 *
 * Options come from the hosting property, not the struct: a module owning a property of this type registers
 * an options provider keyed by (owner struct, property name) and the widget resolves it from the handle.
 */
namespace PCGExCollectionEntryPickerWidget
{
	struct PCGEXCOLLECTIONSEDITOR_API FOptions
	{
		/** Schema authoring: the collection box always shows. Off (override rows): only while the schema left it unlocked. */
		bool bSchemaEdit = true;

		/** Show the lock toggle (schema-edit only). Off where the schema/override split has no meaning. */
		bool bShowLock = true;

		/** Entry types a pick may resolve to (registry lineage). Empty = any. Incompatible entries dim; the
		 *  collection box only offers hosts that can hold one. */
		TArray<PCGExAssetCollection::FTypeId> AllowedEntryTypes;

		/** Declared source collections. Non-empty replaces the collection box with one menu over every source's
		 *  entries, grouped by source; a pick writes both fields. */
		TArray<TSoftObjectPtr<UPCGExAssetCollection>> Sources;
	};

	/** Fills Out for the given ref handle. Called at widget build; may read the host collection through the handle. */
	using FOptionsProvider = TFunction<void(const TSharedRef<IPropertyHandle>&, FOptions&)>;

	/** Keyed by owning struct name + property name (names, not pointers: the owner may live in a module that unloads). */
	PCGEXCOLLECTIONSEDITOR_API void RegisterOptionsProvider(FName OwnerStructName, FName PropertyName, FOptionsProvider Provider);
	PCGEXCOLLECTIONSEDITOR_API void UnregisterOptionsProvider(FName OwnerStructName, FName PropertyName);

	/** The registered provider's options for this handle, else defaults. */
	PCGEXCOLLECTIONSEDITOR_API FOptions ResolveOptions(const TSharedRef<IPropertyHandle>& ValueHandle);

	PCGEXCOLLECTIONSEDITOR_API TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, const FOptions& Options);

	/** Default options with bSchemaEdit set -- the FPCGExProperty_CollectionEntry inline widget entry points. */
	PCGEXCOLLECTIONSEDITOR_API TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle, bool bSchemaEdit);
}
