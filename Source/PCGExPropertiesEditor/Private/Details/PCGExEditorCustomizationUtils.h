// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PropertyHandle.h"

namespace PCGExEditorCustomizationUtils
{
	// External-FStructOnScope rows write directly to struct memory and bypass the owner's
	// PostEditChangeProperty pipeline. Hook the row's value-change to (1) Modify() the owner
	// for dirty + transaction, then (2) broadcast OnObjectPropertyChanged directly -- the
	// signal PCG asset tracking listens to. Routing through PostEditChangeProperty instead
	// would trigger the heavy structural-rebuild path for what is just a value commit.
	//
	// Uses SetOnPropertyValueChangedWithData (not SetOnPropertyValueChanged) so we can read
	// ChangeType and skip Interactive ticks -- drag-in-progress events shouldn't tick-storm
	// every listener of the global OnObjectPropertyChanged delegate.
	//
	// Registered on both the own- and child-changed delegates: multi-component widgets
	// (Vector / Rotator) commit through their X/Y/Z child handles, which fire only the parent's
	// child-changed delegate (FPropertyNode::BroadcastPropertyChangedDelegates), never its own --
	// so without the child registration those rows never dirty the owner. No double-fire: one edit
	// hits one node, and no call site hooks a node together with one of its ancestors.
	inline void HookOwnerChangeOnHandleChanged(
		const TSharedPtr<IPropertyHandle>& Handle,
		const TWeakObjectPtr<UObject>& WeakOwner)
	{
		if (!Handle.IsValid())
		{
			return;
		}

		// One handler registered on both delegates (the setters copy it).
		const TDelegate<void(const FPropertyChangedEvent&)> OnChanged =
			TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(
				[WeakOwner](const FPropertyChangedEvent& InEvent)
				{
					if (InEvent.ChangeType == EPropertyChangeType::Interactive)
					{
						return;
					}
					UObject* Live = WeakOwner.Get();
					if (!Live)
					{
						return;
					}
					Live->Modify();
					FPropertyChangedEvent Event(InEvent.Property, EPropertyChangeType::ValueSet);
					FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Live, Event);
				});

		Handle->SetOnPropertyValueChangedWithData(OnChanged);
		Handle->SetOnChildPropertyValueChangedWithData(OnChanged);
	}
}
