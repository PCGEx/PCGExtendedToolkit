// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyInfoPanel.h"

#include "ScopedTransaction.h"

#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Cages/PCGExValencyAssetContainerBase.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyPattern.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"

namespace Style = PCGExValencyWidgets::Style;

#pragma region SValencyInfoPanel

void SValencyInfoPanel::AddProbeRadiusRow(TSharedRef<SVerticalBox> Content, APCGExValencyCageBase* Cage)
{
	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

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
}

void SValencyInfoPanel::AddOrbitalStatusLine(TSharedRef<SVerticalBox> Content, APCGExValencyCageBase* Cage, bool bShowAssets)
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

	FText StatusText;
	if (bShowAssets)
	{
		const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage);
		const int32 AssetCount = RegularCage ? RegularCage->GetAllAssetEntries().Num() : 0;
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

void SValencyInfoPanel::AddEnabledToggle(TSharedRef<SHorizontalBox> Row, APCGExValencyCageBase* Cage)
{
	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	Row->AddSlot()
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
}

void SValencyInfoPanel::AddModuleSettingsSection(TSharedRef<SVerticalBox> Content, APCGExValencyAssetContainerBase* Container, APCGExValencyCageBase* CageForRebuild)
{
	TWeakObjectPtr<APCGExValencyAssetContainerBase> WeakContainer(Container);
	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(CageForRebuild);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	// Section header
	Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "ModuleSettingsHeader", "Module Settings"))
	];

	// Module Name (regular cages only)
	if (APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Container))
	{
		TWeakObjectPtr<APCGExValencyCage> WeakRegularCage(RegularCage);

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeLabeledTextField(
				NSLOCTEXT("PCGExValency", "InfoModuleName", "Module"),
				RegularCage->ModuleName.IsNone() ? FText::GetEmpty() : FText::FromName(RegularCage->ModuleName),
				NSLOCTEXT("PCGExValency", "ModuleNameHint", "(none)"),
				NSLOCTEXT("PCGExValency", "InfoModuleNameTip", "Module name for fixed picks. Empty = no fixed pick."),
				[WeakRegularCage](const FText& NewText)
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

	// Weight + Min Spawns + Max Spawns + Dead End
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		SNew(SHorizontalBox)
		// Weight
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 2, 0)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "InfoWeightLabel", "Weight"))
			.Font(Style::Label())
			.ColorAndOpacity(Style::LabelColor())
			.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, Weight)))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0, 0, 6, 0)
		[
			SNew(SSpinBox<float>)
			.Value(Container->ModuleSettings.Weight)
			.MinValue(0.001f)
			.Delta(0.1f)
			.Font(Style::Label())
			.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, Weight)))
			.OnValueCommitted_Lambda([WeakContainer, WeakCage](float NewValue, ETextCommit::Type)
			{
				if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeWeight", "Change Module Weight"));
					C->Modify();
					C->ModuleSettings.Weight = FMath::Max(0.001f, NewValue);
					if (APCGExValencyCageBase* CageBase = WeakCage.Get())
					{
						CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				}
			})
		]
		// Min Spawns
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 2, 0)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "InfoMinLabel", "Min"))
			.Font(Style::Label())
			.ColorAndOpacity(Style::LabelColor())
			.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, MinSpawns)))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0, 0, 6, 0)
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
					if (APCGExValencyCageBase* CageBase = WeakCage.Get())
					{
						CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				}
			})
		]
		// Max Spawns
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 2, 0)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "InfoMaxLabel", "Max"))
			.Font(Style::Label())
			.ColorAndOpacity(Style::LabelColor())
			.ToolTipText(PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyModuleSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyModuleSettings, MaxSpawns)))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0, 0, 6, 0)
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
					if (APCGExValencyCageBase* CageBase = WeakCage.Get())
					{
						CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				}
			})
		]
		// Dead End toggle
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
						if (APCGExValencyCageBase* CageBase = WeakCage.Get())
						{
							CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					}
				})
		]
	];

	// Behavior flags
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExEnumCustomization::CreateCheckboxGroup(
			StaticEnum<EPCGExModuleBehavior>(),
			[WeakContainer]() -> uint8
			{
				return WeakContainer.IsValid() ? WeakContainer->ModuleSettings.BehaviorFlags : 0;
			},
			[WeakContainer, WeakCage, WeakMode](uint8 NewValue)
			{
				if (APCGExValencyAssetContainerBase* C = WeakContainer.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeBehaviorFlags", "Change Module Behavior"));
					C->Modify();
					C->ModuleSettings.BehaviorFlags = NewValue;
					if (APCGExValencyCageBase* CageBase = WeakCage.Get())
					{
						CageBase->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				}
			})
	];
}

#pragma endregion

#pragma region SValencyRegularCagePanel

void SValencyRegularCagePanel::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;
	APCGExValencyCage* Cage = InArgs._Cage;
	if (!Cage) return;

	TWeakObjectPtr<APCGExValencyCage> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Type header: blue with color swatch
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeTypeHeader(
			NSLOCTEXT("PCGExValency", "CageTypeLabel", "CAGE"),
			Cage->GetCageDisplayName(),
			Style::CageHeaderColor(),
			&Cage->CageColor)
	];

	// Probe Radius
	AddProbeRadiusRow(Content, Cage);

	// Orbital status with assets
	AddOrbitalStatusLine(Content, Cage, true);

	// Enabled + Policy + Template inline
	{
		TSharedRef<SHorizontalBox> ControlRow = SNew(SHorizontalBox);

		AddEnabledToggle(ControlRow, Cage);

		// Policy radio group
		ControlRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 4, 0)
		[
			PCGExEnumCustomization::CreateRadioGroup(
				StaticEnum<EPCGExModulePlacementPolicy>(),
				[WeakCage]() -> int32
				{
					return WeakCage.IsValid() ? static_cast<int32>(WeakCage->PlacementPolicy) : 0;
				},
				[WeakCage, WeakMode](int32 NewValue)
				{
					if (APCGExValencyCage* C = WeakCage.Get())
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
				[WeakCage]() { return WeakCage.IsValid() && WeakCage->bIsTemplate; },
				[WeakCage, WeakMode]()
				{
					if (APCGExValencyCage* C = WeakCage.Get())
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

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			ControlRow
		];
	}

	// Module settings
	AddModuleSettingsSection(Content, Cage, Cage);

	ChildSlot[Content];
}

#pragma endregion

#pragma region SValencyNullCagePanel

void SValencyNullCagePanel::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;
	APCGExValencyCageNull* Cage = InArgs._Cage;
	if (!Cage) return;

	TWeakObjectPtr<APCGExValencyCageNull> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Type header: red, no swatch
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeTypeHeader(
			NSLOCTEXT("PCGExValency", "PlaceholderTypeLabel", "PLACEHOLDER"),
			Cage->GetCageDisplayName(),
			Style::PlaceholderHeaderColor())
	];

	// Probe Radius
	AddProbeRadiusRow(Content, Cage);

	// Orbital status (no assets)
	AddOrbitalStatusLine(Content, Cage, false);

	// Enabled toggle only
	{
		TSharedRef<SHorizontalBox> ControlRow = SNew(SHorizontalBox);
		AddEnabledToggle(ControlRow, Cage);

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			ControlRow
		];
	}

	// Placeholder Mode radio group
	Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "PlaceholderModeHeader", "Placeholder Mode"))
	];

	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExEnumCustomization::CreateRadioGroup(
			StaticEnum<EPCGExPlaceholderMode>(),
			[WeakCage]() -> int32
			{
				return WeakCage.IsValid() ? static_cast<int32>(WeakCage->PlaceholderMode) : 0;
			},
			[WeakCage, WeakMode](int32 NewValue)
			{
				if (APCGExValencyCageNull* C = WeakCage.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePlaceholderMode", "Change Placeholder Mode"));
					C->Modify();
					C->PlaceholderMode = static_cast<EPCGExPlaceholderMode>(NewValue);
					C->RequestRebuild(EValencyRebuildReason::PropertyChange);
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->RedrawViewports();
					}
				}
			})
	];

	// Description (read-only, dim, if non-empty)
	if (!Cage->Description.IsEmpty())
	{
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Cage->Description))
			.Font(Style::Italic())
			.ColorAndOpacity(Style::DimColor())
			.AutoWrapText(true)
		];
	}

	ChildSlot[Content];
}

#pragma endregion

#pragma region SValencyPatternCagePanel

void SValencyPatternCagePanel::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;
	APCGExValencyCagePattern* Cage = InArgs._Cage;
	if (!Cage) return;

	TWeakObjectPtr<APCGExValencyCagePattern> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Type header: green
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeTypeHeader(
			NSLOCTEXT("PCGExValency", "PatternTypeLabel", "PATTERN"),
			Cage->GetCageDisplayName(),
			Style::PatternHeaderColor())
	];

	// Probe Radius
	AddProbeRadiusRow(Content, Cage);

	// Orbital status (no assets)
	AddOrbitalStatusLine(Content, Cage, false);

	// Enabled + Active + Root toggles
	{
		TSharedRef<SHorizontalBox> ControlRow = SNew(SHorizontalBox);

		AddEnabledToggle(ControlRow, Cage);

		// Active toggle
		ControlRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 4, 0)
		[
			PCGExValencyWidgets::MakeToggleButton(
				NSLOCTEXT("PCGExValency", "InfoActive", "Active"),
				PCGExValencyWidgets::GetPropertyTooltip(APCGExValencyCagePattern::StaticClass(), GET_MEMBER_NAME_CHECKED(APCGExValencyCagePattern, bIsActiveInPattern)),
				[WeakCage]() { return WeakCage.IsValid() && WeakCage->bIsActiveInPattern; },
				[WeakCage, WeakMode]()
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleActive", "Toggle Pattern Active"));
						C->Modify();
						C->bIsActiveInPattern = !C->bIsActiveInPattern;
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RedrawViewports();
						}
					}
				})
		];

		// Root toggle
		ControlRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 0, 0)
		[
			PCGExValencyWidgets::MakeToggleButton(
				NSLOCTEXT("PCGExValency", "InfoRoot", "Root"),
				PCGExValencyWidgets::GetPropertyTooltip(APCGExValencyCagePattern::StaticClass(), GET_MEMBER_NAME_CHECKED(APCGExValencyCagePattern, bIsPatternRoot)),
				[WeakCage]() { return WeakCage.IsValid() && WeakCage->bIsPatternRoot; },
				[WeakCage, WeakMode]()
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleRoot", "Toggle Pattern Root"));
						C->Modify();
						C->bIsPatternRoot = !C->bIsPatternRoot;
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->OnSceneChanged.Broadcast();
							Mode->RedrawViewports();
						}
					}
				})
		];

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			ControlRow
		];
	}

	// Proxied Cages section
	{
		const TArray<TObjectPtr<APCGExValencyCage>>& Proxied = Cage->ProxiedCages;

		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeSectionHeader(FText::Format(
				NSLOCTEXT("PCGExValency", "ProxiedCagesHeader", "Proxied Cages ({0})"),
				FText::AsNumber(Proxied.Num())))
		];

		if (Proxied.Num() == 0)
		{
			Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "ProxiedWildcard", "(wildcard)"))
				.Font(Style::Italic())
				.ColorAndOpacity(Style::DimColor())
			];
		}
		else
		{
			for (const TObjectPtr<APCGExValencyCage>& ProxiedCage : Proxied)
			{
				if (!ProxiedCage) continue;
				Content->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("  %s"), *ProxiedCage->GetCageDisplayName())))
					.Font(Style::Label())
				];
			}
		}
	}

	// Pattern Settings section (root only)
	if (Cage->bIsPatternRoot)
	{
		FPCGExValencyPatternSettings& Settings = Cage->PatternSettings;

		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "PatternSettingsHeader", "Pattern Settings"))
		];

		// Pattern Name
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeLabeledTextField(
				NSLOCTEXT("PCGExValency", "PatternName", "Name"),
				Settings.PatternName.IsNone() ? FText::GetEmpty() : FText::FromName(Settings.PatternName),
				NSLOCTEXT("PCGExValency", "PatternNameHint", "(unnamed)"),
				NSLOCTEXT("PCGExValency", "PatternNameTip", "Unique name for this pattern"),
				[WeakCage](const FText& NewText)
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						const FName NewName = NewText.IsEmpty() ? NAME_None : FName(*NewText.ToString());
						if (C->PatternSettings.PatternName == NewName) return;
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePatternName", "Change Pattern Name"));
						C->Modify();
						C->PatternSettings.PatternName = NewName;
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				})
		];

		// W + Min + Max compact row
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			SNew(SHorizontalBox)
			// Weight
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "PatternWeightLabel", "Weight"))
				.Font(Style::Label())
				.ColorAndOpacity(Style::LabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 6, 0)
			[
				SNew(SSpinBox<float>)
				.Value(Settings.Weight)
				.MinValue(0.001f)
				.Delta(0.1f)
				.Font(Style::Label())
				.OnValueCommitted_Lambda([WeakCage](float NewValue, ETextCommit::Type)
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePatternWeight", "Change Pattern Weight"));
						C->Modify();
						C->PatternSettings.Weight = FMath::Max(0.001f, NewValue);
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				})
			]
			// Min
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "PatternMinLabel", "Min"))
				.Font(Style::Label())
				.ColorAndOpacity(Style::LabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 6, 0)
			[
				SNew(SSpinBox<int32>)
				.Value(Settings.MinMatches)
				.MinValue(0)
				.Font(Style::Label())
				.OnValueCommitted_Lambda([WeakCage](int32 NewValue, ETextCommit::Type)
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePatternMin", "Change Pattern Min Matches"));
						C->Modify();
						C->PatternSettings.MinMatches = FMath::Max(0, NewValue);
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				})
			]
			// Max
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "PatternMaxLabel", "Max"))
				.Font(Style::Label())
				.ColorAndOpacity(Style::LabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 0, 0)
			[
				SNew(SSpinBox<int32>)
				.Value(Settings.MaxMatches)
				.MinValue(-1)
				.Font(Style::Label())
				.OnValueCommitted_Lambda([WeakCage](int32 NewValue, ETextCommit::Type)
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangePatternMax", "Change Pattern Max Matches"));
						C->Modify();
						C->PatternSettings.MaxMatches = FMath::Max(-1, NewValue);
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				})
			]
		];

		// Exclusive toggle
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeToggleButton(
				NSLOCTEXT("PCGExValency", "PatternExclusive", "Exclusive"),
				PCGExValencyWidgets::GetPropertyTooltip(FPCGExValencyPatternSettings::StaticStruct(), GET_MEMBER_NAME_CHECKED(FPCGExValencyPatternSettings, bExclusive)),
				[WeakCage]() { return WeakCage.IsValid() && WeakCage->PatternSettings.bExclusive; },
				[WeakCage, WeakMode]()
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleExclusive", "Toggle Pattern Exclusive"));
						C->Modify();
						C->PatternSettings.bExclusive = !C->PatternSettings.bExclusive;
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RedrawViewports();
						}
					}
				})
		];

		// Output Strategy radio
		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "OutputStrategyHeader", "Output Strategy"))
		];

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExEnumCustomization::CreateRadioGroup(
				StaticEnum<EPCGExPatternOutputStrategy>(),
				[WeakCage]() -> int32
				{
					return WeakCage.IsValid() ? static_cast<int32>(WeakCage->PatternSettings.OutputStrategy) : 0;
				},
				[WeakCage, WeakMode](int32 NewValue)
				{
					if (APCGExValencyCagePattern* C = WeakCage.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeOutputStrategy", "Change Pattern Output Strategy"));
						C->Modify();
						C->PatternSettings.OutputStrategy = static_cast<EPCGExPatternOutputStrategy>(NewValue);
						C->RequestRebuild(EValencyRebuildReason::AssetChange);
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->OnSceneChanged.Broadcast();
							Mode->RedrawViewports();
						}
					}
				})
		];

		// Strategy-specific controls
		if (Settings.OutputStrategy == EPCGExPatternOutputStrategy::Collapse)
		{
			// Transform Mode radio
			Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
			[
				PCGExEnumCustomization::CreateRadioGroup(
					StaticEnum<EPCGExPatternTransformMode>(),
					[WeakCage]() -> int32
					{
						return WeakCage.IsValid() ? static_cast<int32>(WeakCage->PatternSettings.TransformMode) : 0;
					},
					[WeakCage, WeakMode](int32 NewValue)
					{
						if (APCGExValencyCagePattern* C = WeakCage.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeTransformMode", "Change Pattern Transform Mode"));
							C->Modify();
							C->PatternSettings.TransformMode = static_cast<EPCGExPatternTransformMode>(NewValue);
							C->RequestRebuild(EValencyRebuildReason::AssetChange);
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->RedrawViewports();
							}
						}
					})
			];

			// Replacement Asset (read-only)
			const FString AssetName = Settings.ReplacementAsset.IsNull()
				? TEXT("(none)")
				: Settings.ReplacementAsset.GetAssetName();

			Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
			[
				PCGExValencyWidgets::MakeLabeledRow(
					NSLOCTEXT("PCGExValency", "ReplacementAsset", "Replacement"),
					FText::FromString(AssetName))
			];
		}
		else if (Settings.OutputStrategy == EPCGExPatternOutputStrategy::Swap)
		{
			// SwapToModuleName editable text
			Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
			[
				PCGExValencyWidgets::MakeLabeledTextField(
					NSLOCTEXT("PCGExValency", "SwapToModule", "Swap To"),
					Settings.SwapToModuleName.IsNone() ? FText::GetEmpty() : FText::FromName(Settings.SwapToModuleName),
					NSLOCTEXT("PCGExValency", "SwapToHint", "(module name)"),
					NSLOCTEXT("PCGExValency", "SwapToTip", "Module name to swap matched points to"),
					[WeakCage](const FText& NewText)
					{
						if (APCGExValencyCagePattern* C = WeakCage.Get())
						{
							const FName NewName = NewText.IsEmpty() ? NAME_None : FName(*NewText.ToString());
							if (C->PatternSettings.SwapToModuleName == NewName) return;
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeSwapTo", "Change Swap To Module"));
							C->Modify();
							C->PatternSettings.SwapToModuleName = NewName;
							C->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
					})
			];
		}
	}

	ChildSlot[Content];
}

#pragma endregion

#pragma region SValencyPalettePanel

void SValencyPalettePanel::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;
	APCGExValencyAssetPalette* Palette = InArgs._Palette;
	if (!Palette) return;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Type header: amber with color swatch
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeTypeHeader(
			NSLOCTEXT("PCGExValency", "PaletteTypeLabel", "PALETTE"),
			Palette->GetPaletteDisplayName(),
			Style::PaletteHeaderColor(),
			&Palette->PaletteColor)
	];

	// Asset count status line
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		SNew(STextBlock)
		.Text(FText::Format(
			NSLOCTEXT("PCGExValency", "PaletteAssetCount", "{0} assets"),
			FText::AsNumber(Palette->GetAllAssetEntries().Num())))
		.Font(Style::Small())
		.ColorAndOpacity(Style::DimColor())
	];

	// Module settings (no cage for rebuild — palette handles its own rebuild cascade)
	// Palette is an APCGExValencyAssetContainerBase but not an APCGExValencyCageBase,
	// so CageForRebuild is nullptr — the callbacks will just skip the RequestRebuild call.
	AddModuleSettingsSection(Content, Palette, nullptr);

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

	ChildSlot[Content];
}

#pragma endregion
