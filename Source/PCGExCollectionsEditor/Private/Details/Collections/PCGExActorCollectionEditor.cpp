// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExActorCollectionEditor.h"

#include "Editor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Selection.h"
#include "Collections/PCGExActorCollection.h"
#include "Misc/PackageName.h"

static void AddOrUpdateActorEntry(UPCGExActorCollection* Collection, AActor* Actor)
{
	TSoftClassPtr<AActor> ActorClass(Actor->GetClass());
	FSoftObjectPath WorldPath(Actor->GetWorld());
	FName ActorFName = Actor->GetFName();

	FPCGExActorCollectionEntry* Existing = Collection->Entries.FindByPredicate(
		[&](const FPCGExActorCollectionEntry& E)
		{
			return E.DeltaSourceActorName == ActorFName
				&& E.DeltaSourceLevel.ToSoftObjectPath() == WorldPath;
		});

	if (Existing)
	{
		Existing->Actor = ActorClass;
	}
	else
	{
		FPCGExActorCollectionEntry& New = Collection->Entries.Emplace_GetRef();
		New.Actor = ActorClass;
		New.DeltaSourceLevel = TSoftObjectPtr<UWorld>(WorldPath);
		New.DeltaSourceActorName = ActorFName;
	}
}

FPCGExActorCollectionEditor::FPCGExActorCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

void FPCGExActorCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
	FPCGExAssetCollectionEditor::BuildAssetHeaderToolbar(ToolbarBuilder);

#define PCGEX_CURRENT_COLLECTION if (UPCGExActorCollection* Collection = Cast<UPCGExActorCollection>(EditedCollection.Get()))

#pragma region Cleanup

	ToolbarBuilder.BeginSection("CleanupSection");
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						PCGEX_CURRENT_COLLECTION
						{
							UWorld* World = GEditor->GetEditorWorldContext().World();
							if (!World || !World->PersistentLevel) { return; }

							const FSoftObjectPath CurrentWorldPath(World);
							Collection->Modify();

							const int32 Removed = Collection->Entries.RemoveAll(
								[&](const FPCGExActorCollectionEntry& E)
								{
									if (E.DeltaSourceLevel.ToSoftObjectPath() != CurrentWorldPath) { return false; }
									if (E.DeltaSourceActorName == NAME_None) { return false; }

									for (const AActor* Actor : World->PersistentLevel->Actors)
									{
										if (Actor && Actor->GetFName() == E.DeltaSourceActorName) { return false; }
									}
									return true;
								});

							if (Removed > 0)
							{
								Collection->MarkPackageDirty();
								FCoreUObjectDelegates::BroadcastOnObjectModified(Collection);
							}
						}
					})
			),
			NAME_None,
			FText::GetEmpty(),
			INVTEXT("Remove Missing\nRemove entries whose delta source actor no longer exists in the current level.\nEntries referencing other levels are left untouched."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X")
		);

		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						PCGEX_CURRENT_COLLECTION
						{
							Collection->Modify();

							const int32 Removed = Collection->Entries.RemoveAll(
								[](const FPCGExActorCollectionEntry& E)
								{
									if (!E.DeltaSourceLevel.IsNull())
									{
										const FString PackageName = E.DeltaSourceLevel.ToSoftObjectPath().GetLongPackageName();
										if (PackageName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
										{
											return true;
										}

										if (E.DeltaSourceActorName == NAME_None)
										{
											return true;
										}
									}

									if (E.Actor.IsNull() && !E.SubCollection)
									{
										return true;
									}

									return false;
								});

							if (Removed > 0)
							{
								Collection->MarkPackageDirty();
								FCoreUObjectDelegates::BroadcastOnObjectModified(Collection);
							}
						}
					})
			),
			NAME_None,
			FText::GetEmpty(),
			INVTEXT("Cleanup\nRemove broken entries:\n- Delta source level that no longer exists\n- Incomplete delta references (level set but no actor name)\n- Empty entries (no actor class and no subcollection)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Adjust")
		);
	}
	ToolbarBuilder.EndSection();

#pragma endregion

#undef PCGEX_CURRENT_COLLECTION
}

void FPCGExActorCollectionEditor::BuildAddMenuContent(const TSharedRef<SVerticalBox>& MenuBox)
{
	FPCGExAssetCollectionEditor::BuildAddMenuContent(MenuBox);

#define PCGEX_CURRENT_COLLECTION if (UPCGExActorCollection* Collection = Cast<UPCGExActorCollection>(EditedCollection.Get()))

	MenuBox->AddSlot()
		.AutoHeight()
		.Padding(4, 0, 4, 4)
		[
			SNew(SButton)
			.Text(INVTEXT("Add Selected Actors"))
			.OnClicked_Lambda(
				[this]()
				{
					PCGEX_CURRENT_COLLECTION
					{
						USelection* Selection = GEditor->GetSelectedActors();
						if (!Selection || Selection->Num() == 0) { return FReply::Handled(); }

						Collection->Modify();

						for (int32 i = 0; i < Selection->Num(); ++i)
						{
							if (AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i)))
							{
								AddOrUpdateActorEntry(Collection, Actor);
							}
						}

						Collection->MarkPackageDirty();
						FCoreUObjectDelegates::BroadcastOnObjectModified(Collection);
					}
					return FReply::Handled();
				})
			.ToolTipText(INVTEXT("Add currently selected actors from the viewport to this collection.\nExisting entries with matching delta source are updated."))
		];

	TSharedPtr<SEditableTextBox> NameSearchBox;

	MenuBox->AddSlot()
		.AutoHeight()
		.Padding(4, 0, 4, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			  .FillWidth(1.0f)
			  .VAlign(VAlign_Center)
			[
				SAssignNew(NameSearchBox, SEditableTextBox)
				.HintText(INVTEXT("Actor name..."))
			]
			+ SHorizontalBox::Slot()
			  .AutoWidth()
			  .VAlign(VAlign_Center)
			  .Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(INVTEXT("Search"))
				.OnClicked_Lambda(
					[this, NameSearchBox]()
					{
						if (!NameSearchBox.IsValid() || NameSearchBox->GetText().IsEmpty()) { return FReply::Handled(); }

						PCGEX_CURRENT_COLLECTION
						{
							const FString SearchTerm = NameSearchBox->GetText().ToString();
							UWorld* World = GEditor->GetEditorWorldContext().World();
							if (!World || !World->PersistentLevel) { return FReply::Handled(); }

							Collection->Modify();
							int32 Added = 0;

							for (AActor* Actor : World->PersistentLevel->Actors)
							{
								if (!Actor) { continue; }
								if (Actor->GetFName().ToString().Contains(SearchTerm, ESearchCase::IgnoreCase))
								{
									AddOrUpdateActorEntry(Collection, Actor);
									Added++;
								}
							}

							if (Added > 0)
							{
								Collection->MarkPackageDirty();
								FCoreUObjectDelegates::BroadcastOnObjectModified(Collection);
							}
						}
						return FReply::Handled();
					})
				.ToolTipText(INVTEXT("Search for actors by name in the current level and add matching ones."))
			]
		];

#undef PCGEX_CURRENT_COLLECTION
}
