// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FScopedTransaction;
class IPropertyHandle;

/**
 * Edit protocol for widgets that write a property's raw memory instead of calling IPropertyHandle::SetValue
 * (struct values that have to change in one step): a transaction around pre-change, the write and post-change,
 * then the finished notify once the edit is complete.
 *
 *   interactive: BeginInteractive, any number of ApplyInteractive, EndInteractive. One transaction; the closing
 *                ValueSet + finished notifies are sent even on an unchanged value, because hosts that edit a
 *                scratch struct only commit on the finished notify.
 *   discrete:    Commit. Its own transaction.
 *
 * An SSpinBox can be wired to it as is. It reports changes outside a slider movement (wheel, late spin ticks,
 * per-key text): ApplyInteractive drops those. It commits right before closing a movement: Commit inside a
 * gesture ends that gesture, which also recovers one whose widget lost mouse capture without reporting the end.
 *
 * A mutator runs once per edited instance, on that instance's raw value.
 */
class PCGEXCOREEDITOR_API FPCGExRawPropertyEdit
{
public:
	UE_NONCOPYABLE(FPCGExRawPropertyEdit)

	explicit FPCGExRawPropertyEdit(const TSharedRef<IPropertyHandle>& InHandle);
	~FPCGExRawPropertyEdit();

	const TSharedRef<IPropertyHandle>& GetHandle() const
	{
		return Handle;
	}

	bool IsInteractive() const
	{
		return Gesture.IsValid();
	}

	void BeginInteractive(const FText& Description);

	/** Dropped outside a gesture. */
	void ApplyInteractive(TFunctionRef<void(void*)> Mutator);
	void EndInteractive(TFunctionRef<void(void*)> Mutator);

	/** Inside a gesture this is EndInteractive: the commit closes it, and Description is not used. */
	void Commit(const FText& Description, TFunctionRef<void(void*)> Mutator);

	/** Visits the raw value of every edited instance. Sends nothing. */
	void ForEachRawValue(TFunctionRef<void(void*)> Visitor) const;

private:
	TSharedRef<IPropertyHandle> Handle;

	/** The open gesture's transaction; null outside a gesture. */
	TUniquePtr<FScopedTransaction> Gesture;
};
