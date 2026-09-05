// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertySchemaCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "Details/PCGExPropertyLabelRow.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertySchemaCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertySchemaCustomization());
}

const FPCGExPropertySchema* FPCGExPropertySchemaCustomization::AccessSchema() const
{
	if (!PropertyHandlePtr.IsValid())
	{
		return nullptr;
	}
	TArray<void*> RawData;
	PropertyHandlePtr.Pin()->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return nullptr;
	}
	return static_cast<const FPCGExPropertySchema*>(RawData[0]);
}

FText FPCGExPropertySchemaCustomization::GetHeaderNameText() const
{
	const FPCGExPropertySchema* Schema = AccessSchema();
	return Schema ? FText::FromName(Schema->Name) : FText::FromString(TEXT("None"));
}

FText FPCGExPropertySchemaCustomization::GetHeaderTypeText() const
{
	const FPCGExPropertySchema* Schema = AccessSchema();
	if (!Schema)
	{
		return FText::FromString(TEXT("Unknown"));
	}
	const FPCGExProperty* Prop = Schema->GetProperty();
	return Prop ? FText::FromName(Prop->GetDisplayTypeName()) : FText::FromString(TEXT("Unknown"));
}

void FPCGExPropertySchemaCustomization::OnSchemaChanged()
{
	if (!PropertyHandlePtr.IsValid())
	{
		return;
	}

	// Access schema to sync
	TArray<void*> RawData;
	PropertyHandlePtr.Pin()->AccessRawData(RawData);
	if (RawData.IsEmpty() || !RawData[0])
	{
		return;
	}

	FPCGExPropertySchema* Schema = static_cast<FPCGExPropertySchema*>(RawData[0]);
	if (Schema)
	{
		Schema->SyncPropertyName();
	}

	// Note: Parent collection will handle ForceRefresh via its own listener
}

bool FPCGExPropertySchemaCustomization::IsReadOnlySchema(TSharedRef<IPropertyHandle> PropertyHandle) const
{
	// Walk up the property hierarchy looking for ReadOnlySchema on any ancestor handle.
	// IPropertyHandle::HasMetaData checks both compile-time UPROPERTY metadata and
	// runtime instance metadata set via SetInstanceMetaData, so this works for both
	// the static meta=(ReadOnlySchema) case and the dynamic per-instance locking case.
	TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
	while (ParentHandle.IsValid())
	{
		if (ParentHandle->HasMetaData(TEXT("ReadOnlySchema")))
		{
			return true;
		}
		ParentHandle = ParentHandle->GetParentHandle();
	}
	return false;
}

void FPCGExPropertySchemaCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandlePtr = PropertyHandle;
	bIsReadOnly = IsReadOnlySchema(PropertyHandle);

	HeaderRow
		.NameContent()
		[
			PCGExPropertyLabelRow::Build(
				TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertySchemaCustomization::GetHeaderNameText)),
				TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertySchemaCustomization::GetHeaderTypeText)))
		];

	// Suppress UE's default reset arrow on the outer FPCGExPropertySchema row in author
	// mode. The default reset would clear the schema (Name=None, empty Property
	// FInstancedStruct), destroying the entry -- same destructive shape we suppress on
	// FPCGExPropertyOverrideEntry's complex HeaderRow. The inner Property UPROPERTY
	// carries NoResetToDefault, which handles arrow suppression on the value-child row;
	// this handles the outer-row arrow that NoResetToDefault doesn't reach.
	//
	// Skipped in ReadOnlySchema mode: there, the parent
	// FPCGExPropertySchemaCollectionCustomization installs its own archetype-aware
	// override via ApplyLocalSchemaResetOverride at row-add time, and a second override
	// here would conflict (UE asserts on duplicate handlers).
	//
	// bPropagateToChildren=false on purpose: only suppress the outer row's arrow. The
	// Name child row keeps its default reset behavior so authors can clear just the name
	// if they want.
	if (!bIsReadOnly)
	{
		HeaderRow.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateLambda([](TSharedPtr<IPropertyHandle>)
			{
				return false;
			}),
			FResetToDefaultHandler::CreateLambda([](TSharedPtr<IPropertyHandle>)
			{
			}),
			/*bPropagateToChildren*/ false));
	}
}

void FPCGExPropertySchemaCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> NameHandle = PropertyHandle->GetChildHandle(TEXT("Name"));
	TSharedPtr<IPropertyHandle> PropertyInnerHandle = PropertyHandle->GetChildHandle(TEXT("Property"));

	if (bIsReadOnly)
	{
		// Read-only mode: Only show the inner Value field from the FInstancedStruct
		// Schema name and type are shown in header, struct type cannot be changed

		if (!PropertyInnerHandle.IsValid())
		{
			return;
		}

		// Access raw data to get the FInstancedStruct
		TArray<void*> RawData;
		PropertyInnerHandle->AccessRawData(RawData);
		if (RawData.IsEmpty() || !RawData[0])
		{
			return;
		}

		FInstancedStruct* Instance = static_cast<FInstancedStruct*>(RawData[0]);
		if (!Instance || !Instance->IsValid())
		{
			return;
		}

		UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(Instance->GetScriptStruct());
		if (!InnerStruct)
		{
			return;
		}

		uint8* StructMemory = Instance->GetMutableMemory();
		if (!StructMemory)
		{
			return;
		}

		// Check if this type should be inlined (simple type)
		const bool bShouldInline = InnerStruct->HasMetaData(TEXT("PCGExInlineValue"));

		// Create FStructOnScope for the inner struct (FPCGExPropertyCompiled_*)
		TSharedRef<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(InnerStruct, StructMemory);

		if (bShouldInline)
		{
			// For simple types, just add the Value property directly
			if (const FProperty* ValueProperty = InnerStruct->FindPropertyByName(TEXT("Value")))
			{
				IDetailPropertyRow& Row = *ChildBuilder.AddExternalStructureProperty(StructOnScope, ValueProperty->GetFName());

				// If a compact inline widget is registered for this outer struct type, use it
				// instead of the default value widget (which would expand for complex types).
				// Compact-mode lookup: schema is read-only so the type definition is fixed;
				// only value-editing affordances make sense here.
				if (const FPCGExMakeInlineWidgetFn* Factory = FPCGExInlineWidgetRegistry::Find(InnerStruct->GetFName(), EPCGExInlineWidgetMode::Compact))
				{
					TSharedPtr<IPropertyHandle> ValuePropertyHandle = Row.GetPropertyHandle();
					if (ValuePropertyHandle.IsValid())
					{
						Row.CustomWidget()
						   .NameContent()
							[
								ValuePropertyHandle->CreatePropertyNameWidget()
							]
							.ValueContent()
							.MinDesiredWidth(250.0f)
							.MaxDesiredWidth(3000.0f)
							[
								(*Factory)(ValuePropertyHandle.ToSharedRef())
							];
					}
				}
			}
		}
		else
		{
			FPCGExInlineWidgetRegistry::AddComplexValueRows(ChildBuilder, StructOnScope, InnerStruct);
		}
	}
	else
	{
		// Normal mode: Show Name and Property with full editing capabilities

		// Watch for changes and sync
		if (NameHandle.IsValid())
		{
			NameHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnSchemaChanged));
			ChildBuilder.AddProperty(NameHandle.ToSharedRef());
		}

		if (PropertyInnerHandle.IsValid())
		{
			PropertyInnerHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnSchemaChanged));
			PropertyInnerHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnSchemaChanged));
			ChildBuilder.AddProperty(PropertyInnerHandle.ToSharedRef()).ShouldAutoExpand(true);
		}
	}
}
