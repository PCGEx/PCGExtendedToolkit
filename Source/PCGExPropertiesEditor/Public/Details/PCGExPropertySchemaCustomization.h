// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "StructUtils/InstancedStruct.h"

class IPropertyUtilities;

/**
 * Customizes FPCGExPropertySchema to:
 * - Show dynamic header with Name and type
 * - Sync PropertyName and HeaderId when Name or Property changes
 * - Carry Choices across a retype of Property (the picker rebuilds the struct from scratch)
 * - When under a property with ReadOnlySchema metadata:
 *   - Hides Name field and struct type picker (schema is synced from cage)
 *   - Only allows editing the inner Value field (the default value)
 */
class FPCGExPropertySchemaCustomization : public IPropertyTypeCustomization
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
	const struct FPCGExPropertySchema* AccessSchema() const;
	FText GetHeaderNameText() const;
	FText GetHeaderTypeText() const;

	/** Called when Name or Property changes - syncs PropertyName/HeaderId; carries Choices across a retype */
	void OnSchemaChanged();

	/** Property's own node is about to change: snapshot it, the only moment a retype's old value is still there. */
	void OnPropertyPreChange();

	/** Check if this schema is under a property with ReadOnlySchema metadata */
	bool IsReadOnlySchema(TSharedRef<IPropertyHandle> PropertyHandle) const;

	TWeakPtr<IPropertyHandle> PropertyHandlePtr;
	TWeakPtr<IPropertyHandle> PropertyInnerHandlePtr;
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;
	FInstancedStruct PreChangeSnapshot;
	bool bIsReadOnly = false;
};
