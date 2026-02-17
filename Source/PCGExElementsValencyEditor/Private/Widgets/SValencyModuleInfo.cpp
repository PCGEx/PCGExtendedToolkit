// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyModuleInfo.h"

#include "Editor.h"
#include "Selection.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Volumes/ValencyContextVolume.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"
#include "Widgets/SValencyInfoPanel.h"

namespace Style = PCGExValencyWidgets::Style;

#pragma region SValencyModuleInfo

void SValencyModuleInfo::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	ChildSlot
	[
		SAssignNew(ContentArea, SBox)
	];

	if (GEditor)
	{
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddSP(
			this, &SValencyModuleInfo::OnSelectionChangedCallback);
		OnComponentSelectionChangedHandle = GEditor->GetSelectedComponents()->SelectionChangedEvent.AddSP(
			this, &SValencyModuleInfo::OnSelectionChangedCallback);
	}

	if (EditorMode)
	{
		OnSceneChangedHandle = EditorMode->OnSceneChanged.AddSP(
			this, &SValencyModuleInfo::RefreshContent);
	}

	RefreshContent();
}

void SValencyModuleInfo::OnSelectionChangedCallback(UObject* InObject)
{
	RefreshContent();
}

void SValencyModuleInfo::RefreshContent()
{
	if (!ContentArea.IsValid())
	{
		return;
	}

	TSharedRef<SWidget> NewContent = BuildHintContent();
	bool bFoundSpecificContent = false;

	if (GEditor)
	{
		// Check components first (connector selected -> show owning cage info)
		if (USelection* CompSelection = GEditor->GetSelectedComponents())
		{
			for (FSelectionIterator It(*CompSelection); It; ++It)
			{
				if (UPCGExValencyCageConnectorComponent* Connector = Cast<UPCGExValencyCageConnectorComponent>(*It))
				{
					if (APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
					{
						// Dispatch to typed cage panel
						if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(OwnerCage))
						{
							NewContent = SNew(SValencyRegularCagePanel).EditorMode(EditorMode).Cage(RegularCage);
						}
						else if (APCGExValencyCagePattern* PatternCage = Cast<APCGExValencyCagePattern>(OwnerCage))
						{
							NewContent = SNew(SValencyPatternCagePanel).EditorMode(EditorMode).Cage(PatternCage);
						}
						else if (APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(OwnerCage))
						{
							NewContent = SNew(SValencyNullCagePanel).EditorMode(EditorMode).Cage(NullCage);
						}
						bFoundSpecificContent = true;
					}
					break;
				}
			}
		}

		// If no component matched, check actors
		if (!bFoundSpecificContent)
		{
			USelection* Selection = GEditor->GetSelectedActors();
			for (FSelectionIterator It(*Selection); It; ++It)
			{
				if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(*It))
				{
					NewContent = SNew(SValencyRegularCagePanel).EditorMode(EditorMode).Cage(RegularCage);
					break;
				}
				if (APCGExValencyCagePattern* PatternCage = Cast<APCGExValencyCagePattern>(*It))
				{
					NewContent = SNew(SValencyPatternCagePanel).EditorMode(EditorMode).Cage(PatternCage);
					break;
				}
				if (APCGExValencyCageNull* NullCage = Cast<APCGExValencyCageNull>(*It))
				{
					NewContent = SNew(SValencyNullCagePanel).EditorMode(EditorMode).Cage(NullCage);
					break;
				}
				if (AValencyContextVolume* Volume = Cast<AValencyContextVolume>(*It))
				{
					NewContent = BuildVolumeInfoContent(Volume);
					break;
				}
				if (APCGExValencyAssetPalette* Palette = Cast<APCGExValencyAssetPalette>(*It))
				{
					NewContent = SNew(SValencyPalettePanel).EditorMode(EditorMode).Palette(Palette);
					break;
				}
			}
		}
	}

	ContentArea->SetContent(NewContent);
}

TSharedRef<SWidget> SValencyModuleInfo::BuildHintContent()
{
	return PCGExValencyWidgets::MakeHintText(
		NSLOCTEXT("PCGExValency", "SelectHint", "Select a cage, volume, or palette"));
}

TSharedRef<SWidget> SValencyModuleInfo::BuildVolumeInfoContent(AValencyContextVolume* Volume)
{
	if (!Volume)
	{
		return BuildHintContent();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeSectionHeader(FText::FromString(Volume->GetActorNameOrLabel()))
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledColorRow(
			NSLOCTEXT("PCGExValency", "VolumeColor", "Color"),
			Volume->DebugColor)
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "VolumeProbeRadius", "Default Probe Radius"),
			FText::AsNumber(static_cast<int32>(Volume->DefaultProbeRadius)))
	];

	// Bonding rules
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "VolumeBondingRules", "Bonding Rules"),
			Volume->BondingRules
				? FText::FromString(Volume->BondingRules->GetName())
				: NSLOCTEXT("PCGExValency", "None", "(none)"))
	];

	// Connector Set
	{
		UPCGExValencyConnectorSet* EffectiveSet = Volume->GetEffectiveConnectorSet();
		Content->AddSlot().AutoHeight()
		[
			PCGExValencyWidgets::MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "VolumeConnectorSet", "Connector Set"),
				EffectiveSet
					? FText::FromString(EffectiveSet->GetName())
					: NSLOCTEXT("PCGExValency", "VolumeConnectorSetNone", "(none)"))
		];
	}

	// Count contained cages
	TArray<APCGExValencyCageBase*> ContainedCages;
	Volume->CollectContainedCages(ContainedCages);

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "VolumeContainedCages", "Contained Cages"),
			FText::AsNumber(ContainedCages.Num()))
	];

	// List contained cages
	for (APCGExValencyCageBase* ContainedCage : ContainedCages)
	{
		if (!ContainedCage) continue;

		Content->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("  %s"), *ContainedCage->GetCageDisplayName())))
			.Font(Style::Label())
		];
	}

	return Content;
}

#pragma endregion
