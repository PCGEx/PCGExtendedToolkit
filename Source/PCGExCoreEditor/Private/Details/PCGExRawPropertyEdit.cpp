// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExRawPropertyEdit.h"

#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Containers/Array.h"
#include "UObject/UnrealType.h"

namespace PCGExRawPropertyEdit
{
	using FInstances = TArray<void*, TInlineAllocator<4>>;

	/** Raw value of every edited instance. */
	FInstances Gather(const TSharedRef<IPropertyHandle>& Handle)
	{
		FInstances Instances;

		// EnumerateRawData dereferences the handle's property node unchecked.
		if (Handle->IsValidHandle())
		{
			Handle->EnumerateRawData([&Instances](void* Raw, const int32, const int32)
			{
				if (Raw)
				{
					Instances.Add(Raw);
				}
				return true;
			});
		}
		return Instances;
	}

	/**
	 * Pre-change, the write, post-change. False, with nothing sent, when there is no instance to write.
	 * Instances are gathered before the pre-change notify, as IPropertyHandle::SetValue gathers its own.
	 */
	bool Send(const TSharedRef<IPropertyHandle>& Handle, const FInstances& Instances, const TFunctionRef<void(void*)> Mutator, const EPropertyChangeType::Type ChangeType)
	{
		if (Instances.IsEmpty())
		{
			return false;
		}

		Handle->NotifyPreChange();
		for (void* Raw : Instances)
		{
			Mutator(Raw);
		}
		Handle->NotifyPostChange(ChangeType);
		return true;
	}
}

FPCGExRawPropertyEdit::FPCGExRawPropertyEdit(const TSharedRef<IPropertyHandle>& InHandle)
	: Handle(InHandle)
{
}

// Out of line: Gesture's deleter needs FScopedTransaction complete. Releasing it closes a gesture whose
// widgets a panel rebuild dropped before they could end it.
FPCGExRawPropertyEdit::~FPCGExRawPropertyEdit() = default;

void FPCGExRawPropertyEdit::BeginInteractive(const FText& Description)
{
	if (!Gesture && Handle->IsValidHandle())
	{
		Gesture = MakeUnique<FScopedTransaction>(Description);
	}
}

void FPCGExRawPropertyEdit::ApplyInteractive(const TFunctionRef<void(void*)> Mutator)
{
	using namespace PCGExRawPropertyEdit;

	if (Gesture)
	{
		Send(Handle, Gather(Handle), Mutator, EPropertyChangeType::Interactive);
	}
}

void FPCGExRawPropertyEdit::EndInteractive(const TFunctionRef<void(void*)> Mutator)
{
	using namespace PCGExRawPropertyEdit;

	// Out of the member first: the gesture is over for whatever its closing notifies shake loose.
	const TUniquePtr<FScopedTransaction> Closing = MoveTemp(Gesture);
	if (Closing && Send(Handle, Gather(Handle), Mutator, EPropertyChangeType::ValueSet))
	{
		Handle->NotifyFinishedChangingProperties();
	}
}

void FPCGExRawPropertyEdit::Commit(const FText& Description, const TFunctionRef<void(void*)> Mutator)
{
	using namespace PCGExRawPropertyEdit;

	// SSpinBox commits right before it reports the end of a slider movement, so a commit inside a gesture is
	// that gesture's end. Taken as such, it also closes a gesture whose widget lost mouse capture and never
	// reported one, which would otherwise hold the editor's transaction open.
	if (Gesture)
	{
		EndInteractive(Mutator);
		return;
	}

	// Nothing to edit means no transaction either.
	const FInstances Instances = Gather(Handle);
	if (Instances.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(Description);
	Send(Handle, Instances, Mutator, EPropertyChangeType::ValueSet);
	Handle->NotifyFinishedChangingProperties();
}

void FPCGExRawPropertyEdit::ForEachRawValue(const TFunctionRef<void(void*)> Visitor) const
{
	for (void* Raw : PCGExRawPropertyEdit::Gather(Handle))
	{
		Visitor(Raw);
	}
}
