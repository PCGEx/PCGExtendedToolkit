// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class FDetailWidgetRow;
class IDetailChildrenBuilder;
class SWidget;
struct FSlateFontInfo;
struct FSlateColor;

/** Inline editor for FPCGExPartitionQuery: a read-only summary + cell Offset on the header, the grid-size
 *  resolution / 2D mode / explicit grid / size offset folded into a single child row. */
class FPCGExPartitionQueryCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

protected:
	/** Header name column. Here, what the query resolves to: "Self", "Self +1", "Neighbor @ 25600"... */
	virtual TSharedRef<SWidget> MakeNameWidget(const TSharedRef<IPropertyHandle>& PropertyHandle);

	/** Inline X/Y/Z editor for the FIntVector cell offset (FIntVector exposes no default inline value widget). */
	static TSharedRef<SWidget> MakeOffsetWidget(const TSharedPtr<IPropertyHandle>& OffsetHandle);

	/** Italic detail font and dimmed color shared by every read-only hint in these rows. */
	static FSlateFontInfo GetHintFont();
	static FSlateColor GetHintColor();
};
