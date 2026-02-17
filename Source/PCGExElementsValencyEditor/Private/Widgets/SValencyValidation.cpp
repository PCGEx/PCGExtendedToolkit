// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyValidation.h"

#include "Editor.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Views/STableRow.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyCageOrbital.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Volumes/ValencyContextVolume.h"

namespace Style = PCGExValencyWidgets::Style;

void SValencyValidation::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	ChildSlot
	[
		SNew(SExpandableArea)
		.AreaTitle(NSLOCTEXT("PCGExValency", "ValidationHeader", "Validation"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "RunValidation", "Validate Scene"))
				.OnClicked_Lambda([this]()
				{
					RunValidation();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.MaxHeight(200.0f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FValencyValidationMessage>>)
				.ListItemsSource(&Messages)
				.OnGenerateRow(this, &SValencyValidation::OnGenerateRow)
				.OnSelectionChanged(this, &SValencyValidation::OnMessageClicked)
				.SelectionMode(ESelectionMode::Single)
			]
		]
	];

	// Bind to scene changes for auto-refresh
	if (EditorMode)
	{
		OnSceneChangedHandle = EditorMode->OnSceneChanged.AddSP(this, &SValencyValidation::RunValidation);
	}

	// Run initial validation
	RunValidation();
}

void SValencyValidation::RunValidation()
{
	Messages.Empty();

	if (!EditorMode)
	{
		if (ListView.IsValid())
		{
			ListView->RequestListRefresh();
		}
		return;
	}

	ValidateCages();
	ValidateVolumes();
	ValidateScene();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SValencyValidation::OnGenerateRow(TSharedPtr<FValencyValidationMessage> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Severity icon
	FString SeverityIcon;
	FLinearColor SeverityColor;
	switch (Item->Severity)
	{
	case FValencyValidationMessage::ESeverity::Error:
		SeverityIcon = TEXT("X");
		SeverityColor = FLinearColor(1.0f, 0.2f, 0.2f);
		break;
	case FValencyValidationMessage::ESeverity::Warning:
		SeverityIcon = TEXT("!");
		SeverityColor = FLinearColor(1.0f, 0.5f, 0.0f);
		break;
	default:
		SeverityIcon = TEXT("i");
		SeverityColor = FLinearColor(0.5f, 0.7f, 1.0f);
		break;
	}

	return SNew(STableRow<TSharedPtr<FValencyValidationMessage>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			// Severity indicator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(SeverityIcon))
				.Font(Style::Title())
				.ColorAndOpacity(FSlateColor(SeverityColor))
			]
			// Source name
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->SourceName))
				.Font(Style::SmallBold())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
			]
			// Message
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Message))
				.Font(Style::Small())
				.AutoWrapText(true)
			]
		];
}

void SValencyValidation::OnMessageClicked(TSharedPtr<FValencyValidationMessage> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || !Item->SourceActor.IsValid() || !GEditor)
	{
		return;
	}

	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	// Select the source actor
	GEditor->SelectNone(true, true);
	GEditor->SelectActor(Item->SourceActor.Get(), true, true);
}

void SValencyValidation::ValidateCages()
{
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : EditorMode->GetCachedCages())
	{
		APCGExValencyCageBase* Cage = CagePtr.Get();
		if (!Cage)
		{
			continue;
		}

		const FString CageName = Cage->GetCageDisplayName();

		// Check for unconnected orbitals
		const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
		int32 UnconnectedCount = 0;
		for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
		{
			if (Orbital.bEnabled && Orbital.GetDisplayConnection() == nullptr)
			{
				UnconnectedCount++;
			}
		}

		if (UnconnectedCount > 0)
		{
			auto Msg = MakeShared<FValencyValidationMessage>();
			Msg->Severity = FValencyValidationMessage::ESeverity::Warning;
			Msg->SourceActor = Cage;
			Msg->SourceName = CageName;
			Msg->Message = FString::Printf(TEXT("%d unconnected orbitals"), UnconnectedCount);
			Messages.Add(Msg);
		}

		// Check regular cages for no assets and empty mirror entries
		if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
		{
			if (RegularCage->GetAllAssetEntries().Num() == 0 && !RegularCage->bIsTemplate)
			{
				auto Msg = MakeShared<FValencyValidationMessage>();
				Msg->Severity = FValencyValidationMessage::ESeverity::Warning;
				Msg->SourceActor = Cage;
				Msg->SourceName = CageName;
				Msg->Message = TEXT("No registered assets");
				Messages.Add(Msg);
			}

			// Check for empty (unassigned) mirror source entries
			int32 EmptyMirrorCount = 0;
			for (const FPCGExMirrorSource& MirrorEntry : RegularCage->MirrorSources)
			{
				if (!MirrorEntry.IsValid())
				{
					EmptyMirrorCount++;
				}
			}
			if (EmptyMirrorCount > 0)
			{
				auto Msg = MakeShared<FValencyValidationMessage>();
				Msg->Severity = FValencyValidationMessage::ESeverity::Info;
				Msg->SourceActor = Cage;
				Msg->SourceName = CageName;
				Msg->Message = FString::Printf(TEXT("%d mirror source %s unassigned"),
					EmptyMirrorCount, EmptyMirrorCount == 1 ? TEXT("entry") : TEXT("entries"));
				Messages.Add(Msg);
			}
		}

		// Check for no orbital set
		if (!Cage->IsNullCage() && !Cage->GetEffectiveOrbitalSet())
		{
			auto Msg = MakeShared<FValencyValidationMessage>();
			Msg->Severity = FValencyValidationMessage::ESeverity::Error;
			Msg->SourceActor = Cage;
			Msg->SourceName = CageName;
			Msg->Message = TEXT("No orbital set (not in any volume or no override)");
			Messages.Add(Msg);
		}
	}
}

void SValencyValidation::ValidateVolumes()
{
	for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : EditorMode->GetCachedVolumes())
	{
		AValencyContextVolume* Volume = VolumePtr.Get();
		if (!Volume)
		{
			continue;
		}

		const FString VolumeName = Volume->GetActorNameOrLabel();

		// Check for missing bonding rules
		if (!Volume->BondingRules)
		{
			auto Msg = MakeShared<FValencyValidationMessage>();
			Msg->Severity = FValencyValidationMessage::ESeverity::Error;
			Msg->SourceActor = Volume;
			Msg->SourceName = VolumeName;
			Msg->Message = TEXT("No bonding rules assigned");
			Messages.Add(Msg);
		}

		// Check for empty volumes
		TArray<APCGExValencyCageBase*> ContainedCages;
		Volume->CollectContainedCages(ContainedCages);
		if (ContainedCages.Num() == 0)
		{
			auto Msg = MakeShared<FValencyValidationMessage>();
			Msg->Severity = FValencyValidationMessage::ESeverity::Info;
			Msg->SourceActor = Volume;
			Msg->SourceName = VolumeName;
			Msg->Message = TEXT("Contains no cages");
			Messages.Add(Msg);
		}
	}
}

void SValencyValidation::ValidateScene()
{
	// Check for orphaned cages (not in any volume)
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : EditorMode->GetCachedCages())
	{
		APCGExValencyCageBase* Cage = CagePtr.Get();
		if (!Cage || Cage->IsNullCage())
		{
			continue;
		}

		const auto& ContainingVolumes = Cage->GetContainingVolumes();
		bool bInAnyVolume = false;
		for (const TWeakObjectPtr<AValencyContextVolume>& VolumePtr : ContainingVolumes)
		{
			if (VolumePtr.IsValid())
			{
				bInAnyVolume = true;
				break;
			}
		}

		if (!bInAnyVolume)
		{
			auto Msg = MakeShared<FValencyValidationMessage>();
			Msg->Severity = FValencyValidationMessage::ESeverity::Warning;
			Msg->SourceActor = Cage;
			Msg->SourceName = Cage->GetCageDisplayName();
			Msg->Message = TEXT("Not contained in any volume");
			Messages.Add(Msg);
		}
	}

	// Check for mirror cycles (A mirrors B mirrors A)
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : EditorMode->GetCachedCages())
	{
		if (const APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			for (const FPCGExMirrorSource& MirrorEntry : Cage->MirrorSources)
			{
				if (const APCGExValencyCage* SourceCage = Cast<APCGExValencyCage>(MirrorEntry.Source.Get()))
				{
					// Check if the source also mirrors us
					for (const FPCGExMirrorSource& SourceMirrorEntry : SourceCage->MirrorSources)
					{
						if (SourceMirrorEntry.Source.Get() == Cage)
						{
							auto Msg = MakeShared<FValencyValidationMessage>();
							Msg->Severity = FValencyValidationMessage::ESeverity::Warning;
							Msg->SourceActor = const_cast<APCGExValencyCage*>(Cage);
							Msg->SourceName = Cage->GetCageDisplayName();
							Msg->Message = FString::Printf(TEXT("Mirror cycle with %s"), *SourceCage->GetCageDisplayName());
							Messages.Add(Msg);
							break;
						}
					}
				}
			}
		}
	}

	// Show all-clear if no messages
	if (Messages.Num() == 0)
	{
		auto Msg = MakeShared<FValencyValidationMessage>();
		Msg->Severity = FValencyValidationMessage::ESeverity::Info;
		Msg->Message = TEXT("No issues found");
		Messages.Add(Msg);
	}
}
