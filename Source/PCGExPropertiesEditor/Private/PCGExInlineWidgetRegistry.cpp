// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineWidgetRegistry.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "Details/PCGExEditorCustomizationUtils.h"
#include "Styling/AppStyle.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SPCGExLabelPill.h"
#include "Widgets/SPCGExPropertyChoicePicker.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FPCGExInlineWidgetRegistry"

namespace PCGExInlineWidgetRegistry_Private
{
	struct FKey
	{
		FName StructName;
		EPCGExInlineWidgetMode Mode;

		friend bool operator==(const FKey& A, const FKey& B)
		{
			return A.StructName == B.StructName && A.Mode == B.Mode;
		}

		friend uint32 GetTypeHash(const FKey& K)
		{
			return HashCombineFast(GetTypeHash(K.StructName), GetTypeHash(static_cast<uint8>(K.Mode)));
		}
	};

	TMap<FKey, FPCGExMakeInlineWidgetFn>& GetMap()
	{
		static TMap<FKey, FPCGExMakeInlineWidgetFn> Map;
		return Map;
	}
}

void FPCGExInlineWidgetRegistry::Register(FName StructName, EPCGExInlineWidgetMode Mode, FPCGExMakeInlineWidgetFn Factory)
{
	if (StructName.IsNone() || !Factory)
	{
		return;
	}
	PCGExInlineWidgetRegistry_Private::GetMap().Add({StructName, Mode}, MoveTemp(Factory));
}

void FPCGExInlineWidgetRegistry::RegisterAllModes(FName StructName, FPCGExMakeInlineWidgetFn Factory)
{
	if (StructName.IsNone() || !Factory)
	{
		return;
	}
	PCGExInlineWidgetRegistry_Private::GetMap().Add({StructName, EPCGExInlineWidgetMode::Edit}, Factory);
	PCGExInlineWidgetRegistry_Private::GetMap().Add({StructName, EPCGExInlineWidgetMode::Compact}, MoveTemp(Factory));
}

void FPCGExInlineWidgetRegistry::Unregister(FName StructName, EPCGExInlineWidgetMode Mode)
{
	PCGExInlineWidgetRegistry_Private::GetMap().Remove({StructName, Mode});
}

void FPCGExInlineWidgetRegistry::UnregisterAllModes(FName StructName)
{
	PCGExInlineWidgetRegistry_Private::GetMap().Remove({StructName, EPCGExInlineWidgetMode::Edit});
	PCGExInlineWidgetRegistry_Private::GetMap().Remove({StructName, EPCGExInlineWidgetMode::Compact});
}

const FPCGExMakeInlineWidgetFn* FPCGExInlineWidgetRegistry::Find(FName StructName, EPCGExInlineWidgetMode Mode)
{
	return PCGExInlineWidgetRegistry_Private::GetMap().Find({StructName, Mode});
}

bool FPCGExInlineWidgetRegistry::IsInlineValue(const UScriptStruct* PropertyStruct, EPCGExInlineWidgetMode Mode)
{
	if (!PropertyStruct || !PropertyStruct->HasMetaData(TEXT("PCGExInlineValue")))
	{
		return false;
	}
	if (Find(PropertyStruct->GetFName(), Mode))
	{
		return true;
	}
	// IPropertyHandle::CreatePropertyValueWidget() never runs a struct's type customization and draws nothing for it.
	const FProperty* ValueProperty = PCGExProperties::FindValueProperty(PropertyStruct);
	return ValueProperty && !ValueProperty->IsA<FStructProperty>();
}

void FPCGExInlineWidgetRegistry::Clear()
{
	PCGExInlineWidgetRegistry_Private::GetMap().Empty();
}

IDetailPropertyRow* FPCGExInlineWidgetRegistry::AddCompactValueRow(
	IDetailChildrenBuilder& ChildBuilder,
	TSharedRef<FStructOnScope> Scope,
	UScriptStruct* InnerStruct,
	TSharedRef<SWidget> NameContent,
	TAttribute<bool> IsEnabled,
	const FPCGExChoiceRowArgs& Choices)
{
	const FProperty* ValueProperty = PCGExProperties::FindValueProperty(InnerStruct);
	if (!ValueProperty)
	{
		return nullptr;
	}

	IDetailPropertyRow* Row = ChildBuilder.AddExternalStructureProperty(Scope, ValueProperty->GetFName());
	if (!Row)
	{
		return nullptr;
	}

	const FPCGExMakeInlineWidgetFn* Factory = Find(InnerStruct->GetFName(), EPCGExInlineWidgetMode::Compact);
	const TSharedPtr<IPropertyHandle> ValuePropertyHandle = Row->GetPropertyHandle();
	TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;
	bool bLockedNow = false;
	if (ValuePropertyHandle.IsValid())
	{
		ValueWidget = Factory
			? (*Factory)(ValuePropertyHandle.ToSharedRef())
			: ValuePropertyHandle->CreatePropertyValueWidget();

		if (Choices.Host.IsSet())
		{
			const TSharedRef<FPCGExChoiceRowState> State = MakeShared<FPCGExChoiceRowState>(Choices.Host);
			bLockedNow = State->Get().bLocked;
			ValueWidget = WrapValueWidgetWithChoices(ValueWidget, State, ValuePropertyHandle.ToSharedRef(), IsEnabled, Choices);
		}
		else
		{
			ValueWidget = SNew(SBox).IsEnabled(IsEnabled)[ValueWidget];
		}
	}

	// A row's widths are fixed when it is built: a lock toggled later keeps them until the panel rebuilds.
	const bool bWide = (Factory != nullptr) || bLockedNow;
	Row->CustomWidget()
	   .NameContent()
		[
			NameContent
		]
		.ValueContent()
		.MinDesiredWidth(bWide ? WideValueMinWidth : NarrowValueMinWidth)
		.MaxDesiredWidth(bWide ? WideValueMaxWidth : NarrowValueMaxWidth)
		[
			ValueWidget
		];

	return Row;
}

void FPCGExInlineWidgetRegistry::AddComplexValueRows(
	IDetailChildrenBuilder& ChildBuilder,
	TSharedRef<FStructOnScope> Scope,
	UScriptStruct* InnerStruct,
	TAttribute<bool> IsEnabled,
	const TWeakObjectPtr<UObject>& WeakOwner,
	const FPCGExChoiceRowArgs& Choices)
{
	// Types with a registered Compact factory but WITHOUT the PCGExInlineValue meta get their
	// Value rendered through the factory as a FULL-WIDTH row (e.g. the Float Curve editor,
	// whose width must be pinned by the panel rather than track the widget's desired size).
	// Types with the inline meta never reach this path, so existing rows are unaffected.
	const FPCGExMakeInlineWidgetFn* ValueFactory = Find(InnerStruct->GetFName(), EPCGExInlineWidgetMode::Compact);

	const FProperty* ValueProperty = PCGExProperties::FindValueProperty(InnerStruct);
	const TSharedPtr<FPCGExChoiceRowState> State = Choices.Host.IsSet() ? MakeShared<FPCGExChoiceRowState>(Choices.Host).ToSharedPtr() : nullptr;

	for (TFieldIterator<FProperty> It(InnerStruct); It; ++It)
	{
		const FProperty* Property = *It;

		// AddExternalStructureProperty returns null for a non-editable field (identity caches, the recorded pick).
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Choices is a mirror on these rows: the Choices row below is its whole UI.
		const FName PropName = Property->GetFName();
		if (PropName == TEXT("OutputBuffer") || PropName == GET_MEMBER_NAME_CHECKED(FPCGExProperty, Choices))
		{
			continue;
		}

		IDetailPropertyRow* PropRow = ChildBuilder.AddExternalStructureProperty(Scope, PropName);
		if (!PropRow)
		{
			continue;
		}
		PropRow->IsEnabled(IsEnabled);

		const TSharedPtr<IPropertyHandle> PropHandle = PropRow->GetPropertyHandle();
		if (WeakOwner.IsValid())
		{
			PCGExEditorCustomizationUtils::HookOwnerChangeOnHandleChanged(PropHandle, WeakOwner);
		}

		if (Property != ValueProperty || !PropHandle.IsValid())
		{
			continue;
		}

		if (ValueFactory)
		{
			PropRow->CustomWidget(/*bShowChildren=*/false)
			       .WholeRowContent()
			[
				SNew(SBox)
				.IsEnabled(IsEnabled)
				[
					(*ValueFactory)(PropHandle.ToSharedRef())
				]
			];
		}

		if (State.IsValid())
		{
			// Locked: the Choices row stands in for the value editor, whatever its usual shape.
			if (!Choices.bKeepEditorWhenLocked)
			{
				PropRow->Visibility(VisibleUnlessLocked(State.ToSharedRef()));
			}
			AddChoicesRow(ChildBuilder, State.ToSharedRef(), PropHandle.ToSharedRef(), IsEnabled, Choices.EditChoices);
		}
	}
}

TAttribute<EVisibility> FPCGExInlineWidgetRegistry::VisibleUnlessLocked(const TSharedRef<FPCGExChoiceRowState>& State)
{
	return TAttribute<EVisibility>::CreateLambda([State]()
	{
		return State->Get().bLocked ? EVisibility::Collapsed : EVisibility::Visible;
	});
}

TSharedRef<SWidget> FPCGExInlineWidgetRegistry::WrapValueWidgetWithChoices(
	TSharedRef<SWidget> ValueWidget,
	const TSharedRef<FPCGExChoiceRowState>& State,
	const TSharedRef<IPropertyHandle>& NotifyHandle,
	TAttribute<bool> IsEnabled,
	const FPCGExChoiceRowArgs& Choices)
{
	const bool bCanAuthor = Choices.EditChoices.IsBound();
	const bool bKeepEditor = Choices.bKeepEditorWhenLocked;

	// A bound value follows its choice and detaches on edit: dimmed, still editable. Custom stays full.
	// SBorder's tint multiplies through its children; RenderOpacity is a fixed float, not an attribute.
	const TAttribute<FLinearColor> BoundTint = TAttribute<FLinearColor>::CreateLambda([State]() -> FLinearColor
	{
		// Above the disabled-row look, so a bound row still reads as live.
		return FLinearColor(1.0f, 1.0f, 1.0f, State->Get().bBound ? 0.7f : 1.0f);
	});

	// The bound choice's label rides the value as a small pill: labels carry meaning the number alone does not.
	// Hit-test invisible, so the editor under it stays clickable.
	const TSharedRef<SWidget> Badge = SNew(SPCGExLabelPill)
		.Visibility_Lambda([State]() { return State->Get().bBound ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Label_Lambda([State]() { return State->Get().SelectedLabel; });

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.IsEnabled(IsEnabled)
			.Visibility_Lambda([State, bKeepEditor]() { return State->Get().bLocked && !bKeepEditor ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(0)
					.ColorAndOpacity(BoundTint)
					[
						ValueWidget
					]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 0, 6, 0))
				[
					Badge
				]
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			// Outside the enabled box: the menu leads to "Edit Choices...", which a disabled override must still reach.
			SNew(SBox)
			.Visibility_Lambda([State]() { return State->Get().bLocked ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SPCGExPropertyChoicePicker, State, NotifyHandle)
				.Locked(true)
				.CanPick(IsEnabled)
				.OnGetEditContent(Choices.EditChoices)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SNew(SBox)
			.Visibility_Lambda([State, bCanAuthor]()
			{
				const FPCGExChoiceRowState::FSnapshot& Snapshot = State->Get();
				return !Snapshot.bLocked && (Snapshot.bHasChoices || bCanAuthor) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SPCGExPropertyChoicePicker, State, NotifyHandle)
				.Locked(false)
				.CanPick(IsEnabled)
				.OnGetEditContent(Choices.EditChoices)
			]
		];
}

void FPCGExInlineWidgetRegistry::AddChoicesRow(
	IDetailChildrenBuilder& ChildBuilder,
	const TSharedRef<FPCGExChoiceRowState>& State,
	const TSharedRef<IPropertyHandle>& NotifyHandle,
	TAttribute<bool> IsEnabled,
	FOnGetContent EditChoices)
{
	const bool bCanAuthor = EditChoices.IsBound();

	ChildBuilder.AddCustomRow(LOCTEXT("ChoicesRowFilter", "Choices"))
	            .Visibility(TAttribute<EVisibility>::CreateLambda([State, bCanAuthor]()
	            {
		            return State->Get().bHasChoices || bCanAuthor ? EVisibility::Visible : EVisibility::Collapsed;
	            }))
	            .NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChoicesRowLabel", "Choices"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(WideValueMinWidth)
		.MaxDesiredWidth(WideValueMaxWidth)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.Visibility_Lambda([State]() { return State->Get().bLocked ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SPCGExPropertyChoicePicker, State, NotifyHandle)
					.Locked(true)
					.CanPick(IsEnabled)
					.OnGetEditContent(EditChoices)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.Visibility_Lambda([State]() { return State->Get().bLocked ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					SNew(SPCGExPropertyChoicePicker, State, NotifyHandle)
					.Locked(false)
					.CanPick(IsEnabled)
					.OnGetEditContent(EditChoices)
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
