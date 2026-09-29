// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Details/PCGExPartitionQueryCustomization.h"

class SWidget;

/** Inline editor for FPCGExPartitionGrid: the query editor, with the editable Suffix as the header name column. */
class FPCGExPartitionGridCustomization : public FPCGExPartitionQueryCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	virtual TSharedRef<SWidget> MakeNameWidget(const TSharedRef<IPropertyHandle>& PropertyHandle) override;

	/** FName editor that shows an italic, dimmed "Self" placeholder while the suffix is None. */
	static TSharedRef<SWidget> MakeSuffixWidget(const TSharedPtr<IPropertyHandle>& SuffixHandle);
};
