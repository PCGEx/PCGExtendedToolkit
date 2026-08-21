// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"


class SWidget;

namespace PCGExEnumCustomization
{
	/** Enum indices in the order a group should present them, honoring the enum's PCGExDisplayOrder
	 *  meta (comma-separated enumerator names). Entries the meta omits follow in declaration order.
	 *  The hook lives on the ENUM because the inline customizations are registered by name alone and
	 *  have nowhere to receive an ordering argument. */
	PCGEXCOREEDITOR_API
	TArray<int32> GetEnumDisplayOrder(const UEnum* Enum);

	/** Radio group that honors the property's ValidEnumValues / InvalidEnumValues metadata (plus UMETA(Hidden)). */
	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(UEnum* Enum, TFunction<int32()> GetValue, TFunction<void(int32)> SetValue);

	/** Radio group variant that hides specific enum indices (e.g. to gate context-inapplicable values). */
	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum, const TSet<int32>& SkipIndices);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCheckboxGroup(UEnum* Enum, TFunction<uint8()> GetValue, TFunction<void(uint8)> SetValue, const TSet<int32>& SkipIndices = {});

	/** Standard dropdown (SComboButton + menu) for enum-typed property handles. Hides Hidden /
	 *  SkipIndices entries. Use when an inline radio group becomes too dense (e.g. 5+ entries). */
	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateDropdown(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices = {});

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateDropdown(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum, const TSet<int32>& SkipIndices = {});

	/** Single compact button that cycles forward through non-hidden enum entries on click. Label shows the current entry's DisplayName. */
	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCycleButton(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCycleButton(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum);

	PCGEXCOREEDITOR_API
	TSharedRef<SWidget> CreateCycleButton(UEnum* Enum, TFunction<int32()> GetValue, TFunction<void(int32)> SetValue);
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
