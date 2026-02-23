// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"


class SWidget;

namespace PCGExEnumCustomization
{
	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(UEnum* Enum, TFunction<int32()> GetValue, TFunction<void(int32)> SetValue);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(UEnum* Enum, TFunction<uint8()> GetValue, TFunction<void(uint8)> SetValue, const TSet<int32>& SkipIndices = {});
}

class PCGEXCOREEDITOR_API FPCGExInlineEnumCustomization : public IPropertyTypeCustomization
{
public:
	explicit FPCGExInlineEnumCustomization(const FString& InEnumName);

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

protected:
	FString EnumName = TEXT("");
};
