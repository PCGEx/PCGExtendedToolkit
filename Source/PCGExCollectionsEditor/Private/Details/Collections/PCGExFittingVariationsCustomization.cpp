// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExFittingVariationsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyHandle.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"

#define PCGEX_SMALL_LABEL(_TEXT) \
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)]

#define PCGEX_SMALL_LABEL_COL(_TEXT, _COL) \
+ SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Center).Padding(1,8,1,2)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(_COL)).MinDesiredWidth(10)]

#define PCGEX_SEP_LABEL(_TEXT)\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0)\
[SNew(STextBlock).Text(FText::FromString(_TEXT)).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray))]

#define PCGEX_FIELD_BASE(_HANDLE, _TYPE, _PART, _TOOLTIP)\
+ SHorizontalBox::Slot().Padding(1)[\
SNew(SNumericEntryBox<double>).Value_Lambda([=]() -> TOptional<double>{_TYPE V; _HANDLE->GetValue(V); return V._PART;})\
.OnValueCommitted_Lambda([=](double NewVal, ETextCommit::Type){_TYPE V; _HANDLE->GetValue(V); V._PART = NewVal; _HANDLE->SetValue(V);})\
.ToolTipText(FString(_TOOLTIP).IsEmpty() ? _HANDLE->GetToolTipText() : FText::FromString(_TOOLTIP)).AllowSpin(true)

#define PCGEX_STEP_VISIBILITY(_HANDLE)\
.Visibility_Lambda([_HANDLE](){uint8 EnumValue = 0;\
if (_HANDLE->GetValue(EnumValue) == FPropertyAccess::Success){ return EnumValue ? EVisibility::Visible : EVisibility::Collapsed;}\
return EVisibility::Collapsed;})

#define PCGEX_FIELD(_HANDLE, _TYPE, _PART, _TOOLTIP) PCGEX_FIELD_BASE(_HANDLE, _TYPE, _PART, _TOOLTIP)]

// Inline step spinner as third column on an axis row.
// FillWidth(1) alongside the min:max wrapper at FillWidth(2) gives equal thirds.
// When Collapsed (snapping off), the slot is removed from layout and min:max fills 100%.
#define PCGEX_STEP_SLOT(_STEPS_HANDLE, _TYPE, _PART, _SNAP_HANDLE) \
+ SHorizontalBox::Slot().Padding(1).FillWidth(1) \
[ \
	SNew(SBox) \
	PCGEX_STEP_VISIBILITY(_SNAP_HANDLE) \
	.RenderOpacity(0.7f) \
	[ \
		SNew(SNumericEntryBox<double>) \
		.Value_Lambda([_STEPS_HANDLE]() -> TOptional<double>{ _TYPE V; _STEPS_HANDLE->GetValue(V); return V._PART; }) \
		.OnValueCommitted_Lambda([_STEPS_HANDLE](double NewVal, ETextCommit::Type){ _TYPE V; _STEPS_HANDLE->GetValue(V); V._PART = NewVal; _STEPS_HANDLE->SetValue(V); }) \
		.ToolTipText(INVTEXT("Step")) \
		.AllowSpin(true) \
	] \
]

TSharedRef<IPropertyTypeCustomization> FPCGExFittingVariationsCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExFittingVariationsCustomization());
}

void FPCGExFittingVariationsCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Grab parent collection
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	const bool bIsGlobal = PropertyHandle->GetProperty()->GetFName().ToString().Contains(TEXT("Global"));
	if (UPCGExAssetCollection* Collection = !OuterObjects.IsEmpty() ? Cast<UPCGExAssetCollection>(OuterObjects[0]) : nullptr;
		Collection && !bIsGlobal)
	{
		HeaderRow.NameContent()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			+ SHorizontalBox::Slot().Padding(10, 0).FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text_Lambda(
					[Collection]()
					{
						return Collection->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule
							       ? FText::FromString(TEXT("··· Overruled"))
							       : FText::GetEmpty();
					})
				.ColorAndOpacity_Lambda(
					[Collection]()
					{
						return Collection->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule
							       ? FLinearColor(1.0f, 0.5f, 0.1f, 0.5)
							       : FLinearColor::Transparent;
					})
			]

		];
	}
	else
	{
		HeaderRow.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
	}
}

void FPCGExFittingVariationsCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const bool bIsGlobal = PropertyHandle->GetProperty()->GetFName().ToString().Contains(TEXT("Global"));

#define PCGEX_GLOBAL_VISIBILITY(_ID) .Visibility(MakeAttributeLambda([bIsGlobal]() { return !bIsGlobal ? GetDefault<UPCGExCollectionsEditorSettings>()->GetPropertyVisibility(FName(_ID)) : EVisibility::Visible; }))

#pragma region Offset Min/Max

	// Get handles
	TSharedPtr<IPropertyHandle> OffsetMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, OffsetMin));
	TSharedPtr<IPropertyHandle> OffsetMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, OffsetMax));
	TSharedPtr<IPropertyHandle> AbsoluteOffsetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, bAbsoluteOffset));
	TSharedPtr<IPropertyHandle> OffsetSnappingModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, SnapPosition));
	TSharedPtr<IPropertyHandle> OffsetStepsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, OffsetSnap));

	ChildBuilder
		.AddCustomRow(FText::FromString("Offset"))
		PCGEX_GLOBAL_VISIBILITY("VariationOffset")
		.NameContent()
		[
			SNew(SVerticalBox)
			PCGEX_SMALL_LABEL_COL("Offset Min:Max", FLinearColor::White)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 2)
			[
				PCGExEnumCustomization::CreateRadioGroup(OffsetSnappingModeHandle, TEXT("EPCGExVariationSnapping"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL("Abs : ")
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[
					AbsoluteOffsetHandle->CreatePropertyValueWidget()
				]
			]
		]
		.ValueContent()
		.MinDesiredWidth(200)
		[
			SNew(SVerticalBox)

			// X — [min]:[max] [step?]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" X")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(OffsetMinHandle, FVector, X, "Min X") PCGEX_SEP_LABEL(":") PCGEX_FIELD(OffsetMaxHandle, FVector, X, "Max X")
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, FVector, X, OffsetSnappingModeHandle)
			]

			// Row 4: Y
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" Y")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(OffsetMinHandle, FVector, Y, "Min Y") PCGEX_SEP_LABEL(":") PCGEX_FIELD(OffsetMaxHandle, FVector, Y, "Max Y")
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, FVector, Y, OffsetSnappingModeHandle)
			]

			// Row 5: Z
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" Z")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(OffsetMinHandle, FVector, Z, "Min Z") PCGEX_SEP_LABEL(":") PCGEX_FIELD(OffsetMaxHandle, FVector, Z, "Max Z")
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, FVector, Z, OffsetSnappingModeHandle)
			]
		];

#pragma endregion

#pragma region Rotation Min/Max

	// Get handles
	TSharedPtr<IPropertyHandle> RotationMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationMin));
	TSharedPtr<IPropertyHandle> RotationMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationMax));
	TSharedPtr<IPropertyHandle> AbsoluteRotHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, AbsoluteRotation));
	TSharedPtr<IPropertyHandle> RotationSnappingModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, SnapRotation));
	TSharedPtr<IPropertyHandle> RotationStepsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationSnap));

	ChildBuilder
		.AddCustomRow(FText::FromString("Rotation"))
		PCGEX_GLOBAL_VISIBILITY("VariationRotation")
		.NameContent()
		[
			SNew(SVerticalBox)
			PCGEX_SMALL_LABEL_COL("Rotation Min:Max", FLinearColor::White)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 2)
			[
				PCGExEnumCustomization::CreateRadioGroup(RotationSnappingModeHandle, TEXT("EPCGExVariationSnapping"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL("Abs : ")
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[
					PCGExEnumCustomization::CreateCheckboxGroup(AbsoluteRotHandle, TEXT("EPCGExAbsoluteRotationFlags"), {})
				]
			]
		]
		.ValueContent()
		.MinDesiredWidth(200)
		[
			SNew(SVerticalBox)

			// R (Roll)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" R")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(RotationMinHandle, FRotator, Roll, "Min Roll") PCGEX_SEP_LABEL(":") PCGEX_FIELD(RotationMaxHandle, FRotator, Roll, "Max Roll")
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, FRotator, Roll, RotationSnappingModeHandle)
			]

			// Row 4: P (Pitch)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" P")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(RotationMinHandle, FRotator, Pitch, "Min Pitch") PCGEX_SEP_LABEL(":") PCGEX_FIELD(RotationMaxHandle, FRotator, Pitch, "Max Pitch")
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, FRotator, Pitch, RotationSnappingModeHandle)
			]

			// Row 5: Y (Yaw)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" Y")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(RotationMinHandle, FRotator, Yaw, "Min Yaw") PCGEX_SEP_LABEL(":") PCGEX_FIELD(RotationMaxHandle, FRotator, Yaw, "Max Yaw")
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, FRotator, Yaw, RotationSnappingModeHandle)
			]
		];

#pragma endregion

#pragma region Scale Min/Max

	// Get handles
	TSharedPtr<IPropertyHandle> ScaleMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, ScaleMin));
	TSharedPtr<IPropertyHandle> ScaleMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, ScaleMax));
	TSharedPtr<IPropertyHandle> UniformScaleHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, bUniformScale));
	TSharedPtr<IPropertyHandle> ScaleSnappingModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, SnapScale));
	TSharedPtr<IPropertyHandle> ScaleStepsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, ScaleSnap));

	ChildBuilder
		.AddCustomRow(FText::FromString("Scale"))
		PCGEX_GLOBAL_VISIBILITY("VariationScale")
		.NameContent()
		[
			SNew(SVerticalBox)
			PCGEX_SMALL_LABEL_COL("Scale Min:Max", FLinearColor::White)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 2)
			[
				PCGExEnumCustomization::CreateRadioGroup(ScaleSnappingModeHandle, TEXT("EPCGExVariationSnapping"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL("Uniform : ")
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[
					UniformScaleHandle->CreatePropertyValueWidget()
				]
			]
		]
		.ValueContent()
		.MinDesiredWidth(200)
		[
			SNew(SVerticalBox)

			// X
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_SMALL_LABEL(" X")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(ScaleMinHandle, FVector, X, "Min X") PCGEX_SEP_LABEL(":") PCGEX_FIELD(ScaleMaxHandle, FVector, X, "Max X")
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, FVector, X, ScaleSnappingModeHandle)
			]

			// Row 4: Y (disabled when uniform)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([UniformScaleHandle]() { bool bUniform = false; UniformScaleHandle->GetValue(bUniform); return !bUniform; })
				PCGEX_SMALL_LABEL(" Y")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(ScaleMinHandle, FVector, Y, "Min Y") PCGEX_SEP_LABEL(":") PCGEX_FIELD(ScaleMaxHandle, FVector, Y, "Max Y")
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, FVector, Y, ScaleSnappingModeHandle)
			]

			// Row 5: Z (disabled when uniform)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([UniformScaleHandle]() { bool bUniform = false; UniformScaleHandle->GetValue(bUniform); return !bUniform; })
				PCGEX_SMALL_LABEL(" Z")
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox) PCGEX_FIELD(ScaleMinHandle, FVector, Z, "Min Z") PCGEX_SEP_LABEL(":") PCGEX_FIELD(ScaleMaxHandle, FVector, Z, "Max Z")
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, FVector, Z, ScaleSnappingModeHandle)
			]
		];

#undef PCGEX_GLOBAL_VISIBILITY

#pragma endregion
}

#undef PCGEX_SMALL_LABEL
#undef PCGEX_SMALL_LABEL_COL
#undef PCGEX_SEP_LABEL
#undef PCGEX_FIELD_BASE
#undef PCGEX_FIELD
#undef PCGEX_STEP_VISIBILITY
#undef PCGEX_STEP_SLOT
