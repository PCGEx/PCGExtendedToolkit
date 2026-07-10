// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "Curves/KeyHandle.h"

/** Broadcast after the working curve mutates. bInteractive = mid-drag (coalesce undo, only grow the value frame). */
DECLARE_MULTICAST_DELEGATE_OneParam(FPCGExOnPropertyCurveChanged, bool /*bInteractive*/);

/** Broadcast when the selected key changes (or selection is cleared). */
DECLARE_MULTICAST_DELEGATE(FPCGExOnPropertyCurveSelectionChanged);

/**
 * Shared edit state for the inline ramp widgets (graph + key strip).
 *
 * Wraps the transient working FRichCurve and is the single place that enforces the ramp invariants:
 *   - at least one key (a lone key evaluates as a constant, which is the whole ramp)
 *   - no key is special: any key can be deleted (while >= 1 remains), moved, or reordered past any
 *     other by dragging (SetKeyTime re-sorts; the FKeyHandle survives the reorder)
 *
 * Both axes are fully free (no value/position clamp, negative positions allowed) and auto-frame to
 * the key range: the value frame fits keys plus padding; the time frame fits keys but always spans at
 * least [0,1] (edge-to-edge, no padding). Both frames freeze during an interactive drag so the view's
 * screen<->data mapping stays a stable reference (no feedback loop), then refit on commit.
 *
 * The views drive edits by key *handle* (stable across reorders) and read for paint by *index*;
 * index<->handle bridging stays here (only the curve's public API is used).
 */
class FPCGExPropertyCurveEditController
{
public:
	explicit FPCGExPropertyCurveEditController(const TSharedRef<FRichCurve>& InCurve);

	FRichCurve& GetCurve() const { return *Curve; }
	int32 NumKeys() const { return Curve->Keys.Num(); }

	// --- Read by index (for painting / hit-testing) ---
	float GetKeyTimeAt(int32 Index) const;
	float GetKeyValueAt(int32 Index) const;
	/** Stable handle for the key at Index (views grab this on drag-start so a reorder can't lose it). */
	FKeyHandle GetKeyHandleAt(int32 Index) const;

	// --- Read by handle (for the inspector's current selection) ---
	float GetKeyTime(FKeyHandle Handle) const;
	float GetKeyValue(FKeyHandle Handle) const;
	ERichCurveInterpMode GetKeyInterp(FKeyHandle Handle) const;
	bool IsValidKey(FKeyHandle Handle) const;

	// --- Selection ---
	FKeyHandle GetSelectedKey() const { return SelectedKey; }
	int32 GetSelectedIndex() const { return GetKeyIndex(SelectedKey); }
	bool HasSelection() const { return SelectedKey.IsValid() && IsValidKey(SelectedKey); }
	void SetSelectedKeyByIndex(int32 Index);
	void ClearSelection();

	// --- Edits (all notify OnChanged; bInteractive coalesces undo and only grows the value frame) ---
	void AddKeyAtTime(float Time);
	/** Add a key at an explicit time AND value (Shift+click on the graph places one at the cursor). */
	void AddKeyAtTimeValue(float Time, float Value);
	void DeleteKeyByIndex(int32 Index);
	void DeleteSelectedKey();
	void MoveKey(FKeyHandle Handle, float NewTime, float NewValue, bool bInteractive);
	void SetKeyValue(FKeyHandle Handle, float NewValue, bool bInteractive);
	void SetKeyInterp(FKeyHandle Handle, ERichCurveInterpMode Mode);

	/** Close an interactive gesture: refit both frames tightly and emit a final (undoable) change. */
	void CommitInteractive();

	// --- Value-axis framing (Y) ---
	float GetFrameMin() const { return FrameMin; }
	float GetFrameMax() const { return FrameMax; }
	float ValueToFrameAlpha(float Value) const;
	float FrameAlphaToValue(float Alpha) const;

	// --- Time-axis framing (X) -- always spans at least [0,1], expands to include out-of-range keys ---
	float GetTimeFrameMin() const { return TimeFrameMin; }
	float GetTimeFrameMax() const { return TimeFrameMax; }
	float TimeToFrameAlpha(float Time) const;
	float FrameAlphaToTime(float Alpha) const;

	FPCGExOnPropertyCurveChanged OnChanged;
	FPCGExOnPropertyCurveSelectionChanged OnSelectionChanged;

private:
	void NotifyChanged(bool bInteractive) { OnChanged.Broadcast(bInteractive); }

	/** index -> handle via the curve's public ordered handle iterator. Invalid handle if out of range. */
	FKeyHandle GetKeyHandle(int32 Index) const;
	/** handle -> index (INDEX_NONE if stale / invalid). */
	int32 GetKeyIndex(FKeyHandle Handle) const;

	void SetSelectedKey(FKeyHandle Handle);

	/** Recompute the value frame to tightly fit the current keys (+ padding). Called on non-interactive
	 *  edits only; the frame stays frozen during a drag so the Y mapping has a stable reference (no
	 *  feedback loop). */
	void RefitValueFrame();

	/** Recompute the time frame: fits the current keys but always spans at least [0,1] (no padding).
	 *  Frozen during a drag for the same stable-reference reason as the value frame. */
	void RefitTimeFrame();

	TSharedRef<FRichCurve> Curve;

	FKeyHandle SelectedKey = FKeyHandle::Invalid();

	float FrameMin = 0.0f;
	float FrameMax = 1.0f;

	float TimeFrameMin = 0.0f;
	float TimeFrameMax = 1.0f;

	/** Fraction of the value span added as headroom above/below so points sit inside the border. */
	static constexpr float FramePadding = 0.08f;
};
