// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/PCGExRangePropertyWidget.h"

#include "PropertyHandle.h"
#include "Details/PCGExInlineNumericWidgets.h"
#include "Details/PCGExPropertyInlineWidgets.h"
#include "Details/PCGExRawPropertyEdit.h"
#include "Details/Properties/SPCGExRangeSlider.h"
#include "Properties/PCGExProperty_Range.h"
#include "Styling/AppStyle.h"
#include "Templates/Function.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExRangePropertyWidget"

namespace PCGExRangePropertyWidget
{
	/** Rewrites one edited instance's positions (its Value); gets the instance itself for its Min / Max conversions. */
	using FRangeMutator = TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)>;

	/**
	 * Typed front for FPCGExRawPropertyEdit on an FPCGExProperty_Range::Value handle. Gestures and discrete
	 * edits follow that class's rules; on top of them, a discrete edit that would change nothing is skipped.
	 */
	class FRangeBinding
	{
		/** The property whose Value sits at RawValue. */
		const FPCGExProperty_Range& OwnerOf(void* RawValue) const
		{
			return *reinterpret_cast<const FPCGExProperty_Range*>(PCGExPropertyInlineWidgets::AccessOwnerRaw(ValueProperty, RawValue));
		}

		/** Mutator in the raw-value form FPCGExRawPropertyEdit runs. */
		auto Erased(const FRangeMutator Mutator) const
		{
			return [this, Mutator](void* Raw) { Mutator(*static_cast<FVector2D*>(Raw), OwnerOf(Raw)); };
		}

	public:
		/** OwnerOf works by offset from Value's own address, so no other member can be bound. */
		static bool CanBind(const TSharedRef<IPropertyHandle>& InValueHandle)
		{
			const FProperty* Property = InValueHandle->IsValidHandle() ? InValueHandle->GetProperty() : nullptr;
			return Property
				&& Property->GetOwnerStruct() == FPCGExProperty_Range::StaticStruct()
				&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(FPCGExProperty_Range, Value);
		}

		explicit FRangeBinding(const TSharedRef<IPropertyHandle>& InValueHandle)
			: Edit(InValueHandle)
			, ValueProperty(InValueHandle->GetProperty())
		{
			check(CanBind(InValueHandle));
		}

		/** The edited property; null when the handle is dead or the edited instances' positions disagree. Read on the spot, never kept. */
		const FPCGExProperty_Range* Peek() const
		{
			void* Raw = nullptr;
			const TSharedRef<IPropertyHandle>& ValueHandle = Edit.GetHandle();
			if (!ValueHandle->IsValidHandle() || ValueHandle->GetValueData(Raw) != FPropertyAccess::Success)
			{
				return nullptr;
			}
			return &OwnerOf(Raw);
		}

		/** Visits every edited instance; none when the handle is dead. */
		void ForEachInstance(const TFunctionRef<void(const FPCGExProperty_Range&)> Visitor) const
		{
			Edit.ForEachRawValue([this, Visitor](void* Raw) { Visitor(OwnerOf(Raw)); });
		}

		/** The instance that stands for all of them where only one can be shown; null when the handle is dead. */
		const FPCGExProperty_Range* First() const
		{
			const FPCGExProperty_Range* Result = nullptr;
			ForEachInstance([&Result](const FPCGExProperty_Range& Owner) { Result = Result ? Result : &Owner; });
			return Result;
		}

		bool IsInteractive() const
		{
			return Edit.IsInteractive();
		}

		void BeginInteractive()
		{
			Edit.BeginInteractive(LOCTEXT("SetRange", "Set Range"));
		}

		void ApplyInteractive(const FRangeMutator Mutator)
		{
			Edit.ApplyInteractive(Erased(Mutator));
		}

		void EndInteractive(const FRangeMutator Mutator)
		{
			Edit.EndInteractive(Erased(Mutator));
		}

		void Commit(const FRangeMutator Mutator)
		{
			// A commit that ends a gesture is always sent. A discrete one that changes nothing gets no
			// transaction and no notifies, which a dry run on copies decides.
			bool bSend = Edit.IsInteractive();
			if (!bSend)
			{
				Edit.ForEachRawValue([this, Mutator, &bSend](void* Raw)
				{
					const FVector2D& Current = *static_cast<const FVector2D*>(Raw);
					FVector2D Candidate = Current;
					Mutator(Candidate, OwnerOf(Raw));
					bSend |= Candidate != Current;
				});
			}

			if (bSend)
			{
				Edit.Commit(LOCTEXT("SetRange", "Set Range"), Erased(Mutator));
			}
		}

	private:
		FPCGExRawPropertyEdit Edit;
		const FProperty* ValueProperty = nullptr;
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
		const auto EndNumber = [bStart, ToNumber](const FPCGExProperty_Range& Owner)
		{
			return ToNumber(Owner, bStart ? Owner.Value.X : Owner.Value.Y);
		};

		// What the field shows: unset unless every edited instance yields the same number. During a gesture the
		// first instance stands for all, because a field that went unset mid-drag would be swapped out from
		// under its own mouse capture.
		const auto ShownNumber = [Binding, EndNumber]() -> TOptional<double>
		{
			if (Binding->IsInteractive())
			{
				const FPCGExProperty_Range* Driver = Binding->First();
				return Driver ? TOptional<double>(EndNumber(*Driver)) : TOptional<double>();
			}

			TOptional<double> Shown;
			bool bUnanimous = true;
			Binding->ForEachInstance([&Shown, &bUnanimous, &EndNumber](const FPCGExProperty_Range& Owner)
			{
				const double Number = EndNumber(Owner);
				bUnanimous &= !Shown.IsSet() || Shown.GetValue() == Number;
				Shown = Number;
			});
			return bUnanimous ? Shown : TOptional<double>();
		};

		// Shared with the box, so IsShownNumber formats and parses exactly as the field does.
		const TSharedRef<INumericTypeInterface<double>> NumberInterface = MakeShared<TDefaultNumericTypeInterface<double>>();

		// SSpinBox re-commits its text on focus loss, typed in or not, and that text is rounded for display. A
		// commit of the shown number, or of its text read back, is no edit: writing it would round the position.
		const auto IsShownNumber = [ShownNumber, NumberInterface](const double InNumber)
		{
			const TOptional<double> Shown = ShownNumber();
			if (!Shown.IsSet())
			{
				return false;
			}
			if (Shown.GetValue() == InNumber)
			{
				return true;
			}
			const TOptional<double> ReadBack = NumberInterface->FromString(NumberInterface->ToString(Shown.GetValue()), Shown.GetValue());
			return ReadBack.IsSet() && ReadBack.GetValue() == InNumber;
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
				Positions = PCGExRangeSlider::ConformRange(Positions);
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
		// Set whenever there is an instance: SSpinBox takes an unset slider limit for the edge of the double range.
		const auto Limit = [Binding, bStart, ToNumber](const bool bLower) -> TOptional<double>
		{
			const FPCGExProperty_Range* Property = Binding->First();
			if (!Property)
			{
				return TOptional<double>();
			}
			const FVector2D Positions = PCGExRangeSlider::ConformRange(Property->Value);
			const double From = ToNumber(*Property, bStart ? 0.0 : Positions.X);
			const double To = ToNumber(*Property, bStart ? Positions.Y : 1.0);
			return bLower ? FMath::Min(From, To) : FMath::Max(From, To);
		};

		// The wheel is off: SSpinBox reports a notch as a change outside any gesture, which the binding drops.
		// MinValue / MaxValue stay unset, and explicitly: bound, they would clamp a commit before IsShownNumber
		// sees it; defaulted, SSpinBox freezes them as slider limits when ours are unset at construction.
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.AllowWheel(false)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.TypeInterface(NumberInterface)
			.ToolTipText(bStart ? LOCTEXT("StartTooltip", "Range start (X)") : LOCTEXT("EndTooltip", "Range end (Y)"))
			.IsEnabled_Lambda([Binding, bOutputSpace]()
			{
				// An output value can only be typed against bounds that leave a span.
				bool bEditable = !bOutputSpace;
				Binding->ForEachInstance([&bEditable](const FPCGExProperty_Range& Owner) { bEditable |= Owner.Min != Owner.Max; });
				return bEditable;
			})
			.MinValue(TOptional<double>())
			.MaxValue(TOptional<double>())
			.MinSliderValue_Lambda([Limit]() { return Limit(true); })
			.MaxSliderValue_Lambda([Limit]() { return Limit(false); })
			.MinDesiredValueWidth(60.0f)
			.Value_Lambda(ShownNumber)
			.OnBeginSliderMovement_Lambda([Binding]() { Binding->BeginInteractive(); })
			.OnValueChanged_Lambda([Binding, SetEnd](const double NewNumber) { Binding->ApplyInteractive(SetEnd(NewNumber)); })
			.OnValueCommitted_Lambda([Binding, SetEnd, IsShownNumber](const double NewNumber, ETextCommit::Type)
			{
				// Inside a gesture the commit is its end, whatever the number; only a discrete one can be a no-edit.
				if (Binding->IsInteractive() || !IsShownNumber(NewNumber))
				{
					Binding->Commit(SetEnd(NewNumber));
				}
			})
			// SSpinBox's commit has already closed the gesture; bound so its end never hangs on that ordering.
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
		if (!FRangeBinding::CanBind(ValueHandle))
		{
			return SNullWidget::NullWidget;
		}

		const TSharedRef<FRangeBinding> Binding = MakeShared<FRangeBinding>(ValueHandle);

		const TSharedRef<SMenuAnchor> Anchor = SNew(SMenuAnchor)
			.Placement(MenuPlacement_BelowAnchor)
			.OnGetMenuContent_Lambda([Binding]() { return MakeExactFields(Binding); });

		// Weak: the anchor owns the slider.
		const TWeakPtr<SMenuAnchor> WeakAnchor = Anchor;

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
