// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyCompiledCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyCompiledCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyCompiledCustomization());
}

void FPCGExPropertyCompiledCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Collapse the inner group header that FInstancedStructDetails would otherwise inject
	// between the type-picker row and the Value field. The schema header already shows
	// the property name and type, so this wrapper is pure noise.
	HeaderRow.Visibility(EVisibility::Collapsed);
}

void FPCGExPropertyCompiledCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Resolve the outer property's USTRUCT name once; needed for inline-widget lookup
	// and for deciding which auxiliary meta fields (AllowedClass / Range) to show.
	FName OuterStructName = NAME_None;
	if (const FStructProperty* OuterStructProp = CastField<FStructProperty>(PropertyHandle->GetProperty()))
	{
		if (OuterStructProp->Struct)
		{
			OuterStructName = OuterStructProp->Struct->GetFName();
		}
	}

	// Iterate all children and render in declaration order, skipping internal fields.
	// "Value" gets the registered Edit-mode inline widget when available; all other
	// authored fields (e.g. editor-only AllowedClass / Range on opted-in types) fall
	// through to default rendering, which lets their own IPropertyTypeCustomization
	// (if any) take over.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid())
		{
			continue;
		}

		// Identity caches and the recorded pick are not editable; nothing to render for them.
		const FProperty* ChildProperty = ChildHandle->GetProperty();
		if (!ChildProperty || !ChildProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		const FName ChildName = ChildProperty->GetFName();

		if (ChildName == TEXT("Value"))
		{
			if (const FPCGExMakeInlineWidgetFn* Factory = FPCGExInlineWidgetRegistry::Find(OuterStructName, EPCGExInlineWidgetMode::Edit))
			{
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
				Row.CustomWidget(/*bShowChildren=*/false)
				   .NameContent()
					[
						ChildHandle->CreatePropertyNameWidget()
					]
					.ValueContent()
					.MinDesiredWidth(250.0f)
					.MaxDesiredWidth(3000.0f)
					[
						(*Factory)(ChildHandle.ToSharedRef())
					];
				continue;
			}
		}

		ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}
