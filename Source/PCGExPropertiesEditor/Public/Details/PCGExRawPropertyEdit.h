// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealType.h"

class IPropertyHandle;

/**
 * Edit protocol for widgets that write a property's raw memory instead of calling IPropertyHandle::SetValue
 * (struct values that have to change in one step). It opens the transaction and sends the pre-change,
 * post-change and finished notifies that SetValue would:
 *
 *   interactive: BeginInteractive, any number of ApplyInteractive, EndInteractive. One transaction; the closing
 *                ValueSet + finished notifies are sent even on an unchanged value, because hosts that edit a
 *                scratch struct only commit on the finished notify.
 *   discrete:    Commit. Its own transaction.
 *
 * A mutator runs once per edited instance, on that instance's raw value.
 */
class PCGEXPROPERTIESEDITOR_API FPCGExRawPropertyEdit
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
		return bInteractive;
	}

	void BeginInteractive(const FText& Description);
	void ApplyInteractive(TFunctionRef<void(void*)> Mutator);
	void EndInteractive(TFunctionRef<void(void*)> Mutator);
	void Commit(const FText& Description, TFunctionRef<void(void*)> Mutator);

	/** Visits the raw value of every edited instance. Sends nothing. */
	void ForEachRawValue(TFunctionRef<void(void*)> Visitor) const;

private:
	void Write(TFunctionRef<void(void*)> Mutator, EPropertyChangeType::Type ChangeType);

	TSharedRef<IPropertyHandle> Handle;
	bool bInteractive = false;
};
