// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExAttributeToPinCustomization.h"

#include "PropertyHandle.h"
#include "Elements/PCGExAttributesToPins.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExAttributeToPinCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExAttributeToPinCustomization());
}

void FPCGExAttributeToPinCustomization::AddInlineControls(const TSharedRef<IPropertyHandle>& PropertyHandle, const TSharedRef<SHorizontalBox>& Row)
{
	const TSharedPtr<IPropertyHandle> TypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAttributeToPinDetails, Type));
	if (!TypeHandle.IsValid())
	{
		return;
	}

	Row->AddSlot().Padding(1).AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(110)
		[
			TypeHandle->CreatePropertyValueWidget()
		]
	];
}
