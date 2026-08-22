// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyFloatPacker.h"

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExPropertyFloatPacker)

#pragma region FPCGExPackedFloatLayout

#if WITH_EDITOR
int32 FPCGExPackedFloatLayout::EDITOR_PopulateFromSchemaAsset(const UPCGExPropertySchemaAsset* InAsset)
{
	if (!InAsset)
	{
		return 0;
	}

	TArray<FPCGExPropertyResolved> Resolved;
	InAsset->Collection.Resolve(Resolved);

	int32 AddedCount = 0;

	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		if (!Entry.Source || Entry.Source->Name.IsNone())
		{
			continue;
		}

		if (Slots.ContainsByPredicate([&Entry](const FPCGExPackedFloatSlot& In)
		{
			return In.PropertyName == Entry.Source->Name;
		}))
		{
			continue;
		}

		FPCGExPackedFloatSlot& NewSlot = Slots.AddDefaulted_GetRef();
		NewSlot.PropertyName = Entry.Source->Name;
		AddedCount++;
	}

	return AddedCount;
}
#endif

#pragma endregion

#pragma region FPCGExPropertyFloatPacker

bool FPCGExPropertyFloatPacker::Initialize(
	FPCGContext* InContext,
	const IPCGExPropertyProvider* InProvider,
	const FPCGExPackedFloatLayout& Layout,
	const int32 MaxFloats)
{
	Slots.Reset();
	Defaults.Reset();

	if (!InProvider || Layout.Slots.IsEmpty())
	{
		return false;
	}

	Slots.Reserve(Layout.Slots.Num());

	// Only ever moves forward, which is what makes an out-of-order pinned offset detectable.
	int32 Cursor = 0;

	// Named rather than flagged so the escalation below can point at the row that caused it -- it
	// is the one message left once missing-property warnings are quieted.
	FName FirstDroppedName = NAME_None;

	for (const FPCGExPackedFloatSlot& Row : Layout.Slots)
	{
		if (!Row.bEnabled || Row.PropertyName.IsNone())
		{
			continue;
		}

		const bool bPinned = Row.Offset >= 0;

		// An auto row inherits its position from everything above it, so a hole up there moves it.
		if (!bPinned && !FirstDroppedName.IsNone())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' is auto-positioned, but '%s' above it could not be resolved and would move it. Pin '%s' to a float index, or fix '%s'."),
				           *Row.PropertyName.ToString(), *FirstDroppedName.ToString(), *Row.PropertyName.ToString(), *FirstDroppedName.ToString())));
			Slots.Reset();
			Defaults.Reset();
			return false;
		}

		const int32 Start = bPinned ? Row.Offset : Cursor;

		if (Start < Cursor)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' is pinned to float %d, which overlaps the slot above it (next free float is %d)."),
				           *Row.PropertyName.ToString(), Row.Offset, Cursor)));
			Slots.Reset();
			Defaults.Reset();
			return false;
		}

		const FInstancedStruct* Prototype = InProvider->FindPrototypeProperty(Row.PropertyName);
		const FPCGExProperty* PrototypeProp = Prototype ? Prototype->GetPtr<FPCGExProperty>() : nullptr;
		if (!PrototypeProp)
		{
			if (!Layout.bQuietMissingPropertyWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext,
				           FText::FromString(FString::Printf(TEXT("Property '%s' not found in any schema source; its floats are left at zero. Enable 'Quiet Missing Property Warnings' to silence this."), *Row.PropertyName.ToString())));
			}
			Cursor = Start;
			if (FirstDroppedName.IsNone())
			{
				FirstDroppedName = Row.PropertyName;
			}
			continue;
		}

		const int32 Count = PrototypeProp->GetPackedFloatCount();
		if (Count <= 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' (%s) has no float representation; its floats are left at zero."),
				           *Row.PropertyName.ToString(), *PrototypeProp->GetDisplayTypeName().ToString())));
			Cursor = Start;
			if (FirstDroppedName.IsNone())
			{
				FirstDroppedName = Row.PropertyName;
			}
			continue;
		}

		if (Start + Count > MaxFloats)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' would occupy floats %d..%d, past the %d-float budget; its floats are left at zero."),
				           *Row.PropertyName.ToString(), Start, Start + Count - 1, MaxFloats)));
			Cursor = Start;
			if (FirstDroppedName.IsNone())
			{
				FirstDroppedName = Row.PropertyName;
			}
			continue;
		}

		// Grows through any gap a pinned offset left behind, zero-filling it.
		Defaults.SetNumZeroed(Start + Count);

		// A packable GetOutputType without the value-read interface reports a width but yields
		// nothing; keeping that slot would bake a permanent zero and displace everything after it.
		if (!PrototypeProp->PackFloats(TArrayView<float>(Defaults.GetData() + Start, Count)))
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::FromString(FString::Printf(TEXT("Property '%s' (%s) reports %d float(s) but produced no value; its floats are left at zero."),
				           *Row.PropertyName.ToString(), *PrototypeProp->GetDisplayTypeName().ToString(), Count)));
			Cursor = Start;
			if (FirstDroppedName.IsNone())
			{
				FirstDroppedName = Row.PropertyName;
			}
			continue;
		}

		FSlot& Slot = Slots.Emplace_GetRef();
		Slot.PropertyName = Row.PropertyName;
		Slot.StructType = Prototype->GetScriptStruct();
#if WITH_EDITORONLY_DATA
		Slot.TypeName = PrototypeProp->GetDisplayTypeName();
#endif
		Slot.Offset = Start;
		Slot.Count = Count;

		Cursor = Start + Count;
	}

	return !Slots.IsEmpty();
}

void FPCGExPropertyFloatPacker::Pack(TArray<float>& OutFloats, TFunctionRef<const FInstancedStruct*(FName)> ResolveSource) const
{
	// One copy seeds every slot with its prototype value; resolved slots then overwrite theirs.
	OutFloats = Defaults;

	for (const FSlot& Slot : Slots)
	{
		const FInstancedStruct* Source = ResolveSource(Slot.PropertyName);
		if (!Source)
		{
			continue;
		}

		// Vector and Rotator both pack 3 floats but in different component orders, so a width
		// check would reinterpret one as the other.
		if (Source->GetScriptStruct() != Slot.StructType)
		{
			continue;
		}

		if (const FPCGExProperty* SourceProp = Source->GetPtr<FPCGExProperty>())
		{
			SourceProp->PackFloats(TArrayView<float>(OutFloats.GetData() + Slot.Offset, Slot.Count));
		}
	}
}

#if WITH_EDITOR
FString FPCGExPropertyFloatPacker::DescribeLayout() const
{
	FString Result;
	for (const FSlot& Slot : Slots)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(" | ");
		}

		if (Slot.Count == 1)
		{
			Result += FString::Printf(TEXT("[%d] %s (%s)"), Slot.Offset, *Slot.PropertyName.ToString(), *Slot.TypeName.ToString());
		}
		else
		{
			Result += FString::Printf(TEXT("[%d..%d] %s (%s)"), Slot.Offset, Slot.Offset + Slot.Count - 1, *Slot.PropertyName.ToString(), *Slot.TypeName.ToString());
		}
	}
	return Result;
}
#endif

#pragma endregion
