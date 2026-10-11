// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"

class FInstancedStructProvider;
class IPropertyUtilities;

/**
 * FPCGExPropertyChoices in schema-edit mode. Header strip: [Locked] [N choices] [Add Current Value]; children: one
 * row per choice (FPCGExPropertyChoiceCustomization), with the engine's element buttons. Collapsed entirely under a
 * ReadOnlySchema ancestor: values-only hosts never author choices.
 */
class FPCGExPropertyChoicesCustomization : public IPropertyTypeCustomization
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

private:
	FText GetCountText() const;
	FReply OnAddCurrentValue();
	void OnNumItemsChanged();

	TWeakPtr<IPropertyHandle> ChoicesHandlePtr;
	TSharedPtr<IPropertyHandle> ItemsHandle;
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;
	bool bReadOnly = false;
};

/**
 * One choice as one row: the Label editor as the name, the carrier's own Value editor as the value (Compact factory,
 * else the default widget). A carrier of a complex type keeps a preview in the header and expands to its editor.
 * A carrier the host type no longer accepts shows why instead of an editor.
 */
class FPCGExPropertyChoiceCustomization : public IPropertyTypeCustomization
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

private:
	/** FPCGExPropertyChoice::Value, the carrier FInstancedStruct. */
	TSharedPtr<IPropertyHandle> CarrierHandle;
	/** Keeps the carrier's child-structure nodes alive for the row's lifetime. */
	TSharedPtr<FInstancedStructProvider> CarrierProvider;
	/** The carrier's own "Value" field. */
	TSharedPtr<IPropertyHandle> CarrierValueHandle;
	bool bInlineCarrier = false;
};
