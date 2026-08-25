// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Details/InputSettings/PCGExInputShorthandsCustomization.h"


class SWidget;

/** Input-shorthand row with a leading comparison-operator dropdown and a trailing tolerance field. */
class FPCGExCompareShorthandCustomization : public FPCGExInputShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

class FPCGExCompareShorthandVectorCustomization : public FPCGExCompareShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle) override;
};

class FPCGExCompareShorthandRotatorCustomization : public FPCGExCompareShorthandCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle) override;
};
