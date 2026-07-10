// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineWidgetRegistry.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/StructOnScope.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Details/PCGExEditorCustomizationUtils.h"

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
	if (ValuePropertyHandle.IsValid())
	{
		ValueWidget = Factory
			? (*Factory)(ValuePropertyHandle.ToSharedRef())
			: ValuePropertyHandle->CreatePropertyValueWidget();
	}

	const bool bHasCustomFactory = (Factory != nullptr);
	Row.CustomWidget()
	   .NameContent()
		[NameContent]
		.ValueContent()
		.MinDesiredWidth(bHasCustomFactory ? 250.0f : 125.0f)
		.MaxDesiredWidth(bHasCustomFactory ? 3000.0f : 600.0f)
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

	for (TFieldIterator<FProperty> It(InnerStruct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		const FName PropName = Property->GetFName();
		if (PropName == TEXT("PropertyName") || PropName == TEXT("HeaderId") || PropName == TEXT("OutputBuffer"))
		{
			continue;
		}

		IDetailPropertyRow& PropRow = *ChildBuilder.AddExternalStructureProperty(Scope, PropName);
		PropRow.IsEnabled(IsEnabled);

		if (WeakOwner.IsValid())
		{
			PCGExEditorCustomizationUtils::HookOwnerChangeOnHandleChanged(PropRow.GetPropertyHandle(), WeakOwner);
		}

		if (ValueFactory && PropName == TEXT("Value"))
		{
			if (TSharedPtr<IPropertyHandle> ValueHandle = PropRow.GetPropertyHandle(); ValueHandle.IsValid())
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
		}
	}
}
