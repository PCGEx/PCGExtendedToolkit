// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Sketch/PCGExClusterSketchModel.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Misc/NotifyHook.h"
#include "ScopedTransaction.h"
#include "Widgets/SCompoundWidget.h"

class FPCGExSketchEditController;
class IDetailsView;
class IStructureDetailsView;
class SWidgetSwitcher;
class SVerticalBox;
struct FPropertyAndParent;
struct FPropertyChangedEvent;
class FStructOnScope;
class FTransactionObjectEvent;

/** One selectable sketch in a multi-sketch host's header picker. */
struct FPCGExSketchPanelEntry
{
	FText Label;
	TSharedPtr<FPCGExSketchEditController> Controller;
};

/**
 * Everything the panel needs from whichever host embeds it. The asset editor and the level mode both
 * fill one of these and get the same panel.
 */
struct FPCGExSketchPanelContext
{
	/** LIVE, never a snapshot: the mode's active binding changes under the panel while it is up. */
	TFunction<TSharedPtr<FPCGExSketchEditController>()> ResolveActiveController;

	/** The asset or component the model belongs to -- what write-backs Modify() and PostEditChange(). */
	TFunction<UObject*()> ResolveSketchObject;

	/** Fired after the panel mutates, for whatever the host owns OUTSIDE the panel (viewport, details).
	 *  Not a rebuild request: UpdatePrimaryModePanel pulls the panel widgets once per spawn, so the
	 *  panel always refreshes itself. */
	FSimpleDelegate RequestBodyRefresh;

	/** Optional, and set together: hosts holding more than one live sketch. Unset hides the picker. */
	TFunction<void(TArray<FPCGExSketchPanelEntry>&)> EnumerateSketches;
	TFunction<void(const TSharedPtr<FPCGExSketchEditController>&)> SetActiveSketch;
};

/**
 * The sketch authoring side panel, shared verbatim by the standalone asset editor and the in-level
 * mode. Deliberately vends TWO widgets rather than one root: the mode host renders a pinned palette
 * area above its own SScrollBox, and the panel's tab strip has to live up there or it scrolls away.
 *
 *   MakeHeader() -> tab strip + global actions + the multi-sketch picker (pinned).
 *   MakeBody()   -> this widget, the page switcher (scrolled BY THE HOST -- never nest an SScrollBox).
 *
 * Every mutation the panel makes is one FScopedTransaction on the target's transaction object, and
 * ends on FPCGExSketchEditController::NotifyModelChanged like every gesture does.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API SPCGExSketchPanel : public SCompoundWidget, public FNotifyHook
{
public:
	/** Declaration order IS the switcher's slot order -- SetPage indexes it by cast. */
	enum class EPage : uint8
	{
		Selection,
		Sketch,
		Options
	};

	SLATE_BEGIN_ARGS(SPCGExSketchPanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FPCGExSketchPanelContext& InContext);
	virtual ~SPCGExSketchPanel() override;

	/** Pinned strip. A host may call this more than once (the mode rebuilds its palette on every
	 *  UpdatePrimaryModePanel); each instance drives this one panel. */
	TSharedRef<SWidget> MakeHeader();

	/** The page switcher -- this widget. The host supplies the scrolling. */
	TSharedRef<SWidget> MakeBody();

	void SetPage(EPage InPage);

	EPage GetPage() const
	{
		return ActivePage;
	}

	//~ Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	//~ End SWidget

	//~ Begin FNotifyHook (the constraint bodies only). A slider drag reaches the model on every tick, inside
	//~ one transaction opened at the first pre-change and closed by the commit.
	// The FEditPropertyChain overloads stay reachable, or Clang's -Woverloaded-virtual fails the build.
	using FNotifyHook::NotifyPreChange;
	using FNotifyHook::NotifyPostChange;
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~ End FNotifyHook

private:
	//~ Resolution
	TSharedPtr<FPCGExSketchEditController> ActiveController() const;
	/** Mutable model of the active controller, or null when there is none / it is read-only. */
	FPCGExClusterSketchModel* EditableModel() const;

	/** Which authored domain the selection addresses. A selection spanning both is deliberately None:
	 *  one record assignment cannot mean two things. */
	enum class EDomain : uint8
	{
		None,
		Vertex,
		Edge
	};

	EDomain SelectionDomain() const;
	/** Selected indices of the resolved domain, ascending -- a TSet iterates by slot, not by pick order,
	 *  so the lowest index is the only deterministic "primary". */
	void GatherDomainSelection(EDomain InDomain, TArray<int32>& OutIndices) const;
	const FPCGExSketchDataLayer* ResolveLayer(EDomain InDomain) const;
	FPCGExSketchDataLayer* ResolveLayerMutable(EDomain InDomain) const;
	uint32 PrimaryDataId() const;

	//~ Refresh
	/** Deferred to the next frame: a refresh triggered from inside a widget's own event handling would
	 *  tear down the widget mid-handler. */
	void QueueRefresh(bool bForceReseed);
	/** OnChanged target; silent while the panel is writing its own edit back. */
	void RequestRefresh();
	/** Re-seeds only when the seeded IDENTITY moved (controller, host, domain, record) unless forced --
	 *  a value-level re-seed would drop whatever the user is mid-typing. */
	void RefreshNow(bool bForceReseed);
	void RebindController();
	void SeedRecordScope();
	void SeedDataScope();
	void RefreshValidation();

	//~ Chrome (bound as attributes; never cached)
	bool IsEditingEnabled() const;
	/** Rebuild CachedSketches. The picker's visibility and label are bound attributes, so they must
	 *  never enumerate (it allocates a label per binding). */
	void RefreshSketchEntries();
	bool HasMultipleSketches() const;
	FText ActiveSketchLabel() const;
	FText SelectionSummaryText() const;
	FText CurrentRecordText() const;
	FText SharedCountText() const;
	FText ValidationText() const;
	EVisibility RecordRowVisibility() const;
	EVisibility SharedCountVisibility() const;
	EVisibility DanglingVisibility() const;

	//~ Write-back (owning FStructOnScope root: the customizations' own owner hooks are skipped, so
	//~ Modify() / PostEditChange() are hand-wired here)
	void OnRecordPropertyChanged(const FPropertyChangedEvent& Event);
	void OnDataPropertyChanged(const FPropertyChangedEvent& Event);
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);
	/** The host's own details view reaches the same schemas; a stale scope would revert them. */
	void OnHostPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	/** Admits only the host's snap-provider and decorator properties, under either host's names. */
	bool IsHostPropertyVisible(const FPropertyAndParent& PropertyAndParent) const;
	/** Drops the component's own Transform/Sockets rows, which are custom rows the property filter misses. */
	bool IsHostCustomRowVisible(FName InRowName, FName InParentName) const;

	//~ Record authoring. By value, not by reference: these are delegate payload targets, and payloads
	//~ deduce the parameter type from the bound value.
	void AssignRecord(uint32 InRecordId);
	void ActivateSketch(TSharedPtr<FPCGExSketchEditController> InController);
	FReply OnMakeUniqueClicked();
	FReply OnBreakLinkClicked();
	TSharedRef<SWidget> MakeRecordMenu();
	TSharedRef<SWidget> MakeSketchMenu();

	//~ Page bodies
	TSharedRef<SWidget> BuildSelectionPage();
	TSharedRef<SWidget> BuildSketchPage();
	TSharedRef<SWidget> BuildOptionsPage();

	/** UDeveloperSettings only broadcasts on edit -- the Settings Editor is what normally persists it,
	 *  so a panel-hosted view has to write the config itself. */
	void OnAuthoringOptionChanged(const FPropertyChangedEvent& Event);

	FPCGExSketchPanelContext Context;
	EPage ActivePage = EPage::Selection;

	TSharedPtr<SWidgetSwitcher> PageSwitcher;

	//~ Constraints, Selection page. "Add Constraint" is the ONE way in (it offers only the types that fit
	//~ the selection and attaches them to it). Each constraint naming a selected element is then shown as
	//~ its own details BODY -- an owning copy rooted at the constraint's concrete struct, so no array
	//~ machinery and no type picker can detach it from its subjects -- written back by id. Ordering is
	//~ the Sketch page's global list.
	TSharedRef<SWidget> MakeAddConstraintMenu();
	void AddConstraintOfType(const UScriptStruct* InType);

	struct FSelectionConstraintEntry
	{
		uint32 Id = 0;
		TSharedPtr<FStructOnScope> Scope;
		TSharedPtr<IStructureDetailsView> View;
	};

	TSharedPtr<SVerticalBox> SelectionConstraintsBox;
	TArray<FSelectionConstraintEntry> SelectionConstraintEntries;
	/** Identity the bodies were seeded from: selection ids + the ids of the constraints shown. A VALUE
	 *  change never rebuilds them (that would drop focus mid-edit); values refresh into the live scopes. */
	uint64 SeededConstraintsKey = 0;
	void SeedSelectionConstraintBodies(bool bForce);
	/** Model -> every body's scope memory, in place; the widgets read it without a rebuild. */
	void RefreshSelectionConstraintValues();
	/** Every body's scope -> model, by id, same-type-guarded. No transaction handling, no notify. */
	void WriteBackSelectionConstraints();
	void OnSelectionConstraintChanged(const FPropertyChangedEvent& Event, uint32 InConstraintId);
	FText ConstraintResidualText(uint32 InConstraintId) const;
	/** Open from the first NotifyPreChange of an edit to its commit. */
	TUniquePtr<FScopedTransaction> ConstraintEditTransaction;

	/** Owning copies. The Records array carries no CPF_Edit flag, so no property handle can reach it --
	 *  a copy fed to an IStructureDetailsView is the only binding that exists. */
	TSharedPtr<IStructureDetailsView> RecordDetailsView;
	TSharedPtr<FStructOnScope> RecordScope;

	/** Rooted at the TIER itself, so its three members render flat. A host-rooted view would bury them
	 *  under the payload/model/data rows that only exist to reach them. */
	TSharedPtr<IStructureDetailsView> DataDetailsView;
	TSharedPtr<FStructOnScope> DataScope;

	/** Rooted at the target's details object, which differs from the host: see GetDetailsObject. */
	TSharedPtr<IDetailsView> HostDetailsView;

	/** Rooted at the authoring settings CDO. Deliberately outside the read-only gate: gesture
	 *  preferences are the user's, not the sketch's. */
	TSharedPtr<IDetailsView> OptionsDetailsView;

	/** Identity the scopes were seeded from -- what RefreshNow compares against. */
	TWeakPtr<FPCGExSketchEditController> SeededController;
	TWeakObjectPtr<UObject> SeededObject;
	/** Tracked separately: creating or deleting an inline sketch moves this while the host stands still. */
	TWeakObjectPtr<UObject> SeededDetailsObject;
	EDomain SeededDomain = EDomain::None;
	uint32 SeededRecordId = 0;

	TWeakPtr<FPCGExSketchEditController> BoundController;
	FDelegateHandle BoundChangedHandle;
	FDelegateHandle TransactedHandle;
	FDelegateHandle PropertyChangedHandle;

	/** The picker's bound attributes read this instead of enumerating the host's bindings per paint. */
	TArray<FPCGExSketchPanelEntry> CachedSketches;

	/** Validation is O(V^2) on free vertices, so it is recomputed on revision moves, never per paint. */
	FPCGExClusterSketchValidation Validation;
	int32 ValidatedRevision = INDEX_NONE;
	/** Undo cannot rewind ModelRevision, so a forced reseed sets this to bypass the revision cache. */
	bool bValidationDirty = false;

	/** Suppresses the OnChanged echo of the panel's own write-back. */
	bool bIsSyncing = false;
	bool bRefreshQueued = false;
	bool bForceReseedQueued = false;
};
