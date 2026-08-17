// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"

class IPropertyHandle;

/**
 * Detail customization for FPCGExProperty_FloatCurve (schema / instanced-struct contexts).
 *
 * Renders the property's FRuntimeFloatCurve through the inline PCGEx curve editor as a
 * full-width child row; PropertyName is deliberately not shown (entry headers display it,
 * mirroring FPCGExPropertyCompiledCustomization for the other property types).
 *
 * The actual editor + write-back logic lives in SPCGExPropertyCurveValueWidget, shared with
 * the Compact inline-widget registry factory that renders override entry rows.
 */
class FPCGExPropertyFloatCurveCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization interface

private:
	/** Handle to the FRuntimeFloatCurve 'Value' member of FPCGExProperty_FloatCurve. */
	TSharedPtr<IPropertyHandle> ValueHandle;
};

/**
 * Raw FRuntimeFloatCurve rows owned by FPCGExProperty subclasses (identifier-gated GLOBAL
 * registration -- identifier callbacks are consulted before the identifier-less engine one).
 *
 * Load-bearing safety: the engine FCurveStructCustomization calls GEditor->RegisterForUndo in
 * its CONSTRUCTOR, but a row whose widget is replaced via CustomWidget() never runs
 * CustomizeHeader (FDetailPropertyRow::OnItemNodeInitialized instantiates the type
 * customization unconditionally, then skips the header for user-customized rows) -- leaving a
 * live undo client with a null StructPropertyHandle that crashes on the next undo. This class
 * is instantiation-safe by construction and renders through the shared PCGEx curve widget.
 */
class FPCGExRuntimeFloatCurveCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization interface
};

/** Matches FRuntimeFloatCurve properties declared inside FPCGExProperty-derived structs or FPCGExWeightCurve. */
class FPCGExPropertyOwnedCurveIdentifier : public IPropertyTypeIdentifier
{
public:
	virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override;
};
