// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExRawPropertyEdit.h"

#include "Editor.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"

FPCGExRawPropertyEdit::FPCGExRawPropertyEdit(const TSharedRef<IPropertyHandle>& InHandle)
	: Handle(InHandle)
{
}

FPCGExRawPropertyEdit::~FPCGExRawPropertyEdit()
{
	// A panel rebuild can drop the editing widgets mid-gesture; the transaction they opened must not outlive them.
	if (bInteractive && GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FPCGExRawPropertyEdit::BeginInteractive(const FText& Description)
{
	if (bInteractive || !Handle->IsValidHandle())
	{
		return;
	}
	bInteractive = true;
	if (GEditor)
	{
		GEditor->BeginTransaction(Description);
	}
}

void FPCGExRawPropertyEdit::ApplyInteractive(const TFunctionRef<void(void*)> Mutator)
{
	if (bInteractive)
	{
		Write(Mutator, EPropertyChangeType::Interactive);
	}
}

void FPCGExRawPropertyEdit::EndInteractive(const TFunctionRef<void(void*)> Mutator)
{
	if (!bInteractive)
	{
		return;
	}
	bInteractive = false;
	Write(Mutator, EPropertyChangeType::ValueSet);
	Handle->NotifyFinishedChangingProperties();
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FPCGExRawPropertyEdit::Commit(const FText& Description, const TFunctionRef<void(void*)> Mutator)
{
	// Nothing to edit means no transaction either.
	bool bAnyInstance = false;
	ForEachRawValue([&bAnyInstance](void*) { bAnyInstance = true; });
	if (!bAnyInstance)
	{
		return;
	}

	const FScopedTransaction Transaction(Description);
	Write(Mutator, EPropertyChangeType::ValueSet);
	Handle->NotifyFinishedChangingProperties();
}

void FPCGExRawPropertyEdit::ForEachRawValue(const TFunctionRef<void(void*)> Visitor) const
{
	if (!Handle->IsValidHandle())
	{
		return;
	}

	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	for (void* Raw : RawData)
	{
		if (Raw)
		{
			Visitor(Raw);
		}
	}
}

void FPCGExRawPropertyEdit::Write(const TFunctionRef<void(void*)> Mutator, const EPropertyChangeType::Type ChangeType)
{
	if (!Handle->IsValidHandle())
	{
		return;
	}

	Handle->NotifyPreChange();
	ForEachRawValue(Mutator);
	Handle->NotifyPostChange(ChangeType);
}
