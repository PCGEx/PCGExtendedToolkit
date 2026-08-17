// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#include "PCGExPropertyCurveEditController.h"

#include "UObject/UnrealType.h"

namespace PCGExPropertyCurveEditController
{
	const FName MetaTimeMin(TEXT("PCGExCurveTimeMin"));
	const FName MetaTimeMax(TEXT("PCGExCurveTimeMax"));
	const FName MetaValueMin(TEXT("PCGExCurveValueMin"));
	const FName MetaValueMax(TEXT("PCGExCurveValueMax"));

	TOptional<float> GetFloatMeta(const FProperty* Property, const FName& Key)
	{
		float Value = 0.0f;
		if (Property && Property->HasMetaData(Key) && LexTryParseString(Value, *Property->GetMetaData(Key)))
		{
			return Value;
		}
		return TOptional<float>();
	}
}

FPCGExPropertyCurveClamps FPCGExPropertyCurveClamps::FromProperty(const FProperty* Property)
{
	using namespace PCGExPropertyCurveEditController;

	FPCGExPropertyCurveClamps Clamps;
	Clamps.TimeMin = GetFloatMeta(Property, MetaTimeMin);
	Clamps.TimeMax = GetFloatMeta(Property, MetaTimeMax);
	Clamps.ValueMin = GetFloatMeta(Property, MetaValueMin);
	Clamps.ValueMax = GetFloatMeta(Property, MetaValueMax);
	return Clamps;
}

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
	// Clamp before seeding, so the value is evaluated where the key will actually land -- seeding at
	// the raw time would sample an out-of-clamp segment and drop the key off-curve at its clamped spot.
	const float ClampedTime = Clamps.ClampTime(Time);
	AddKeyAtTimeValue(ClampedTime, Curve->Eval(ClampedTime));
}

void FPCGExPropertyCurveEditController::AddKeyAtTimeValue(float Time, float Value)
{
	// Add exactly where asked, within the owner's optional clamps (free by default).
	const FKeyHandle Handle = Curve->AddKey(Clamps.ClampTime(Time), Clamps.ClampValue(Value));

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

	// Time and value are free within the owner's optional clamps. SetKeyTime re-sorts the curve if this
	// moves the key past a neighbour, and the handle survives that reorder, so keys reorder naturally as
	// they're dragged.
	// Suppress SetKeyValue's own tangent pass and recompute once at the end (SetKeyTime still forces one
	// internally via delete/add, so two passes is the floor with this API -- down from three).
	Curve->SetKeyTime(Handle, Clamps.ClampTime(NewTime));
	Curve->SetKeyValue(Handle, Clamps.ClampValue(NewValue), /*bAutoSetTangents=*/false);
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
	Curve->SetKeyValue(Handle, Clamps.ClampValue(NewValue), /*bAutoSetTangents=*/false);
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

void FPCGExPropertyCurveEditController::FlipTime()
{
	const int32 Num = NumKeys();
	if (Num == 0)
	{
		return;
	}

	// Keys are time-sorted, so the extremes bracket the occupied span; mirroring across them keeps
	// that span identical, which is what makes the flip an exact involution.
	const float MinT = Curve->Keys[0].Time;
	const float MaxT = Curve->Keys.Last().Time;

	struct FFlippedKey
	{
		FKeyHandle Handle;
		float NewTime = 0.0f;
		ERichCurveInterpMode NewInterp = RCIM_Linear;
		float NewArrive = 0.0f;
		float NewLeave = 0.0f;
		float NewArriveWeight = 0.0f;
		float NewLeaveWeight = 0.0f;
		TEnumAsByte<ERichCurveTangentWeightMode> NewWeightMode = RCTWM_WeightedNone;
	};

	// Snapshot every key's mirrored state up front: applying mutates the ordering mid-walk.
	TArray<FFlippedKey> Flipped;
	Flipped.Reserve(Num);

	int32 Index = 0;
	for (auto It = Curve->GetKeyHandleIterator(); It; ++It, ++Index)
	{
		const FRichCurveKey& Key = Curve->Keys[Index];
		FFlippedKey& Target = Flipped.AddDefaulted_GetRef();
		Target.Handle = *It;
		Target.NewTime = MinT + MaxT - Key.Time;
		// A horizontal mirror swaps each key's arrive/leave sides and negates the slopes.
		Target.NewArrive = -Key.LeaveTangent;
		Target.NewLeave = -Key.ArriveTangent;
		Target.NewArriveWeight = Key.LeaveTangentWeight;
		Target.NewLeaveWeight = Key.ArriveTangentWeight;
		switch (Key.TangentWeightMode.GetValue())
		{
		case RCTWM_WeightedArrive:
			Target.NewWeightMode = RCTWM_WeightedLeave;
			break;
		case RCTWM_WeightedLeave:
			Target.NewWeightMode = RCTWM_WeightedArrive;
			break;
		default:
			Target.NewWeightMode = Key.TangentWeightMode;
			break;
		}
		// Interp lives on the segment's LEFT key; after the mirror the segment leaving key k is the
		// mirror of the segment that ENTERED it, i.e. key (k-1)'s. Key 0's takes the last key's
		// (segment-unused) interp so the flip stays a perfect involution.
		Target.NewInterp = (Index > 0 ? Curve->Keys[Index - 1] : Curve->Keys[Num - 1]).InterpMode.GetValue();
	}

	// Move times first -- SetKeyTime re-sorts and clones the whole old key under the same handle --
	// then overwrite the mirrored fields on the settled keys.
	for (const FFlippedKey& Target : Flipped)
	{
		Curve->SetKeyTime(Target.Handle, Target.NewTime);
	}
	for (const FFlippedKey& Target : Flipped)
	{
		FRichCurveKey& Key = Curve->GetKey(Target.Handle);
		Key.InterpMode = Target.NewInterp;
		Key.ArriveTangent = Target.NewArrive;
		Key.LeaveTangent = Target.NewLeave;
		Key.ArriveTangentWeight = Target.NewArriveWeight;
		Key.LeaveTangentWeight = Target.NewLeaveWeight;
		Key.TangentWeightMode = Target.NewWeightMode;
	}
	Curve->AutoSetTangents();

	RefitValueFrame();
	RefitTimeFrame();
	NotifyChanged(/*bInteractive=*/false);
}

void FPCGExPropertyCurveEditController::FlipValues()
{
	if (NumKeys() == 0)
	{
		return;
	}

	float MinV = Curve->Keys[0].Value;
	float MaxV = MinV;
	for (const FRichCurveKey& Key : Curve->Keys)
	{
		MinV = FMath::Min(MinV, Key.Value);
		MaxV = FMath::Max(MaxV, Key.Value);
	}

	for (FRichCurveKey& Key : Curve->Keys)
	{
		Key.Value = MinV + MaxV - Key.Value;
		// A vertical mirror negates slopes; arrive/leave keep their sides.
		Key.ArriveTangent = -Key.ArriveTangent;
		Key.LeaveTangent = -Key.LeaveTangent;
	}
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
