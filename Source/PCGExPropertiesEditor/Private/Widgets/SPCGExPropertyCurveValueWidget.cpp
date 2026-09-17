// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "SPCGExPropertyCurveValueWidget.h"

#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "AssetRegistry/AssetData.h"
#include "Curves/CurveFloat.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"

#include "PCGExPropertyCurveEditController.h"
#include "SPCGExPropertyCurveEditor.h"

#define LOCTEXT_NAMESPACE "SPCGExPropertyCurveValueWidget"

namespace PCGExPropertyCurveValueWidget
{
	// Enumerate the FRuntimeFloatCurve instances behind the handle (one per selected object).
	void ForEachRuntimeCurve(const TSharedPtr<IPropertyHandle>& Handle, TFunctionRef<void(FRuntimeFloatCurve&)> Fn)
	{
		if (!Handle.IsValid())
		{
			return;
		}
		TArray<void*> RawData;
		Handle->AccessRawData(RawData);
		for (void* Raw : RawData)
		{
			if (Raw)
			{
				Fn(*static_cast<FRuntimeFloatCurve*>(Raw));
			}
		}
	}
}

void SPCGExPropertyCurveValueWidget::Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InValueHandle)
{
	ValueHandle = InValueHandle;

	PullFromProperty();

	Controller = MakeShared<FPCGExPropertyCurveEditController>(CurveData.ToSharedRef());
	Controller->SetClamps(InArgs._Clamps);
	Controller->OnChanged.AddSP(this, &SPCGExPropertyCurveValueWidget::HandleCurveChanged);

	UCurveFloat* External = GetExternalCurve();
	Controller->SetReadOnly(External != nullptr);
	LastSyncedAsset = External;
	BindAssetWatch(External);

	// Drift detection (undo/redo, archetype propagation, force-delete) -- see class comment.
	SetCanTick(true);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			BuildAssetRow()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(EditorWidget, SPCGExPropertyCurveEditor, Controller.ToSharedRef())
		]
	];
}

SPCGExPropertyCurveValueWidget::~SPCGExPropertyCurveValueWidget()
{
	UnbindAssetWatch();
}

void SPCGExPropertyCurveValueWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Controller.IsValid())
	{
		return;
	}

	// Compare both the asset AND the read-only state: a force-deleted asset nulls the pointer, which
	// then equals a stale (also null) weak ptr -- the read-only mismatch is what catches that case.
	UCurveFloat* External = GetExternalCurve();
	if (External != LastSyncedAsset.Get() || (External != nullptr) != Controller->IsReadOnly())
	{
		SyncFromProperty();
	}
}

TSharedRef<SWidget> SPCGExPropertyCurveValueWidget::BuildAssetRow()
{
	if (!ValueHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Handle-less binding on purpose (see class comment): the path is read from raw memory and the
	// write goes through HandleAssetPicked with the Value handle's notifies, exactly like the keys.
	return SNew(SObjectPropertyEntryBox)
		.ObjectPath(this, &SPCGExPropertyCurveValueWidget::GetExternalCurvePath)
		.AllowedClass(UCurveFloat::StaticClass())
		.OnObjectChanged(this, &SPCGExPropertyCurveValueWidget::HandleAssetPicked)
		.AllowClear(true)
		.DisplayBrowse(true)
		.DisplayThumbnail(false)
		.DisplayUseSelected(true)
		.ToolTipText(LOCTEXT("ExternalCurveTooltip",
		                     "Curve asset override. When set, the asset's keys drive this property (the inline editor below becomes a read-only preview of them). Clear to author keys inline again.\n"
		                     "This asset is also what Blueprint Set/Get Property nodes read and write for a Float Curve property."));
}

void SPCGExPropertyCurveValueWidget::HandleCurveChanged(bool bInteractive)
{
	PushToProperty(bInteractive);
}

UCurveFloat* SPCGExPropertyCurveValueWidget::GetExternalCurve() const
{
	UCurveFloat* Result = nullptr;
	bool bFound = false;
	PCGExPropertyCurveValueWidget::ForEachRuntimeCurve(ValueHandle, [&](FRuntimeFloatCurve& Runtime)
	{
		if (bFound)
		{
			return;
		}
		Result = Runtime.ExternalCurve;
		bFound = true;
	});
	return Result;
}

FString SPCGExPropertyCurveValueWidget::GetExternalCurvePath() const
{
	const UCurveFloat* External = GetExternalCurve();
	return External ? External->GetPathName() : FString();
}

void SPCGExPropertyCurveValueWidget::HandleAssetPicked(const FAssetData& AssetData)
{
	if (!ValueHandle.IsValid())
	{
		return;
	}

	// Empty asset data = "Clear". GetAsset loads on demand; the picker is filtered to UCurveFloat so
	// the cast only guards against a stale registry entry.
	UCurveFloat* NewCurve = AssetData.IsValid() ? Cast<UCurveFloat>(AssetData.GetAsset()) : nullptr;

	ValueHandle->NotifyPreChange();
	PCGExPropertyCurveValueWidget::ForEachRuntimeCurve(ValueHandle, [NewCurve](FRuntimeFloatCurve& Runtime)
	{
		Runtime.ExternalCurve = NewCurve;
	});
	ValueHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	ValueHandle->NotifyFinishedChangingProperties();

	SyncFromProperty();
}

void SPCGExPropertyCurveValueWidget::SyncFromProperty()
{
	if (!Controller.IsValid())
	{
		return;
	}

	UCurveFloat* External = GetExternalCurve();
	const bool bReadOnly = External != nullptr;
	const bool bBecomingReadOnly = bReadOnly && !Controller->IsReadOnly();

	Controller->SetReadOnly(bReadOnly);
	LastSyncedAsset = External;
	PullFromProperty();
	Controller->ResetView();
	BindAssetWatch(External);

	// The controller already rejects edits while read-only; dropping focus avoids a focus ring on a dimmed graph.
	if (bBecomingReadOnly && EditorWidget.IsValid() && EditorWidget->HasFocusedDescendants())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	}
}

void SPCGExPropertyCurveValueWidget::HandleWatchedAssetUpdated(UCurveBase* Curve, EPropertyChangeType::Type ChangeType)
{
	if (!Controller.IsValid() || Curve != WatchedAsset.Get())
	{
		return;
	}
	PullFromProperty();
	Controller->ResetView();
}

void SPCGExPropertyCurveValueWidget::BindAssetWatch(UCurveFloat* External)
{
	if (External == WatchedAsset.Get())
	{
		return;
	}

	UnbindAssetWatch();
	if (External)
	{
		WatchedAsset = External;
		AssetUpdateHandle = External->OnUpdateCurve.AddSP(this, &SPCGExPropertyCurveValueWidget::HandleWatchedAssetUpdated);
	}
}

void SPCGExPropertyCurveValueWidget::UnbindAssetWatch()
{
	if (UCurveFloat* Previous = WatchedAsset.Get(); Previous && AssetUpdateHandle.IsValid())
	{
		Previous->OnUpdateCurve.Remove(AssetUpdateHandle);
	}
	AssetUpdateHandle.Reset();
	WatchedAsset.Reset();
}

void SPCGExPropertyCurveValueWidget::PullFromProperty()
{
	bool bLoaded = false;
	bool bFromAsset = false;
	PCGExPropertyCurveValueWidget::ForEachRuntimeCurve(ValueHandle, [&](FRuntimeFloatCurve& Runtime)
	{
		if (bLoaded)
		{
			return; // multi-select: edit the first instance's curve, push to all
		}
		// Mirror the resolution SampleAt / GetRichCurveConst use: an assigned asset wins over the inline keys.
		if (Runtime.ExternalCurve)
		{
			*CurveData = Runtime.ExternalCurve->FloatCurve;
			bFromAsset = true;
		}
		else
		{
			*CurveData = Runtime.EditorCurveData;
		}
		bLoaded = true;
	});

	// Reject unusable payloads (no keys, or non-finite key data).
	bool bSane = bLoaded && CurveData->Keys.Num() >= 1;
	if (bSane)
	{
		for (const FRichCurveKey& Key : CurveData->Keys)
		{
			if (!FMath::IsFinite(Key.Time) || !FMath::IsFinite(Key.Value))
			{
				bSane = false;
				break;
			}
		}
	}

	if (!bSane)
	{
		// Inline data falls back to an editable 0->1 ramp. An asset preview shows what sampling returns:
		// a keyless FRichCurve evaluates to its default, drawn as a flat line.
		const float AssetDefault = bFromAsset ? CurveData->Eval(0.0f) : 0.0f;
		const float Flat = FMath::IsFinite(AssetDefault) ? AssetDefault : 0.0f;
		const float Lo = bFromAsset ? Flat : 0.0f;
		const float Hi = bFromAsset ? Flat : 1.0f;
		CurveData->Reset();
		const FKeyHandle First = CurveData->AddKey(0.0f, Lo);
		const FKeyHandle Last = CurveData->AddKey(1.0f, Hi);
		CurveData->GetKey(First).InterpMode = RCIM_Linear;
		CurveData->GetKey(Last).InterpMode = RCIM_Linear;
	}

	CurveData->AutoSetTangents();
}

void SPCGExPropertyCurveValueWidget::PushToProperty(bool bInteractive)
{
	if (!ValueHandle.IsValid())
	{
		return;
	}

	// Read-only: the working curve holds the ASSET's keys, never write them back to the inline data.
	// A gesture in flight at the flip still gets its button-up (capture release is not enabled-gated)
	// and must close the interactive change it opened, or the transaction stays stranded.
	if (Controller.IsValid() && Controller->IsReadOnly())
	{
		if (!bInteractive && bInteractiveChangeOpen)
		{
			bInteractiveChangeOpen = false;
			ValueHandle->NotifyPreChange();
			ValueHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			ValueHandle->NotifyFinishedChangingProperties();
		}
		return;
	}

	// The non-interactive push that ends a drag MUST run even if the value is unchanged: it is
	// the finalizing notification that closes the change the first interactive notify opened.
	// Skipping it strands the transaction -- undo breaks and PCG never settles out of
	// interactive regeneration.
	ValueHandle->NotifyPreChange();

	PCGExPropertyCurveValueWidget::ForEachRuntimeCurve(ValueHandle, [&](FRuntimeFloatCurve& Runtime)
	{
		Runtime.EditorCurveData = *CurveData;
	});

	ValueHandle->NotifyPostChange(bInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
	if (bInteractive)
	{
		bInteractiveChangeOpen = true;
	}
	else
	{
		bInteractiveChangeOpen = false;
		ValueHandle->NotifyFinishedChangingProperties();
	}
}

#undef LOCTEXT_NAMESPACE
