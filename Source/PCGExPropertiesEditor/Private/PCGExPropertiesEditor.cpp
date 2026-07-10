// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertiesEditor.h"

#include "PCGExBuiltInInlineWidgets.h"
#include "PCGExEnumSelector.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExPropertiesEditorStyle.h"
#include "PCGExProperty.h"
#include "PCGExPropertyType_Struct.h"
#include "PCGExPropertyTypes.h"
#include "PCGExPropertyWriter.h"
#include "PropertyEditorModule.h"
#include "Metadata/PCGObjectPropertyOverride.h"
#include "Details/PCGExEnumSelectorCustomization.h"
#include "Details/PCGExNumericRangeCustomization.h"
#include "Details/PCGExObjectPropertyOverrideDescriptionCustomization.h"
#include "Details/PCGExPropertyCompiledCustomization.h"
#include "Details/PCGExPropertyFloatCurveCustomization.h"
#include "Details/PCGExPropertyOutputConfigCustomization.h"
#include "Details/PCGExPropertyOverrideEntryCustomization.h"
#include "Details/PCGExPropertyOverridesCustomization.h"
#include "Details/PCGExPropertySchemaCollectionCustomization.h"
#include "Details/PCGExPropertySchemaCustomization.h"
#include "Details/PCGExWeightedPropertyOverridesCustomization.h"

#define LOCTEXT_NAMESPACE "FPCGExPropertiesEditorModule"

void FPCGExPropertiesEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Style first: the curve editor widgets look its brushes up by style name at paint time.
	Style = MakeShared<FPCGExPropertiesEditorStyle>();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Register FPCGExEnumSelector customization - replaces engine FEnumSelectorDetails
	// (which crashes when its detail panel is rebuilt mid-callstack via ForceRefresh).
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExEnumSelector::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExEnumSelectorCustomization::MakeInstance)
		);

	// Register FPCGExNumericRange customization - compact one-row strip for numeric range
	// meta on Float/Double/Int32/Int64 property types.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExNumericRange::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExNumericRangeCustomization::MakeInstance)
		);

	// Register FPCGExPropertySchemaCollection customization - handles schema array changes
	// Used by Tuple (Composition), Collections (CollectionProperties), Valency (DefaultProperties)
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertySchemaCollection::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertySchemaCollectionCustomization::MakeInstance)
		);

	// Register FPCGExPropertySchema customization - handles individual schema entry changes
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertySchema::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertySchemaCustomization::MakeInstance)
		);

	// Register FPCGExPropertyOverrides customization - provides toggle-checkbox UI
	// Used by Collections (entry overrides) and Tuple (row values)
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertyOverrides::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyOverridesCustomization::MakeInstance)
		);

	// Register FPCGExPropertyOverrideEntry customization - handles individual entry display
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertyOverrideEntry::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyOverrideEntryCustomization::MakeInstance)
		);

	// Register FPCGExPropertyOutputConfig customization - compact inline [x] Prop -> Attribute row
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertyOutputConfig::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyOutputConfigCustomization::MakeInstance)
		);

	// FPCGExPropertySampledOutputConfig reuses the output-config customization: same header row,
	// extra fields (Time) fall through CustomizeChildren. Type-layout registration is per struct
	// name, so the derived struct needs its own explicit registration.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExPropertySampledOutputConfig::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyOutputConfigCustomization::MakeInstance)
		);

	// FPCGObjectPropertyOverrideDescription (engine struct) -> compact inline [source] -> [target]. Global by FName.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGObjectPropertyOverrideDescription::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExObjectPropertyOverrideDescriptionCustomization::MakeInstance)
		);

	// Register FPCGExWeightedPropertyOverrides customization - weight in header + flattened overrides
	// Used by DistributeTuple (Values array)
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExWeightedPropertyOverrides::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExWeightedPropertyOverridesCustomization::MakeInstance)
		);

	// Register FPCGExPropertyCompiled customization for all 17 concrete types
	// This hides PropertyName field (shown in entry header) and only shows value fields
#define REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(TypeName) \
		PropertyModule.RegisterCustomPropertyTypeLayout( \
			FPCGExProperty_##TypeName::StaticStruct()->GetFName(), \
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyCompiledCustomization::MakeInstance) \
		);

	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Bool)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Int32)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Int64)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Float)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Double)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(String)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Name)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Vector2)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Vector)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Vector4)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Color)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Rotator)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Quat)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Transform)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(SoftObjectPath)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(SoftClassPath)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Enum)
	REGISTER_PROPERTY_COMPILED_CUSTOMIZATION(Struct)

#undef REGISTER_PROPERTY_COMPILED_CUSTOMIZATION

	// FPCGExProperty_FloatCurve gets the inline curve editor instead of the generic compiled
	// layout -- it renders the whole value surface itself (and hides PropertyName the same way).
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPCGExProperty_FloatCurve::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGExPropertyFloatCurveCustomization::MakeInstance)
		);

	// Register built-in compact inline widgets for Vector / Vector2D / Rotator property types
	PCGExBuiltInInlineWidgets::RegisterAll();
}

void FPCGExPropertiesEditorModule::ShutdownModule()
{
	FPCGExInlineWidgetRegistry::Clear();

	// Destroying the style unregisters it from the Slate style registry.
	Style.Reset();

	IPCGExEditorModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExPropertiesEditorModule, PCGExPropertiesEditor)
