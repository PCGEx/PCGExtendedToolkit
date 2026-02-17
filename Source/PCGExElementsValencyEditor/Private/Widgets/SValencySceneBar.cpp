// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencySceneBar.h"

#include "Editor.h"
#include "Selection.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Volumes/ValencyContextVolume.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"

namespace Style = PCGExValencyWidgets::Style;

#pragma region SValencySceneBar

void SValencySceneBar::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	ChildSlot
	[
		SAssignNew(ContentArea, SBox)
	];

	if (GEditor)
	{
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddSP(
			this, &SValencySceneBar::OnSelectionChangedCallback);
		OnComponentSelectionChangedHandle = GEditor->GetSelectedComponents()->SelectionChangedEvent.AddSP(
			this, &SValencySceneBar::OnSelectionChangedCallback);
	}

	if (EditorMode)
	{
		OnSceneChangedHandle = EditorMode->OnSceneChanged.AddSP(
			this, &SValencySceneBar::RefreshContent);
	}

	RefreshContent();
}

void SValencySceneBar::OnSelectionChangedCallback(UObject* InObject)
{
	RefreshContent();
}

void SValencySceneBar::RefreshContent()
{
	if (!ContentArea.IsValid() || !EditorMode)
	{
		return;
	}

	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	// Determine current context volumes based on selection
	TArray<TWeakObjectPtr<AValencyContextVolume>> ContextVolumes;
	APCGExValencyCageBase* SelectedCage = nullptr;

	if (GEditor)
	{
		// Check for selected cage
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(*It))
			{
				SelectedCage = Cage;
				ContextVolumes = Cage->GetContainingVolumes();
				break;
			}
		}
	}

	// Build label text
	FText ContextLabel;
	if (SelectedCage)
	{
		// Filter to valid volumes
		int32 ValidCount = 0;
		FString FirstName;
		for (const TWeakObjectPtr<AValencyContextVolume>& VolPtr : ContextVolumes)
		{
			if (AValencyContextVolume* Vol = VolPtr.Get())
			{
				ValidCount++;
				if (FirstName.IsEmpty())
				{
					FirstName = Vol->GetActorNameOrLabel();
				}
			}
		}

		if (ValidCount == 0)
		{
			ContextLabel = NSLOCTEXT("PCGExValency", "NoContext", "No context");
		}
		else if (ValidCount == 1)
		{
			ContextLabel = FText::FromString(FirstName);
		}
		else
		{
			ContextLabel = FText::Format(
				NSLOCTEXT("PCGExValency", "NContexts", "{0} contexts"),
				FText::AsNumber(ValidCount));
		}
	}
	else
	{
		const int32 TotalVolumes = EditorMode->GetCachedVolumes().Num();
		ContextLabel = FText::Format(
			NSLOCTEXT("PCGExValency", "AllVolumes", "{0} volumes"),
			FText::AsNumber(TotalVolumes));
	}

	// Build menu for the combo button
	TArray<TWeakObjectPtr<AValencyContextVolume>> MenuVolumes;
	if (SelectedCage)
	{
		MenuVolumes = ContextVolumes;
	}
	else
	{
		for (const TWeakObjectPtr<AValencyContextVolume>& VolPtr : EditorMode->GetCachedVolumes())
		{
			MenuVolumes.Add(VolPtr);
		}
	}

	ContentArea->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			PCGExValencyWidgets::MakeRebuildAllButton(EditorMode)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(4, 1))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(ContextLabel)
				.Font(Style::Label())
			]
			.OnGetMenuContent_Lambda([MenuVolumes]() -> TSharedRef<SWidget>
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				bool bHasEntries = false;
				for (const TWeakObjectPtr<AValencyContextVolume>& VolPtr : MenuVolumes)
				{
					AValencyContextVolume* Vol = VolPtr.Get();
					if (!Vol) continue;

					bHasEntries = true;
					TWeakObjectPtr<AActor> WeakActor(Vol);
					const FLinearColor VolColor = Vol->DebugColor;
					const FString VolName = Vol->GetActorNameOrLabel();

					MenuBuilder.AddMenuEntry(
						FUIAction(FExecuteAction::CreateLambda([WeakActor]()
						{
							if (AActor* A = WeakActor.Get())
							{
								if (GEditor)
								{
									GEditor->SelectNone(true, true);
									GEditor->SelectActor(A, true, true);
								}
							}
						})),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 6, 0)
						[
							SNew(SColorBlock)
							.Color(VolColor)
							.Size(FVector2D(10, 10))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(VolName))
							.Font(Style::Label())
						],
						NAME_None,
						FText::Format(NSLOCTEXT("PCGExValency", "SelectVolumeTip", "Select volume '{0}'"),
							FText::FromString(VolName))
					);
				}

				if (!bHasEntries)
				{
					MenuBuilder.AddMenuEntry(
						NSLOCTEXT("PCGExValency", "NoVolumesAvailable", "(no volumes)"),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction()
					);
				}

				return MenuBuilder.MakeWidget();
			})
			.ToolTipText(NSLOCTEXT("PCGExValency", "ParentContextTip", "Parent context volumes. Click to select a volume."))
		]
	);
}

#pragma endregion
