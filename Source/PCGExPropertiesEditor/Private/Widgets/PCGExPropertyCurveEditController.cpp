// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#include "PCGExPropertyCurveEditController.h"

FPCGExPropertyCurveEditController::FPCGExPropertyCurveEditController(const TSharedRef<FRichCurve>& InCurve)
	: Curve(InCurve)
{
	RefitValueFrame();
	RefitTimeFrame();
}

#pragma region Index <-> handle

FKeyHandle FPCGExPropertyCurveEditController::GetKeyHandle(int32 Index) const
{
	if (!Curve->Keys.IsValidIndex(Index))
	{
		return FKeyHandle::Invalid();
	}

	// GetKeyHandleIterator yields handles in key (index) order.
	int32 i = 0;
	for (auto It = Curve->GetKeyHandleIterator(); It; ++It, ++i)
	{
		if (i == Index)
		{
			return *It;
		}
	}
	return FKeyHandle::Invalid();
}

int32 FPCGExPropertyCurveEditController::GetKeyIndex(FKeyHandle Handle) const
{
	return Curve->IsKeyHandleValid(Handle) ? Curve->GetIndexSafe(Handle) : INDEX_NONE;
}

#pragma endregion

#pragma region Reads

float FPCGExPropertyCurveEditController::GetKeyTimeAt(int32 Index) const
{
	return Curve->Keys.IsValidIndex(Index) ? Curve->Keys[Index].Time : 0.0f;
}

float FPCGExPropertyCurveEditController::GetKeyValueAt(int32 Index) const
{
	return Curve->Keys.IsValidIndex(Index) ? Curve->Keys[Index].Value : 0.0f;
}

FKeyHandle FPCGExPropertyCurveEditController::GetKeyHandleAt(int32 Index) const
{
	return GetKeyHandle(Index);
}

float FPCGExPropertyCurveEditController::GetKeyTime(FKeyHandle Handle) const
{
	return Curve->IsKeyHandleValid(Handle) ? Curve->GetKeyTime(Handle) : 0.0f;
}

float FPCGExPropertyCurveEditController::GetKeyValue(FKeyHandle Handle) const
{
	return Curve->IsKeyHandleValid(Handle) ? Curve->GetKeyValue(Handle) : 0.0f;
}

ERichCurveInterpMode FPCGExPropertyCurveEditController::GetKeyInterp(FKeyHandle Handle) const
{
	return Curve->IsKeyHandleValid(Handle) ? Curve->GetKey(Handle).InterpMode.GetValue() : RCIM_Linear;
}

bool FPCGExPropertyCurveEditController::IsValidKey(FKeyHandle Handle) const
{
	return Curve->IsKeyHandleValid(Handle);
}

#pragma endregion

#pragma region Selection

void FPCGExPropertyCurveEditController::SetSelectedKey(FKeyHandle Handle)
{
	if (!IsValidKey(Handle) || SelectedKey == Handle)
	{
		return;
	}
	SelectedKey = Handle;
	OnSelectionChanged.Broadcast();
}

void FPCGExPropertyCurveEditController::SetSelectedKeyByIndex(int32 Index)
{
	SetSelectedKey(GetKeyHandle(Index));
}

void FPCGExPropertyCurveEditController::ClearSelection()
{
	if (!SelectedKey.IsValid())
	{
		return;
	}
	SelectedKey = FKeyHandle::Invalid();
	OnSelectionChanged.Broadcast();
}

#pragma endregion

#pragma region Edits

void FPCGExPropertyCurveEditController::AddKeyAtTime(float Time)
{
	// Seed the value by evaluating the current curve at Time, so the new key sits on the ramp.
	AddKeyAtTimeValue(Time, Curve->Eval(Time));
}

void FPCGExPropertyCurveEditController::AddKeyAtTimeValue(float Time, float Value)
{
	// Positions and values are free: add exactly where asked.
	const FKeyHandle Handle = Curve->AddKey(Time, Value);

	// Inherit interpolation from the left neighbour (fallback to the right, else linear).
	const int32 Idx = GetKeyIndex(Handle);
	ERichCurveInterpMode Interp = RCIM_Linear;
	if (Idx != INDEX_NONE)
	{
		if (Idx > 0)
		{
			Interp = Curve->Keys[Idx - 1].InterpMode.GetValue();
		}
		else if (Idx + 1 < NumKeys())
		{
			Interp = Curve->Keys[Idx + 1].InterpMode.GetValue();
		}
	}
	Curve->GetKey(Handle).InterpMode = Interp;
	Curve->AutoSetTangents();

	SelectedKey = Handle;
	OnSelectionChanged.Broadcast();

	RefitValueFrame();
	RefitTimeFrame();
	NotifyChanged(/*bInteractive=*/false);
}

void FPCGExPropertyCurveEditController::DeleteKeyByIndex(int32 Index)
{
	// Keep at least one key -- a lone key is a valid ramp (it evaluates as a constant). No key is
	// special, so any key is deletable down to that floor.
	if (NumKeys() <= 1 || !Curve->Keys.IsValidIndex(Index))
	{
		return;
	}

	const FKeyHandle Handle = GetKeyHandle(Index);
	if (!IsValidKey(Handle))
	{
		return;
	}

	const bool bWasSelected = SelectedKey.IsValid() && (SelectedKey == Handle);

	Curve->DeleteKey(Handle);
	Curve->AutoSetTangents();

	if (bWasSelected)
	{
		// Select the neighbour that now occupies (or sits just before) the freed index.
		const int32 NewSel = FMath::Clamp(Index, 0, NumKeys() - 1);
		SelectedKey = GetKeyHandle(NewSel);
		OnSelectionChanged.Broadcast();
	}

	RefitValueFrame();
	RefitTimeFrame();
	NotifyChanged(/*bInteractive=*/false);
}

void FPCGExPropertyCurveEditController::DeleteSelectedKey()
{
	if (HasSelection())
	{
		DeleteKeyByIndex(GetSelectedIndex());
	}
}

void FPCGExPropertyCurveEditController::MoveKey(FKeyHandle Handle, float NewTime, float NewValue, bool bInteractive)
{
	if (!IsValidKey(Handle))
	{
		return;
	}

	// No clamp: time and value are both free. SetKeyTime re-sorts the curve if this moves the key past a
	// neighbour, and the handle survives that reorder, so keys reorder naturally as they're dragged.
	// Suppress SetKeyValue's own tangent pass and recompute once at the end (SetKeyTime still forces one
	// internally via delete/add, so two passes is the floor with this API -- down from three).
	Curve->SetKeyTime(Handle, NewTime);
	Curve->SetKeyValue(Handle, NewValue, /*bAutoSetTangents=*/false);
	Curve->AutoSetTangents();

	// Both frames freeze during an interactive drag (only refit on commit). Freezing gives the view a
	// stable screen<->data mapping: without it, a drag would grow the frame, which would remap the same
	// cursor position to an ever-larger value -- a feedback loop that blows values up.
	if (!bInteractive)
	{
		RefitValueFrame();
		RefitTimeFrame();
	}
	NotifyChanged(bInteractive);
}

void FPCGExPropertyCurveEditController::SetKeyValue(FKeyHandle Handle, float NewValue, bool bInteractive)
{
	if (!IsValidKey(Handle))
	{
		return;
	}
	// Recompute tangents once, after the value is set (not inside SetKeyValue).
	Curve->SetKeyValue(Handle, NewValue, /*bAutoSetTangents=*/false);
	Curve->AutoSetTangents();

	// Frame is frozen during an interactive drag (only refit on commit) -- this is what stops the
	// value/frame feedback loop that otherwise blows values up to astronomical numbers.
	if (!bInteractive)
	{
		RefitValueFrame();
	}
	NotifyChanged(bInteractive);
}

void FPCGExPropertyCurveEditController::SetKeyInterp(FKeyHandle Handle, ERichCurveInterpMode Mode)
{
	if (!IsValidKey(Handle))
	{
		return;
	}
	Curve->GetKey(Handle).InterpMode = Mode;
	Curve->AutoSetTangents();

	RefitValueFrame();
	NotifyChanged(/*bInteractive=*/false);
}

void FPCGExPropertyCurveEditController::CommitInteractive()
{
	RefitValueFrame();
	RefitTimeFrame();
	NotifyChanged(/*bInteractive=*/false);
}

#pragma endregion

#pragma region Framing

void FPCGExPropertyCurveEditController::RefitValueFrame()
{
	float MinV = 0.0f;
	float MaxV = 1.0f;

	if (Curve->Keys.Num() > 0)
	{
		MinV = MaxV = Curve->Keys[0].Value;
		for (const FRichCurveKey& Key : Curve->Keys)
		{
			MinV = FMath::Min(MinV, Key.Value);
			MaxV = FMath::Max(MaxV, Key.Value);
		}
	}

	float Span = MaxV - MinV;
	if (Span < KINDA_SMALL_NUMBER)
	{
		// Flat ramp: nothing to fit. Centre a unit window on the value.
		const float Center = (Curve->Keys.Num() > 0) ? Curve->Keys[0].Value : 0.5f;
		MinV = Center - 0.5f;
		MaxV = Center + 0.5f;
		Span = 1.0f;
	}

	const float Pad = Span * FramePadding;
	FrameMin = MinV - Pad;
	FrameMax = MaxV + Pad;
}

float FPCGExPropertyCurveEditController::ValueToFrameAlpha(float Value) const
{
	const float Span = FrameMax - FrameMin;
	return (Span > KINDA_SMALL_NUMBER) ? (Value - FrameMin) / Span : 0.5f;
}

float FPCGExPropertyCurveEditController::FrameAlphaToValue(float Alpha) const
{
	return FrameMin + Alpha * (FrameMax - FrameMin);
}

void FPCGExPropertyCurveEditController::RefitTimeFrame()
{
	float MinT = 0.0f;
	float MaxT = 1.0f;

	if (Curve->Keys.Num() > 0)
	{
		MinT = MaxT = Curve->Keys[0].Time;
		for (const FRichCurveKey& Key : Curve->Keys)
		{
			MinT = FMath::Min(MinT, Key.Time);
			MaxT = FMath::Max(MaxT, Key.Time);
		}
	}

	// Always keep the canonical [0,1] domain in view (min span 1, edge-to-edge -- no padding), and
	// expand to include any key that has been dragged outside it (in either direction).
	TimeFrameMin = FMath::Min(0.0f, MinT);
	TimeFrameMax = FMath::Max(1.0f, MaxT);
}

float FPCGExPropertyCurveEditController::TimeToFrameAlpha(float Time) const
{
	const float Span = TimeFrameMax - TimeFrameMin;
	return (Span > KINDA_SMALL_NUMBER) ? (Time - TimeFrameMin) / Span : 0.0f;
}

float FPCGExPropertyCurveEditController::FrameAlphaToTime(float Alpha) const
{
	return TimeFrameMin + Alpha * (TimeFrameMax - TimeFrameMin);
}

#pragma endregion
