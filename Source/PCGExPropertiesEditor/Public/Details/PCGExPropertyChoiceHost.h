// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "StructUtils/StructView.h"
#include "Templates/SharedPointer.h"

class FStructOnScope;
class IPropertyHandle;
struct FPCGExPropertyChoice;

/** How a property handle leads to the property struct a row edits. */
enum class EPCGExPropertyHostKind : uint8
{
	/** The handle's value is the property struct itself. */
	Property,
	/** The handle's value is the FInstancedStruct holding it. */
	Instanced,
	/** The handle's value is an FPCGExPropertyOverrideEntry; the host is its Value. */
	Entry,
	/** No handle: a property struct in a scope the row's owner holds for the row's whole lifetime. */
	Owned,
};

/**
 * Reaches the property struct a row edits through a property handle, on demand. A host can be freed and reallocated
 * between two frames (undo, a row sync, a retype): the handle either follows it or reports that it could not, which a
 * cached pointer does not. Build it from a handle rooted in the edited object, never from an external-structure row's
 * own handle: that one aliases the very memory this exists not to trust. The one scope form is for a display copy the
 * row's owner allocates itself and outlives the row with.
 */
class PCGEXPROPERTIESEDITOR_API FPCGExPropertyHostRef
{
public:
	FPCGExPropertyHostRef() = default;
	FPCGExPropertyHostRef(const TSharedPtr<IPropertyHandle>& InHandle, EPCGExPropertyHostKind InKind);
	explicit FPCGExPropertyHostRef(const TSharedPtr<FStructOnScope>& InOwnedScope);

	bool IsSet() const
	{
		return Handle.IsValid() || OwnedScope.IsValid();
	}

	/** The host as it is now; invalid when the handle is gone, edits several objects, or holds no property. */
	FStructView Resolve() const;

	const TSharedPtr<IPropertyHandle>& GetHandle() const
	{
		return Handle;
	}

	/** The raw value behind Handle when it is valid and edits exactly one instance; null otherwise. */
	static void* GetSingleRawData(const TSharedPtr<IPropertyHandle>& Handle);

private:
	TSharedPtr<IPropertyHandle> Handle;
	TSharedPtr<FStructOnScope> OwnedScope;
	EPCGExPropertyHostKind Kind = EPCGExPropertyHostKind::Property;
};

/** A row's choice state, read off its host at most once per Slate tick and shared by every widget of that row. */
class PCGEXPROPERTIESEDITOR_API FPCGExChoiceRowState
{
public:
	struct FSnapshot
	{
		bool bHostValid = false;
		bool bHasChoices = false;
		bool bLocked = false;

		/** Index of the choice the value stands on (the bound one, else the first match); INDEX_NONE for a custom value. */
		int32 Selected = INDEX_NONE;

		/** The value is bound to Selected: it follows that choice's edits. */
		bool bBound = false;

		/** Selected's label; empty for a custom value. */
		FText SelectedLabel;

		/** Preview of the host's value. Only filled while locked, the one state that shows it. */
		FText ValuePreview;
	};

	explicit FPCGExChoiceRowState(const FPCGExPropertyHostRef& InHost);

	const FSnapshot& Get() const;

	const FPCGExPropertyHostRef& GetHost() const
	{
		return Host;
	}

	/** What a choice is called on screen: its Label, or a placeholder when it has none. */
	static FText LabelOf(const FPCGExPropertyChoice& Choice);

private:
	FPCGExPropertyHostRef Host;
	mutable FSnapshot Snapshot;
	mutable double SnapshotTime = -1.0;
};

/** What a value row needs to carry choices. Host unset: the row shows no choices UI at all. */
struct FPCGExChoiceRowArgs
{
	FPCGExPropertyHostRef Host;

	/** Bound: the arrow's menu leads with "Edit Choices..." and the arrow shows even without choices. */
	FOnGetContent EditChoices;

	/** For a type whose value editor also edits its structure, on the row that declares it: locked keeps the editor. */
	bool bKeepEditorWhenLocked = false;
};

namespace PCGExPropertyChoiceHost
{
	/** The inner struct and memory of the FInstancedStruct that is Handle's first instance; false when there is none. */
	bool AccessInstancedStruct(const TSharedPtr<IPropertyHandle>& Handle, UScriptStruct*& OutStruct, uint8*& OutMemory);
}
