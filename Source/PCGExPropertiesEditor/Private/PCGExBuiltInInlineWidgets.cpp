// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExBuiltInInlineWidgets.h"

#include "PCGExInlineWidgetRegistry.h"
#include "PCGExPropertyTypes.h"
#include "Details/PCGExEnumSelectorWidget.h"
#include "Details/PCGExInlineNumericWidgets.h"
#include "Details/PCGExPropertyInlineWidgets.h"
#include "Widgets/SPCGExPropertyCurveValueWidget.h"

namespace PCGExBuiltInInlineWidgets
{
	void RegisterAll()
	{
		// Numeric inline widgets render identically in Edit and Compact contexts -- no
		// "definition" controls (the value IS the definition), so register under both.
		FPCGExInlineWidgetRegistry::RegisterAllModes(FPCGExProperty_Vector::StaticStruct()->GetFName(), &PCGExInlineNumericWidgets::MakeVectorWidget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(FPCGExProperty_Vector2::StaticStruct()->GetFName(), &PCGExInlineNumericWidgets::MakeVector2Widget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(FPCGExProperty_Rotator::StaticStruct()->GetFName(), &PCGExInlineNumericWidgets::MakeRotatorWidget);

		// Enum: compact-mode only. The schema-edit (Edit) path falls through to the default
		// AddProperty(ValueHandle), which routes to FPCGExEnumSelectorCustomization -- that
		// renders the full editor (class picker + value picker). The compact mode used in
		// override / readonly-schema contexts hides the class picker.
		FPCGExInlineWidgetRegistry::Register(
			FPCGExProperty_Enum::StaticStruct()->GetFName(),
			EPCGExInlineWidgetMode::Compact,
			[](const TSharedRef<IPropertyHandle>& ValueHandle) -> TSharedRef<SWidget>
			{
				return PCGExEnumSelectorWidget::Make(ValueHandle, /*bAllowClassPicker=*/false);
			});

		// Soft path types: filtered pickers that read the sibling editor-only AllowedClass
		// field. Registered under both modes -- the AllowedClass is schema-authored in Edit
		// and inherited via SyncStructuralFromSchema in Compact, so the same widget works.
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_SoftObjectPath::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeSoftObjectPathWidget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_SoftClassPath::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeSoftClassPathWidget);

		// Numeric types: clamp-aware widgets that read the sibling editor-only Range field.
		// Registered under both modes -- Range is structural and propagated like AllowedClass.
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_Float::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeClampedFloatWidget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_Double::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeClampedDoubleWidget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_Int32::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeClampedInt32Widget);
		FPCGExInlineWidgetRegistry::RegisterAllModes(
			FPCGExProperty_Int64::StaticStruct()->GetFName(),
			&PCGExPropertyInlineWidgets::MakeClampedInt64Widget);

		// Float curve: compact-mode only, mirroring Enum. The schema-edit (Edit) path falls
		// through to the default AddProperty route, which renders through
		// FPCGExPropertyFloatCurveCustomization. FPCGExProperty_FloatCurve is deliberately NOT
		// PCGExInlineValue, so override/readonly rows route through AddComplexValueRows, which
		// picks this factory up and renders the editor as a full-width row (an inline value
		// column would track the editor's desired width and jitter while typing).
		FPCGExInlineWidgetRegistry::Register(
			FPCGExProperty_FloatCurve::StaticStruct()->GetFName(),
			EPCGExInlineWidgetMode::Compact,
			[](const TSharedRef<IPropertyHandle>& ValueHandle) -> TSharedRef<SWidget>
			{
				return SNew(SPCGExPropertyCurveValueWidget, ValueHandle);
			});
	}
}
