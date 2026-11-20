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
#include "Details/PCGExPropertyChoicesCustomization.h"
#include "Details/PCGExPropertyLabelRow.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertySchemaCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertySchemaCustomization());
}

const FPCGExPropertySchema* FPCGExPropertySchemaCustomization::AccessSchema() const
{
	const FPCGExPropertySchema* First = nullptr;
	ForEachSchema([&First](FPCGExPropertySchema& Schema)
	{
		First = First ? First : &Schema;
	});
	return First;
}

void FPCGExPropertySchemaCustomization::ForEachSchema(const TFunctionRef<void(FPCGExPropertySchema&)> Visitor) const
{
	// EnumerateRawData dereferences the handle's property node unchecked.
	const TSharedPtr<IPropertyHandle> PropertyHandle = PropertyHandlePtr.Pin();
	if (!PropertyHandle.IsValid() || !PropertyHandle->IsValidHandle())
	{
		return;
	}
	PropertyHandle->EnumerateRawData([&Visitor](void* Raw, const int32 /*Index*/, const int32 /*Num*/)
	{
		if (Raw)
		{
			Visitor(*static_cast<FPCGExPropertySchema*>(Raw));
		}
		return true;
	});
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
	ForEachSchema([](FPCGExPropertySchema& Schema)
	{
		Schema.SyncPropertyName();
		// The host's own sync has already finished a retype in progress; this covers a host that has none.
		Schema.ConsumeRetype();
	});

	// Note: Parent collection will handle ForceRefresh via its own listener
}

void FPCGExPropertySchemaCustomization::OnPropertyPreChange()
{
	// Raised for a change of the Property node itself (the type picker, a paste), not for an edit under it.
	ForEachSchema([](FPCGExPropertySchema& Schema)
	{
		Schema.RetypedFrom = Schema.Property;
	});
}

void FPCGExPropertySchemaCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandlePtr = PropertyHandle;
	bIsReadOnly = PCGExPropertyChoicesUI::IsUnderReadOnlySchema(PropertyHandle);

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

		UScriptStruct* InnerStruct = nullptr;
		uint8* StructMemory = nullptr;
		if (!PCGExPropertyChoiceHost::AccessInstancedStruct(PropertyInnerHandle, InnerStruct, StructMemory))
		{
			return;
		}

		// Check if this type should be inlined (simple type)
		const bool bShouldInline = FPCGExInlineWidgetRegistry::IsInlineValue(InnerStruct, EPCGExInlineWidgetMode::Compact);

		// Create FStructOnScope for the inner struct (FPCGExPropertyCompiled_*)
		TSharedRef<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(InnerStruct, StructMemory);

		// Values-only: the choices are picked from here, never authored. Read through the Property handle, which
		// follows the struct where the scope above only aliases today's memory.
		FPCGExChoiceRowArgs Choices;
		Choices.Host = FPCGExPropertyHostRef(PropertyInnerHandle, EPCGExPropertyHostKind::Instanced);

		if (bShouldInline)
		{
			// Same funnel as override rows: Compact factory, choices picker, default widget.
			if (const FProperty* ValueProperty = PCGExProperties::FindValueProperty(InnerStruct))
			{
				FPCGExInlineWidgetRegistry::AddCompactValueRow(
					ChildBuilder, StructOnScope, InnerStruct,
					SNew(STextBlock)
					.Text(ValueProperty->GetDisplayNameText())
					.Font(IDetailLayoutBuilder::GetDetailFont()),
					TAttribute<bool>(true), Choices);
			}
		}
		else
		{
			FPCGExInlineWidgetRegistry::AddComplexValueRows(ChildBuilder, StructOnScope, InnerStruct, TAttribute<bool>(true), nullptr, Choices);
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
			PropertyInnerHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnPropertyPreChange));
			PropertyInnerHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnSchemaChanged));
			PropertyInnerHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertySchemaCustomization::OnSchemaChanged));
			ChildBuilder.AddProperty(PropertyInnerHandle.ToSharedRef()).ShouldAutoExpand(true);
		}
	}
}
