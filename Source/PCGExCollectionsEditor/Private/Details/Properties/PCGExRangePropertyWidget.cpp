// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/PCGExRangePropertyWidget.h"

#include "Editor.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Details/PCGExInlineNumericWidgets.h"
#include "Details/Properties/SPCGExRangeSlider.h"
#include "Properties/PCGExProperty_Range.h"
#include "Styling/AppStyle.h"
#include "Templates/Function.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExRangePropertyWidget"

namespace PCGExRangePropertyWidget
{
	/** Positions with start <= end, both inside 0..1: what the slider draws and what an edit writes back. */
	FVector2D ConformPositions(const FVector2D& Positions)
	{
		return PCGExRangeSlider::ConformRange(Positions, FVector2D(0.0, 1.0));
	}

	/**
	 * Typed access to the FPCGExProperty_Range instances behind a Value handle, plus the notify protocol their
	 * edits follow so undo, owner dirtying and PCG regeneration behave on every host:
	 *
	 *   interactive: BeginInteractive, any number of ApplyInteractive, EndInteractive. One transaction; the closing
	 *                ValueSet + finished notifies are sent even on an unchanged value, because hosts that edit a
	 *                scratch struct only commit on the finished notify.
	 *   discrete:    Commit. Its own transaction, skipped when nothing would change.
	 *
	 * A mutator runs once per edited instance: it rewrites that instance's positions (its Value) and gets the
	 * instance itself for its Min / Max conversions.
	 */
	class FRangeBinding
	{
	public:
		UE_NONCOPYABLE(FRangeBinding)

		explicit FRangeBinding(const TSharedRef<IPropertyHandle>& InValueHandle)
			: ValueHandle(InValueHandle)
		{
			const FProperty* ValueProperty = ValueHandle->GetProperty();
			if (ValueProperty && ValueProperty->GetOwnerStruct() == FPCGExProperty_Range::StaticStruct())
			{
				ValueOffset = ValueProperty->GetOffset_ForInternal();
			}
		}

		~FRangeBinding()
		{
			// A panel rebuild can drop the widgets mid-gesture; the transaction they opened must not outlive them.
			if (bInteractive && GEditor)
			{
				GEditor->EndTransaction();
			}
		}

		/** The edited property; null when the handle is dead or the edited instances disagree. Read on the spot, never kept. */
		const FPCGExProperty_Range* Peek() const
		{
			void* Raw = nullptr;
			if (!IsBound() || ValueHandle->GetValueData(Raw) != FPropertyAccess::Success || !Raw)
			{
				return nullptr;
			}
			return &OwnerOf(Raw);
		}

		void BeginInteractive()
		{
			if (bInteractive || !IsBound())
			{
				return;
			}
			bInteractive = true;
			if (GEditor)
			{
				GEditor->BeginTransaction(LOCTEXT("SetRange", "Set Range"));
			}
		}

		void ApplyInteractive(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			if (bInteractive)
			{
				Write(Mutator, EPropertyChangeType::Interactive);
			}
		}

		void EndInteractive(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			if (!bInteractive)
			{
				return;
			}
			bInteractive = false;
			Write(Mutator, EPropertyChangeType::ValueSet);
			ValueHandle->NotifyFinishedChangingProperties();
			if (GEditor)
			{
				GEditor->EndTransaction();
			}
		}

		void Commit(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			if (bInteractive || !IsBound() || !WouldChange(Mutator))
			{
				return;
			}
			const FScopedTransaction Transaction(LOCTEXT("SetRange", "Set Range"));
			Write(Mutator, EPropertyChangeType::ValueSet);
			ValueHandle->NotifyFinishedChangingProperties();
		}

	private:
		bool IsBound() const
		{
			return ValueOffset != INDEX_NONE && ValueHandle->IsValidHandle();
		}

		/** The property owning RawValue. Reached by offset from the value: parent-handle navigation is unreliable on
		 *  rows built by AddExternalStructureProperty. */
		const FPCGExProperty_Range& OwnerOf(const void* RawValue) const
		{
			return *reinterpret_cast<const FPCGExProperty_Range*>(static_cast<const uint8*>(RawValue) - ValueOffset);
		}

		/** Visits each edited instance as (its positions, itself). */
		void ForEachInstance(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Visitor) const
		{
			TArray<void*> RawData;
			ValueHandle->AccessRawData(RawData);
			for (void* Raw : RawData)
			{
				if (Raw)
				{
					Visitor(*static_cast<FVector2D*>(Raw), OwnerOf(Raw));
				}
			}
		}

		bool WouldChange(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator) const
		{
			bool bChanges = false;
			ForEachInstance([&Mutator, &bChanges](FVector2D& Positions, const FPCGExProperty_Range& Owner)
			{
				FVector2D Candidate = Positions;
				Mutator(Candidate, Owner);
				bChanges |= Candidate != Positions;
			});
			return bChanges;
		}

		void Write(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator, const EPropertyChangeType::Type ChangeType)
		{
			if (!IsBound())
			{
				return;
			}
			ValueHandle->NotifyPreChange();
			ForEachInstance(Mutator);
			ValueHandle->NotifyPostChange(ChangeType);
		}

		TSharedRef<IPropertyHandle> ValueHandle;
		int32 ValueOffset = INDEX_NONE;
		bool bInteractive = false;
	};

	FText FormatNumber(const double InValue)
	{
		static const FNumberFormattingOptions Options = FNumberFormattingOptions().SetUseGrouping(false).SetMaximumFractionalDigits(6);
		return FText::AsNumber(InValue, &Options);
	}

	FText FormatTooltip(const FRangeBinding& Binding)
	{
		const FPCGExProperty_Range* Property = Binding.Peek();
		if (!Property)
		{
			return LOCTEXT("TooltipMultiple", "Multiple values.\nDouble-click to set exact values.");
		}
		const FVector2D Range = Property->GetRange();
		return FText::Format(
			LOCTEXT("TooltipFormat", "{0} to {1}\nNormalized: {2} to {3}  (bounds: {4} to {5})\nDrag an end or the bar. Double-click for exact values."),
			FormatNumber(Range.X), FormatNumber(Range.Y),
			FormatNumber(Property->Value.X), FormatNumber(Property->Value.Y),
			FormatNumber(Property->Min), FormatNumber(Property->Max));
	}

	/**
	 * One exact-value field of the popover: the start (X) or the end (Y) of the range, edited either as the
	 * stored 0..1 position or as the output value that position remaps to.
	 */
	TSharedRef<SWidget> MakeEndField(const TSharedRef<FRangeBinding>& Binding, const bool bStart, const bool bOutputSpace)
	{
		// The field's numbers versus positions, on a given instance. An output value has no position when that
		// instance's Min == Max.
		const auto ToNumber = [bOutputSpace](const FPCGExProperty_Range& Owner, const double Position)
		{
			return bOutputSpace ? Owner.PositionToValue(Position) : Position;
		};
		const auto ToPosition = [bOutputSpace](const FPCGExProperty_Range& Owner, const double InNumber, double& OutPosition)
		{
			if (bOutputSpace)
			{
				return Owner.ValueToPosition(InNumber, OutPosition);
			}
			OutPosition = InNumber;
			return true;
		};

		// The edited end stays between its own bound and the other end: the ends can meet, never cross.
		const auto SetEnd = [bStart, ToPosition](const double NewNumber)
		{
			return [bStart, ToPosition, NewNumber](FVector2D& Positions, const FPCGExProperty_Range& Owner)
			{
				double NewPosition = 0.0;
				if (!ToPosition(Owner, NewNumber, NewPosition))
				{
					return;
				}
				Positions = ConformPositions(Positions);
				if (bStart)
				{
					Positions.X = FMath::Clamp(NewPosition, 0.0, Positions.Y);
				}
				else
				{
					Positions.Y = FMath::Clamp(NewPosition, Positions.X, 1.0);
				}
			};
		};

		// The end's travel in positions, expressed in the field's numbers; Min > Max flips which side is lower.
		const auto Limit = [Binding, bStart, ToNumber](const bool bLower) -> TOptional<double>
		{
			const FPCGExProperty_Range* Property = Binding->Peek();
			if (!Property)
			{
				return TOptional<double>();
			}
			const FVector2D Positions = ConformPositions(Property->Value);
			const double From = ToNumber(*Property, bStart ? 0.0 : Positions.X);
			const double To = ToNumber(*Property, bStart ? Positions.Y : 1.0);
			return bLower ? FMath::Min(From, To) : FMath::Max(From, To);
		};

		// SSpinBox ordering this relies on: a typed commit fires OnValueCommitted then OnValueChanged; a spin fires
		// OnValueChanged per move and ends with OnValueCommitted then OnEndSliderMovement. The binding ignores
		// whichever call falls outside its current mode.
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(bStart ? LOCTEXT("StartTooltip", "Range start (X)") : LOCTEXT("EndTooltip", "Range end (Y)"))
			.IsEnabled_Lambda([Binding, bOutputSpace]()
			{
				// Nothing to type an output value against while the bounds leave no span.
				const FPCGExProperty_Range* Property = Binding->Peek();
				return !bOutputSpace || !Property || Property->Min != Property->Max;
			})
			.MinValue_Lambda([Limit]() { return Limit(true); })
			.MaxValue_Lambda([Limit]() { return Limit(false); })
			.MinSliderValue_Lambda([Limit]() { return Limit(true); })
			.MaxSliderValue_Lambda([Limit]() { return Limit(false); })
			.MinDesiredValueWidth(60.0f)
			.Value_Lambda([Binding, bStart, ToNumber]() -> TOptional<double>
			{
				const FPCGExProperty_Range* Property = Binding->Peek();
				if (!Property)
				{
					return TOptional<double>();
				}
				return ToNumber(*Property, bStart ? Property->Value.X : Property->Value.Y);
			})
			.OnBeginSliderMovement_Lambda([Binding]() { Binding->BeginInteractive(); })
			.OnValueChanged_Lambda([Binding, SetEnd](const double NewNumber) { Binding->ApplyInteractive(SetEnd(NewNumber)); })
			.OnValueCommitted_Lambda([Binding, SetEnd](const double NewNumber, ETextCommit::Type) { Binding->Commit(SetEnd(NewNumber)); })
			.OnEndSliderMovement_Lambda([Binding, SetEnd](const double NewNumber) { Binding->EndInteractive(SetEnd(NewNumber)); })
			.Label()
			[
				SNumericEntryBox<double>::BuildNarrowColorLabel(bStart ? PCGExInlineNumericWidgets::AxisColorX : PCGExInlineNumericWidgets::AxisColorY)
			];
	}

	/** Popover content: the stored 0..1 positions on the first row, the output values they remap to on the second. */
	TSharedRef<SWidget> MakeExactFields(const TSharedRef<FRangeBinding>& Binding)
	{
		const auto MakeRowLabel = [](const FText& InLabel, const FText& InToolTip)
		{
			return SNew(STextBlock)
				.Text(InLabel)
				.ToolTipText(InToolTip)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground());
		};

		return SNew(SBox)
			.Padding(FMargin(6.0f))
			.MinDesiredWidth(280.0f)
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.0f)
				.FillColumn(2, 1.0f)

				+ SGridPanel::Slot(0, 0).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					MakeRowLabel(LOCTEXT("NormalizedLabel", "Normalized"), LOCTEXT("NormalizedTooltip", "Position of each end between Min (0) and Max (1). This is what the property stores."))
				]
				+ SGridPanel::Slot(1, 0).Padding(0, 0, 2, 0)
				[
					MakeEndField(Binding, /*bStart=*/true, /*bOutputSpace=*/false)
				]
				+ SGridPanel::Slot(2, 0).Padding(2, 0, 0, 0)
				[
					MakeEndField(Binding, /*bStart=*/false, /*bOutputSpace=*/false)
				]

				+ SGridPanel::Slot(0, 1).VAlign(VAlign_Center).Padding(0, 4, 8, 0)
				[
					MakeRowLabel(LOCTEXT("ValueLabel", "Value"), LOCTEXT("ValueTooltip", "What the property outputs: each position remapped to Min..Max."))
				]
				+ SGridPanel::Slot(1, 1).Padding(0, 4, 2, 0)
				[
					MakeEndField(Binding, /*bStart=*/true, /*bOutputSpace=*/true)
				]
				+ SGridPanel::Slot(2, 1).Padding(2, 4, 0, 0)
				[
					MakeEndField(Binding, /*bStart=*/false, /*bOutputSpace=*/true)
				]
			];
	}

	TSharedRef<SWidget> Make(const TSharedRef<IPropertyHandle>& ValueHandle)
	{
		if (!ValueHandle->IsValidHandle())
		{
			return SNullWidget::NullWidget;
		}

		const TSharedRef<FRangeBinding> Binding = MakeShared<FRangeBinding>(ValueHandle);

		const TSharedRef<SMenuAnchor> Anchor = SNew(SMenuAnchor)
			.Placement(MenuPlacement_BelowAnchor)
			.OnGetMenuContent_Lambda([Binding]() { return MakeExactFields(Binding); });

		// Weak: the anchor owns the slider.
		const TWeakPtr<SMenuAnchor> WeakAnchor = Anchor;

		// The slider edits the stored positions, so it keeps its default 0..1 bounds whatever Min / Max are.
		Anchor->SetContent(
			SNew(SPCGExRangeSlider)
			.ToolTipText_Lambda([Binding]() { return FormatTooltip(*Binding); })
			.Value_Lambda([Binding]() -> TOptional<FVector2D>
			{
				const FPCGExProperty_Range* Property = Binding->Peek();
				return Property ? TOptional<FVector2D>(Property->Value) : TOptional<FVector2D>();
			})
			.OnBeginDrag_Lambda([Binding]() { Binding->BeginInteractive(); })
			.OnValueChanged_Lambda([Binding](const FVector2D& NewPositions)
			{
				Binding->ApplyInteractive([&NewPositions](FVector2D& Positions, const FPCGExProperty_Range&) { Positions = NewPositions; });
			})
			.OnEndDrag_Lambda([Binding](const FVector2D& NewPositions)
			{
				Binding->EndInteractive([&NewPositions](FVector2D& Positions, const FPCGExProperty_Range&) { Positions = NewPositions; });
			})
			.OnDoubleClicked_Lambda([WeakAnchor]()
			{
				if (const TSharedPtr<SMenuAnchor> PinnedAnchor = WeakAnchor.Pin())
				{
					PinnedAnchor->SetIsOpen(true);
				}
			}));

		return Anchor;
	}
}

#undef LOCTEXT_NAMESPACE
