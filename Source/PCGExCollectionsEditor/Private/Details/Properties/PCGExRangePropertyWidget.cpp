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
	/**
	 * Typed front for FPCGExRawPropertyEdit on an FPCGExProperty_Range::Value handle. A mutator rewrites one
	 * instance's positions (its Value) and gets the instance itself for its Min / Max conversions.
	 *
	 * Apply and Commit route by whether a gesture is open, so one SSpinBox can feed it whatever it sends: a
	 * change inside a gesture joins it, a commit inside a gesture closes it, and either one outside a gesture
	 * is a discrete edit, skipped when nothing would change.
	 */
	class FRangeBinding
	{
	public:
		explicit FRangeBinding(const TSharedRef<IPropertyHandle>& InValueHandle)
			: Edit(InValueHandle)
		{
			// Only a handle on FPCGExProperty_Range::Value can be resolved back to its owning property.
			const FProperty* Property = InValueHandle->GetProperty();
			if (Property && Property->GetOwnerStruct() == FPCGExProperty_Range::StaticStruct())
			{
				ValueProperty = Property;
			}
		}

		/** The edited property; null when the handle is dead or the edited instances disagree. Read on the spot, never kept. */
		const FPCGExProperty_Range* Peek() const
		{
			void* Raw = nullptr;
			if (!IsBound() || Edit.GetHandle()->GetValueData(Raw) != FPropertyAccess::Success)
			{
				return nullptr;
			}
			return OwnerOf(Raw);
		}

		void BeginInteractive()
		{
			if (IsBound())
			{
				Edit.BeginInteractive(LOCTEXT("SetRange", "Set Range"));
			}
		}

		void Apply(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			if (Edit.IsInteractive())
			{
				Edit.ApplyInteractive([this, &Mutator](void* Raw) { Mutate(*static_cast<FVector2D*>(Raw), Raw, Mutator); });
			}
			else
			{
				CommitDiscrete(Mutator);
			}
		}

		void Commit(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			if (Edit.IsInteractive())
			{
				EndInteractive(Mutator);
			}
			else
			{
				CommitDiscrete(Mutator);
			}
		}

		void EndInteractive(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)> Mutator)
		{
			Edit.EndInteractive([this, &Mutator](void* Raw) { Mutate(*static_cast<FVector2D*>(Raw), Raw, Mutator); });
		}

	private:
		bool IsBound() const
		{
			return ValueProperty && Edit.GetHandle()->IsValidHandle();
		}

		const FPCGExProperty_Range* OwnerOf(void* RawValue) const
		{
			return PCGExPropertyInlineWidgets::AccessOwner<const FPCGExProperty_Range>(ValueProperty, RawValue);
		}

		/** Runs Mutator on Positions as an edit of the instance whose raw value is RawValue. */
		void Mutate(FVector2D& Positions, void* RawValue, const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)>& Mutator) const
		{
			if (const FPCGExProperty_Range* Owner = OwnerOf(RawValue))
			{
				Mutator(Positions, *Owner);
			}
		}

		void CommitDiscrete(const TFunctionRef<void(FVector2D&, const FPCGExProperty_Range&)>& Mutator)
		{
			if (!IsBound())
			{
				return;
			}

			// Dry run on copies first: an edit that changes nothing gets no transaction and no notifies.
			bool bChanges = false;
			Edit.ForEachRawValue([this, &Mutator, &bChanges](void* Raw)
			{
				const FVector2D& Current = *static_cast<const FVector2D*>(Raw);
				FVector2D Candidate = Current;
				Mutate(Candidate, Raw, Mutator);
				bChanges |= Candidate != Current;
			});

			if (bChanges)
			{
				Edit.Commit(LOCTEXT("SetRange", "Set Range"), [this, &Mutator](void* Raw) { Mutate(*static_cast<FVector2D*>(Raw), Raw, Mutator); });
			}
		}

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

		// Shared with the box, so IsOwnText formats and parses exactly as the field does.
		const TSharedRef<INumericTypeInterface<double>> NumberInterface = MakeShared<TDefaultNumericTypeInterface<double>>();

		// SSpinBox re-commits its text on focus loss, typed in or not. A number that is only the shown value's
		// text read back is not an edit: writing it would round the stored position to that text.
		const auto IsOwnText = [Binding, bStart, ToNumber, NumberInterface](const double InNumber)
		{
			const FPCGExProperty_Range* Property = Binding->Peek();
			if (!Property)
			{
				return false;
			}
			const double ShownNumber = ToNumber(*Property, bStart ? Property->Value.X : Property->Value.Y);
			const TOptional<double> ReadBack = NumberInterface->FromString(NumberInterface->ToString(ShownNumber), ShownNumber);
			return ReadBack.IsSet() && ReadBack.GetValue() == InNumber;
		};

		// The edited end stays between its own bound and the other end: the ends can meet, never cross.
		const auto SetEnd = [bStart, ToPosition, IsOwnText](const double NewNumber)
		{
			// Decided once per edit, not per instance: instances that disagree must all take the typed number.
			const bool bOwnText = IsOwnText(NewNumber);
			return [bStart, ToPosition, NewNumber, bOwnText](FVector2D& Positions, const FPCGExProperty_Range& Owner)
			{
				double NewPosition = 0.0;
				if (bOwnText || !ToPosition(Owner, NewNumber, NewPosition))
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
		const auto Limit = [Binding, bStart, ToNumber](const bool bLower) -> TOptional<double>
		{
			const FPCGExProperty_Range* Property = Binding->Peek();
			if (!Property)
			{
				return TOptional<double>();
			}
			const FVector2D Positions = PCGExRangeSlider::ConformRange(Property->Value);
			const double From = ToNumber(*Property, bStart ? 0.0 : Positions.X);
			const double To = ToNumber(*Property, bStart ? Positions.Y : 1.0);
			return bLower ? FMath::Min(From, To) : FMath::Max(From, To);
		};

		// What SSpinBox sends: a typed or arrow-key edit is OnValueCommitted then OnValueChanged, a wheel tick is
		// OnValueChanged alone, a spin is OnValueChanged per move closed by OnValueCommitted then
		// OnEndSliderMovement. Slider limits only: SetEnd clamps, and needs typed text to reach it unclamped.
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.TypeInterface(NumberInterface)
			.ToolTipText(bStart ? LOCTEXT("StartTooltip", "Range start (X)") : LOCTEXT("EndTooltip", "Range end (Y)"))
			.IsEnabled_Lambda([Binding, bOutputSpace]()
			{
				// Nothing to type an output value against while the bounds leave no span.
				const FPCGExProperty_Range* Property = Binding->Peek();
				return !bOutputSpace || !Property || Property->Min != Property->Max;
			})
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
			.OnValueChanged_Lambda([Binding, SetEnd](const double NewNumber) { Binding->Apply(SetEnd(NewNumber)); })
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
				Binding->Apply([&NewPositions](FVector2D& Positions, const FPCGExProperty_Range&) { Positions = NewPositions; });
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
