// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExActorCollectionEditor.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"

#include "Selection.h"
#include "Collections/PCGExActorCollection.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace PCGExActorCollectionEditor
{
	void AddOrUpdateActorEntry(UPCGExActorCollection* Collection, AActor* Actor)
	{
		TSoftClassPtr<AActor> ActorClass(Actor->GetClass());
		const TSoftObjectPtr<AActor> ActorRef(Actor);

		FPCGExActorCollectionEntry* Existing = Collection->Entries.FindByPredicate(
			[&](const FPCGExActorCollectionEntry& E)
			{
				return E.DeltaSourceActor == ActorRef;
			});

		if (Existing)
		{
			Existing->Actor = ActorClass;
		}
		else
		{
			FPCGExActorCollectionEntry& New = Collection->Entries.Emplace_GetRef();
			New.Actor = ActorClass;
			New.DeltaSourceActor = ActorRef;
		}
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
							if (!World || !World->PersistentLevel)
							{
								return;
							}

							const FName CurrentWorldPackage = World->GetPackage()->GetFName();
							Collection->Modify();

							// The current level is loaded, so a path that no longer resolves is a missing actor.
							const int32 Removed = Collection->Entries.RemoveAll(
								[&](const FPCGExActorCollectionEntry& E)
								{
									const FSoftObjectPath& Path = E.DeltaSourceActor.ToSoftObjectPath();
									if (!Path.IsValid() || Path.GetLongPackageFName() != CurrentWorldPackage)
									{
										return false;
									}
									return Path.ResolveObject() == nullptr;
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
									if (!E.DeltaSourceActor.IsNull())
									{
										const FString PackageName = E.DeltaSourceActor.ToSoftObjectPath().GetLongPackageName();
										if (PackageName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
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
			INVTEXT("Cleanup\nRemove broken entries:\n- Delta source actor whose level no longer exists\n- Empty entries (no actor class and no subcollection)"),
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
					if (!Selection || Selection->Num() == 0)
					{
						return FReply::Handled();
					}

					Collection->Modify();

					for (int32 i = 0; i < Selection->Num(); ++i)
					{
						if (AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i)))
						{
							PCGExActorCollectionEditor::AddOrUpdateActorEntry(Collection, Actor);
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
					if (!NameSearchBox.IsValid() || NameSearchBox->GetText().IsEmpty())
					{
						return FReply::Handled();
					}

					PCGEX_CURRENT_COLLECTION
					{
						const FString SearchTerm = NameSearchBox->GetText().ToString();
						UWorld* World = GEditor->GetEditorWorldContext().World();
						if (!World || !World->PersistentLevel)
						{
							return FReply::Handled();
						}

						Collection->Modify();
						int32 Added = 0;

						for (AActor* Actor : World->PersistentLevel->Actors)
						{
							if (!Actor)
							{
								continue;
							}
							if (Actor->GetFName().ToString().Contains(SearchTerm, ESearchCase::IgnoreCase))
							{
								PCGExActorCollectionEditor::AddOrUpdateActorEntry(Collection, Actor);
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

const UClass* FPCGExActorCollectionEditor::GetTilePickerAllowedClass() const
{
	return AActor::StaticClass();
}
