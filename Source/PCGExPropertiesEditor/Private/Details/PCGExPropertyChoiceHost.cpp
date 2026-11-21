// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyChoiceHost.h"

#include "Framework/Application/SlateApplication.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "UObject/StructOnScope.h"

#define LOCTEXT_NAMESPACE "PCGExPropertyChoiceHost"

namespace PCGExPropertyChoiceHost
{
	FStructView ViewOfProperty(FInstancedStruct& Instance)
	{
		return Instance.GetPtr<FPCGExProperty>() ? FStructView(Instance) : FStructView();
	}

	bool AccessInstancedStruct(const TSharedPtr<IPropertyHandle>& Handle, UScriptStruct*& OutStruct, uint8*& OutMemory)
	{
		if (!Handle.IsValid())
		{
			return false;
		}

		TArray<void*> RawData;
		Handle->AccessRawData(RawData);
		if (RawData.IsEmpty() || !RawData[0])
		{
			return false;
		}

		FInstancedStruct* Instance = static_cast<FInstancedStruct*>(RawData[0]);
		if (!Instance || !Instance->IsValid())
		{
			return false;
		}

		UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(Instance->GetScriptStruct());
		uint8* StructMemory = Instance->GetMutableMemory();
		if (!InnerStruct || !StructMemory)
		{
			return false;
		}

		OutStruct = InnerStruct;
		OutMemory = StructMemory;
		return true;
	}
}

#pragma region FPCGExPropertyHostRef

FPCGExPropertyHostRef::FPCGExPropertyHostRef(const TSharedPtr<IPropertyHandle>& InHandle, const EPCGExPropertyHostKind InKind)
	: Handle(InHandle)
	, Kind(InKind)
{
}

FPCGExPropertyHostRef::FPCGExPropertyHostRef(const TSharedPtr<FStructOnScope>& InOwnedScope)
	: OwnedScope(InOwnedScope)
	, Kind(EPCGExPropertyHostKind::Owned)
{
}

void* FPCGExPropertyHostRef::GetSingleRawData(const TSharedPtr<IPropertyHandle>& Handle)
{
	// EnumerateRawData dereferences the handle's property node unchecked.
	if (!Handle.IsValid() || !Handle->IsValidHandle())
	{
		return nullptr;
	}

	void* Raw = nullptr;
	int32 NumInstances = 0;
	Handle->EnumerateRawData([&Raw, &NumInstances](void* Data, const int32 /*Index*/, const int32 Num)
	{
		Raw = Data;
		NumInstances = Num;
		return false;
	});
	return NumInstances == 1 ? Raw : nullptr;
}

FStructView FPCGExPropertyHostRef::Resolve() const
{
	if (Kind == EPCGExPropertyHostKind::Owned)
	{
		const UScriptStruct* Struct = OwnedScope.IsValid() ? Cast<UScriptStruct>(OwnedScope->GetStruct()) : nullptr;
		return Struct && Struct->IsChildOf(FPCGExProperty::StaticStruct())
			? FStructView(Struct, OwnedScope->GetStructMemory())
			: FStructView();
	}

	void* Raw = GetSingleRawData(Handle);
	if (!Raw)
	{
		return FStructView();
	}

	switch (Kind)
	{
	case EPCGExPropertyHostKind::Property:
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(Handle->GetProperty());
			const UScriptStruct* Struct = StructProperty ? StructProperty->Struct.Get() : nullptr;
			return Struct && Struct->IsChildOf(FPCGExProperty::StaticStruct())
				? FStructView(Struct, static_cast<uint8*>(Raw))
				: FStructView();
		}
	case EPCGExPropertyHostKind::Instanced:
		return PCGExPropertyChoiceHost::ViewOfProperty(*static_cast<FInstancedStruct*>(Raw));
	case EPCGExPropertyHostKind::Entry:
		return PCGExPropertyChoiceHost::ViewOfProperty(static_cast<FPCGExPropertyOverrideEntry*>(Raw)->Value);
	default:
		checkNoEntry();
		return FStructView();
	}
}

#pragma endregion

#pragma region FPCGExChoiceRowState

FPCGExChoiceRowState::FPCGExChoiceRowState(const FPCGExPropertyHostRef& InHost)
	: Host(InHost)
{
}

const FPCGExChoiceRowState::FSnapshot& FPCGExChoiceRowState::Get() const
{
	// Slate's clock, not GFrameCounter: a modal window ticks its widgets without an engine frame going by.
	const double Now = FSlateApplication::Get().GetCurrentTime();
	if (SnapshotTime == Now)
	{
		return Snapshot;
	}
	SnapshotTime = Now;
	Snapshot = FSnapshot();

	const FStructView View = Host.Resolve();
	const FPCGExProperty* Property = View.GetPtr<FPCGExProperty>();
	if (!Property)
	{
		return Snapshot;
	}

	Snapshot.bHostValid = true;
	Snapshot.bHasChoices = !Property->Choices.Items.IsEmpty();
	Snapshot.bLocked = Snapshot.bHasChoices && Property->Choices.bLocked;
	if (!Snapshot.bHasChoices)
	{
		return Snapshot;
	}

	const int32 Bound = PCGExProperties::FindBoundChoice(View, Property->Choices);
	Snapshot.bBound = Bound != INDEX_NONE;
	Snapshot.Selected = Snapshot.bBound ? Bound : PCGExProperties::FindMatchingChoice(View, Property->Choices);
	if (Snapshot.Selected != INDEX_NONE)
	{
		Snapshot.SelectedLabel = LabelOf(Property->Choices.Items[Snapshot.Selected]);
	}
	if (Snapshot.bLocked)
	{
		Snapshot.ValuePreview = PCGExProperties::GetValuePreviewText(View);
	}
	return Snapshot;
}

FText FPCGExChoiceRowState::LabelOf(const FPCGExPropertyChoice& Choice)
{
	return Choice.Label.IsEmpty() ? LOCTEXT("UnnamedChoice", "(unnamed)") : FText::FromString(Choice.Label);
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
