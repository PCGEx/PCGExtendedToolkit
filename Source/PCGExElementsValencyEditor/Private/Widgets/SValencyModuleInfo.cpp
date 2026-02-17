// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyModuleInfo.h"

#include "Editor.h"
#include "Selection.h"
#include "ScopedTransaction.h"

#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Cages/PCGExValencyAssetContainerBase.h"
#include "Volumes/ValencyContextVolume.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"

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
						NewContent = BuildCageInfoContent(OwnerCage);
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
				if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(*It))
				{
					NewContent = BuildCageInfoContent(Cage);
					break;
				}
				if (AValencyContextVolume* Volume = Cast<AValencyContextVolume>(*It))
				{
					NewContent = BuildVolumeInfoContent(Volume);
					break;
				}
				if (APCGExValencyAssetPalette* Palette = Cast<APCGExValencyAssetPalette>(*It))
				{
					NewContent = BuildPaletteInfoContent(Palette);
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

TSharedRef<SWidget> SValencyModuleInfo::BuildCageInfoContent(APCGExValencyCageBase* Cage)
{
	if (!Cage)
	{
		return BuildHintContent();
	}

	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Row 1: Name + Color inline
	{
		TSharedRef<SHorizontalBox> NameRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Cage->GetCageDisplayName()))
				.Font(Style::Bold())
			];

		if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
		{
			NameRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SColorBlock)
				.Color(RegularCage->CageColor)
				.Size(FVector2D(Style::SwatchSize, Style::SwatchSize))
			];
		}

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			NameRow
		];
	}

	// Row 2: Probe Radius (editable spinbox)
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeLabeledSpinBox(
			NSLOCTEXT("PCGExValency", "InfoProbeRadius", "Probe Radius"),
			Cage->ProbeRadius, -1.0f, 1.0f,
			NSLOCTEXT("PCGExValency", "ProbeRadiusTip", "Probe radius for detecting nearby cages (-1 = use volume default, 0 = receive-only)"),
			[WeakCage, WeakMode](float NewValue)
			{
				if (APCGExValencyCageBase* C = WeakCage.Get())
				{
					const float Clamped = FMath::Max(-1.0f, NewValue);
					if (FMath::IsNearlyEqual(C->ProbeRadius, Clamped)) return;
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeProbeRadius", "Change Probe Radius"));
					C->Modify();
					C->ProbeRadius = Clamped;
					C->RequestRebuild(EValencyRebuildReason::ConnectionChange);
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->RedrawViewports();
					}
				}
			})
	];

	// Row 3: Orbitals + Assets compact status line
	{
		const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
		int32 ConnectedCount = 0;
		for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
		{
			if (Orbital.GetDisplayConnection() != nullptr)
			{
				ConnectedCount++;
			}
		}

		const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage);
		const int32 AssetCount = RegularCage ? RegularCage->GetAllAssetEntries().Num() : 0;

		FText StatusText;
		if (RegularCage)
		{
			StatusText = FText::Format(
				NSLOCTEXT("PCGExValency", "InfoStatusLine", "{0}/{1} orbitals \u00B7 {2} assets"),
				FText::AsNumber(ConnectedCount), FText::AsNumber(Orbitals.Num()),
				FText::AsNumber(AssetCount));
		}
		else
		{
			StatusText = FText::Format(
				NSLOCTEXT("PCGExValency", "InfoStatusLineNoAssets", "{0}/{1} orbitals"),
				FText::AsNumber(ConnectedCount), FText::AsNumber(Orbitals.Num()));
		}

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			SNew(STextBlock)
			.Text(StatusText)
			.Font(Style::Small())
			.ColorAndOpacity(Style::DimColor())
		];
	}

	// Row 4: Enabled + Policy + Template inline
	{
		TSharedRef<SHorizontalBox> ControlRow = SNew(SHorizontalBox);

		// Enabled toggle
		ControlRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			PCGExValencyWidgets::MakeToggleButton(
				NSLOCTEXT("PCGExValency", "InfoEnabled", "Enabled"),
				PCGExValencyWidgets::GetPropertyTooltip(APCGExValencyCageBase::StaticClass(), GET_MEMBER_NAME_CHECKED(APCGExValencyCageBase, bEnabledForCompilation)),
				[WeakCage]() { return WeakCage.IsValid() && WeakCage->bEnabledForCompilation; },
				[WeakCage, WeakMode]()
				{
					if (APCGExValencyCageBase* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleCageEnabled", "Toggle Cage Enabled"));
						C->Modify();
						C->bEnabledForCompilation = !C->bEnabledForCompilation;
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RedrawViewports();
						}
					}
				})
		];

		// Policy radio group + Template toggle (regular cages only)
		if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
		{
			TWeakObjectPtr<APCGExValencyCage> WeakRegularCage(RegularCage);

			ControlRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 4, 0)
			[
				PCGExEnumCustomization::CreateRadioGroup(
					StaticEnum<EPCGExModulePlacementPolicy>(),
					[WeakRegularCage]() -> int32
					{
						return WeakRegularCage.IsValid() ? static_cast<int32>(WeakRegularCage->PlacementPolicy) : 0;
					},
					[WeakRegularCage, WeakMode](int32 NewValue)
					{
						if (APCGExValencyCage* C = WeakRegularCage.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePlacementPolicy", "Change Placement Policy"));
							C->Modify();
							C->PlacementPolicy = static_cast<EPCGExModulePlacementPolicy>(NewValue);
							C->RequestRebuild(EValencyRebuildReason::AssetChange);
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->RedrawViewports();
							}
						}
					})
			];

			// Template toggle
			ControlRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				PCGExValencyWidgets::MakeToggleButton(
					NSLOCTEXT("PCGExValency", "InfoTemplate", "Template"),
					NSLOCTEXT("PCGExValency", "InfoTemplateTip", "Template cages are empty boilerplate \u2014 no module is created, 'no assets' warnings are suppressed."),
					[WeakRegularCage]() { return WeakRegularCage.IsValid() && WeakRegularCage->bIsTemplate; },
					[WeakRegularCage, WeakMode]()
					{
						if (APCGExValencyCage* C = WeakRegularCage.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleTemplate", "Toggle Cage Template"));
							C->Modify();
							C->bIsTemplate = !C->bIsTemplate;
							C->RequestRebuild(EValencyRebuildReason::AssetChange);
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->RedrawViewports();
							}
						}
					})
			];
		}

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			ControlRow
		];
	}

	// Module settings section
	if (APCGExValencyAssetContainerBase* Container = Cast<APCGExValencyAssetContainerBase>(Cage))
	{
		TWeakObjectPtr<APCGExValencyAssetContainerBase> WeakContainer(Container);

		// Row 5: Module Settings header
		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "ModuleSettingsHeader", "Module Settings"))
		];

		// Row 6: Module Name (regular cages only)
		if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
		{
			TWeakObjectPtr<APCGExValencyCage> WeakRegularCage(RegularCage);

			Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
			[
				PCGExValencyWidgets::MakeLabeledTextField(
					NSLOCTEXT("PCGExValency", "InfoModuleName", "Module"),
					RegularCage->ModuleName.IsNone() ? FText::GetEmpty() : FText::FromName(RegularCage->ModuleName),
					NSLOCTEXT("PCGExValency", "ModuleNameHint", "(none)"),
					NSLOCTEXT("PCGExValency", "InfoModuleNameTip", "Module name for fixed picks. Empty = no fixed pick."),
					[WeakRegularCage, WeakMode](const FText& NewText)
					{
						if (APCGExValencyCage* C = WeakRegularCage.Get())
						{
							const FName NewName = NewText.IsEmpty() ? NAME_None : FName(*NewText.ToString());
							if (C->ModuleName == NewName) return;
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeModuleName", "Change Module Name"));
							C->Modify();
							C->ModuleName = NewName;
							C->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					})
			];
		}

		// Row 7: Weight spinbox
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeLabeledSpinBox(
				NSLOCTEXT("PCGExValency", "InfoWeight", "Weight"),
				Container->ModuleSettings.Weight, 0.001f, 0.1f,
				PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, Weight)),
				[WeakContainer, WeakCage](float NewValue)
				{
					if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeWeight", "Change Module Weight"));
						C->Modify();
						C->ModuleSettings.Weight = FMath::Max(0.001f, NewValue);
						if (APCGExValencyCageBase* CageBase = Cast<APCGExValencyCageBase>(C))
						{
							CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					}
				})
		];

		// Row 8: Min / Max Spawns dual row
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(Style::LabelWidth)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("PCGExValency", "InfoMinSpawns", "Min Spawns"))
					.Font(Style::Label())
					.ColorAndOpacity(Style::LabelColor())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 8, 0)
			[
				SNew(SSpinBox<int32>)
				.Value(Container->ModuleSettings.MinSpawns)
				.MinValue(0)
				.Font(Style::Label())
				.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, MinSpawns)))
				.OnValueCommitted_Lambda([WeakContainer, WeakCage](int32 NewValue, ETextCommit::Type)
				{
					if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeMinSpawns", "Change Min Spawns"));
						C->Modify();
						C->ModuleSettings.MinSpawns = FMath::Max(0, NewValue);
						if (APCGExValencyCageBase* CageBase = Cast<APCGExValencyCageBase>(C))
						{
							CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "InfoMaxSpawns", "Max"))
				.Font(Style::Label())
				.ColorAndOpacity(Style::LabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.Value(Container->ModuleSettings.MaxSpawns)
				.MinValue(-1)
				.Font(Style::Label())
				.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, MaxSpawns)))
				.OnValueCommitted_Lambda([WeakContainer, WeakCage](int32 NewValue, ETextCommit::Type)
				{
					if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeMaxSpawns", "Change Max Spawns"));
						C->Modify();
						C->ModuleSettings.MaxSpawns = FMath::Max(-1, NewValue);
						if (APCGExValencyCageBase* CageBase = Cast<APCGExValencyCageBase>(C))
						{
							CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					}
				})
			]
		];

		// Row 9: Dead End + Preferred Start toggle buttons
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				PCGExValencyWidgets::MakeToggleButton(
					NSLOCTEXT("PCGExValency", "InfoDeadEnd", "Dead End"),
					PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, bIsDeadEnd)),
					[WeakContainer]() { return WeakContainer.IsValid() && WeakContainer->ModuleSettings.bIsDeadEnd; },
					[WeakContainer, WeakCage, WeakMode]()
					{
						if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleDeadEnd", "Toggle Dead End"));
							C->Modify();
							C->ModuleSettings.bIsDeadEnd = !C->ModuleSettings.bIsDeadEnd;
							if (APCGExValencyCageBase* CageBase = Cast<APCGExValencyCageBase>(C))
							{
								CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
							}
						}
					})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				PCGExValencyWidgets::MakeToggleButton(
					NSLOCTEXT("PCGExValency", "InfoPrefStart", "Preferred Start"),
					PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, bPreferredStartingPoint)),
					[WeakContainer]() { return WeakContainer.IsValid() && WeakContainer->ModuleSettings.bPreferredStartingPoint; },
					[WeakContainer, WeakCage, WeakMode]()
					{
						if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "TogglePrefStart", "Toggle Preferred Start"));
							C->Modify();
							C->ModuleSettings.bPreferredStartingPoint = !C->ModuleSettings.bPreferredStartingPoint;
							if (APCGExValencyCageBase* CageBase = Cast<APCGExValencyCageBase>(C))
							{
								CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
							}
						}
					})
			]
		];
	}

	return Content;
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

TSharedRef<SWidget> SValencyModuleInfo::BuildPaletteInfoContent(APCGExValencyAssetPalette* Palette)
{
	if (!Palette)
	{
		return BuildHintContent();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeSectionHeader(FText::FromString(Palette->GetPaletteDisplayName()))
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledColorRow(
			NSLOCTEXT("PCGExValency", "PaletteColor", "Color"),
			Palette->PaletteColor)
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "PaletteAssets", "Assets"),
			FText::AsNumber(Palette->GetAllAssetEntries().Num()))
	];

	// Mirroring cages
	TArray<APCGExValencyCage*> MirroringCages;
	Palette->FindMirroringCages(MirroringCages);

	if (MirroringCages.Num() > 0)
	{
		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeSectionHeader(FText::Format(
				NSLOCTEXT("PCGExValency", "PaletteMirroring", "Mirrored by ({0})"),
				FText::AsNumber(MirroringCages.Num())))
		];

		for (APCGExValencyCage* MirrorCage : MirroringCages)
		{
			if (!MirrorCage) continue;

			Content->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("  %s"), *MirrorCage->GetCageDisplayName())))
				.Font(Style::Label())
			];
		}
	}

	return Content;
}

#pragma endregion
