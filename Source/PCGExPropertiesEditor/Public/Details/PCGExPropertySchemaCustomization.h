// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Templates/Function.h"

struct FPCGExPropertySchema;

/**
 * Customizes FPCGExPropertySchema to:
 * - Show dynamic header with Name and type
 * - Sync PropertyName and HeaderId when Name or Property changes
 * - Tell the schema what Property was before a retype, so its choices can cross it (FPCGExPropertySchema::RetypedFrom)
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
	const FPCGExPropertySchema* AccessSchema() const;
	/** Every edited schema: one per selected object. */
	void ForEachSchema(TFunctionRef<void(FPCGExPropertySchema&)> Visitor) const;
	FText GetHeaderNameText() const;
	FText GetHeaderTypeText() const;

	/** Called when Name or Property changes: syncs PropertyName / HeaderId, and finishes a retype no host sync did. */
	void OnSchemaChanged();

	/** Property's own node is about to change: the only moment a retype's old value is still there to hand over. */
	void OnPropertyPreChange();

	/** The schema row's own handle, which the row keeps alive. Child handles are new objects per GetChildHandle call
	 *  and die with the local that received them: nothing here may count on one. */
	TWeakPtr<IPropertyHandle> PropertyHandlePtr;
	bool bIsReadOnly = false;
};
