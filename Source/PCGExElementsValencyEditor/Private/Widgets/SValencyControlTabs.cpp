// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyControlTabs.h"

#include "Editor.h"
#include "Selection.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "EditorMode/PCGExValencyDrawHelper.h"
#include "Cages/PCGExValencyCageBase.h"
#include "Cages/PCGExValencyCage.h"
#include "Cages/PCGExValencyCagePattern.h"
#include "Cages/PCGExValencyCageNull.h"
#include "Cages/PCGExValencyAssetPalette.h"
#include "Cages/PCGExValencyAssetContainerBase.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Volumes/ValencyContextVolume.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"

namespace Style = PCGExValencyWidgets::Style;

#pragma region SValencyControlTabs

void SValencyControlTabs::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	ChildSlot
	[
		SAssignNew(RootArea, SBox)
	];

	if (GEditor)
	{
		OnSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddSP(
			this, &SValencyControlTabs::OnSelectionChangedCallback);
		OnComponentSelectionChangedHandle = GEditor->GetSelectedComponents()->SelectionChangedEvent.AddSP(
			this, &SValencyControlTabs::OnSelectionChangedCallback);
	}

	if (EditorMode)
	{
		OnSceneChangedHandle = EditorMode->OnSceneChanged.AddSP(
			this, &SValencyControlTabs::RefreshContent);
	}

	RefreshContent();
}

void SValencyControlTabs::OnSelectionChangedCallback(UObject* InObject)
{
	if (bIsUpdatingSelection)
	{
		return;
	}
	RefreshContent();
}

APCGExValencyCageBase* SValencyControlTabs::GetSelectedCage() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	// Check components first (connector -> owning cage)
	if (USelection* CompSelection = GEditor->GetSelectedComponents())
	{
		for (FSelectionIterator It(*CompSelection); It; ++It)
		{
			if (UPCGExValencyCageConnectorComponent* Connector = Cast<UPCGExValencyCageConnectorComponent>(*It))
			{
				return Cast<APCGExValencyCageBase>(Connector->GetOwner());
			}
		}
	}

	// Check actors
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(*It))
		{
			return Cage;
		}
	}

	return nullptr;
}

void SValencyControlTabs::RefreshContent()
{
	if (!RootArea.IsValid() || bIsUpdatingSelection)
	{
		return;
	}

	// Validate connector detail state
	if (bShowingConnectorDetail)
	{
		bool bDetailStillValid = false;
		if (UPCGExValencyCageConnectorComponent* Conn = DetailPanelConnector.Get())
		{
			if (GEditor && Conn->GetOwner())
			{
				bDetailStillValid = GEditor->GetSelectedActors()->IsSelected(Conn->GetOwner());
			}
		}
		if (!bDetailStillValid)
		{
			bShowingConnectorDetail = false;
			DetailPanelConnector.Reset();
		}
	}

	// Check if volume, palette, or null cage is selected (hide tabs — no connectors/assets)
	bool bHideTabs = false;
	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (Cast<AValencyContextVolume>(*It) || Cast<APCGExValencyAssetPalette>(*It) || Cast<APCGExValencyCageNull>(*It))
			{
				bHideTabs = true;
				break;
			}
		}
	}

	if (bHideTabs)
	{
		RootArea->SetContent(SNullWidget::NullWidget);
		return;
	}

	APCGExValencyCageBase* Cage = GetSelectedCage();
	if (!Cage || Cage->IsNullCage())
	{
		RootArea->SetContent(SNullWidget::NullWidget);
		return;
	}

	RootArea->SetContent(BuildTabContent(Cage));
}

TSharedRef<SWidget> SValencyControlTabs::BuildTabContent(APCGExValencyCageBase* Cage)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Tab bar using SSegmentedControl
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		SNew(SSegmentedControl<int32>)
		.Value(ActiveTabIndex)
		.OnValueChanged_Lambda([this](int32 NewValue)
		{
			ActiveTabIndex = NewValue;
			bShowingConnectorDetail = false;
			DetailPanelConnector.Reset();
			RefreshContent();
		})
		+ SSegmentedControl<int32>::Slot(0)
			.Text(NSLOCTEXT("PCGExValency", "TabConnectors", "Connectors"))
		+ SSegmentedControl<int32>::Slot(1)
			.Text(NSLOCTEXT("PCGExValency", "TabAssets", "Assets"))
		+ SSegmentedControl<int32>::Slot(2)
			.Text(NSLOCTEXT("PCGExValency", "TabPlacement", "Placement"))
	];

	// Tab content
	TSharedRef<SWidget> TabContent = SNullWidget::NullWidget;
	switch (ActiveTabIndex)
	{
	case 0: TabContent = BuildConnectorsTab(Cage); break;
	case 1: TabContent = BuildAssetsTab(Cage); break;
	case 2: TabContent = BuildPlacementTab(Cage); break;
	}

	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		TabContent
	];

	return Content;
}

TSharedRef<SWidget> SValencyControlTabs::BuildConnectorsTab(APCGExValencyCageBase* Cage)
{
	// If showing connector detail, render it inside this tab
	if (bShowingConnectorDetail)
	{
		if (UPCGExValencyCageConnectorComponent* Conn = DetailPanelConnector.Get())
		{
			return BuildConnectorDetail(Conn);
		}
		// Connector went invalid, fall through to list
		bShowingConnectorDetail = false;
		DetailPanelConnector.Reset();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

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

	// Header row with connector count and Add button
	Content->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			PCGExValencyWidgets::MakeSectionHeader(FText::Format(
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
		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding, 0, Style::RowPadding)
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

	// Related section (containing volumes, mirrors, mirrored-by)
	Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
	[
		MakeRelatedSection(Cage)
	];

	return Content;
}

TSharedRef<SWidget> SValencyControlTabs::BuildAssetsTab(APCGExValencyCageBase* Cage)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	const APCGExValencyAssetContainerBase* Container = Cast<APCGExValencyAssetContainerBase>(Cage);
	if (!Container)
	{
		Content->AddSlot().AutoHeight()
		[
			PCGExValencyWidgets::MakeHintText(
				NSLOCTEXT("PCGExValency", "NoAssetsAvail", "No asset container"))
		];
		return Content;
	}

	const TArray<FPCGExValencyAssetEntry> AllEntries = Container->GetAllAssetEntries();

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeSectionHeader(FText::Format(
			NSLOCTEXT("PCGExValency", "AssetsHeader", "Assets ({0})"),
			FText::AsNumber(AllEntries.Num())))
	];

	if (AllEntries.Num() == 0)
	{
		Content->AddSlot().AutoHeight().Padding(4, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeHintText(
				NSLOCTEXT("PCGExValency", "NoAssets", "No assets registered"))
		];
	}
	else
	{
		for (const FPCGExValencyAssetEntry& Entry : AllEntries)
		{
			FString AssetName = Entry.Asset.GetAssetName();
			if (AssetName.IsEmpty())
			{
				AssetName = Entry.Asset.ToString();
			}

			Content->AddSlot().AutoHeight().Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(AssetName))
				.Font(Style::Label())
				.ToolTipText(FText::FromString(Entry.Asset.ToString()))
			];
		}
	}

	return Content;
}

TSharedRef<SWidget> SValencyControlTabs::BuildPlacementTab(APCGExValencyCageBase* Cage)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	const APCGExValencyAssetContainerBase* Container = Cast<APCGExValencyAssetContainerBase>(Cage);
	if (!Container)
	{
		Content->AddSlot().AutoHeight()
		[
			PCGExValencyWidgets::MakeHintText(
				NSLOCTEXT("PCGExValency", "NoPlacementInfo", "No placement info available"))
		];
		return Content;
	}

	const FPCGExValencyModuleSettings& Settings = Container->ModuleSettings;

	// Placement Conditions
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "PlacementConditionsHeader", "Placement Conditions"))
	];

	if (Settings.PlacementConditions.Num() == 0)
	{
		Content->AddSlot().AutoHeight().Padding(4, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeHintText(
				NSLOCTEXT("PCGExValency", "NoConditions", "No conditions defined (places unconditionally)"))
		];
	}
	else
	{
		for (int32 i = 0; i < Settings.PlacementConditions.Num(); ++i)
		{
			const FInstancedStruct& Instance = Settings.PlacementConditions[i];
			if (!Instance.GetScriptStruct()) continue;

			const FString TypeName = Instance.GetScriptStruct()->GetDisplayNameText().ToString();

			Content->AddSlot().AutoHeight().Padding(4, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("[%d]"), i)))
					.Font(Style::SmallBold())
					.ColorAndOpacity(Style::AccentColor())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TypeName))
					.Font(Style::Label())
				]
			];
		}
	}

	// Bounds Modifier
	Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "BoundsModHeader", "Bounds Modifier"))
	];

	const FPCGExBoundsModifier& BM = Settings.BoundsModifier;
	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "BMScale", "Scale"),
			FText::Format(NSLOCTEXT("PCGExValency", "BMScaleVal", "({0}, {1}, {2})"),
				FText::AsNumber(BM.Scale.X), FText::AsNumber(BM.Scale.Y), FText::AsNumber(BM.Scale.Z)))
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeLabeledRow(
			NSLOCTEXT("PCGExValency", "BMOffset", "Offset"),
			FText::Format(NSLOCTEXT("PCGExValency", "BMOffsetVal", "({0}, {1}, {2})"),
				FText::AsNumber(BM.Offset.X), FText::AsNumber(BM.Offset.Y), FText::AsNumber(BM.Offset.Z)))
	];

	// Connector Transform Strategy
	if (Container->ConnectorTransformStrategy.IsValid())
	{
		const FString StrategyName = Container->ConnectorTransformStrategy.GetScriptStruct()
			? Container->ConnectorTransformStrategy.GetScriptStruct()->GetDisplayNameText().ToString()
			: TEXT("(unknown)");

		Content->AddSlot().AutoHeight().Padding(0, Style::SectionGap, 0, 0)
		[
			PCGExValencyWidgets::MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "ConnTransform", "Transform Strategy"),
				FText::FromString(StrategyName))
		];
	}

	return Content;
}

TSharedRef<SWidget> SValencyControlTabs::BuildConnectorDetail(UPCGExValencyCageConnectorComponent* Connector)
{
	if (!Connector)
	{
		bShowingConnectorDetail = false;
		return SNullWidget::NullWidget;
	}

	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> WeakConnector(Connector);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// Back to Connectors list button
	Content->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SButton)
		.Text(NSLOCTEXT("PCGExValency", "BackToConnectors", "\u25C0 Back"))
		.ToolTipText(NSLOCTEXT("PCGExValency", "BackToConnectorsTip", "Return to the connector list"))
		.ContentPadding(FMargin(4, 1))
		.OnClicked_Lambda([this]() -> FReply
		{
			DetailPanelConnector.Reset();
			bShowingConnectorDetail = false;
			RefreshContent();
			return FReply::Handled();
		})
	];

	Content->AddSlot().AutoHeight()
	[
		PCGExValencyWidgets::MakeSectionHeader(FText::Format(
			NSLOCTEXT("PCGExValency", "ConnectorHeader", "Connector: {0}"),
			FText::FromName(Connector->Identifier)))
	];

	// Owning cage
	if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
	{
		Content->AddSlot().AutoHeight()
		[
			PCGExValencyWidgets::MakeLabeledRow(
				NSLOCTEXT("PCGExValency", "ConnectorOwner", "Cage"),
				FText::FromString(OwnerCage->GetCageDisplayName()))
		];
	}

	// Editable Name
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeLabeledControl(
			NSLOCTEXT("PCGExValency", "ConnectorIdentifier", "Identifier"),
			SNew(SEditableTextBox)
			.Text(FText::FromName(Connector->Identifier))
			.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorIdentifierTip", "Unique connector identifier within this cage"))
			.Font(Style::Label())
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
		)
	];

	// Editable Type
	{
		UPCGExValencyConnectorSet* EffectiveSet = nullptr;
		if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
		{
			EffectiveSet = OwnerCage->GetEffectiveConnectorSet();
		}

		TSharedRef<SWidget> TypeWidget = SNullWidget::NullWidget;

		if (EffectiveSet && EffectiveSet->ConnectorTypes.Num() > 0)
		{
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
								.Font(Style::Label())
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
						.Font(Style::Label())
					]
				];
		}
		else
		{
			TypeWidget = SNew(SEditableTextBox)
				.Text(FText::FromName(Connector->ConnectorType))
				.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorTypeTip", "Connector type name. Assign a ConnectorSet for type dropdown."))
				.Font(Style::Label())
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

		Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
		[
			PCGExValencyWidgets::MakeLabeledControl(
				NSLOCTEXT("PCGExValency", "ConnectorType", "Type"),
				TypeWidget
			)
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

	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeLabeledControl(
			NSLOCTEXT("PCGExValency", "ConnectorPolarity", "Polarity"),
			SNew(SButton)
			.Text(GetPolarityLabel(Connector->Polarity))
			.ToolTipText(NSLOCTEXT("PCGExValency", "ConnectorPolarityTip", "Cycle polarity: Universal (connects to any), Plug (outward), Port (inward)"))
			.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
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
					this->RefreshContent();
				}
				return FReply::Handled();
			})
		)
	];

	// Enabled checkbox
	Content->AddSlot().AutoHeight().Padding(0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeLabeledControl(
			NSLOCTEXT("PCGExValency", "ConnectorEnabled", "Enabled"),
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
		)
	];

	// Constraints section
	{
		UPCGExValencyConnectorSet* EffConnSet = nullptr;
		if (const APCGExValencyCageBase* OwnerCage = Cast<APCGExValencyCageBase>(Connector->GetOwner()))
		{
			EffConnSet = OwnerCage->GetEffectiveConnectorSet();
		}

		int32 ConstraintCount = 0;

		if (EffConnSet)
		{
			const int32 TypeIdx = EffConnSet->FindConnectorTypeIndex(Connector->ConnectorType);
			if (EffConnSet->ConnectorTypes.IsValidIndex(TypeIdx))
			{
				const TArray<FInstancedStruct>& Defaults = EffConnSet->ConnectorTypes[TypeIdx].DefaultConstraints;
				ConstraintCount = Defaults.Num();

				if (ConstraintCount > 0)
				{
					Content->AddSlot().AutoHeight().Padding(0, 6, 0, Style::RowPadding)
					[
						PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "ConstraintsHeader", "Constraints"))
					];

					for (int32 i = 0; i < Defaults.Num(); ++i)
					{
						const FInstancedStruct& Instance = Defaults[i];
						if (!Instance.GetScriptStruct()) continue;

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
								.Font(Style::SmallBold())
								.ColorAndOpacity(Style::AccentColor())
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TypeName))
								.Font(Style::Label())
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
			Content->AddSlot().AutoHeight().Padding(0, 6, 0, Style::RowPadding)
			[
				PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "ConstraintsHeaderEmpty", "Constraints"))
			];

			Content->AddSlot().AutoHeight().Padding(8, 1)
			[
				PCGExValencyWidgets::MakeHintText(
					NSLOCTEXT("PCGExValency", "NoConstraints", "No constraints defined"))
			];
		}
	}

	// Mirror buttons
	Content->AddSlot().AutoHeight().Padding(0, 6, 0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "MirrorHeader", "Mirror"))
	];

	{
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
						const bool bCageRelative = Mods.IsShiftDown();
						const bool bDuplicate = Mods.IsShiftDown() && Mods.IsAltDown();

						const FTransform T = FPCGExValencyDrawHelper::ComputeMirroredTransform(
							S->GetRelativeTransform(), AxisMask, bCageRelative);

						if (bDuplicate)
						{
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
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_X"), 1,
					NSLOCTEXT("PCGExValency", "MirrorXTip", "Mirror X. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_Y"), 2,
					NSLOCTEXT("PCGExValency", "MirrorYTip", "Mirror Y. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_Z"), 4,
					NSLOCTEXT("PCGExValency", "MirrorZTip", "Mirror Z. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_XY"), 1 | 2,
					NSLOCTEXT("PCGExValency", "MirrorXYTip", "Mirror XY. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_YZ"), 2 | 4,
					NSLOCTEXT("PCGExValency", "MirrorYZTip", "Mirror YZ. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_XZ"), 1 | 4,
					NSLOCTEXT("PCGExValency", "MirrorXZTip", "Mirror XZ. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeMirrorButton(FName("PCGEx.ActionIcon.RotOrder_XYZ"), 1 | 2 | 4,
					NSLOCTEXT("PCGExValency", "MirrorXYZTip", "Mirror XYZ. Shift: cage-relative. Shift+Alt: duplicate at mirror."))
			]
		];
	}

	// Action buttons
	Content->AddSlot().AutoHeight().Padding(0, 6, 0, Style::RowPadding)
	[
		PCGExValencyWidgets::MakeSectionHeader(NSLOCTEXT("PCGExValency", "ActionsHeader", "Actions"))
	];

	{
		const bool bIsBPDefined = (Connector->CreationMethod != EComponentCreationMethod::Instance);

		// Reset button for BP-defined connectors
		if (bIsBPDefined)
		{
			Content->AddSlot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("PCGExValency", "ResetConnector", "Reset to Blueprint Defaults"))
				.ToolTipText(NSLOCTEXT("PCGExValency", "ResetConnectorTip", "Reset editable properties to Blueprint defaults.\n+ Shift : also reset transform"))
				.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
				{
					UPCGExValencyCageConnectorComponent* S = WeakConnector.Get();
					if (!S) { return FReply::Handled(); }

					const UPCGExValencyCageConnectorComponent* Archetype = Cast<UPCGExValencyCageConnectorComponent>(S->GetArchetype());
					if (!Archetype) { return FReply::Handled(); }

					FScopedTransaction Transaction(NSLOCTEXT("PCGExValency", "ResetConnectorDefaults", "Reset Connector to Blueprint Defaults"));
					S->Modify();
					S->bEnabled = Archetype->bEnabled;
					S->Polarity = Archetype->Polarity;
					S->DebugColorOverride = Archetype->DebugColorOverride;

					const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
					if (Mods.IsShiftDown())
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

TSharedRef<SWidget> SValencyControlTabs::MakeCompactConnectorRow(UPCGExValencyCageConnectorComponent* ConnectorComp, bool bIsActive)
{
	TWeakObjectPtr<UPCGExValencyCageConnectorComponent> WeakConnector(ConnectorComp);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	const FLinearColor RowBgColor = bIsActive
		? FLinearColor(0.12f, 0.25f, 0.45f, 1.0f)
		: FLinearColor(0.02f, 0.02f, 0.02f, 0.5f);

	const FLinearColor AccentColor = bIsActive
		? FLinearColor(0.3f, 0.6f, 1.0f, 1.0f)
		: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	auto GetPolaritySymbol = [](EPCGExConnectorPolarity P) -> FText
	{
		switch (P)
		{
		case EPCGExConnectorPolarity::Universal: return FText::FromString(TEXT("\u25C9"));
		case EPCGExConnectorPolarity::Plug:      return FText::FromString(TEXT("\u25CF"));
		case EPCGExConnectorPolarity::Port:      return FText::FromString(TEXT("\u25CB"));
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

	// Resolve icon via ConnectorSet
	UPCGExValencyConnectorSet* EffectiveSet = nullptr;
	if (const APCGExValencyCageBase* Cage = Cast<APCGExValencyCageBase>(ConnectorComp->GetOwner()))
	{
		EffectiveSet = Cage->GetEffectiveConnectorSet();
	}

	TWeakObjectPtr<UPCGExValencyConnectorSet> WeakSet(EffectiveSet);

	// Icon dot widget
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
									return PCGExValencyWidgets::GetConnectorIconText(Set, Idx);
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
					const FText Icon = PCGExValencyWidgets::GetConnectorIconText(EffectiveSet, i);

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
							.Font(Style::Label())
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
					// [BP] badge
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
								.Font(Style::SmallBold())
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.9f, 0.5f)))
								.ToolTipText(NSLOCTEXT("PCGExValency", "BPBadgeTip", "Blueprint-defined connector (cannot be removed on instances)"))
							])
						: static_cast<TSharedRef<SWidget>>(SNullWidget::NullWidget)
					]
					// Enable/disable checkbox
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
					// Icon dot
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 2, 0)
					[
						IconDotWidget
					]
					// Clickable name
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
							.Font(Style::Label())
						]
					]
					// Polarity cycling button
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
						]
					]
					// More info / actions button (...)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Fill)
					.Padding(1, 0)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("PCGExValency", "MoreInfoDots", "..."))
						.ToolTipText(NSLOCTEXT("PCGExValency", "MoreInfoTip", "Details\n+ Alt : duplicate"))
						.ContentPadding(FMargin(2, 0))
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([WeakConnector, WeakMode, this]() -> FReply
						{
							if (UPCGExValencyCageConnectorComponent* S = WeakConnector.Get())
							{
								const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
								if (Mods.IsAltDown())
								{
									if (UPCGExValencyCageEditorMode* Mode = WeakMode.Get())
									{
										Mode->DuplicateConnector(S);
									}
								}
								else
								{
									// Navigate to detail panel
									DetailPanelConnector = S;
									bShowingConnectorDetail = true;
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

TSharedRef<SWidget> SValencyControlTabs::MakeAddConnectorButton(APCGExValencyCageBase* Cage)
{
	TWeakObjectPtr<APCGExValencyCageBase> WeakCage(Cage);
	TWeakObjectPtr<UPCGExValencyCageEditorMode> WeakMode(EditorMode);

	UPCGExValencyConnectorSet* EffectiveSet = Cage->GetEffectiveConnectorSet();

	if (EffectiveSet && EffectiveSet->ConnectorTypes.Num() > 0)
	{
		return SNew(SComboButton)
			.ContentPadding(FMargin(4, 1))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "AddConnector", "+ Add"))
				.Font(Style::Label())
			]
			.OnGetMenuContent_Lambda([WeakCage, WeakMode, EffectiveSet]() -> TSharedRef<SWidget>
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				if (EffectiveSet)
				{
					for (const FPCGExValencyConnectorEntry& Entry : EffectiveSet->ConnectorTypes)
					{
						const FName TypeName = Entry.ConnectorType;

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

TSharedRef<SWidget> SValencyControlTabs::MakeRelatedSection(APCGExValencyCageBase* Cage)
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
			PCGExValencyWidgets::MakeSectionHeader(FText::Format(
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
					.ToolTipText(NSLOCTEXT("PCGExValency", "SelectVolumeTip2", "Click to select this volume"))
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
						.Font(Style::Label())
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
			Section->AddSlot().AutoHeight().Padding(0, Style::RowPadding, 0, 0)
			[
				PCGExValencyWidgets::MakeSectionHeader(FText::Format(
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
							.Font(Style::Label())
						]
					];
				}
			}
		}
	}

	// Mirrored By
	if (APCGExValencyAssetContainerBase* Container = Cast<APCGExValencyAssetContainerBase>(Cage))
	{
		TArray<APCGExValencyCage*> MirroringCages;
		Container->FindMirroringCages(MirroringCages);

		if (MirroringCages.Num() > 0)
		{
			bHasContent = true;
			Section->AddSlot().AutoHeight().Padding(0, Style::RowPadding, 0, 0)
			[
				PCGExValencyWidgets::MakeSectionHeader(FText::Format(
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
						.Font(Style::Label())
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

#pragma endregion
