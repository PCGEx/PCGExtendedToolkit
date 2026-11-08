// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPartitionQueryCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PCGCommon.h"
#include "PropertyHandle.h"
#include "Details/PCGExPartitionDetails.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionQueryCustomization"

namespace PCGExPartitionQueryCustomization
{
	const FName HideCellOffsetMeta = TEXT("HideCellOffset");

	FText MakeExplicitGridText(const FPCGExPartitionQuery& InQuery)
	{
		// Unbounded and Uninitialized have no size: name them, unless a size step puts them back on the grid ladder.
		const bool bValidGrid = PCGHiGenGrid::IsValidGrid(InQuery.ExplicitGrid);
		if (!bValidGrid && InQuery.GridSizeOffset == 0)
		{
			return StaticEnum<EPCGHiGenGrid>()->GetDisplayNameTextByValue(static_cast<int64>(InQuery.ExplicitGrid));
		}

		const uint32 GridSize = PCGExPartitionGrid::OffsetGridSize(bValidGrid ? PCGHiGenGrid::GridToGridSize(InQuery.ExplicitGrid) : 0u, InQuery.GridSizeOffset);
		return FText::AsNumber(GridSize, &FNumberFormattingOptions::DefaultNoGrouping());
	}

	FText MakeStepText(const FPCGExPartitionQuery& InQuery)
	{
		return FText::FromString(FString::Printf(TEXT("%+d"), InQuery.GridSizeOffset));
	}

	FText MakeSummary(const FPCGExPartitionQuery& InQuery)
	{
		// A forced 2D grid ignores the Z offset; Auto is only known at execution, so Z counts there.
		const bool bNeighbor = InQuery.Offset.X != 0 || InQuery.Offset.Y != 0 || (InQuery.Offset.Z != 0 && InQuery.Grid2D != EPCGExGrid2DMode::Force2D);
		const FText Subject = bNeighbor ? LOCTEXT("SummaryNeighbor", "Neighbor") : LOCTEXT("SummarySelf", "Self");

		if (InQuery.GridSizeResolution == EPCGExPartitionResolution::Explicit)
		{
			return FText::Format(LOCTEXT("SummaryExplicit", "{0} @ {1}"), Subject, MakeExplicitGridText(InQuery));
		}

		if (InQuery.GridSizeOffset == 0) { return Subject; }

		return FText::Format(LOCTEXT("SummaryStepped", "{0} {1}"), Subject, MakeStepText(InQuery));
	}

	// Without a cell offset there is no Self/Neighbor subject: only the grid it resolves to.
	FText MakeGridSummary(const FPCGExPartitionQuery& InQuery)
	{
		if (InQuery.GridSizeResolution == EPCGExPartitionResolution::Explicit) { return MakeExplicitGridText(InQuery); }

		if (InQuery.GridSizeOffset == 0) { return LOCTEXT("SummaryFromComponent", "From Component"); }

		return FText::Format(LOCTEXT("SummaryFromComponentStepped", "From Component {0}"), MakeStepText(InQuery));
	}

	FText GetSummary(const TSharedPtr<IPropertyHandle>& InHandle, const bool bGridOnly)
	{
		TOptional<FText> Summary;
		bool bMultiple = false;

		InHandle->EnumerateConstRawData([&Summary, &bMultiple, bGridOnly](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (!RawData) { return true; }

			const FPCGExPartitionQuery& Query = *static_cast<const FPCGExPartitionQuery*>(RawData);
			const FText Current = bGridOnly ? MakeGridSummary(Query) : MakeSummary(Query);
			if (!Summary.IsSet()) { Summary = Current; }
			else if (!Summary->EqualTo(Current)) { bMultiple = true; }

			return !bMultiple;
		});

		if (bMultiple) { return LOCTEXT("SummaryMultiple", "Multiple Values"); }
		return Summary.Get(FText::GetEmpty());
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExPartitionQueryCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPartitionQueryCustomization());
}

FSlateFontInfo FPCGExPartitionQueryCustomization::GetHintFont()
{
	FSlateFontInfo ItalicFont = IDetailLayoutBuilder::GetDetailFont();
	ItalicFont.TypefaceFontName = TEXT("Italic");
	return ItalicFont;
}

FSlateColor FPCGExPartitionQueryCustomization::GetHintColor()
{
	return FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.35f));
}

TSharedRef<SWidget> FPCGExPartitionQueryCustomization::MakeNameWidget(const TSharedRef<IPropertyHandle>& PropertyHandle)
{
	const TSharedPtr<IPropertyHandle> Handle = PropertyHandle;

	return SNew(STextBlock)
		.Text_Lambda([Handle]() { return PCGExPartitionQueryCustomization::GetSummary(Handle, false); })
		.ToolTipText(PropertyHandle->GetToolTipText())
		.Font(GetHintFont())
		.ColorAndOpacity(GetHintColor());
}

TSharedRef<SWidget> FPCGExPartitionQueryCustomization::MakeOffsetWidget(const TSharedPtr<IPropertyHandle>& OffsetHandle)
{
	// FIntVector renders its X/Y/Z as expandable child rows and has no inline value widget, so build
	// one from the component handles (each int child does provide a numeric value widget).
	const TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	uint32 NumChildren = 0;
	if (OffsetHandle.IsValid() && OffsetHandle->GetNumChildren(NumChildren) == FPropertyAccess::Success)
	{
		for (uint32 i = 0; i < NumChildren; ++i)
		{
			const TSharedPtr<IPropertyHandle> Component = OffsetHandle->GetChildHandle(i);
			if (!Component.IsValid()) { continue; }

			Box->AddSlot()
				.FillWidth(1.f)
				.Padding(i == 0 ? 0.f : 2.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).MinDesiredWidth(36.f)
					[
						Component->CreatePropertyValueWidget()
					]
				];
		}
	}
	return Box;
}

void FPCGExPartitionQueryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (PropertyHandle->HasMetaData(PCGExPartitionQueryCustomization::HideCellOffsetMeta))
	{
		const TSharedPtr<IPropertyHandle> Handle = PropertyHandle;

		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(150)
			[
				SNew(STextBlock)
				.Text_Lambda([Handle]() { return PCGExPartitionQueryCustomization::GetSummary(Handle, true); })
				.Font(GetHintFont())
				.ColorAndOpacity(GetHintColor())
			];
		return;
	}

	const TSharedPtr<IPropertyHandle> OffsetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionQuery, Offset));

	HeaderRow
		.NameContent()
		[
			MakeNameWidget(PropertyHandle)
		]
		.ValueContent()
		.MinDesiredWidth(150)
		[
			MakeOffsetWidget(OffsetHandle)
		];
}

void FPCGExPartitionQueryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> ResolutionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionQuery, GridSizeResolution));
	const TSharedPtr<IPropertyHandle> Grid2DHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionQuery, Grid2D));
	const TSharedPtr<IPropertyHandle> ExplicitGridHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionQuery, ExplicitGrid));
	const TSharedPtr<IPropertyHandle> GridSizeOffsetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPartitionQuery, GridSizeOffset));

	ChildBuilder.AddCustomRow(LOCTEXT("GridRowFilter", "Grid Size"))
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
			[
				PCGExEnumCustomization::CreateRadioGroup(ResolutionHandle, TEXT("EPCGExPartitionResolution"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				PCGExEnumCustomization::CreateDropdown(Grid2DHandle, TEXT("EPCGExGrid2DMode"))
			]
		]
		.ValueContent()
		.MinDesiredWidth(220)
		[
			SNew(SHorizontalBox)
			// Explicit grid dropdown -- dimmed (but interactive) while the size comes from the component.
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
				.Padding(0)
				.VAlign(VAlign_Center)
				.ColorAndOpacity_Lambda([ResolutionHandle]() -> FLinearColor
				{
					uint8 Value = 0;
					const bool bFromComponent = ResolutionHandle->GetValue(Value) == FPropertyAccess::Success
						&& Value == static_cast<uint8>(EPCGExPartitionResolution::FromComponent);
					return FLinearColor(1.f, 1.f, 1.f, bFromComponent ? 0.8f : 1.f);
				})
				[
					PCGExEnumCustomization::CreateDropdown(ExplicitGridHandle, TEXT("EPCGHiGenGrid"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("GridSizeOffsetPlus", "+")).Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(52.f)
				[
					GridSizeOffsetHandle->CreatePropertyValueWidget()
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
