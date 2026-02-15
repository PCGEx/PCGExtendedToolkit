// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyInspector.h"

#include "Editor.h"
#include "Selection.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Colors/SColorBlock.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "EditorMode/PCGExValencyDrawHelper.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyCageOrbital.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Volumes/ValencyContextVolume.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Cages/PCGExValencyAssetContainerBase.h"

namespace PCGExValencyInspector
{
	/** Get icon text for a connector type, resolving auto-assign via ConnectorSet. */
	static FText GetIconText(const UPCGExValencyConnectorSet* Set, int32 TypeArrayIndex)
	{
		const int32 Effective = Set ? Set->GetEffectiveIconIndex(TypeArrayIndex) : TypeArrayIndex;
		const TCHAR C = PCGExValencyConnector::GetIconChar(Effective);
		return FText::FromString(FString(1, &C));
	}
}

void SValencyInspector::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "InspectorHeader", "Inspector"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(ContentArea, SBox)
		]
	];

	// Bind to selection changes (use AddSP for automatic cleanup when widget is destroyed)
	if (GEditor)
	{
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddSP(
			this, &SValencyInspector::OnSelectionChangedCallback);

		OnComponentSelectionChangedHandle = GEditor->GetSelectedComponents()->SelectionChangedEvent.AddSP(
			this, &SValencyInspector::OnSelectionChangedCallback);
	}

	// Bind to scene changes so stats/related sections stay up-to-date
	if (EditorMode)
	{
		OnSceneChangedHandle = EditorMode->OnSceneChanged.AddSP(
			this, &SValencyInspector::OnSceneChangedCallback);
	}

	RefreshContent();
}

void SValencyInspector::RefreshContent()
{
	if (!ContentArea.IsValid() || bIsUpdatingSelection)
	{
		return;
	}

	// If we're intentionally on the detail panel, stay there as long as
	// the connector is valid and its owning cage is still the selected actor.
	// This prevents property-change actions (polarity, type, etc.) from
	// bouncing back to the cage view via stray refresh calls.
	if (DetailPanelConnector.IsValid())
	{
		if (UPCGExValencyCageConnectorComponent* Conn = DetailPanelConnector.Get())
		{
			if (GEditor)
			{
				AActor* Owner = Conn->GetOwner();
				if (Owner && GEditor->GetSelectedActors()->IsSelected(Owner))
				{
					StaticCastSharedPtr<SBox>(ContentArea)->SetContent(BuildConnectorContent(Conn));
					return;
				}
			}
		}
		// Connector invalid or cage no longer selected — clear and fall through
		DetailPanelConnector.Reset();
	}

	TSharedRef<SWidget> NewContent = BuildSceneStatsContent();
	bool bFoundSpecificContent = false;

	if (GEditor)
	{
		// Check for selected components first (connector → redirect to cage view)
		if (USelection* CompSelection = GEditor->GetSelectedComponents())
		{
			for (FSelectionIterator It(*CompSelection); It; ++It)
			{
				if (UPCGExValencyCageConnectorComponent* Connector = Cast<UPCGExValencyCageConnectorComponent>(*It))
				{
					if (APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
					{
						NewContent = BuildCageContent(OwnerCage);
					}
					bFoundSpecificContent = true;
					break;
				}
			}
		}

		// If no component selected, check actors
		if (!bFoundSpecificContent)
		{
			USelection* Selection = GEditor->GetSelectedActors();
			for (FSelectionIterator It(*Selection); It; ++It)
			{
				if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(*It))
				{
					NewContent = BuildCageContent(Cage);
					break;
				}
				if (AValencyContextVolume* Volume = Cast<AValencyContextVolume>(*It))
				{
					NewContent = BuildVolumeContent(Volume);
					break;
				}
				if (APCGExValencyAssetPalette* Palette = Cast<APCGExValencyAssetPalette>(*It))
				{
					NewContent = BuildPaletteContent(Palette);
					break;
				}
			}
		}
	}

	StaticCastSharedPtr<SBox>(ContentArea)->SetContent(NewContent);
}

void SValencyInspector::OnSelectionChangedCallback(UObject* InObject)
{
	RefreshContent();
}

void SValencyInspector::OnSceneChangedCallback()
{
	RefreshContent();
}

TSharedRef<SWidget> SValencyInspector::BuildSceneStatsContent()
{
	if (!EditorMode)
	{
		return SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "NoSelection", "No selection"))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)));
	}

	const int32 CageCount = EditorMode->GetCachedCages().Num();
	const int32 VolumeCount = EditorMode->GetCachedVolumes().Num();
	const int32 PaletteCount = EditorMode->GetCachedPalettes().Num();

	// Count total assets
	int32 TotalAssets = 0;
	for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : EditorMode->GetCachedCages())
	{
		if (const APCGExValencyCage* Cage = Cast<APCGExValencyCage>(CagePtr.Get()))
		{
			TotalAssets += Cage->GetAllAssetEntries().Num();
		}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "StatsScene", "Scene"),
				FText::Format(NSLOCTEXT("PCGExValency", "StatsSceneValue", "{0} cages, {1} volumes, {2} palettes"),
					FText::AsNumber(CageCount), FText::AsNumber(VolumeCount), FText::AsNumber(PaletteCount)))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "StatsTotalAssets", "Total Assets"),
				FText::AsNumber(TotalAssets))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			MakeRebuildAllButton()
		];
}

TSharedRef<SWidget> SValencyInspector::BuildCageContent(APCGExValencyCageBase* Cage)
{
	if (!Cage)
	{
		return BuildSceneStatsContent();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Header row with name and Rebuild All button
	Content->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			MakeSectionHeader(FText::FromString(Cage->GetCageDisplayName()))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeRebuildAllButton()
		]
	];

	if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
	{
		Content->AddSlot().AutoHeight()
		[
			MakeLabeledColorRow(NSLOCTEXT("PCGExValency", "CageColor", "Color"), RegularCage->CageColor)
		];
	}

	// Orbital status
	const TArray<FPCGExValencyCageOrbital>& Orbitals = Cage->GetOrbitals();
	int32 ConnectedCount = 0;
	for (const FPCGExValencyCageOrbital& Orbital : Orbitals)
	{
		if (Orbital.GetDisplayConnection() != nullptr)
		{
			ConnectedCount++;
		}
	}

	Content->AddSlot().AutoHeight()
	[
		MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "CageOrbitals", "Orbitals"),
			FText::Format(NSLOCTEXT("PCGExValency", "CageOrbitalsValue", "{0}/{1} connected"),
				FText::AsNumber(ConnectedCount), FText::AsNumber(Orbitals.Num())))
	];

	// Asset count for regular cages
	if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
	{
		Content->AddSlot().AutoHeight()
		[
			MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "CageAssets", "Assets"),
				FText::AsNumber(RegularCage->GetAllAssetEntries().Num()))
		];
	}

	// Probe radius
	Content->AddSlot().AutoHeight()
	[
		MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "CageProbeRadius", "Probe Radius"),
			FText::Format(NSLOCTEXT("PCGExValency", "CageProbeRadiusValue", "{0}"),
				FText::AsNumber(static_cast<int32>(Cage->GetEffectiveProbeRadius()))))
	];

	// Connector Set status
	if (UPCGExValencyConnectorSet* EffectiveSet = Cage->GetEffectiveConnectorSet())
	{
		Content->AddSlot().AutoHeight()
		[
			MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "CageConnectorSet", "Connector Set"),
				FText::FromString(EffectiveSet->GetName()))
		];
	}

	// Connector components - interactive section
	TArray<UPCGExValencyCageConnectorComponent*> ConnectorComponents;
	Cage->GetConnectorComponents(ConnectorComponents);

	// Detect currently active connector for highlight
	UPCGExValencyCageConnectorComponent* ActiveConnector = nullptr;
	if (UPCGExValencyCageConnectorComponent* SelectedConn = UPCGExValencyCageEditorMode::GetSelectedConnector())
	{
		if (SelectedConn->GetOwner() == Cage)
		{
			ActiveConnector = SelectedConn;
		}
	}

	{
		// Header row with connector count and Add button
		Content->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				MakeSectionHeader(FText::Format(
					NSLOCTEXT("PCGExValency", "CageConnectors", "Connectors ({0})"),
					FText::AsNumber(ConnectorComponents.Num())))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeAddConnectorButton(Cage)
			]
		];

		// Search field when connector count > 6
		if (ConnectorComponents.Num() > 6)
		{
			Content->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
			[
				SNew(SSearchBox)
				.InitialText(FText::FromString(ConnectorSearchFilter))
				.OnTextChanged_Lambda([this](const FText& NewText)
				{
					ConnectorSearchFilter = NewText.ToString();
					RefreshContent();
				})
			];
		}

		for (UPCGExValencyCageConnectorComponent* ConnectorComp : ConnectorComponents)
		{
			if (!ConnectorComp) continue;

			// Apply search filter
			if (!ConnectorSearchFilter.IsEmpty())
			{
				const bool bMatchesName = ConnectorComp->Identifier.ToString().Contains(ConnectorSearchFilter);
				const bool bMatchesType = ConnectorComp->ConnectorType.ToString().Contains(ConnectorSearchFilter);
				if (!bMatchesName && !bMatchesType) continue;
			}

			const bool bIsActive = (ConnectorComp == ActiveConnector);
			Content->AddSlot().AutoHeight().Padding(0, 1)
			[
				MakeCompactConnectorRow(ConnectorComp, bIsActive)
			];
		}
	}

	// Related section (containing volumes, mirrors, mirrored-by)
	Content->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		MakeRelatedSection(Cage)
	];

	return Content;
}

TSharedRef<SWidget> SValencyInspector::BuildConnectorContent(UPCGExValencyCageConnectorComponent* Connector)
{
	if (!Connector)
	{
		return BuildSceneStatsContent();
	}

	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> WeakConnector(Connector);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Back to Cage button
	Content->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SButton)
		.Text(NSLOCTEXT("PCGExValency", "BackToCage", "<< Back to Cage"))
		.ToolTipText(NSLOCTEXT("PCGExValency", "BackToCageTip", "Return to the cage connector list"))
		.OnClicked_Lambda([WeakConnector, this]() -> FReply
		{
			DetailPanelConnector.Reset();
			if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
			{
				if (GEditor && S->GetOwner())
				{
					// Deselect component, keep actor selected -> triggers cage view
					bIsUpdatingSelection = true;
					GEditor->SelectComponent(S, false, true);
					bIsUpdatingSelection = false;
					RefreshContent();
				}
			}
			return FReply::Handled();
		})
	];

	Content->AddSlot().AutoHeight()
	[
		MakeSectionHeader(FText::Format(
			NSLOCTEXT("PCGExValency", "ConnectorHeader", "Connector: {0}"),
			FText::FromName(Connector->Identifier)))
	];

	// Owning cage
	if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
	{
		Content->AddSlot().AutoHeight()
		[
			MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "ConnectorOwner", "Cage"),
				FText::FromString(OwnerCage->GetCageDisplayName()))
		];
	}

	// Editable Name
	Content->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "ConnectorIdentifier", "Identifier"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4, 0)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromName(Connector->Identifier))
			.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorIdentifierTip", "Unique connector identifier within this cage"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.OnTextCommitted_Lambda([WeakConnector, this](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeConnectorIdentifier", "Change Connector Identifier"));
					S->Modify();
					S->Identifier = FName(*NewText.ToString());
					if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
					{
						Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
					this->RefreshContent();
				}
			})
		]
	];

	// Editable Type - dropdown when ConnectorSet available, freeform text fallback
	{
		// Get available types from effective ConnectorSet
		UPCGExValencyConnectorSet* EffectiveSet = nullptr;
		if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
		{
			EffectiveSet = OwnerCage->GetEffectiveConnectorSet();
		}

		TSharedRef<SWidget> TypeWidget = SNullWidget::NullWidget;

		if (EffectiveSet && EffectiveSet->ConnectorTypes.Num() > 0)
		{
			// Build options list
			TSharedPtr<TArray<TSharedPtr<FName>>> TypeOptionsPtr = MakeShared<TArray<TSharedPtr<FName>>>();
			TSharedPtr<TArray<FLinearColor>> TypeColorsPtr = MakeShared<TArray<FLinearColor>>();
			TSharedPtr<FName> CurrentSelection;

			for (const FPCGExValencyConnectorEntry& Entry : EffectiveSet->ConnectorTypes)
			{
				TSharedPtr<FName> Option = MakeShared<FName>(Entry.ConnectorType);
				TypeOptionsPtr->Add(Option);
				TypeColorsPtr->Add(Entry.DebugColor);
				if (Entry.ConnectorType == Connector->ConnectorType)
				{
					CurrentSelection = Option;
				}
			}

			TypeWidget = SNew(SHorizontalBox)
				// Color swatch for current type
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SColorBlock)
					.Color_Lambda([WeakConnector, EffectiveSet]() -> FLinearColor
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							if (EffectiveSet)
							{
								const int32 Idx = EffectiveSet->FindConnectorTypeIndex(S->ConnectorType);
								if (EffectiveSet->ConnectorTypes.IsValidIndex(Idx))
								{
									return EffectiveSet->ConnectorTypes[Idx].DebugColor;
								}
							}
						}
						return FLinearColor(0.3f, 0.3f, 0.3f);
					})
					.Size(FVector2D(12, 12))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboBox<TSharedPtr<FName>>)
					.OptionsSource(TypeOptionsPtr.Get())
					.InitiallySelectedItem(CurrentSelection)
					.OnGenerateWidget_Lambda([TypeOptionsPtr, TypeColorsPtr](TSharedPtr<FName> InItem) -> TSharedRef<SWidget>
					{
						// Find color for this item
						FLinearColor ItemColor(0.3f, 0.3f, 0.3f);
						for (int32 i = 0; i < TypeOptionsPtr->Num(); ++i)
						{
							if ((*TypeOptionsPtr)[i] == InItem)
							{
								ItemColor = (*TypeColorsPtr)[i];
								break;
							}
						}

						return SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SColorBlock)
								.Color(ItemColor)
								.Size(FVector2D(10, 10))
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromName(*InItem))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							];
					})
					.OnSelectionChanged_Lambda([WeakConnector, this](TSharedPtr<FName> NewValue, ESelectInfo::Type)
					{
						if (!NewValue.IsValid()) return;
						if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							if (S->ConnectorType == *NewValue) return;
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeConnectorType", "Change Connector Type"));
							S->Modify();
							S->ConnectorType = *NewValue;
							if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
							{
								Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
							}
							this->RefreshContent();
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([WeakConnector]() -> FText
						{
							if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
							{
								return FText::FromName(S->ConnectorType);
							}
							return FText::GetEmpty();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				];
		}
		else
		{
			// Fallback: freeform text when no ConnectorSet available
			TypeWidget = SNew(SEditableTextBox)
				.Text(FText::FromName(Connector->ConnectorType))
				.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorTypeTip", "Connector type name \u2014 determines compatibility during solving. Assign a ConnectorSet for type dropdown."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.OnTextCommitted_Lambda([WeakConnector, this](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeConnectorType", "Change Connector Type"));
						S->Modify();
						S->ConnectorType = FName(*NewText.ToString());
						if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
						{
							Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
						this->RefreshContent();
					}
				});
		}

		Content->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 1)
			[
				SNew(SBox)
				.WidthOverride(100)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("PCGExValency", "ConnectorType", "Type"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 0)
			[
				TypeWidget
			]
		];
	}

	// Polarity cycling
	auto GetPolarityLabel = [](EPCGExConnectorPolarity P) -> FText
	{
		switch (P)
		{
		case EPCGExConnectorPolarity::Universal: return NSLOCTEXT("PCGExValency", "PolarityUniversalDetail", "Universal *");
		case EPCGExConnectorPolarity::Plug:      return NSLOCTEXT("PCGExValency", "PolarityPlugDetail", "Plug >>");
		case EPCGExConnectorPolarity::Port:      return NSLOCTEXT("PCGExValency", "PolarityPortDetail", "<< Port");
		default:                                 return FText::GetEmpty();
		}
	};

	Content->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "ConnectorPolarity", "Polarity"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(GetPolarityLabel(Connector->Polarity))
			.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorPolarityTip", "Cycle polarity: Universal (connects to any), Plug (outward), Port (inward)"))
			.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
			{
				if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "CyclePolarity", "Cycle Connector Polarity"));
					S->Modify();
					// Universal -> Plug -> Port -> Universal
					switch (S->Polarity)
					{
					case EPCGExConnectorPolarity::Universal: S->Polarity = EPCGExConnectorPolarity::Plug; break;
					case EPCGExConnectorPolarity::Plug:      S->Polarity = EPCGExConnectorPolarity::Port; break;
					case EPCGExConnectorPolarity::Port:      S->Polarity = EPCGExConnectorPolarity::Universal; break;
					}
					if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
					{
						Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->RedrawViewports();
					}
					this->RefreshContent();
				}
				return FReply::Handled();
			})
		]
	];

	// Enabled checkbox
	Content->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "ConnectorEnabled", "Enabled"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SCheckBox)
			.IsChecked(Connector->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorEnabledTip", "Disabled connectors are ignored during compilation"))
			.OnCheckStateChanged_Lambda([WeakConnector, WeakMode, this](ECheckBoxState NewState)
			{
				if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
				{
					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleEnabled", "Toggle Connector Enabled"));
					S->Modify();
					S->bEnabled = (NewState == ECheckBoxState::Checked);
					if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
					{
						Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->RedrawViewports();
					}
					this->RefreshContent();
				}
			})
		]
	];

	// Constraints section
	{
		// Get effective constraints for this connector type
		UPCGExValencyConnectorSet* EffConnSet = nullptr;
		if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
		{
			EffConnSet = OwnerCage->GetEffectiveConnectorSet();
		}

		int32 ConstraintCount = 0;
		FString ConstraintSource;

		if (EffConnSet)
		{
			const int32 TypeIdx = EffConnSet->FindConnectorTypeIndex(Connector->ConnectorType);
			if (EffConnSet->ConnectorTypes.IsValidIndex(TypeIdx))
			{
				const TArray<FInstancedStruct>& Defaults = EffConnSet->ConnectorTypes[TypeIdx].DefaultConstraints;
				ConstraintCount = Defaults.Num();
				ConstraintSource = ConstraintCount > 0 ? TEXT("Default") : TEXT("None");

				if (ConstraintCount > 0)
				{
					Content->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
					[
						MakeSectionHeader(NSLOCTEXT("PCGExValency", "ConstraintsHeader", "Constraints"))
					];

					for (int32 i = 0; i < Defaults.Num(); ++i)
					{
						const FInstancedStruct& Instance = Defaults[i];
						if (!Instance.GetScriptStruct()) { continue; }

						const FPCGExConnectorConstraint* Constraint = Instance.GetPtr<FPCGExConnectorConstraint>();
						const FString TypeName = Instance.GetScriptStruct()->GetDisplayNameText().ToString();
						const bool bIsEnabled = Constraint && Constraint->bEnabled;

						FString RoleStr;
						if (Constraint)
						{
							switch (Constraint->GetRole())
							{
							case EPCGExConstraintRole::Generator: RoleStr = TEXT("Gen"); break;
							case EPCGExConstraintRole::Modifier:  RoleStr = TEXT("Mod"); break;
							case EPCGExConstraintRole::Filter:    RoleStr = TEXT("Flt"); break;
							case EPCGExConstraintRole::Preset:    RoleStr = TEXT("Pre"); break;
							case EPCGExConstraintRole::Branch:    RoleStr = TEXT("Br");  break;
							}
						}

						Content->AddSlot().AutoHeight().Padding(8, 1)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("[%s]"), *RoleStr)))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.1f)))
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TypeName))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(bIsEnabled ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f)))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(4, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("PCGExValency", "ConstraintDefaultBadge", "[Default]"))
								.Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.7f)))
							]
						];
					}
				}
			}
		}

		if (ConstraintCount == 0)
		{
			Content->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
			[
				MakeSectionHeader(NSLOCTEXT("PCGExValency", "ConstraintsHeaderEmpty", "Constraints"))
			];

			Content->AddSlot().AutoHeight().Padding(8, 1)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "NoConstraints", "No constraints defined"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			];
		}
	}

	// Mirror buttons (X/Y/Z) — mirror connector orientation, optionally position
	Content->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		MakeSectionHeader(NSLOCTEXT("PCGExValency", "MirrorHeader", "Mirror"))
	];

	{
		// AxisMask: bit 0=X, bit 1=Y, bit 2=Z
		auto MakeMirrorButton = [&](const FName& IconName, int32 AxisMask, const FText& Tooltip) -> TSharedRef<SWidget>
		{
			return SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
				.ToolTipText(Tooltip)
				.OnHovered_Lambda([WeakConnector, WeakMode, AxisMask]()
				{
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->SetMirrorGhostPreview(WeakConnector.Get(), AxisMask);
					}
				})
				.OnUnhovered_Lambda([WeakMode]()
				{
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->ClearMirrorGhostPreview();
					}
				})
				.OnClicked_Lambda([WeakConnector, WeakMode, AxisMask, this]() -> FReply
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
						const bool bCageRelative = Mods.IsControlDown();
						const bool bDuplicate = Mods.IsControlDown() && Mods.IsAltDown();

						const FTransform T = FPCGExValencyDrawHelper::ComputeMirroredTransform(
							S->GetRelativeTransform(), AxisMask, bCageRelative);

						if (bDuplicate)
						{
							// Ctrl+Alt: duplicate connector at mirrored position
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "MirrorDuplicateConnector", "Mirror Duplicate Connector"));
								if (UPCGExValencyCageConnectorComponent* NewConn = Mode->DuplicateConnector(S))
								{
									NewConn->Modify();
									NewConn->SetRelativeTransform(T);
								}
							}
						}
						else
						{
							// Normal/Ctrl: mirror in place
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "MirrorConnector", "Mirror Connector"));
							S->Modify();
							S->SetRelativeTransform(T);
						}

						if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
						{
							Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RedrawViewports();
						}
						if (GEditor) { GEditor->NoteSelectionChange(); }
					}
					return FReply::Handled();
				})
				[
					SNew(SImage).Image(FAppStyle::Get().GetBrush(IconName))
				];
		};

		Content->AddSlot().AutoHeight().Padding(8, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_X"), 1,
					NSLOCTEXT("PCGExValency", "MirrorXTip", "Mirror X. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_Y"), 2,
					NSLOCTEXT("PCGExValency", "MirrorYTip", "Mirror Y. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_Z"), 4,
					NSLOCTEXT("PCGExValency", "MirrorZTip", "Mirror Z. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_XY"), 1 | 2,
					NSLOCTEXT("PCGExValency", "MirrorXYTip", "Mirror XY. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_YZ"), 2 | 4,
					NSLOCTEXT("PCGExValency", "MirrorYZTip", "Mirror YZ. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_XZ"), 1 | 4,
					NSLOCTEXT("PCGExValency", "MirrorXZTip", "Mirror XZ. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeMirrorButton(
					FName("PCGEx.ActionIcon.RotOrder_XYZ"), 1 | 2 | 4,
					NSLOCTEXT("PCGExValency", "MirrorXYZTip", "Mirror XYZ. Ctrl: cage-relative. Ctrl+Alt: duplicate at mirror."))
			]
		];
	}

	// Action buttons
	Content->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		MakeSectionHeader(NSLOCTEXT("PCGExValency", "ActionsHeader", "Actions"))
	];

	{
		const bool bIsBPDefined = (Connector->CreationMethod != EComponentCreationMethod::Instance);

		// Reset button — for BP-defined connectors, resets editable properties to archetype defaults
		if (bIsBPDefined)
		{
			Content->AddSlot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "ResetConnector", "Reset to Blueprint Defaults"))
				.ToolTipText(NSLOCTEXT("PCGExValency", "ResetConnectorTip", "Reset editable properties to Blueprint defaults. Hold Ctrl to also reset transform."))
				.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
				{
					UPCGExValencyCageConnectorComponent* S = WeakConnector.Get();
					if (!S) { return FReply::Handled(); }

					const UPCGExValencyCageConnectorComponent* Archetype = Cast<UPCGExValencyCageConnectorComponent>(S->GetArchetype());
					if (!Archetype) { return FReply::Handled(); }

					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ResetConnectorDefaults", "Reset Connector to Blueprint Defaults"));
					S->Modify();

					// Reset editable properties
					S->bEnabled = Archetype->bEnabled;
					S->Polarity = Archetype->Polarity;
					S->DebugColorOverride = Archetype->DebugColorOverride;

					// Ctrl held = also reset transform
					const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
					if (Mods.IsControlDown())
					{
						S->SetRelativeTransform(Archetype->GetRelativeTransform());
						if (GEditor) { GEditor->NoteSelectionChange(); }
					}

					if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
					{
						Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
					if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
					{
						Mode->RedrawViewports();
					}
					this->RefreshContent();
					return FReply::Handled();
				})
			];
		}

		Content->AddSlot().AutoHeight().Padding(0, 1, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "DuplicateConnector", "Duplicate"))
				.ToolTipText(NSLOCTEXT("PCGExValency", "DuplicateConnectorTip", "Create a copy of this connector with a small offset (Ctrl+D)"))
				.OnClicked_Lambda([WeakConnector, WeakMode]() -> FReply
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->DuplicateConnector(S);
						}
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "RemoveConnectorBtn", "Remove"))
				.ToolTipText(bIsBPDefined
					? NSLOCTEXT("PCGExValency", "RemoveConnectorBPTip", "Cannot remove Blueprint-defined connector")
					: NSLOCTEXT("PCGExValency", "RemoveConnectorTip", "Delete this connector from the cage (Delete key)"))
				.IsEnabled(!bIsBPDefined)
				.OnClicked_Lambda([WeakConnector, WeakMode]() -> FReply
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RemoveConnector(S);
						}
					}
					return FReply::Handled();
				})
			]
		];
	}

	return Content;
}

TSharedRef<SWidget> SValencyInspector::BuildVolumeContent(AValencyContextVolume* Volume)
{
	if (!Volume)
	{
		return BuildSceneStatsContent();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight()
	[
		MakeSectionHeader(FText::FromString(Volume->GetActorNameOrLabel()))
	];

	Content->AddSlot().AutoHeight()
	[
		MakeLabeledColorRow(NSLOCTEXT("PCGExValency", "VolumeColor", "Color"), Volume->DebugColor)
	];

	Content->AddSlot().AutoHeight()
	[
		MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "VolumeProbeRadius", "Default Probe Radius"),
			FText::AsNumber(static_cast<int32>(Volume->DefaultProbeRadius)))
	];

	// Bonding rules
	Content->AddSlot().AutoHeight()
	[
		MakeLabeledRow(
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
			MakeLabeledRow(
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
		MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "VolumeContainedCages", "Contained Cages"),
			FText::AsNumber(ContainedCages.Num()))
	];

	// List contained cages
	if (ContainedCages.Num() > 0)
	{
		for (APCGExValencyCageBase* Cage : ContainedCages)
		{
			if (!Cage) continue;

			Content->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("  %s"), *Cage->GetCageDisplayName())))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		}
	}

	Content->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		MakeRebuildAllButton()
	];

	return Content;
}

TSharedRef<SWidget> SValencyInspector::BuildPaletteContent(APCGExValencyAssetPalette* Palette)
{
	if (!Palette)
	{
		return BuildSceneStatsContent();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight()
	[
		MakeSectionHeader(FText::FromString(Palette->GetPaletteDisplayName()))
	];

	Content->AddSlot().AutoHeight()
	[
		MakeLabeledColorRow(NSLOCTEXT("PCGExValency", "PaletteColor", "Color"), Palette->PaletteColor)
	];

	Content->AddSlot().AutoHeight()
	[
		MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "PaletteAssets", "Assets"),
			FText::AsNumber(Palette->GetAllAssetEntries().Num()))
	];

	// Mirroring cages
	TArray<APCGExValencyCage*> MirroringCages;
	Palette->FindMirroringCages(MirroringCages);

	if (MirroringCages.Num() > 0)
	{
		Content->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			MakeSectionHeader(FText::Format(
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
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
		}
	}

	return Content;
}

TSharedRef<SWidget> SValencyInspector::MakeCompactConnectorRow(UPCGExValencyCageConnectorComponent* ConnectorComp, bool bIsActive)
{
	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> WeakConnector(ConnectorComp);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	const FLinearColor RowBgColor = bIsActive
		? FLinearColor(0.12f, 0.25f, 0.45f, 1.0f)
		: FLinearColor(0.02f, 0.02f, 0.02f, 0.5f);

	const FLinearColor AccentColor = bIsActive
		? FLinearColor(0.3f, 0.6f, 1.0f, 1.0f)
		: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Polarity symbols
	auto GetPolaritySymbol = [](EPCGExConnectorPolarity P) -> FText
	{
		switch (P)
		{
		case EPCGExConnectorPolarity::Universal: return FText::FromString(TEXT("\u25C9")); // ◉
		case EPCGExConnectorPolarity::Plug:      return FText::FromString(TEXT("\u25CF")); // ●
		case EPCGExConnectorPolarity::Port:      return FText::FromString(TEXT("\u25CB")); // ○
		default:                                 return FText::GetEmpty();
		}
	};

	auto GetPolarityTooltip = [](EPCGExConnectorPolarity P) -> FText
	{
		switch (P)
		{
		case EPCGExConnectorPolarity::Universal: return NSLOCTEXT("PCGExValency", "PolarityUniTip", "Universal \u2014 connects to any polarity. Click to cycle.");
		case EPCGExConnectorPolarity::Plug:      return NSLOCTEXT("PCGExValency", "PolarityPlugTip", "Plug \u2014 connects to Port or Universal. Click to cycle.");
		case EPCGExConnectorPolarity::Port:      return NSLOCTEXT("PCGExValency", "PolarityPortTip", "Port \u2014 connects to Plug or Universal. Click to cycle.");
		default:                                 return FText::GetEmpty();
		}
	};

	// Resolve icon character and color for the connector type
	UPCGExValencyConnectorSet* EffectiveSet = nullptr;
	if (const APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(ConnectorComp->GetOwner()))
	{
		EffectiveSet = Cage->GetEffectiveConnectorSet();
	}

	TWeakObjectPtr<UPCGExValencyConnectorSet> WeakSet(EffectiveSet);

	// Build the icon dot widget — clickable type picker when ConnectorSet available
	TSharedRef<SWidget> IconDotWidget = SNullWidget::NullWidget;

	if (EffectiveSet && EffectiveSet->ConnectorTypes.Num() > 0)
	{
		IconDotWidget = SNew(SComboButton)
			.HasDownArrow(false)
			.ContentPadding(0)
			.ToolTipText_Lambda([WeakConnector, WeakSet]() -> FText
			{
				if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
				{
					if (const UPCGExValencyConnectorSet* Set = WeakSet.Get())
					{
						const int32 Idx = Set->FindConnectorTypeIndex(S->ConnectorType);
						if (!Set->ConnectorTypes.IsValidIndex(Idx))
						{
							return FText::Format(
								NSLOCTEXT("PCGExValency", "TypeNotFoundTip", "Type '{0}' not found in ConnectorSet"),
								FText::FromName(S->ConnectorType));
						}
					}
					return FText::FromName(S->ConnectorType);
				}
				return FText::GetEmpty();
			})
			.ButtonContent()
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([WeakConnector, WeakSet]() -> FText
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							if (const UPCGExValencyConnectorSet* Set = WeakSet.Get())
							{
								const int32 Idx = Set->FindConnectorTypeIndex(S->ConnectorType);
								if (Set->ConnectorTypes.IsValidIndex(Idx))
								{
									return PCGExValencyInspector::GetIconText(Set, Idx);
								}
							}
						}
						return FText::FromString(TEXT("?"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity_Lambda([WeakConnector, WeakSet]() -> FSlateColor
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							if (const UPCGExValencyConnectorSet* Set = WeakSet.Get())
							{
								const int32 Idx = Set->FindConnectorTypeIndex(S->ConnectorType);
								if (Set->ConnectorTypes.IsValidIndex(Idx))
								{
									return FSlateColor(Set->ConnectorTypes[Idx].DebugColor);
								}
							}
						}
						return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
					})
				]
			]
			.OnGetMenuContent_Lambda([WeakConnector, WeakMode, EffectiveSet]() -> TSharedRef<SWidget>
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				for (int32 i = 0; i < EffectiveSet->ConnectorTypes.Num(); ++i)
				{
					const FPCGExValencyConnectorEntry& Entry = EffectiveSet->ConnectorTypes[i];
					const FName TypeName = Entry.ConnectorType;
					const FText Icon = PCGExValencyInspector::GetIconText(EffectiveSet, i);

					MenuBuilder.AddMenuEntry(
						FUIAction(FExecuteAction::CreateLambda([WeakConnector, WeakMode, TypeName]()
						{
							if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
							{
								if (S->ConnectorType == TypeName) return;
								FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ChangeConnectorType", "Change Connector Type"));
								S->Modify();
								S->ConnectorType = TypeName;
								if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
								{
									Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
								}
								if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
								{
									Mode->RedrawViewports();
								}
							}
						})),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 6, 0)
						[
							SNew(STextBlock)
							.Text(Icon)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FSlateColor(Entry.DebugColor))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromName(TypeName))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						],
						NAME_None,
						FText::Format(NSLOCTEXT("PCGExValency", "TypePickerEntryTip", "Set type to '{0}'"), FText::FromName(TypeName))
					);
				}

				return MenuBuilder.MakeWidget();
			});
	}
	else
	{
		// No ConnectorSet — plain icon dot, no picker
		IconDotWidget = SNew(SBox)
			.ToolTipText_Lambda([WeakConnector]() -> FText
			{
				if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
				{
					return FText::FromName(S->ConnectorType);
				}
				return FText::GetEmpty();
			})
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("?")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
				]
			];
	}

	const bool bIsBlueprintDefined = (ConnectorComp->CreationMethod != EComponentCreationMethod::Instance);

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
		.Padding(0)
		.ColorAndOpacity_Lambda([WeakConnector]() -> FLinearColor
		{
			if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
			{
				return S->bEnabled ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f, 0.7f);
			}
			return FLinearColor::White;
		})
		[
			SNew(SHorizontalBox)
			// Left accent bar
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(3.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(AccentColor)
				]
			]
			// Row content
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(RowBgColor)
				.Padding(FMargin(4, 5))
				[
					SNew(SHorizontalBox)
					// [BP] badge for Blueprint-defined connectors
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				bIsBlueprintDefined
				? static_cast<TSharedRef<SWidget>>(SNew(SBorder)
					.BorderBackgroundColor(FLinearColor(0.15f, 0.35f, 0.15f, 1.0f))
					.Padding(FMargin(3, 0))
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("PCGExValency", "BPBadge", "BP"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.9f, 0.5f)))
						.ToolTipText(NSLOCTEXT("PCGExValency", "BPBadgeTip", "Blueprint-defined connector (cannot be removed on instances)"))
					])
				: static_cast<TSharedRef<SWidget>>(SNullWidget::NullWidget)
			]
			// Enable/disable checkbox (first item)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakConnector]() -> ECheckBoxState
				{
					if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						return S->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
					return ECheckBoxState::Checked;
				})
				.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorRowEnabledTip", "Enable/disable this connector"))
				.OnCheckStateChanged_Lambda([WeakConnector, WeakMode](ECheckBoxState NewState)
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ToggleEnabled", "Toggle Connector Enabled"));
						S->Modify();
						S->bEnabled = (NewState == ECheckBoxState::Checked);
						if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
						{
							Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
						}
						if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
						{
							Mode->RedrawViewports();
						}
					}
				})
			]
			// Icon dot — shows type icon on colored background, click to pick type
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				IconDotWidget
			]
			// Clickable name - selects in viewport without leaving cage view
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(2, 0)
			[
				SNew(SButton)
				.ContentPadding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorRowNameTip", "Click to select this connector in the viewport"))
				.OnClicked_Lambda([WeakConnector, this]() -> FReply
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						if (GEditor)
						{
							bIsUpdatingSelection = true;
							GEditor->GetSelectedComponents()->DeselectAll();
							if (AActor* Owner = S->GetOwner())
							{
								GEditor->SelectActor(Owner, true, true);
							}
							GEditor->SelectComponent(S, true, true);
							bIsUpdatingSelection = false;
							RefreshContent();
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([WeakConnector]() -> FText
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							return FText::FromName(S->Identifier);
						}
						return FText::GetEmpty();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			// Polarity cycling button (◉/●/○) - fixed width to prevent layout shift
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			.Padding(1, 0)
			[
				SNew(SBox)
				.WidthOverride(22)
				[
					SNew(SButton)
					.Text_Lambda([WeakConnector, GetPolaritySymbol]() -> FText
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							return GetPolaritySymbol(S->Polarity);
						}
						return FText::GetEmpty();
					})
					.ToolTipText_Lambda([WeakConnector, GetPolarityTooltip]() -> FText
					{
						if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							return GetPolarityTooltip(S->Polarity);
						}
						return FText::GetEmpty();
					})
					.ContentPadding(FMargin(2, 0))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked_Lambda([WeakConnector, WeakMode]() -> FReply
					{
						if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "CyclePolarity", "Cycle Connector Polarity"));
							S->Modify();
							switch (S->Polarity)
							{
							case EPCGExConnectorPolarity::Universal: S->Polarity = EPCGExConnectorPolarity::Plug; break;
							case EPCGExConnectorPolarity::Plug:      S->Polarity = EPCGExConnectorPolarity::Port; break;
							case EPCGExConnectorPolarity::Port:      S->Polarity = EPCGExConnectorPolarity::Universal; break;
							}
							if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(S->GetOwner()))
							{
								Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
							}
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->RedrawViewports();
							}
						}
						return FReply::Handled();
					})
				] // SBox
			]
			// More info / actions button (...) - compact
			// Click: detail panel, Ctrl+click: delete, Alt+click: duplicate
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			.Padding(1, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "MoreInfoDots", "..."))
				.ToolTipText_Lambda([WeakConnector]() -> FText
				{
					if (const UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						if (S->CreationMethod != EComponentCreationMethod::Instance)
						{
							return NSLOCTEXT("PCGExValency", "MoreInfoTipBP", "Details (Blueprint-defined: Alt to duplicate)");
						}
					}
					return NSLOCTEXT("PCGExValency", "MoreInfoTip", "Details (Ctrl: delete, Alt: duplicate)");
				})
				.ContentPadding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
				{
					if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
					{
						const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
						if (Mods.IsControlDown() && S->CreationMethod == EComponentCreationMethod::Instance)
						{
							// Ctrl+click = Delete connector (instance-only)
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->RemoveConnector(S);
							}
						}
						else if (Mods.IsAltDown())
						{
							// Alt+click = Duplicate connector
							if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
							{
								Mode->DuplicateConnector(S);
							}
						}
						else
						{
							// Normal click = Navigate to detail panel
							DetailPanelConnector = S;
							if (GEditor)
							{
								bIsUpdatingSelection = true;
								GEditor->GetSelectedComponents()->DeselectAll();
								if (AActor* Owner = S->GetOwner())
								{
									GEditor->SelectActor(Owner, true, true);
								}
								GEditor->SelectComponent(S, true, true);
								bIsUpdatingSelection = false;
								RefreshContent();
							}
						}
					}
					return FReply::Handled();
				})
			]
			] // inner SBorder content
		] // content slot
	]; // outer SBorder
}

TSharedRef<SWidget> SValencyInspector::MakeAddConnectorButton(APCGExValencyCageBase* Cage)
{
	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	// Check if ConnectorSet is available for type picker
	UPCGExValencyConnectorSet* EffectiveSet = Cage->GetEffectiveConnectorSet();

	if (EffectiveSet && EffectiveSet->ConnectorTypes.Num() > 0)
	{
		// Dropdown button with type menu
		return SNew(SComboButton)
			.ContentPadding(FMargin(4, 1))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "AddConnector", "+ Add"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			.OnGetMenuContent_Lambda([WeakCage, WeakMode, EffectiveSet]() -> TSharedRef<SWidget>
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				if (EffectiveSet)
				{
					for (const FPCGExValencyConnectorEntry& Entry : EffectiveSet->ConnectorTypes)
					{
						const FName TypeName = Entry.ConnectorType;
						const FLinearColor TypeColor = Entry.DebugColor;

						MenuBuilder.AddMenuEntry(
							FText::FromName(TypeName),
							FText::Format(NSLOCTEXT("PCGExValency", "AddTypedConnectorTip", "Add connector of type '{0}'"), FText::FromName(TypeName)),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([WeakCage, WeakMode, TypeName]()
							{
								if (APCGExValencyCageBase* C = WeakCage.Get())
								{
									if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
									{
										if (UPCGExValencyCageConnectorComponent* NewConn = Mode->AddConnectorToCage(C))
										{
											NewConn->ConnectorType = TypeName;
										}
									}
								}
							}))
						);
					}
				}

				return MenuBuilder.MakeWidget();
			})
			.ToolTipText(NSLOCTEXT("PCGExValency", "AddConnectorTypedTip", "Add a connector with a specific type"));
	}

	// Fallback: plain button when no ConnectorSet available
	return SNew(SButton)
		.Text(NSLOCTEXT("PCGExValency", "AddConnector", "+ Add"))
		.ToolTipText(NSLOCTEXT("PCGExValency", "AddConnectorTip", "Add a new connector to this cage (Ctrl+Shift+A)"))
		.ContentPadding(FMargin(4, 1))
		.OnClicked_Lambda([WeakCage, WeakMode]() -> FReply
		{
			if (APCGExValencyCageBase* C = WeakCage.Get())
			{
				if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
				{
					Mode->AddConnectorToCage(C);
				}
			}
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SValencyInspector::MakeRebuildAllButton()
{
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	return SNew(SButton)
		.Text(NSLOCTEXT("PCGExValency", "RebuildAll", "Rebuild All"))
		.ToolTipText(NSLOCTEXT("PCGExValency", "RebuildAllTip", "Rebuild all cages in the scene"))
		.ContentPadding(FMargin(4, 1))
		.OnClicked_Lambda([WeakMode]() -> FReply
		{
			if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
			{
				for (const TWeakObjectPtr<APCGExValencyCageBase>& CagePtr : Mode->GetCachedCages())
				{
					if (APCGExValencyCageBase* Cage = CagePtr.Get())
					{
						Cage->RequestRebuild(EValencyRebuildReason::AssetChange);
					}
				}
			}
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SValencyInspector::MakeRelatedSection(APCGExValencyCageBase* Cage)
{
	TSharedRef<SVerticalBox> Section = SNew(SVerticalBox);
	bool bHasContent = false;

	// Containing Volumes
	const TArray<TWeakObjectPtr<AValencyContextVolume>>& Volumes = Cage->GetContainingVolumes();
	if (Volumes.Num() > 0)
	{
		bHasContent = true;
		Section->AddSlot().AutoHeight()
		[
			MakeSectionHeader(FText::Format(
				NSLOCTEXT("PCGExValency", "ContainingVolumes", "Containing Volumes ({0})"),
				FText::AsNumber(Volumes.Num())))
		];

		for (const TWeakObjectPtr<AValencyContextVolume>& VolPtr : Volumes)
		{
			if (AValencyContextVolume* Vol = VolPtr.Get())
			{
				TWeakObjectPtr<AActor> WeakActor(Vol);
				Section->AddSlot().AutoHeight()
				[
					SNew(SButton)
					.ContentPadding(FMargin(4, 1))
					.ToolTipText(NSLOCTEXT("PCGExValency", "SelectVolumeTip", "Click to select this volume"))
					.OnClicked_Lambda([WeakActor]() -> FReply
					{
						if (AActor* A = WeakActor.Get())
						{
							if (GEditor)
							{
								GEditor->SelectNone(true, true);
								GEditor->SelectActor(A, true, true);
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(Vol->GetActorNameOrLabel()))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				];
			}
		}
	}

	// Mirror sources (APCGExValencyCage only)
	if (const APCGExValencyCage* RegularCage = Cast<APCGExValencyCage>(Cage))
	{
		if (RegularCage->MirrorSources.Num() > 0)
		{
			bHasContent = true;
			Section->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeSectionHeader(FText::Format(
					NSLOCTEXT("PCGExValency", "Mirrors", "Mirrors ({0})"),
					FText::AsNumber(RegularCage->MirrorSources.Num())))
			];

			for (const FPCGExMirrorSource& MirrorEntry : RegularCage->MirrorSources)
			{
				if (AActor* SourceActor = MirrorEntry.Source.Get())
				{
					TWeakObjectPtr<AActor> WeakActor(SourceActor);
					Section->AddSlot().AutoHeight()
					[
						SNew(SButton)
						.ContentPadding(FMargin(4, 1))
						.ToolTipText(NSLOCTEXT("PCGExValency", "SelectMirrorSourceTip", "Click to select this mirror source"))
						.OnClicked_Lambda([WeakActor]() -> FReply
						{
							if (AActor* A = WeakActor.Get())
							{
								if (GEditor)
								{
									GEditor->SelectNone(true, true);
									GEditor->SelectActor(A, true, true);
								}
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(SourceActor->GetActorNameOrLabel()))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
					];
				}
			}
		}
	}

	// Mirrored By (cages that use this cage as a mirror source)
	if (APCGExValencyAssetContainerBase* Container = Cast<APCGExValencyAssetContainerBase>(Cage))
	{
		TArray<APCGExValencyCage*> MirroringCages;
		Container->FindMirroringCages(MirroringCages);

		if (MirroringCages.Num() > 0)
		{
			bHasContent = true;
			Section->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeSectionHeader(FText::Format(
					NSLOCTEXT("PCGExValency", "MirroredBy", "Mirrored By ({0})"),
					FText::AsNumber(MirroringCages.Num())))
			];

			for (APCGExValencyCage* MirrorCage : MirroringCages)
			{
				if (!MirrorCage) continue;
				TWeakObjectPtr<AActor> WeakActor(MirrorCage);
				Section->AddSlot().AutoHeight()
				[
					SNew(SButton)
					.ContentPadding(FMargin(4, 1))
					.ToolTipText(NSLOCTEXT("PCGExValency", "SelectMirroringCageTip", "Click to select this mirroring cage"))
					.OnClicked_Lambda([WeakActor]() -> FReply
					{
						if (AActor* A = WeakActor.Get())
						{
							if (GEditor)
							{
								GEditor->SelectNone(true, true);
								GEditor->SelectActor(A, true, true);
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(MirrorCage->GetCageDisplayName()))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				];
			}
		}
	}

	if (!bHasContent)
	{
		return SNullWidget::NullWidget;
	}

	return Section;
}

TSharedRef<SWidget> SValencyInspector::MakeLabeledRow(const FText& Label, const FText& Value)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4, 1)
		[
			SNew(STextBlock)
			.Text(Value)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		];
}

TSharedRef<SWidget> SValencyInspector::MakeLabeledColorRow(const FText& Label, const FLinearColor& Color)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 1)
		[
			SNew(SBox)
			.WidthOverride(100)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 1)
		[
			SNew(SColorBlock)
			.Color(Color)
			.Size(FVector2D(16, 16))
		];
}

TSharedRef<SWidget> SValencyInspector::MakeSectionHeader(const FText& Title)
{
	return SNew(STextBlock)
		.Text(Title)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
		.Margin(FMargin(0, 2, 0, 1));
}
