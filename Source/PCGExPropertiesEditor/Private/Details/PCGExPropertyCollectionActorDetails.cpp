// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyCollectionActorDetails.h"

#include "BlueprintEditorModule.h"
#include "DetailBuilderTypes.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailPropertyRow.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExPropertySchema.h"
#include "PropertyHandle.h"
#include "SWarningOrErrorBox.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExPropertyCollectionActorDetails"

namespace PCGExPropertyCollectionActorDetails
{
	// Jump to wherever the schema is authored. Blueprint-authored components open the owning
	// Blueprint on their SCS node (the subobject editor resolves a level instance to its template);
	// everything else selects the component in the level details, which is where its schema lives.
	void EditSchema(const TWeakObjectPtr<UPCGExPropertyCollectionComponent>& WeakComponent)
	{
		UPCGExPropertyCollectionComponent* Component = WeakComponent.Get();
		if (!Component || !GEditor)
		{
			return;
		}

		const AActor* Owner = Component->GetOwner();
		UBlueprint* Blueprint = (Owner && Component->CreationMethod != EComponentCreationMethod::Instance)
			? UBlueprint::GetBlueprintFromClass(Owner->GetClass())
			: nullptr;

		if (Blueprint)
		{
			if (const TSharedPtr<IBlueprintEditor> BlueprintEditor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, /*bOpenEditor*/ true))
			{
				BlueprintEditor->FocusWindow();
				BlueprintEditor->FindAndSelectSubobjectEditorTreeNode(Component, /*IsCntrlDown*/ false);
			}
			return;
		}

		GEditor->SelectComponent(Component, /*bInSelected*/ true, /*bNotify*/ true);
	}

	TSharedRef<SWidget> BuildCategoryHeader(UPCGExPropertyCollectionComponent* Component)
	{
		TArray<FPCGExPropertyResolved> Resolved;
		Component->GetProperties().Resolve(Resolved);
		const FText CountText = FText::Format(
			Resolved.Num() == 1 ? LOCTEXT("PropertyCountSingular", "{0} property") : LOCTEXT("PropertyCountPlural", "{0} properties"),
			Resolved.Num());

		const TWeakObjectPtr<UPCGExPropertyCollectionComponent> WeakComponent = Component;

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(CountText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("EditSchemaTooltip", "Open the schema definition: the Blueprint that authored this component, or the component itself when it was added to this actor."))
				.OnClicked_Lambda([WeakComponent]()
				{
					EditSchema(WeakComponent);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Edit"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EditSchema", "Edit Schema"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			];
	}

	void ExtendActorDetails(IDetailLayoutBuilder& DetailLayout, const FGetSelectedActors& GetSelectedActors)
	{
		const TArray<TWeakObjectPtr<AActor>>& SelectedActors = GetSelectedActors.Execute();
		if (SelectedActors.Num() != 1)
		{
			return;
		}

		// FindOnActor is the component every runtime consumer reads; the hoist mirrors that pick.
		UPCGExPropertyCollectionComponent* Component = UPCGExPropertyCollectionComponent::FindOnActor(SelectedActors[0].Get());
		if (!Component)
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailLayout.EditCategory(
			TEXT("PCGExProperties"), LOCTEXT("CategoryName", "PCGEx Properties"), ECategoryPriority::Important);
		Category.HeaderContent(BuildCategoryHeader(Component));

		IDetailPropertyRow* Row = Category.AddExternalObjectProperty(
			TArray<UObject*>{Component},
			GET_MEMBER_NAME_CHECKED(UPCGExPropertyCollectionComponent, Properties),
			EPropertyLocation::Default,
			FAddPropertyParams().UniqueId(TEXT("PCGExPropertyCollectionValues")));
		if (!Row)
		{
			return;
		}

		// CustomizeHeader runs at widget-build time, after this returns, so the metadata is visible to it.
		Row->GetPropertyHandle()->SetInstanceMetaData(ValuesOnlyMetaKey(), TEXT("true"));
	}
}

#pragma region FPCGExPropertyCollectionComponentDetails

TSharedRef<IDetailCustomization> FPCGExPropertyCollectionComponentDetails::MakeInstance()
{
	return MakeShared<FPCGExPropertyCollectionComponentDetails>();
}

void FPCGExPropertyCollectionComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}

	// Templates have no owning actor to check against; duplicates inside a Blueprint are out of scope.
	const UPCGExPropertyCollectionComponent* Component = Cast<UPCGExPropertyCollectionComponent>(Objects[0].Get());
	if (!Component || Component->IsTemplate())
	{
		return;
	}

	const UPCGExPropertyCollectionComponent* Primary = UPCGExPropertyCollectionComponent::FindOnActor(Component->GetOwner());
	if (!Primary || Primary == Component)
	{
		return;
	}

	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UPCGExPropertyCollectionComponent, Properties));

	DetailBuilder.EditCategory(TEXT("Properties"))
		.AddCustomRow(LOCTEXT("IgnoredFilter", "Ignored"))
		.WholeRowContent()
		[
			SNew(SWarningOrErrorBox)
			.MessageStyle(EMessageStyle::Warning)
			.Message(FText::Format(
				LOCTEXT("DuplicateComponent", "Only one Property Collection per actor is supported. This component is ignored; '{0}' is the one in use."),
				FText::FromName(Primary->GetFName())))
		];
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
