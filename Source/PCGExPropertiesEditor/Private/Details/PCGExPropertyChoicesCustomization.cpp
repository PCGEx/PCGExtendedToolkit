// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyChoicesCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "InstancedStructDetails.h"
#include "PCGExInlineWidgetRegistry.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/StyleColors.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FPCGExPropertyChoicesCustomization"

namespace PCGExPropertyChoicesCustomization
{
	bool IsUnderReadOnlySchema(const TSharedRef<IPropertyHandle>& Handle)
	{
		for (TSharedPtr<IPropertyHandle> Parent = Handle->GetParentHandle(); Parent.IsValid(); Parent = Parent->GetParentHandle())
		{
			if (Parent->HasMetaData(TEXT("ReadOnlySchema")))
			{
				return true;
			}
		}
		return false;
	}

	// The property struct instance Handle sits in: the nearest ancestor that is a property-derived struct row, or the
	// FInstancedStruct holding one (a struct row made from a provider may expose no FProperty). Single-object edits only.
	FStructView ResolveHost(const TSharedRef<IPropertyHandle>& Handle)
	{
		for (TSharedPtr<IPropertyHandle> Parent = Handle->GetParentHandle(); Parent.IsValid(); Parent = Parent->GetParentHandle())
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(Parent->GetProperty());
			if (!StructProperty || !StructProperty->Struct)
			{
				continue;
			}

			const bool bPropertyRow = StructProperty->Struct->IsChildOf(FPCGExProperty::StaticStruct());
			const bool bInstancedStruct = StructProperty->Struct == FInstancedStruct::StaticStruct();
			if (!bPropertyRow && !bInstancedStruct)
			{
				continue;
			}

			TArray<void*> RawData;
			Parent->AccessRawData(RawData);
			if (RawData.Num() != 1 || !RawData[0])
			{
				return FStructView();
			}
			if (bPropertyRow)
			{
				return FStructView(StructProperty->Struct, static_cast<uint8*>(RawData[0]));
			}

			FInstancedStruct* Instance = static_cast<FInstancedStruct*>(RawData[0]);
			const UScriptStruct* Type = Instance->GetScriptStruct();
			return Type && Type->IsChildOf(FPCGExProperty::StaticStruct()) ? FStructView(*Instance) : FStructView();
		}
		return FStructView();
	}
}

#pragma region FPCGExPropertyChoicesCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyChoicesCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyChoicesCustomization());
}

void FPCGExPropertyChoicesCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ChoicesHandlePtr = PropertyHandle;
	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	bReadOnly = PCGExPropertyChoicesCustomization::IsUnderReadOnlySchema(PropertyHandle);
	ItemsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyChoices, Items));
	const TSharedPtr<IPropertyHandle> LockedHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyChoices, bLocked));

	if (bReadOnly || !ItemsHandle.IsValid() || !LockedHandle.IsValid())
	{
		HeaderRow.Visibility(EVisibility::Collapsed);
		return;
	}

	// Rows are added per element below; the engine re-lays-out on a count change but never re-runs this
	// customization, so a shape change needs the deferred full rebuild.
	if (const TSharedPtr<IPropertyHandleArray> ItemsArray = ItemsHandle->AsArray())
	{
		ItemsArray->SetOnNumElementsChanged(FSimpleDelegate::CreateSP(this, &FPCGExPropertyChoicesCustomization::OnNumItemsChanged));
	}

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(3000.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				LockedHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LockedLabel", "Locked"))
				.ToolTipText(LockedHandle->GetToolTipText())
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(this, &FPCGExPropertyChoicesCustomization::GetCountText)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(12, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddCurrentValue", "Add Current Value"))
				.ToolTipText(LOCTEXT("AddCurrentValueToolTip", "Add the property's current value as a new choice."))
				.OnClicked(this, &FPCGExPropertyChoicesCustomization::OnAddCurrentValue)
			]
		];
}

void FPCGExPropertyChoicesCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (bReadOnly || !ItemsHandle.IsValid())
	{
		return;
	}

	// Elements as direct children (no "Items" level): each keeps the engine's insert / delete / duplicate buttons.
	uint32 NumItems = 0;
	ItemsHandle->GetNumChildren(NumItems);
	for (uint32 i = 0; i < NumItems; ++i)
	{
		if (const TSharedPtr<IPropertyHandle> Element = ItemsHandle->GetChildHandle(i))
		{
			ChildBuilder.AddProperty(Element.ToSharedRef());
		}
	}
}

FText FPCGExPropertyChoicesCustomization::GetCountText() const
{
	uint32 NumItems = 0;
	if (ItemsHandle.IsValid())
	{
		ItemsHandle->GetNumChildren(NumItems);
	}
	return NumItems == 1
		? LOCTEXT("OneChoice", "1 choice")
		: FText::Format(LOCTEXT("ChoiceCount", "{0} choices"), FText::AsNumber(static_cast<int32>(NumItems)));
}

FReply FPCGExPropertyChoicesCustomization::OnAddCurrentValue()
{
	const TSharedPtr<IPropertyHandle> ChoicesHandle = ChoicesHandlePtr.Pin();
	if (!ChoicesHandle.IsValid() || !ItemsHandle.IsValid())
	{
		return FReply::Handled();
	}

	const FStructView Host = PCGExPropertyChoicesCustomization::ResolveHost(ChoicesHandle.ToSharedRef());
	TArray<void*> RawData;
	ItemsHandle->AccessRawData(RawData);
	if (!Host.IsValid() || RawData.Num() != 1 || !RawData[0])
	{
		return FReply::Handled();
	}

	TArray<FPCGExPropertyChoice>* Items = static_cast<TArray<FPCGExPropertyChoice>*>(RawData[0]);
	const FName Label(*FString::Printf(TEXT("Choice %d"), Items->Num() + 1));

	// Raw add inside the handle's notify bracket: the owner is Modify()'d by NotifyPreChange, so undo restores the
	// whole array, and NotifyPostChange routes the host's structural sync. The rebuild re-aliases every row.
	FScopedTransaction Transaction(LOCTEXT("AddChoice", "Add Choice"));
	ItemsHandle->NotifyPreChange();
	Items->Add(PCGExProperties::MakeChoiceFromValue(FConstStructView(Host), Label));
	ItemsHandle->NotifyPostChange(EPropertyChangeType::ArrayAdd);
	ItemsHandle->NotifyFinishedChangingProperties();

	OnNumItemsChanged();
	return FReply::Handled();
}

void FPCGExPropertyChoicesCustomization::OnNumItemsChanged()
{
	if (const TSharedPtr<IPropertyUtilities> Utils = WeakPropertyUtilities.Pin())
	{
		Utils->RequestForceRefresh();
	}
}

#pragma endregion

#pragma region FPCGExPropertyChoiceCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyChoiceCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyChoiceCustomization());
}

void FPCGExPropertyChoiceCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> LabelHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyChoice, Label));
	CarrierHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyChoice, Value));
	if (!LabelHandle.IsValid() || !CarrierHandle.IsValid())
	{
		HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
		return;
	}

	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	const FPCGExPropertyChoice* Choice = (RawData.Num() == 1 && RawData[0]) ? static_cast<const FPCGExPropertyChoice*>(RawData[0]) : nullptr;
	const FStructView Host = PCGExPropertyChoicesCustomization::ResolveHost(PropertyHandle);
	const UScriptStruct* CarrierType = Choice ? Choice->Value.GetScriptStruct() : nullptr;
	const bool bCompatible = Choice && CarrierType && Host.IsValid() && PCGExProperties::IsChoiceCompatible(FConstStructView(Host), *Choice);

	TSharedRef<SWidget> ValueWidget = SNew(STextBlock)
		.Text(CarrierType
			      ? FText::Format(LOCTEXT("IncompatibleCarrier", "No longer matches the property ({0})"), CarrierType->GetDisplayNameText())
			      : LOCTEXT("EmptyCarrier", "Empty choice"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.ColorAndOpacity(FStyleColors::Warning);

	if (bCompatible)
	{
		// The carrier's fields as real child nodes: edits route through the engine's notify chain like any other row.
		CarrierProvider = MakeShared<FInstancedStructProvider>(CarrierHandle);
		CarrierHandle->RemoveChildren();
		for (const TSharedPtr<IPropertyHandle>& Field : CarrierHandle->AddChildStructure(CarrierProvider.ToSharedRef()))
		{
			if (Field.IsValid() && Field->GetProperty() && Field->GetProperty()->GetFName() == TEXT("Value"))
			{
				CarrierValueHandle = Field;
				break;
			}
		}

		bInlineCarrier = CarrierType->HasMetaData(TEXT("PCGExInlineValue"));
		if (CarrierValueHandle.IsValid())
		{
			if (bInlineCarrier)
			{
				const FPCGExMakeInlineWidgetFn* Factory = FPCGExInlineWidgetRegistry::Find(CarrierType->GetFName(), EPCGExInlineWidgetMode::Compact);
				ValueWidget = Factory ? (*Factory)(CarrierValueHandle.ToSharedRef()) : CarrierValueHandle->CreatePropertyValueWidget();
			}
			else
			{
				// Complex carriers expand to their editor; the header shows a live preview.
				const TWeakPtr<IPropertyHandle> WeakCarrier = CarrierHandle;
				ValueWidget = SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([WeakCarrier]() -> FText
					{
						TArray<void*> CarrierRaw;
						if (const TSharedPtr<IPropertyHandle> Pinned = WeakCarrier.Pin())
						{
							Pinned->AccessRawData(CarrierRaw);
						}
						if (CarrierRaw.Num() != 1 || !CarrierRaw[0])
						{
							return FText::GetEmpty();
						}
						return PCGExProperties::GetValuePreviewText(FConstStructView(*static_cast<const FInstancedStruct*>(CarrierRaw[0])));
					});
			}
		}
	}

	HeaderRow
		.NameContent()
		[
			LabelHandle->CreatePropertyValueWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(3000.0f)
		[
			ValueWidget
		];
}

void FPCGExPropertyChoiceCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (!bInlineCarrier && CarrierValueHandle.IsValid())
	{
		ChildBuilder.AddProperty(CarrierValueHandle.ToSharedRef());
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
