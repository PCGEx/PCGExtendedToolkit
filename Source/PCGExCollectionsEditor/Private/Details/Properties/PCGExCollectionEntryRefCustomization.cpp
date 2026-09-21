// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Properties/PCGExCollectionEntryRefCustomization.h"

#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Details/Properties/PCGExCollectionEntryPickerWidget.h"

TSharedRef<IPropertyTypeCustomization> FPCGExCollectionEntryRefCustomization::MakeInstance()
{
	return MakeShared<FPCGExCollectionEntryRefCustomization>();
}

void FPCGExCollectionEntryRefCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(400.0f)
		.MaxDesiredWidth(3000.0f)
		[
			PCGExCollectionEntryPickerWidget::Make(PropertyHandle, PCGExCollectionEntryPickerWidget::ResolveOptions(PropertyHandle))
		];
}
