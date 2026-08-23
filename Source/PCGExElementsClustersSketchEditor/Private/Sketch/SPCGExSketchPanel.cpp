// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/SPCGExSketchPanel.h"

#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "Misc/TransactionObjectEvent.h"
#include "Modules/ModuleManager.h"
#include "Sketch/PCGExClusterSketchAuthoringSettings.h"
#include "Sketch/PCGExClusterSketchData.h"
#include "Sketch/PCGExSketchEditController.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/StructOnScope.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExSketchPanel"

namespace PCGExSketchPanel
{
	/** READ view of the model. The mutable target returns null for a read-only host, which would take
	 *  inspection down along with editing. */
	const FPCGExClusterSketchModel* ReadModel(const TSharedPtr<FPCGExSketchEditController>& InController)
	{
		if (!InController)
		{
			return nullptr;
		}
		const IPCGExSketchEditTarget& ConstTarget = InController->GetTarget();
		return ConstTarget.GetModel();
	}

	FText RecordDisplayName(const FPCGExSketchDataRecord& InRecord)
	{
		return InRecord.Label.IsNone() ? LOCTEXT("UnnamedRecord", "(unnamed)") : FText::FromName(InRecord.Label);
	}
}

#pragma region Construction

void SPCGExSketchPanel::Construct(const FArguments& InArgs, const FPCGExSketchPanelContext& InContext)
{
	Context = InContext;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowScrollBar = false;
	DetailsArgs.bShowObjectLabel = false; // the panel already says which sketch this is

	const FStructureDetailsViewArgs StructArgs;
	const TSharedPtr<FStructOnScope> NullStruct;

	RecordDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, NullStruct);

	// Object-rooted, unlike the two struct scopes: the host owns these properties, so its own
	// PostEditChangeProperty runs and no write-back is needed.
	DataDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, NullStruct);
	HostDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	HostDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SPCGExSketchPanel::IsHostPropertyVisible));
	HostDetailsView->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateSP(this, &SPCGExSketchPanel::IsHostCustomRowVisible));

	// A read-only host still inspects; the delegate is what keeps its values from being committed.
	const FIsPropertyEditingEnabled EditingEnabled = FIsPropertyEditingEnabled::CreateSP(this, &SPCGExSketchPanel::IsEditingEnabled);
	if (IDetailsView* Inner = RecordDetailsView->GetDetailsView())
	{
		Inner->SetIsPropertyEditingEnabledDelegate(EditingEnabled);
	}
	if (IDetailsView* Inner = DataDetailsView->GetDetailsView())
	{
		Inner->SetIsPropertyEditingEnabledDelegate(EditingEnabled);
	}
	DataDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SPCGExSketchPanel::OnDataPropertyChanged);
	HostDetailsView->SetIsPropertyEditingEnabledDelegate(EditingEnabled);

	// No editing gate and no re-seeding: this view is rooted at a CDO that outlives every sketch.
	OptionsDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	OptionsDetailsView->SetObject(GetMutableDefault<UPCGExClusterSketchAuthoringSettings>());
	OptionsDetailsView->OnFinishedChangingProperties().AddSP(this, &SPCGExSketchPanel::OnAuthoringOptionChanged);

	RecordDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SPCGExSketchPanel::OnRecordPropertyChanged);

	// Undo restores the model wholesale under the scopes; without a host object the customizations'
	// own ForceRefresh guard never runs, so the rebuild has to be driven from here.
	TransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(this, &SPCGExSketchPanel::OnObjectTransacted);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SPCGExSketchPanel::OnHostPropertyChanged);

	ChildSlot
	[
		SAssignNew(PageSwitcher, SWidgetSwitcher)
		.WidgetIndex(ActivePage)

		+ SWidgetSwitcher::Slot()
		.Padding(4.0f)
		[
			BuildSelectionPage()
		]

		+ SWidgetSwitcher::Slot()
		.Padding(4.0f)
		[
			BuildSketchPage()
		]

		+ SWidgetSwitcher::Slot()
		.Padding(4.0f)
		[
			BuildOptionsPage()
		]
	];

	RefreshNow(/*bForceReseed*/ true);
}

SPCGExSketchPanel::~SPCGExSketchPanel()
{
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	}
	if (TransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactedHandle);
	}
	if (const TSharedPtr<FPCGExSketchEditController> Bound = BoundController.Pin();
		Bound && BoundChangedHandle.IsValid())
	{
		Bound->OnChanged.Remove(BoundChangedHandle);
	}
}

TSharedRef<SWidget> SPCGExSketchPanel::MakeBody()
{
	return SharedThis(this);
}

void SPCGExSketchPanel::SetPage(const EPage InPage)
{
	ActivePage = InPage;
	if (PageSwitcher.IsValid())
	{
		PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(InPage));
	}
}

void SPCGExSketchPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// The mode swaps its active binding with no signal of its own, and only the NEW controller can
	// announce anything afterwards -- so the switch itself has to be noticed here.
	if (ActiveController() != BoundController.Pin())
	{
		QueueRefresh(/*bForceReseed*/ true);
	}
}

#pragma endregion

#pragma region Header

TSharedRef<SWidget> SPCGExSketchPanel::MakeHeader()
{
	// Shared, not raw: the header lives in the HOST's widget tree, so its lambdas must keep the panel alive.
	TSharedRef<SPCGExSketchPanel> Self = SharedThis(this);

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(4.0f)
		[
			SNew(SVerticalBox)

			// Which sketch: only a host binding several ever fills the enumerator.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SComboButton)
				.Visibility_Lambda([Self]()
				{
					return Self->HasMultipleSketches() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnGetMenuContent(FOnGetContent::CreateSP(Self, &SPCGExSketchPanel::MakeSketchMenu))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([Self]()
					{
						return Self->ActiveSketchLabel();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SSegmentedControl<EPage>)
				.UniformPadding(FMargin(8.0f, 3.0f))
				.Value_Lambda([Self]()
				{
					return Self->GetPage();
				})
				.OnValueChanged_Lambda([Self](const EPage InPage)
				{
					Self->SetPage(InPage);
				})

				+ SSegmentedControl<EPage>::Slot(EPage::Selection)
				.Text(LOCTEXT("PageSelection", "Selection"))
				.ToolTip(LOCTEXT("PageSelectionTip", "What is selected, and the data record it reads its authored values from."))

				+ SSegmentedControl<EPage>::Slot(EPage::Sketch)
				.Text(LOCTEXT("PageSketch", "Sketch"))
				.ToolTip(LOCTEXT("PageSketchTip", "Sketch-wide properties, the per-domain schemas, cleanup and validation."))

				+ SSegmentedControl<EPage>::Slot(EPage::Options)
				.Text(LOCTEXT("PageOptions", "Options"))
				.ToolTip(LOCTEXT("PageOptionsTip", "How the authoring gestures behave. Personal preferences, shared by every sketch."))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectAll", "Select All"))
					.ContentPadding(FMargin(4, 1))
					.IsEnabled_Lambda([Self]()
					{
						return Self->ActiveController().IsValid();
					})
					.OnClicked_Lambda([Self]() -> FReply
					{
						if (const TSharedPtr<FPCGExSketchEditController> Controller = Self->ActiveController())
						{
							Controller->SelectAll();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearSelection", "Clear"))
					.ContentPadding(FMargin(4, 1))
					.IsEnabled_Lambda([Self]()
					{
						return Self->ActiveController().IsValid();
					})
					.OnClicked_Lambda([Self]() -> FReply
					{
						if (const TSharedPtr<FPCGExSketchEditController> Controller = Self->ActiveController())
						{
							Controller->ClearSelection();
							Controller->NotifyModelChanged();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.Padding(FMargin(4, 1))
					.ToolTipText(LOCTEXT("SnapTip", "Snap authored positions to the sketch's lattice basis."))
					.IsEnabled_Lambda([Self]()
					{
						return Self->ActiveController().IsValid();
					})
					.IsChecked_Lambda([Self]()
					{
						const TSharedPtr<FPCGExSketchEditController> Controller = Self->ActiveController();
						return (Controller && Controller->IsSnapEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Self](const ECheckBoxState InState)
					{
						if (const TSharedPtr<FPCGExSketchEditController> Controller = Self->ActiveController())
						{
							Controller->SetSnapEnabled(InState == ECheckBoxState::Checked);
						}
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Snap", "Snap"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SPCGExSketchPanel::MakeSketchMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (const FPCGExSketchPanelEntry& Entry : CachedSketches)
	{
		MenuBuilder.AddMenuEntry(
			Entry.Label,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SPCGExSketchPanel::ActivateSketch, Entry.Controller)));
	}

	return MenuBuilder.MakeWidget();
}

void SPCGExSketchPanel::ActivateSketch(TSharedPtr<FPCGExSketchEditController> InController)
{
	if (Context.SetActiveSketch)
	{
		Context.SetActiveSketch(InController);
	}
	QueueRefresh(/*bForceReseed*/ true);
}

#pragma endregion

#pragma region Pages

TSharedRef<SWidget> SPCGExSketchPanel::BuildSelectionPage()
{
	return SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SPCGExSketchPanel::SelectionSummaryText)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Warning)
				.Text_Lambda([this]()
				{
					const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
					return Controller ? Controller->GetTarget().GetReadOnlyReason() : FText::GetEmpty();
				})
				.Visibility_Lambda([this]()
				{
					return (ActiveController() && !IsEditingEnabled()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]

			// [record v] [Make Unique] [Break Link]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPCGExSketchPanel::RecordRowVisibility)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0, 0, 4, 0)
				[
					SNew(SComboButton)
					.IsEnabled(this, &SPCGExSketchPanel::IsEditingEnabled)
					.OnGetMenuContent(FOnGetContent::CreateSP(this, &SPCGExSketchPanel::MakeRecordMenu))
					.ButtonContent()
					[
						SNew(STextBlock).Text(this, &SPCGExSketchPanel::CurrentRecordText)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("MakeUnique", "Make Unique"))
					.ToolTipText(LOCTEXT("MakeUniqueTip", "Mint a record seeded from the current one and point the selection at it, leaving every other item on the original."))
					.ContentPadding(FMargin(4, 1))
					.IsEnabled(this, &SPCGExSketchPanel::IsEditingEnabled)
					.OnClicked(this, &SPCGExSketchPanel::OnMakeUniqueClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("BreakLink", "Break Link"))
					.ToolTipText(LOCTEXT("BreakLinkTip", "Drop the reference; the selection falls back to the layer schema's own values. The record itself stays until it is purged."))
					.ContentPadding(FMargin(4, 1))
					.IsEnabled(this, &SPCGExSketchPanel::IsEditingEnabled)
					.OnClicked(this, &SPCGExSketchPanel::OnBreakLinkClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(this, &SPCGExSketchPanel::SharedCountText)
				.Visibility(this, &SPCGExSketchPanel::SharedCountVisibility)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Error)
				.Text(LOCTEXT("DanglingRecord", "This reference names no record -- it prints as schema defaults. Break the link, or pick a record."))
				.Visibility(this, &SPCGExSketchPanel::DanglingVisibility)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				RecordDetailsView->GetWidget().ToSharedRef()
			];
}

TSharedRef<SWidget> SPCGExSketchPanel::BuildSketchPage()
{
	auto MakeCleanupButton = [this](const FText& InLabel, const FText& InTip, TFunction<void(FPCGExSketchEditController&)> InOp)
	{
		return SNew(SButton)
			.Text(InLabel)
			.ToolTipText(InTip)
			.ContentPadding(FMargin(4, 1))
			.IsEnabled(this, &SPCGExSketchPanel::IsEditingEnabled)
			.OnClicked_Lambda([this, InOp]() -> FReply
			{
				if (const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController())
				{
					InOp(*Controller);
				}
				QueueRefresh(/*bForceReseed*/ true);
				return FReply::Handled();
			});
	};

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0f, 4.0f))

			+ SWrapBox::Slot()
			[
				MakeCleanupButton(
					LOCTEXT("MergeCollocated", "Merge Collocated"),
					LOCTEXT("MergeCollocatedTip", "Merge every vertex resolving onto an already-occupied printed location. Clusters cannot carry collocated vertices."),
					[](FPCGExSketchEditController& InController)
					{
						InController.MergeCollocatedVertices();
					})
			]

			+ SWrapBox::Slot()
			[
				MakeCleanupButton(
					LOCTEXT("RemoveInvalidEdges", "Remove Invalid Edges"),
					LOCTEXT("RemoveInvalidEdgesTip", "Drop out-of-range, self-loop and duplicate edges. Out-of-range ones are dormant until the vertex array grows past them."),
					[](FPCGExSketchEditController& InController)
					{
						InController.RemoveInvalidEdges();
					})
			]

			+ SWrapBox::Slot()
			[
				MakeCleanupButton(
					LOCTEXT("SplitOverlapping", "Split Overlapping Edges"),
					LOCTEXT("SplitOverlappingTip", "Split every edge passing through a vertex, and materialize every crossing."),
					[](FPCGExSketchEditController& InController)
					{
						InController.SplitOverlappingEdges();
					})
			]

			+ SWrapBox::Slot()
			[
				MakeCleanupButton(
					LOCTEXT("PurgeRecords", "Purge Unused Data Records"),
					LOCTEXT("PurgeRecordsTip", "Drop every record no item references. Never automatic -- an untransacted purge would let delete, save then undo resurrect a broken reference."),
					[](FPCGExSketchEditController& InController)
					{
						InController.PurgeUnusedDataRecords();
					})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 6, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6, 4))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SPCGExSketchPanel::ValidationText)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 6, 0, 0)
		[
			DataDetailsView->GetWidget().ToSharedRef()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			HostDetailsView.ToSharedRef()
		];
}

TSharedRef<SWidget> SPCGExSketchPanel::BuildOptionsPage()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6, 4))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("OptionsPreamble", "Saved per user, and mirrored in Editor Preferences under Plugins."))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 6, 0, 0)
		[
			OptionsDetailsView.ToSharedRef()
		];
}

void SPCGExSketchPanel::OnAuthoringOptionChanged(const FPropertyChangedEvent& Event)
{
	GetMutableDefault<UPCGExClusterSketchAuthoringSettings>()->SaveConfig();
}

#pragma endregion

#pragma region Resolution

TSharedPtr<FPCGExSketchEditController> SPCGExSketchPanel::ActiveController() const
{
	return Context.ResolveActiveController ? Context.ResolveActiveController() : nullptr;
}

FPCGExClusterSketchModel* SPCGExSketchPanel::EditableModel() const
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	if (!Controller || !Controller->GetTarget().CanEdit())
	{
		return nullptr;
	}
	return Controller->GetTarget().GetModel();
}

SPCGExSketchPanel::EDomain SPCGExSketchPanel::SelectionDomain() const
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	if (!Controller)
	{
		return EDomain::None;
	}

	const bool bVertices = !Controller->GetSelectedVertices().IsEmpty();
	const bool bEdges = !Controller->GetSelectedEdges().IsEmpty();
	// Neither, or both.
	if (bVertices == bEdges)
	{
		return EDomain::None;
	}
	return bVertices ? EDomain::Vertex : EDomain::Edge;
}

void SPCGExSketchPanel::GatherDomainSelection(const EDomain InDomain, TArray<int32>& OutIndices) const
{
	OutIndices.Reset();

	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(Controller);
	if (!Model || InDomain == EDomain::None)
	{
		return;
	}

	const int32 Num = InDomain == EDomain::Vertex ? Model->Vertices.Num() : Model->Edges.Num();
	for (const int32 Index : InDomain == EDomain::Vertex ? Controller->GetSelectedVertices() : Controller->GetSelectedEdges())
	{
		if (Index >= 0 && Index < Num)
		{
			OutIndices.Add(Index);
		}
	}
	OutIndices.Sort();
}

const FPCGExSketchDataLayer* SPCGExSketchPanel::ResolveLayer(const EDomain InDomain) const
{
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(ActiveController());
	if (!Model || InDomain == EDomain::None)
	{
		return nullptr;
	}
	return &Model->Data.GetLayer(InDomain == EDomain::Vertex);
}

FPCGExSketchDataLayer* SPCGExSketchPanel::ResolveLayerMutable(const EDomain InDomain) const
{
	FPCGExClusterSketchModel* Model = EditableModel();
	if (!Model || InDomain == EDomain::None)
	{
		return nullptr;
	}
	return &Model->Data.GetLayer(InDomain == EDomain::Vertex);
}

uint32 SPCGExSketchPanel::PrimaryDataId() const
{
	const EDomain Domain = SelectionDomain();
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(ActiveController());
	if (!Model || Domain == EDomain::None)
	{
		return PCGExSketch::InvalidRecordId;
	}

	TArray<int32> Indices;
	GatherDomainSelection(Domain, Indices);
	if (Indices.IsEmpty())
	{
		return PCGExSketch::InvalidRecordId;
	}

	// The primary speaks for a multi-selection: the picker shows its record, and picking assigns to all.
	return Domain == EDomain::Vertex ? Model->Vertices[Indices[0]].DataId : Model->Edges[Indices[0]].DataId;
}

#pragma endregion

#pragma region Refresh

void SPCGExSketchPanel::QueueRefresh(const bool bForceReseed)
{
	bForceReseedQueued |= bForceReseed;
	if (bRefreshQueued)
	{
		return;
	}

	bRefreshQueued = true;
	RegisterActiveTimer(
		0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType
			{
				bRefreshQueued = false;
				const bool bForce = bForceReseedQueued;
				bForceReseedQueued = false;
				RefreshNow(bForce);
				return EActiveTimerReturnType::Stop;
			}));
}

void SPCGExSketchPanel::RequestRefresh()
{
	if (bIsSyncing)
	{
		return;
	}
	QueueRefresh(/*bForceReseed*/ false);
}

void SPCGExSketchPanel::RefreshNow(const bool bForceReseed)
{
	RebindController();
	RefreshSketchEntries();

	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	UObject* Host = Context.ResolveSketchObject ? Context.ResolveSketchObject() : nullptr;
	UObject* DetailsObject = Controller ? Controller->GetTarget().GetDetailsObject() : nullptr;
	const EDomain Domain = SelectionDomain();
	const uint32 DataId = PrimaryDataId();

	if (bForceReseed)
	{
		bValidationDirty = true;
	}
	RefreshValidation();

	const bool bIdentityMoved =
		bForceReseed
		|| SeededController.Pin() != Controller
		|| SeededObject.Get() != Host
		|| SeededDetailsObject.Get() != DetailsObject
		|| SeededDomain != Domain
		|| SeededRecordId != DataId;

	if (!bIdentityMoved)
	{
		return;
	}

	// Push any focused detail widget through its normal focus-lost path BEFORE the scopes swap under
	// it: Slate destroying a focused widget as part of the rebuild skips its commit, and its lambdas
	// would be left referencing the replaced scope.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	}

	SeededController = Controller;
	SeededObject = Host;
	SeededDetailsObject = DetailsObject;
	SeededDomain = Domain;
	SeededRecordId = DataId;

	// Snap provider and decorator rows come from the details object -- the asset itself, or a component's
	// payload, which expose the same authored properties. Schema and record rows use their own scopes.
	if (HostDetailsView.IsValid())
	{
		HostDetailsView->SetObject(DetailsObject, /*bForceRefresh*/ true);
	}

	SeedRecordScope();
	SeedDataScope();
}

void SPCGExSketchPanel::RebindController()
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	const TSharedPtr<FPCGExSketchEditController> Bound = BoundController.Pin();
	if (Controller == Bound)
	{
		return;
	}

	if (Bound && BoundChangedHandle.IsValid())
	{
		Bound->OnChanged.Remove(BoundChangedHandle);
	}
	BoundChangedHandle.Reset();

	BoundController = Controller;
	// Revisions are per-controller counters, so the cached validation cannot be compared across a switch.
	ValidatedRevision = INDEX_NONE;

	if (Controller)
	{
		BoundChangedHandle = Controller->OnChanged.AddSP(this, &SPCGExSketchPanel::RequestRefresh);
	}
}

void SPCGExSketchPanel::SeedDataScope()
{
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(ActiveController());
	if (!Model)
	{
		DataScope.Reset();
		if (DataDetailsView.IsValid())
		{
			DataDetailsView->SetStructureData(nullptr);
		}
		return;
	}

	UScriptStruct* DataStruct = FPCGExSketchData::StaticStruct();
	DataScope = MakeShared<FStructOnScope>(DataStruct);
	DataStruct->CopyScriptStruct(DataScope->GetStructMemory(), &Model->Data);

	if (DataDetailsView.IsValid())
	{
		DataDetailsView->SetStructureData(DataScope);
	}
}

void SPCGExSketchPanel::OnDataPropertyChanged(const FPropertyChangedEvent& Event)
{
	if (!DataScope.IsValid() || DataScope->GetStruct() != FPCGExSketchData::StaticStruct())
	{
		return;
	}

	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	FPCGExClusterSketchModel* Model = EditableModel();
	if (!Controller || !Model)
	{
		return;
	}

	UObject* Host = Controller->GetTarget().GetTransactionObject();
	const FPCGExSketchData* Edited = reinterpret_cast<const FPCGExSketchData*>(DataScope->GetStructMemory());

	{
		TGuardValue<bool> SyncGuard(bIsSyncing, true);
		const FScopedTransaction Transaction(LOCTEXT("EditSchemas", "Edit Sketch Data Schemas"));
		Controller->GetTarget().BeginAuthoring();

		// Schemas only: the Selection page mutates Records behind this view, so the scope's copy of them
		// is stale the moment a record is minted or edited.
		Model->Data.SketchProperties = Edited->SketchProperties;
		Model->Data.VertexLayer.Schema = Edited->VertexLayer.Schema;
		Model->Data.EdgeLayer.Schema = Edited->EdgeLayer.Schema;

		Model->Data.EDITOR_SyncAll();

		if (Host)
		{
			Host->PostEditChange();
			PCGExEditor::NotifyObjectChanged(Host);
		}
		Controller->NotifyModelChanged();
		Context.RequestBodyRefresh.ExecuteIfBound();
	}

	// EDITOR_SyncAll reshaped record overrides and schema identity: both scopes are structurally stale.
	QueueRefresh(/*bForceReseed*/ true);
}

void SPCGExSketchPanel::SeedRecordScope()
{
	const FPCGExSketchDataLayer* Layer = ResolveLayer(SeededDomain);
	const FPCGExSketchDataRecord* Record = Layer ? Layer->FindRecord(SeededRecordId) : nullptr;

	if (!Record)
	{
		RecordScope.Reset();
		if (RecordDetailsView.IsValid())
		{
			RecordDetailsView->SetStructureData(nullptr);
		}
		return;
	}

	UScriptStruct* RecordStruct = FPCGExSketchDataRecord::StaticStruct();
	RecordScope = MakeShared<FStructOnScope>(RecordStruct);
	RecordStruct->CopyScriptStruct(RecordScope->GetStructMemory(), Record);

	if (RecordDetailsView.IsValid())
	{
		RecordDetailsView->SetStructureData(RecordScope);
	}
}

bool SPCGExSketchPanel::IsHostCustomRowVisible(const FName InRowName, const FName InParentName) const
{
	// Denylist, not an allowlist: the schema-collection customization builds its own custom rows and an
	// allowlist would hide them along with the engine's.
	static const TSet<FName> Rejected = {FName("Transform"), FName("TransformCommon"), FName("Sockets")};
	return !Rejected.Contains(InParentName) && !Rejected.Contains(InRowName);
}

bool SPCGExSketchPanel::IsHostPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
	// Whole subtrees the page owns.
	static const TSet<FName> Subtrees = {FName("SnapProvider"), FName("Decorators")};

	const FName Name = PropertyAndParent.Property.GetFName();
	if (Subtrees.Contains(Name))
	{
		return true;
	}
	for (const FProperty* Parent : PropertyAndParent.ParentProperties)
	{
		if (Parent && Subtrees.Contains(Parent->GetFName()))
		{
			return true;
		}
	}
	return false;
}

void SPCGExSketchPanel::RefreshValidation()
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(Controller);

	if (!Model)
	{
		Validation = FPCGExClusterSketchValidation();
		ValidatedRevision = INDEX_NONE;
		return;
	}

	const int32 Revision = Controller->GetModelRevision();
	if (Revision == ValidatedRevision && !bValidationDirty)
	{
		return;
	}
	bValidationDirty = false;

	ValidatedRevision = Revision;
	Model->Validate(Validation);
}

#pragma endregion

#pragma region Write-back

void SPCGExSketchPanel::OnRecordPropertyChanged(const FPropertyChangedEvent& Event)
{
	// A scope built for another struct (a stale seed across an undo) would copy across a mismatched layout.
	if (!RecordScope.IsValid() || RecordScope->GetStruct() != FPCGExSketchDataRecord::StaticStruct())
	{
		return;
	}

	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	FPCGExSketchDataLayer* Layer = ResolveLayerMutable(SeededDomain);
	// Re-resolved BY ID, never by a cached element pointer: the array may have moved since the seed.
	FPCGExSketchDataRecord* Record = Layer ? Layer->FindRecordMutable(SeededRecordId) : nullptr;
	if (!Controller || !Record)
	{
		return;
	}

	UObject* Host = Controller->GetTarget().GetTransactionObject();
	const FPCGExSketchDataRecord* Edited = reinterpret_cast<const FPCGExSketchDataRecord*>(RecordScope->GetStructMemory());

	TGuardValue<bool> SyncGuard(bIsSyncing, true);
	const FScopedTransaction Transaction(LOCTEXT("EditRecord", "Edit Sketch Data Record"));
	Controller->GetTarget().BeginAuthoring();

	// Id is the key, not payload -- writing it back from a scope taken before a repair would restore a
	// retired identity.
	Record->Label = Edited->Label;
	Record->Values = Edited->Values;

	if (Host)
	{
		Host->PostEditChange();
		PCGExEditor::NotifyObjectChanged(Host);
	}
	Controller->NotifyModelChanged();
	Context.RequestBodyRefresh.ExecuteIfBound();
}

void SPCGExSketchPanel::OnHostPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// bIsSyncing covers the panel's own write-backs, which already re-seed.
	if (bIsSyncing || !Object)
	{
		return;
	}

	// The tier is a struct on the host's model, so a tier edit arrives as a host (or payload) change.
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	if (!Controller || !Controller->GetTarget().OwnsObject(Object))
	{
		return;
	}
	QueueRefresh(/*bForceReseed*/ true);
}

void SPCGExSketchPanel::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (Event.GetEventType() != ETransactionObjectEventType::UndoRedo)
	{
		return;
	}
	// Through the target, not the host object: a construction-script component is never transacted
	// itself (Modify redirects to its actor), and the model lives in a payload subobject.
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	if (!Controller || !Controller->GetTarget().OwnsObject(Object))
	{
		return;
	}
	QueueRefresh(/*bForceReseed*/ true);
}

#pragma endregion

#pragma region Record authoring

void SPCGExSketchPanel::AssignRecord(const uint32 InRecordId)
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	FPCGExClusterSketchModel* Model = EditableModel();
	const EDomain Domain = SelectionDomain();
	if (!Controller || !Model || Domain == EDomain::None)
	{
		return;
	}

	TArray<int32> Indices;
	GatherDomainSelection(Domain, Indices);
	if (Indices.IsEmpty())
	{
		return;
	}

	UObject* Host = Controller->GetTarget().GetTransactionObject();

	{
		TGuardValue<bool> SyncGuard(bIsSyncing, true);
		const FScopedTransaction Transaction(LOCTEXT("AssignRecord", "Assign Sketch Data Record"));
		Controller->GetTarget().BeginAuthoring();

		// Every selected item, deliberately: assigning one record to a multi-selection IS the sharing
		// gesture, and duplicate ids across items are legal by design.
		for (const int32 Index : Indices)
		{
			Domain == EDomain::Vertex ? Model->SetVertexDataId(Index, InRecordId) : Model->SetEdgeDataId(Index, InRecordId);
		}

		if (Host)
		{
			Host->PostEditChange();
			PCGExEditor::NotifyObjectChanged(Host);
		}
		Controller->NotifyModelChanged();
		Context.RequestBodyRefresh.ExecuteIfBound();
	}

	QueueRefresh(/*bForceReseed*/ true);
}

FReply SPCGExSketchPanel::OnMakeUniqueClicked()
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	const EDomain Domain = SelectionDomain();
	FPCGExSketchDataLayer* Layer = ResolveLayerMutable(Domain);
	FPCGExClusterSketchModel* Model = EditableModel();
	if (!Controller || !Model || !Layer)
	{
		return FReply::Handled();
	}

	TArray<int32> Indices;
	GatherDomainSelection(Domain, Indices);
	if (Indices.IsEmpty())
	{
		return FReply::Handled();
	}

	// Copied BY VALUE first: AddRecord reallocates Records, which would dangle a source pointer.
	FName SeedLabel = Domain == EDomain::Vertex ? FName("Vertex Data") : FName("Edge Data");
	TArray<FPCGExPropertyOverrideEntry> SeedOverrides;
	if (const FPCGExSketchDataRecord* Source = Layer->FindRecord(PrimaryDataId()))
	{
		SeedLabel = FName(*(Source->Label.ToString() + TEXT(" Copy")));
		SeedOverrides = Source->Values.Overrides;
	}

	UObject* Host = Controller->GetTarget().GetTransactionObject();

	{
		TGuardValue<bool> SyncGuard(bIsSyncing, true);
		const FScopedTransaction Transaction(LOCTEXT("MakeRecordUnique", "Make Sketch Data Record Unique"));
		Controller->GetTarget().BeginAuthoring();

		const uint32 NewId = Layer->AddRecord(SeedLabel);
		if (!SeedOverrides.IsEmpty())
		{
			if (FPCGExSketchDataRecord* Minted = Layer->FindRecordMutable(NewId))
			{
				Minted->Values.Overrides = MoveTemp(SeedOverrides);
			}
		}

		for (const int32 Index : Indices)
		{
			Domain == EDomain::Vertex ? Model->SetVertexDataId(Index, NewId) : Model->SetEdgeDataId(Index, NewId);
		}

		if (Host)
		{
			Host->PostEditChange();
			PCGExEditor::NotifyObjectChanged(Host);
		}
		Controller->NotifyModelChanged();
		Context.RequestBodyRefresh.ExecuteIfBound();
	}

	QueueRefresh(/*bForceReseed*/ true);
	return FReply::Handled();
}

FReply SPCGExSketchPanel::OnBreakLinkClicked()
{
	AssignRecord(PCGExSketch::InvalidRecordId);
	return FReply::Handled();
}

TSharedRef<SWidget> SPCGExSketchPanel::MakeRecordMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("NoRecord", "<None>"),
		LOCTEXT("NoRecordTip", "Store nothing; every field resolves to the layer schema's own value."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPCGExSketchPanel::AssignRecord, PCGExSketch::InvalidRecordId)));

	if (const FPCGExSketchDataLayer* Layer = ResolveLayer(SelectionDomain()))
	{
		for (const FPCGExSketchDataRecord& Record : Layer->Records)
		{
			MenuBuilder.AddMenuEntry(
				PCGExSketchPanel::RecordDisplayName(Record),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SPCGExSketchPanel::AssignRecord, Record.Id)));
		}
	}

	return MenuBuilder.MakeWidget();
}

#pragma endregion

#pragma region Chrome

bool SPCGExSketchPanel::IsEditingEnabled() const
{
	return EditableModel() != nullptr;
}

void SPCGExSketchPanel::RefreshSketchEntries()
{
	CachedSketches.Reset();
	if (Context.EnumerateSketches)
	{
		Context.EnumerateSketches(CachedSketches);
	}
}

bool SPCGExSketchPanel::HasMultipleSketches() const
{
	return Context.SetActiveSketch && CachedSketches.Num() > 1;
}

FText SPCGExSketchPanel::ActiveSketchLabel() const
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	for (const FPCGExSketchPanelEntry& Entry : CachedSketches)
	{
		if (Entry.Controller == Controller)
		{
			return Entry.Label;
		}
	}
	return LOCTEXT("NoActiveSketch", "No active sketch");
}

FText SPCGExSketchPanel::SelectionSummaryText() const
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	if (!Controller)
	{
		return LOCTEXT("NoSketchBound", "No sketch is bound.");
	}

	const int32 NumVertices = Controller->GetSelectedVertices().Num();
	const int32 NumEdges = Controller->GetSelectedEdges().Num();
	if (NumVertices == 0 && NumEdges == 0)
	{
		return LOCTEXT("NothingSelected", "Nothing selected.");
	}

	return FText::Format(
		LOCTEXT("SelectionCounts", "{0} vertices, {1} edges selected."),
		FText::AsNumber(NumVertices), FText::AsNumber(NumEdges));
}

FText SPCGExSketchPanel::CurrentRecordText() const
{
	const uint32 DataId = PrimaryDataId();
	if (DataId == PCGExSketch::InvalidRecordId)
	{
		return LOCTEXT("NoRecordShort", "<None>");
	}

	const FPCGExSketchDataLayer* Layer = ResolveLayer(SelectionDomain());
	const FPCGExSketchDataRecord* Record = Layer ? Layer->FindRecord(DataId) : nullptr;
	return Record ? PCGExSketchPanel::RecordDisplayName(*Record) : LOCTEXT("MissingRecord", "<missing>");
}

FText SPCGExSketchPanel::SharedCountText() const
{
	const TSharedPtr<FPCGExSketchEditController> Controller = ActiveController();
	const FPCGExClusterSketchModel* Model = PCGExSketchPanel::ReadModel(Controller);
	const EDomain Domain = SelectionDomain();
	const uint32 DataId = PrimaryDataId();
	if (!Model || Domain == EDomain::None || DataId == PCGExSketch::InvalidRecordId)
	{
		return FText::GetEmpty();
	}

	const int32 Shares = Domain == EDomain::Vertex ? Model->CountVertexReferences(DataId) : Model->CountEdgeReferences(DataId);
	return FText::Format(LOCTEXT("SharedByN", "Shared by {0}."), FText::AsNumber(Shares));
}

FText SPCGExSketchPanel::ValidationText() const
{
	TArray<FText> Lines;

	auto AddCount = [&Lines](const int32 InCount, const FText& InLabel)
	{
		if (InCount > 0)
		{
			Lines.Add(FText::Format(INVTEXT("{0}: {1}"), InLabel, FText::AsNumber(InCount)));
		}
	};

	AddCount(Validation.InvalidEdges, LOCTEXT("VInvalidEdges", "Invalid edges"));
	AddCount(Validation.SelfLoops, LOCTEXT("VSelfLoops", "Self-loops"));
	AddCount(Validation.DuplicateEdges, LOCTEXT("VDuplicateEdges", "Duplicate edges"));
	AddCount(Validation.IsolatedVertices, LOCTEXT("VIsolated", "Isolated vertices (dropped at compile)"));
	AddCount(Validation.CollocatedVertices, LOCTEXT("VCollocated", "Collocated vertices"));

	auto AddLayer = [&Lines, &AddCount](const FPCGExClusterSketchValidation::FLayerIssues& InIssues, const FText& InName)
	{
		for (const FName& Rejected : InIssues.InvalidNames)
		{
			Lines.Add(FText::Format(LOCTEXT("VRejectedName", "{0}: \"{1}\" is not a printable attribute name"), InName, FText::FromName(Rejected)));
		}
		AddCount(InIssues.DanglingRefs, FText::Format(LOCTEXT("VDangling", "{0}: references naming no record"), InName));
		AddCount(InIssues.DuplicateRecordIds, FText::Format(LOCTEXT("VDupeIds", "{0}: records sharing an id"), InName));
	};

	AddLayer(Validation.SketchLayerIssues, LOCTEXT("LayerSketch", "Sketch properties"));
	AddLayer(Validation.VertexLayerIssues, LOCTEXT("LayerVertex", "Vertex layer"));
	AddLayer(Validation.EdgeLayerIssues, LOCTEXT("LayerEdge", "Edge layer"));

	if (Lines.IsEmpty())
	{
		return LOCTEXT("ValidationClean", "No issues found.");
	}
	return FText::Join(INVTEXT("\n"), Lines);
}

EVisibility SPCGExSketchPanel::RecordRowVisibility() const
{
	return SelectionDomain() == EDomain::None ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPCGExSketchPanel::SharedCountVisibility() const
{
	return (SelectionDomain() != EDomain::None && RecordScope.IsValid()) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPCGExSketchPanel::DanglingVisibility() const
{
	const uint32 DataId = PrimaryDataId();
	if (DataId == PCGExSketch::InvalidRecordId)
	{
		return EVisibility::Collapsed;
	}

	const FPCGExSketchDataLayer* Layer = ResolveLayer(SelectionDomain());
	return (Layer && Layer->FindRecord(DataId)) ? EVisibility::Collapsed : EVisibility::Visible;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
