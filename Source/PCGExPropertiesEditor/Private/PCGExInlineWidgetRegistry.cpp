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
#include "UObject/StructOnScope.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
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

	static TMap<FKey, FPCGExMakeInlineWidgetFn>& GetMap()
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

void FPCGExInlineWidgetRegistry::Clear()
{
	PCGExInlineWidgetRegistry_Private::GetMap().Empty();
}

IDetailPropertyRow* FPCGExInlineWidgetRegistry::AddCompactValueRow(
	IDetailChildrenBuilder& ChildBuilder,
	TSharedRef<FStructOnScope> Scope,
	UScriptStruct* InnerStruct,
	TSharedRef<SWidget> NameContent,
	TAttribute<bool> IsEnabled)
{
	const FProperty* ValueProperty = InnerStruct->FindPropertyByName(TEXT("Value"));
	if (!ValueProperty)
	{
		return nullptr;
	}

	IDetailPropertyRow& Row = *ChildBuilder.AddExternalStructureProperty(Scope, ValueProperty->GetFName());

	const FPCGExMakeInlineWidgetFn* Factory = Find(InnerStruct->GetFName(), EPCGExInlineWidgetMode::Compact);
	TSharedPtr<IPropertyHandle> ValuePropertyHandle = Row.GetPropertyHandle();
	TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;
	bool bLocked = false;
	if (ValuePropertyHandle.IsValid())
	{
		ValueWidget = Factory
			? (*Factory)(ValuePropertyHandle.ToSharedRef())
			: ValuePropertyHandle->CreatePropertyValueWidget();

		const FStructView Host(InnerStruct, Scope->GetStructMemory());
		if (HasChoices(Host, &bLocked))
		{
			ValueWidget = WrapValueWidgetWithChoices(ValueWidget, Host, ValuePropertyHandle.ToSharedRef());
		}
	}

	const bool bWide = (Factory != nullptr) || bLocked;
	Row.CustomWidget()
	   .NameContent()
		[NameContent]
		.ValueContent()
		.MinDesiredWidth(bWide ? 250.0f : 125.0f)
		.MaxDesiredWidth(bWide ? 3000.0f : 600.0f)
		[
			SNew(SBox)
			.IsEnabled(IsEnabled)
			[
				ValueWidget
			]
		];

	return &Row;
}

void FPCGExInlineWidgetRegistry::AddComplexValueRows(
	IDetailChildrenBuilder& ChildBuilder,
	TSharedRef<FStructOnScope> Scope,
	UScriptStruct* InnerStruct,
	TAttribute<bool> IsEnabled,
	const TWeakObjectPtr<UObject>& WeakOwner)
{
	// Types with a registered Compact factory but WITHOUT the PCGExInlineValue meta get their
	// Value rendered through the factory as a FULL-WIDTH row (e.g. the Float Curve editor,
	// whose width must be pinned by the panel rather than track the widget's desired size).
	// Types with the inline meta never reach this path, so existing rows are unaffected.
	const FPCGExMakeInlineWidgetFn* ValueFactory = Find(InnerStruct->GetFName(), EPCGExInlineWidgetMode::Compact);

	const FStructView Host(InnerStruct, Scope->GetStructMemory());
	bool bLocked = false;
	const bool bHasChoices = HasChoices(Host, &bLocked);

	for (TFieldIterator<FProperty> It(InnerStruct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		// AddExternalStructureProperty returns null for a non-editable field (identity caches, the recorded pick).
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Choices is a read-only mirror on these rows; the schema-edit path owns its editor.
		const FName PropName = Property->GetFName();
		if (PropName == TEXT("OutputBuffer") || PropName == TEXT("Choices"))
		{
			continue;
		}

		IDetailPropertyRow& PropRow = *ChildBuilder.AddExternalStructureProperty(Scope, PropName);
		PropRow.IsEnabled(IsEnabled);

		if (WeakOwner.IsValid())
		{
			PCGExEditorCustomizationUtils::HookOwnerChangeOnHandleChanged(PropRow.GetPropertyHandle(), WeakOwner);
		}

		if (PropName != TEXT("Value"))
		{
			continue;
		}
		TSharedPtr<IPropertyHandle> ValueHandle = PropRow.GetPropertyHandle();
		if (!ValueHandle.IsValid())
		{
			continue;
		}

		if (bHasChoices)
		{
			HookUnbindOnFreeEdit(ValueHandle.ToSharedRef(), Host);
			if (bLocked)
			{
				// Locked: the picker stands in for the value editor, whatever its usual shape.
				PropRow.CustomWidget(/*bShowChildren=*/false)
				       .NameContent()
					[
						ValueHandle->CreatePropertyNameWidget()
					]
					.ValueContent()
					.MinDesiredWidth(250.0f)
					.MaxDesiredWidth(3000.0f)
					[
						SNew(SBox)
						.IsEnabled(IsEnabled)
						[
							SNew(SPCGExPropertyChoicePicker, Host, ValueHandle.ToSharedRef()).Locked(true)
						]
					];
				continue;
			}
		}

		if (ValueFactory)
		{
			PropRow.CustomWidget(/*bShowChildren=*/false)
			       .WholeRowContent()
			[
				SNew(SBox)
				.IsEnabled(IsEnabled)
				[
					(*ValueFactory)(ValueHandle.ToSharedRef())
				]
			];
		}

		// Unlocked: the value keeps its editor; a quick pick sits right under it.
		if (bHasChoices)
		{
			AddChoicesRow(ChildBuilder, Host, ValueHandle.ToSharedRef(), IsEnabled);
		}
	}
}

bool FPCGExInlineWidgetRegistry::HasChoices(const FConstStructView Host, bool* bOutLocked)
{
	const UScriptStruct* Struct = Host.GetScriptStruct();
	const FPCGExProperty* Property = Struct && Struct->IsChildOf(FPCGExProperty::StaticStruct()) ? Host.GetPtr<FPCGExProperty>() : nullptr;
	const bool bHasChoices = Property && !Property->Choices.Items.IsEmpty();
	if (bOutLocked)
	{
		*bOutLocked = bHasChoices && Property->Choices.bLocked;
	}
	return bHasChoices;
}

TSharedRef<SWidget> FPCGExInlineWidgetRegistry::WrapValueWidgetWithChoices(
	TSharedRef<SWidget> ValueWidget,
	const FStructView Host,
	const TSharedRef<IPropertyHandle>& NotifyHandle)
{
	bool bLocked = false;
	if (!HasChoices(Host, &bLocked))
	{
		return ValueWidget;
	}

	HookUnbindOnFreeEdit(NotifyHandle, Host);
	if (bLocked)
	{
		return SNew(SPCGExPropertyChoicePicker, Host, NotifyHandle).Locked(true);
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			ValueWidget
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SNew(SPCGExPropertyChoicePicker, Host, NotifyHandle).Locked(false)
		];
}

void FPCGExInlineWidgetRegistry::AddChoicesRow(
	IDetailChildrenBuilder& ChildBuilder,
	const FStructView Host,
	const TSharedRef<IPropertyHandle>& NotifyHandle,
	TAttribute<bool> IsEnabled)
{
	bool bLocked = false;
	if (!HasChoices(Host, &bLocked))
	{
		return;
	}

	ChildBuilder.AddCustomRow(LOCTEXT("ChoicesRowFilter", "Choices"))
	            .NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChoicesRowLabel", "Choices"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(3000.0f)
		[
			SNew(SBox)
			.IsEnabled(IsEnabled)
			[
				SNew(SPCGExPropertyChoicePicker, Host, NotifyHandle).Locked(bLocked)
			]
		];
}

void FPCGExInlineWidgetRegistry::HookUnbindOnFreeEdit(const TSharedRef<IPropertyHandle>& Handle, const FStructView Host)
{
	// Multicast on the node, so it coexists with HookOwnerChangeOnHandleChanged. A pick re-applies an identical
	// value and keeps its binding; anything else that commits through this handle is a free edit.
	const TDelegate<void(const FPropertyChangedEvent&)> OnChanged =
		TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(
			[Host](const FPropertyChangedEvent& InEvent)
			{
				if (InEvent.ChangeType == EPropertyChangeType::Interactive)
				{
					return;
				}
				PCGExProperties::UnbindDivergedChoice(Host);
			});

	Handle->SetOnPropertyValueChangedWithData(OnChanged);
	Handle->SetOnChildPropertyValueChangedWithData(OnChanged);
}

#undef LOCTEXT_NAMESPACE
