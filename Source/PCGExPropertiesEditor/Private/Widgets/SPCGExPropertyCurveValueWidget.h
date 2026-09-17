// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "PCGExPropertyCurveEditController.h"

class IPropertyHandle;
class SPCGExPropertyCurveEditor;
class UCurveBase;
class UCurveFloat;
struct FAssetData;

/**
 * Self-contained inline curve editor bound to an FRuntimeFloatCurve property handle
 * (FPCGExProperty_FloatCurve::Value). Owns the transient working FRichCurve, the edit
 * controller, and the write-back:
 *
 *   pull:  Value.EditorCurveData -> working curve (linear 0->1 fallback on unusable payloads)
 *   push:  working curve -> every selected instance's Value.EditorCurveData, through
 *          NotifyPreChange / NotifyPostChange(Interactive|ValueSet) / NotifyFinishedChangingProperties
 *          so undo and PCG regeneration behave. The finalizing non-interactive push always
 *          writes (even on an unchanged value) to close the change the interactive one opened.
 *
 * Asset override: a picker row above the graph edits Value.ExternalCurve through the Value handle's raw
 * memory, with the same notify pattern as the key push. NOT a child handle: GetChildHandle can return
 * null for AddExternalStructureProperty handles, and its change delegate does not fire on undo/redo.
 * Tick re-syncs whenever the live asset or the read-only state drifts from the last sync, which covers
 * the picker, undo/redo, archetype propagation and force-delete alike.
 *
 * While an asset is assigned the graph is a read-only preview of the asset's keys, refreshed through
 * UCurveBase::OnUpdateCurve. Push is suppressed, except for the notify that closes an interactive
 * change opened before the flip.
 *
 * Used by BOTH render paths of the Float Curve property:
 *   - FPCGExPropertyFloatCurveCustomization (schema / instanced-struct details)
 *   - the Compact inline-widget registry factory (override entry rows)
 */
class SPCGExPropertyCurveValueWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyCurveValueWidget)
		{
		}

		/** Optional per-axis edit clamps (default: both axes free). */
		SLATE_ARGUMENT(FPCGExPropertyCurveClamps, Clamps)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InValueHandle);
	virtual ~SPCGExPropertyCurveValueWidget() override;

	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget interface

private:
	void PullFromProperty();
	void PushToProperty(bool bInteractive);
	void HandleCurveChanged(bool bInteractive);

	/** Value.ExternalCurve of the first selected instance (multi-select previews the first, like the keys). */
	UCurveFloat* GetExternalCurve() const;
	/** Path shown by the picker (first instance). */
	FString GetExternalCurvePath() const;
	/** Picker committed an asset (or None): write ExternalCurve on every selected instance and re-sync. */
	void HandleAssetPicked(const FAssetData& AssetData);
	/** Re-derive read-only state, preview contents and the asset watch from the live property. */
	void SyncFromProperty();
	/** The watched asset was edited in its own editor: refresh the preview. */
	void HandleWatchedAssetUpdated(UCurveBase* Curve, EPropertyChangeType::Type ChangeType);
	void BindAssetWatch(UCurveFloat* External);
	void UnbindAssetWatch();

	TSharedRef<SWidget> BuildAssetRow();

	TSharedPtr<IPropertyHandle> ValueHandle;
	TSharedPtr<FRichCurve> CurveData = MakeShared<FRichCurve>();
	TSharedPtr<FPCGExPropertyCurveEditController> Controller;
	TSharedPtr<SPCGExPropertyCurveEditor> EditorWidget;

	/** Asset the preview / read-only state was last synced against (drift check in Tick). */
	TWeakObjectPtr<UCurveFloat> LastSyncedAsset;
	/** Same asset, as the OnUpdateCurve subscription target. */
	TWeakObjectPtr<UCurveFloat> WatchedAsset;
	FDelegateHandle AssetUpdateHandle;

	/** An Interactive notify has been sent and its finalizing ValueSet has not -- must be closed even if
	 *  the widget flips read-only mid-gesture. */
	bool bInteractiveChangeOpen = false;
};
