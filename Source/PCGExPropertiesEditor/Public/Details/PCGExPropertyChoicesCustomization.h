// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExPropertyChoiceHost.h"
#include "Framework/SlateDelegates.h"
#include "IPropertyTypeCustomization.h"

class FStructOnScope;
class IDetailsView;
class UPCGExPropertySchemaAsset;
struct FPCGExPropertyChoices;
struct FPCGExPropertyChoicesPatch;
struct FPCGExPropertySchemaCollection;

/**
 * One open choices popover: what it edits, the scratch copy its details view shows, and the single way that copy
 * reaches the real data. The edited data is only ever reached through property handles, checked on every use: the
 * panel that opened the popover can rebuild under it (undo, a refresh, a host that reseeds its view), and from then
 * on IsValid() is false and nothing here touches memory again.
 *
 * Declaration: the list a property declares. Patch: an importer's rewrite, stored on its import override entry.
 */
class PCGEXPROPERTIESEDITOR_API FPCGExChoicesEditSession : public TSharedFromThis<FPCGExChoicesEditSession>
{
public:
	/** ChoicesHandle is the FPCGExPropertyChoices member of the property Host leads to. */
	static TSharedRef<FPCGExChoicesEditSession> MakeForDeclaration(const TSharedRef<IPropertyHandle>& ChoicesHandle, const FPCGExPropertyHostRef& Host);

	/** EntryHandle is the import override entry; Host leads to its Value. */
	static TSharedRef<FPCGExChoicesEditSession> MakeForPatch(const TSharedRef<IPropertyHandle>& EntryHandle, const FPCGExPropertyHostRef& Host);

	bool IsPatch() const
	{
		return bPatch;
	}

	bool IsValid() const;

	/** The property the choices answer to; invalid once the session is. */
	FStructView ResolveHost() const;

	/** The single object edited; null for none or several. */
	UObject* GetOwner() const
	{
		return Owner.Get();
	}

	/** Root struct shown by the popover's view: FPCGExPropertyChoicesEditTarget or FPCGExPropertyChoicesPatchEditTarget. */
	TSharedRef<FStructOnScope> GetScratch() const
	{
		return Scratch.ToSharedRef();
	}

	/** The edited copy; null in the other mode. */
	FPCGExPropertyChoices* GetScratchChoices() const;
	FPCGExPropertyChoicesPatch* GetScratchPatch() const;

	/** Patch only: what this importer inherits, resolved without its own layer. False when it cannot be resolved. */
	bool GetInheritedChoices(FPCGExPropertyChoices& Out) const;

	/** Name the edited property resolves under. */
	FName GetPropertyName() const;

	/** The schema asset declaring the edited property; null when the owner declares it itself and is not one. */
	const UPCGExPropertySchemaAsset* GetDeclaringAsset() const;

	/**
	 * Write the scratch to the real data in one transaction, through the same pre / post-change bracket as a typed
	 * edit, then take back what the host made of it. Nothing happens, and nothing is notified, when the scratch
	 * already equals the real data. True if something was written.
	 */
	bool Commit(const FText& Description);

	/** Commit for an action that reshaped the scratch by hand: the view is told to rebuild either way. */
	void CommitAction(const FText& Description);

	/** Take the real data into the scratch. True if the scratch changed. */
	bool PullFromReal();

	/** The scratch no longer matches the rows the view built: rebuild them. */
	FSimpleMulticastDelegate OnScratchChanged;

private:
	FPCGExChoicesEditSession() = default;

	/** The one edited instance behind TargetHandle; null when invalid. */
	void* GetTargetRaw() const;
	FPCGExPropertySchemaCollection* ResolveCollection() const;
	bool ScratchEqualsReal() const;
	void WriteReal(void* Raw) const;

	bool bPatch = false;
	/** Declaration: the property's Choices. Patch: the import override entry. */
	TSharedPtr<IPropertyHandle> TargetHandle;
	FPCGExPropertyHostRef Host;
	/** Patch only: the FPCGExPropertySchemaCollection whose import layer holds the entry. */
	TSharedPtr<IPropertyHandle> CollectionHandle;
	TWeakObjectPtr<UObject> Owner;

	TSharedPtr<FStructOnScope> Scratch;
	/** The edited struct inside the scratch root. */
	void* ScratchMember = nullptr;
	bool bCommitting = false;
};

namespace PCGExPropertyChoicesUI
{
	/** True when an ancestor of Handle carries ReadOnlySchema (values-only hosts): nothing under it authors a schema. */
	PCGEXPROPERTIESEDITOR_API bool IsUnderReadOnlySchema(const TSharedRef<IPropertyHandle>& Handle);

	/** Popover content authoring the choices the property Host leads to declares. Unbound without a handle. */
	PCGEXPROPERTIESEDITOR_API FOnGetContent MakeDeclarationEditor(const TSharedPtr<IPropertyHandle>& ChoicesHandle, const FPCGExPropertyHostRef& Host);

	/** Popover content authoring an import override entry's choices patch. Unbound without a handle. */
	PCGEXPROPERTIESEDITOR_API FOnGetContent MakePatchEditor(const TSharedPtr<IPropertyHandle>& EntryHandle, const FPCGExPropertyHostRef& Host);

	/** Register the customizations of the choices structs on View, each bound to Session. For the popover's own view. */
	void RegisterSessionLayouts(IDetailsView& View, const TSharedRef<FPCGExChoicesEditSession>& Session);

	/** What the choices structs get in every other view: no row. They are authored in the popover only. */
	TSharedRef<IPropertyTypeCustomization> MakeHiddenCustomization();
}
