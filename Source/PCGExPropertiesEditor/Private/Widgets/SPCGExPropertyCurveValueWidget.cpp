// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "SPCGExPropertyCurveValueWidget.h"

#include "PropertyHandle.h"
#include "Curves/CurveFloat.h"

#include "PCGExPropertyCurveEditController.h"
#include "SPCGExPropertyCurveEditor.h"

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
	Controller->OnChanged.AddSP(this, &SPCGExPropertyCurveValueWidget::HandleCurveChanged);

	ChildSlot
	[
		SNew(SPCGExPropertyCurveEditor, Controller.ToSharedRef())
	];
}

void SPCGExPropertyCurveValueWidget::HandleCurveChanged(bool bInteractive)
{
	PushToProperty(bInteractive);
}

void SPCGExPropertyCurveValueWidget::PullFromProperty()
{
	bool bLoaded = false;
	PCGExPropertyCurveValueWidget::ForEachRuntimeCurve(ValueHandle, [&](FRuntimeFloatCurve& Runtime)
	{
		if (bLoaded)
		{
			return; // multi-select: edit the first instance's curve, push to all
		}
		*CurveData = Runtime.EditorCurveData;
		bLoaded = true;
	});

	// Reject unusable payloads (no keys, or non-finite key data) -- fall back to a linear
	// 0->1 ramp so the editor always shows a valid curve instead of a blank graph.
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
		CurveData->Reset();
		const FKeyHandle First = CurveData->AddKey(0.0f, 0.0f);
		const FKeyHandle Last = CurveData->AddKey(1.0f, 1.0f);
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
	if (!bInteractive)
	{
		ValueHandle->NotifyFinishedChangingProperties();
	}
}
