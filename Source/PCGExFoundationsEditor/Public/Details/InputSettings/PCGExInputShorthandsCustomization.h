// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Details/PCGExInputShorthandsDetails.h"


class SWidget;

namespace PCGExInputShorthandsCustomization
{
	/** Trailing gear popover collapsing a shorthand row's options (Input mode + bCleanupAttribute) into one button. */
	TSharedRef<SWidget> CreateOptionsPopover(const TSharedPtr<IPropertyHandle>& InputHandle, const TSharedPtr<IPropertyHandle>& CleanupHandle);
}

class FPCGExInputShorthandCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle);
	virtual TSharedRef<SWidget> CreateAttributeWidget(TSharedPtr<IPropertyHandle> AttributeHandle);
};

class FPCGExInputShorthandVectorCustomization : public FPCGExInputShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle) override;
};

class FPCGExInputShorthandDirectionCustomization : public FPCGExInputShorthandVectorCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

class FPCGExInputShorthandRotatorCustomization : public FPCGExInputShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle) override;
};

class FPCGExInputShorthandSoftObjectPathCustomization : public FPCGExInputShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle) override;
};
