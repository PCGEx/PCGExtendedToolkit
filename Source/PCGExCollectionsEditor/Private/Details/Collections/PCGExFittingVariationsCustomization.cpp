// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExFittingVariationsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyHandle.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/PCGExInlineNumericWidgets.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExFittingVariationsCustomization
{
	// Row-prefix tints: softer-than-canon axis colors so vertical stacking still reads as
	// separate axes (since this layout is vertical rather than UE's canonical inline triplet),
	// without fighting the per-field colored labels supplied by MakeAxisEntry.
	constexpr FLinearColor AxisRowTintX = FLinearColor(0.85f, 0.45f, 0.45f, 0.65f);
	constexpr FLinearColor AxisRowTintY = FLinearColor(0.45f, 0.80f, 0.45f, 0.65f);
	constexpr FLinearColor AxisRowTintZ = FLinearColor(0.45f, 0.55f, 0.85f, 0.65f);
}

// Row-prefix axis label: small, subtly tinted toward the axis so vertically-stacked rows
// still read as "this is the X row" at a glance. Colored sidebar on each numeric entry
// remains the primary axis indicator.
#define PCGEX_AXIS_LABEL(_TEXT, _TINT) \
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0) \
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(_TINT)).MinDesiredWidth(10)]

#define PCGEX_SMALL_LABEL(_TEXT) \
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)]

#define PCGEX_SMALL_LABEL_COL(_TEXT, _COL) \
+ SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Center).Padding(1,8,1,2)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(_COL)).MinDesiredWidth(10)]

#define PCGEX_SEP_LABEL(_TEXT)\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0)\
[SNew(STextBlock).Text(FText::FromString(_TEXT)).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray))]

#define PCGEX_STEP_VISIBILITY(_HANDLE)\
.Visibility_Lambda([_HANDLE](){uint8 EnumValue = 0;\
if (_HANDLE->GetValue(EnumValue) == FPropertyAccess::Success){ return EnumValue ? EVisibility::Visible : EVisibility::Collapsed;}\
return EVisibility::Collapsed;})

// Min/Max numeric entry bound to the _AXIS_NAME child of the FVector / FRotator parent
// handle. MakeAxisEntry supplies the UE-canonical narrow colored sidebar label, unbounded
// spin range, drag transactions, and (when _TYPE_INTERFACE != nullptr) the degree suffix.
#define PCGEX_AXIS_FIELD(_PARENT_HANDLE, _AXIS_NAME, _AXIS_COLOR, _TYPE_INTERFACE) \
+ SHorizontalBox::Slot().Padding(1).FillWidth(1) \
[ \
	PCGExInlineNumericWidgets::MakeAxisEntry(_PARENT_HANDLE->GetChildHandle(TEXT(_AXIS_NAME)), _AXIS_COLOR, _TYPE_INTERFACE) \
]

// Inline step spinner as third column on an axis row.
// FillWidth(1) alongside the min:max wrapper at FillWidth(2) gives equal thirds.
// When Collapsed (snapping off), the slot is removed from layout and min:max fills 100%.
#define PCGEX_STEP_SLOT(_STEPS_HANDLE, _AXIS_NAME, _SNAP_HANDLE, _AXIS_COLOR, _TYPE_INTERFACE) \
+ SHorizontalBox::Slot().Padding(1).FillWidth(1) \
[ \
	SNew(SBox) \
	PCGEX_STEP_VISIBILITY(_SNAP_HANDLE) \
	.RenderOpacity(0.7f) \
	[ \
		PCGExInlineNumericWidgets::MakeAxisEntry(_STEPS_HANDLE->GetChildHandle(TEXT(_AXIS_NAME)), _AXIS_COLOR, _TYPE_INTERFACE) \
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

			// X -- [min]:[max] [step?]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" X", PCGExFittingVariationsCustomization::AxisRowTintX)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(OffsetMinHandle, "X", PCGExInlineNumericWidgets::AxisColorX, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(OffsetMaxHandle, "X", PCGExInlineNumericWidgets::AxisColorX, nullptr)
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, "X", OffsetSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorX, nullptr)
			]

			// Row 4: Y
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" Y", PCGExFittingVariationsCustomization::AxisRowTintY)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(OffsetMinHandle, "Y", PCGExInlineNumericWidgets::AxisColorY, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(OffsetMaxHandle, "Y", PCGExInlineNumericWidgets::AxisColorY, nullptr)
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, "Y", OffsetSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorY, nullptr)
			]

			// Row 5: Z
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" Z", PCGExFittingVariationsCustomization::AxisRowTintZ)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(OffsetMinHandle, "Z", PCGExInlineNumericWidgets::AxisColorZ, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(OffsetMaxHandle, "Z", PCGExInlineNumericWidgets::AxisColorZ, nullptr)
				]
				PCGEX_STEP_SLOT(OffsetStepsHandle, "Z", OffsetSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorZ, nullptr)
			]
		];

#pragma endregion

#pragma region Rotation Min/Max

	TSharedPtr<IPropertyHandle> RotationMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationMin));
	TSharedPtr<IPropertyHandle> RotationMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationMax));
	TSharedPtr<IPropertyHandle> AbsoluteRotHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, AbsoluteRotation));
	TSharedPtr<IPropertyHandle> RotationSnappingModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, SnapRotation));
	TSharedPtr<IPropertyHandle> RotationStepsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExFittingVariations, RotationSnap));

	// Shared degree-suffix interface for all rotation numeric entries (matches native FRotator display).
	const TSharedRef<INumericTypeInterface<double>> DegreeInterface = MakeShared<PCGExInlineNumericWidgets::FDegreesNumericTypeInterface>();

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

			// R (Roll) -- tinted X because Roll is the X-axis rotation
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" R", PCGExFittingVariationsCustomization::AxisRowTintX)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(RotationMinHandle, "Roll", PCGExInlineNumericWidgets::AxisColorX, DegreeInterface)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(RotationMaxHandle, "Roll", PCGExInlineNumericWidgets::AxisColorX, DegreeInterface)
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, "Roll", RotationSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorX, DegreeInterface)
			]

			// Row 4: P (Pitch) -- Y-axis rotation
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" P", PCGExFittingVariationsCustomization::AxisRowTintY)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(RotationMinHandle, "Pitch", PCGExInlineNumericWidgets::AxisColorY, DegreeInterface)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(RotationMaxHandle, "Pitch", PCGExInlineNumericWidgets::AxisColorY, DegreeInterface)
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, "Pitch", RotationSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorY, DegreeInterface)
			]

			// Row 5: Y (Yaw) -- Z-axis rotation
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				PCGEX_AXIS_LABEL(" Y", PCGExFittingVariationsCustomization::AxisRowTintZ)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(RotationMinHandle, "Yaw", PCGExInlineNumericWidgets::AxisColorZ, DegreeInterface)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(RotationMaxHandle, "Yaw", PCGExInlineNumericWidgets::AxisColorZ, DegreeInterface)
				]
				PCGEX_STEP_SLOT(RotationStepsHandle, "Yaw", RotationSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorZ, DegreeInterface)
			]
		];

#pragma endregion

#pragma region Scale Min/Max

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
				PCGEX_AXIS_LABEL(" X", PCGExFittingVariationsCustomization::AxisRowTintX)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(ScaleMinHandle, "X", PCGExInlineNumericWidgets::AxisColorX, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(ScaleMaxHandle, "X", PCGExInlineNumericWidgets::AxisColorX, nullptr)
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, "X", ScaleSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorX, nullptr)
			]

			// Row 4: Y (disabled when uniform)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([UniformScaleHandle]()
				{
					bool bUniform = false;
					UniformScaleHandle->GetValue(bUniform);
					return !bUniform;
				})
				PCGEX_AXIS_LABEL(" Y", PCGExFittingVariationsCustomization::AxisRowTintY)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(ScaleMinHandle, "Y", PCGExInlineNumericWidgets::AxisColorY, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(ScaleMaxHandle, "Y", PCGExInlineNumericWidgets::AxisColorY, nullptr)
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, "Y", ScaleSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorY, nullptr)
			]

			// Row 5: Z (disabled when uniform)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([UniformScaleHandle]()
				{
					bool bUniform = false;
					UniformScaleHandle->GetValue(bUniform);
					return !bUniform;
				})
				PCGEX_AXIS_LABEL(" Z", PCGExFittingVariationsCustomization::AxisRowTintZ)
				+ SHorizontalBox::Slot().Padding(1).FillWidth(2)
				[
					SNew(SHorizontalBox)
					PCGEX_AXIS_FIELD(ScaleMinHandle, "Z", PCGExInlineNumericWidgets::AxisColorZ, nullptr)
					PCGEX_SEP_LABEL(":")
					PCGEX_AXIS_FIELD(ScaleMaxHandle, "Z", PCGExInlineNumericWidgets::AxisColorZ, nullptr)
				]
				PCGEX_STEP_SLOT(ScaleStepsHandle, "Z", ScaleSnappingModeHandle, PCGExInlineNumericWidgets::AxisColorZ, nullptr)
			]
		];

#undef PCGEX_GLOBAL_VISIBILITY

#pragma endregion
}

#undef PCGEX_AXIS_LABEL
#undef PCGEX_AXIS_FIELD
#undef PCGEX_SMALL_LABEL
#undef PCGEX_SMALL_LABEL_COL
#undef PCGEX_SEP_LABEL
#undef PCGEX_STEP_VISIBILITY
#undef PCGEX_STEP_SLOT
