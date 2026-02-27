// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorMacros.h"

#include "AssetThumbnail.h"
#include "PCGExCollectionsEditorSettings.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Core/PCGExAssetCollection.h"
#include "UObject/UnrealType.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/SPCGExCollectionGridView.h"
#include "PCGExProperty.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"

FPCGExAssetCollectionEditor::FPCGExAssetCollectionEditor()
{
	OnHiddenAssetPropertyNamesChanged = UPCGExCollectionsEditorSettings::OnHiddenAssetPropertyNamesChanged.AddRaw(this, &FPCGExAssetCollectionEditor::ForceRefreshTabs);
}

FPCGExAssetCollectionEditor::~FPCGExAssetCollectionEditor()
{
	UPCGExCollectionsEditorSettings::OnHiddenAssetPropertyNamesChanged.Remove(OnHiddenAssetPropertyNamesChanged);
}

bool FPCGExAssetCollectionEditor::IsPropertyUnderEntries(const FPropertyAndParent& PropertyAndParent)
{
	// Check if property IS "Entries"
	if (PropertyAndParent.Property.GetFName() == PCGExAssetCollectionEditor::EntriesName)
		return true;

	// Check all parents for "Entries" OR "PropertyOverrides"
	// PropertyOverrides and its children must always be visible (PCGExPropertiesEditor controls them)
	for (const FProperty* Parent : PropertyAndParent.ParentProperties)
	{
		if (Parent)
		{
			const FName ParentName = Parent->GetFName();
			if (ParentName == PCGExAssetCollectionEditor::EntriesName || ParentName == FName("PropertyOverrides") || ParentName == FName("Overrides"))
				return true;
		}
	}

	// CRITICAL: Properties created via AddExternalStructureProperty (used in PropertyOverrides value widgets)
	// may have incomplete parent chains. Check if ANY parent property's OWNER STRUCT derives from FPCGExPropertyCompiled.
	// This supports full extensibility - custom property types automatically work.

	static UScriptStruct* PropertyCompiledStruct = FPCGExProperty::StaticStruct();

	// Check the property itself's owner struct
	if (UStruct* OwnerStruct = PropertyAndParent.Property.GetOwnerStruct())
	{
		if (UScriptStruct* OwnerScriptStruct = Cast<UScriptStruct>(OwnerStruct))
		{
			if (OwnerScriptStruct->IsChildOf(PropertyCompiledStruct))
				return true;
		}
	}

	// Check all parent properties' owner structs
	// Example: X property's parent is Value property, Value's owner is FPCGExPropertyCompiled_Vector
	for (const FProperty* Parent : PropertyAndParent.ParentProperties)
	{
		if (UStruct* ParentOwnerStruct = Parent->GetOwnerStruct())
		{
			if (UScriptStruct* ParentOwnerScriptStruct = Cast<UScriptStruct>(ParentOwnerStruct))
			{
				if (ParentOwnerScriptStruct->IsChildOf(PropertyCompiledStruct))
					return true;
			}
		}
	}

	return false;
}

void FPCGExAssetCollectionEditor::InitEditor(UPCGExAssetCollection* InCollection, const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	RegisterPropertyNameMapping(GetMutableDefault<UPCGExCollectionsEditorSettings>()->PropertyNamesMap);

	EditedCollection = InCollection;

	const TArray<UObject*> ObjectsToEdit = {InCollection};
	constexpr bool bCreateDefaultStandaloneMenu = true;
	constexpr bool bCreateDefaultToolbar = true;

	CreateTabs(Tabs);

	TSharedRef<FTabManager::FArea> Area =
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal);

	const TSharedRef<FTabManager::FLayout> Layout =
		FTabManager::NewLayout("PCGExAssetCollectionEditor_Layout_v6")
		->AddArea(Area);

	TSharedRef<FTabManager::FStack> MainStack = FTabManager::NewStack();
	// Add tabs in reverse order so grid comes first; list view closed by default
	for (int i = Tabs.Num() - 1; i >= 0; i--)
	{
		const ETabState::Type State = Tabs[i].Id == FName("Assets") ? ETabState::ClosedTab : ETabState::OpenedTab;
		MainStack->AddTab(Tabs[i].Id, State);
	}
	Area->Split(MainStack);

	if (!Tabs.IsEmpty()) { MainStack->SetForegroundTab(Tabs.Last().Id); }

	InitAssetEditor(EToolkitMode::Standalone, InitToolkitHost, FName("PCGExAssetCollectionEditor"), Layout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsToEdit);

	// Toolbar extender
	TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FPCGExAssetCollectionEditor::BuildEditorToolbar)
	);

	AddToolbarExtender(ToolbarExtender);
	RegenerateMenusAndToolbars();
}

UPCGExAssetCollection* FPCGExAssetCollectionEditor::GetEditedCollection() const
{
	return EditedCollection.Get();
}

void FPCGExAssetCollectionEditor::RegisterPropertyNameMapping(TMap<FName, FName>& Mapping)
{
#define PCGEX_DECL_ASSET_FILTER(_NAME, _ID, _LABEL, _TOOLTIP)PCGExAssetCollectionEditor::FilterInfos& _NAME = FilterInfos.Emplace(FName(_ID), PCGExAssetCollectionEditor::FilterInfos(FName(_ID),FTEXT(_LABEL), FTEXT(_TOOLTIP)));

	PCGEX_DECL_ASSET_FILTER(Variations, "AssetEditor.Variations", "Variations", "Show/hide Variations")
	Mapping.Add(FName("VariationMode"), Variations.Id);
	Mapping.Add(FName("Variations"), Variations.Id);

	PCGEX_DECL_ASSET_FILTER(Variations_Offset, "AssetEditor.Variations.Offset", "Var : Offset", "Show/hide Variations : Offset")
	Mapping.Add(FName("VariationOffset"), Variations_Offset.Id);
	PCGEX_DECL_ASSET_FILTER(Variations_Rotation, "AssetEditor.Variations.Rotation", "Var : Rot", "Show/hide Variations : Rotation")
	Mapping.Add(FName("VariationRotation"), Variations_Rotation.Id);
	PCGEX_DECL_ASSET_FILTER(Variations_Scale, "AssetEditor.Variations.Scale", "Var : Scale", "Show/hide Variations : Scale")
	Mapping.Add(FName("VariationScale"), Variations_Scale.Id);

	PCGEX_DECL_ASSET_FILTER(Tags, "AssetEditor.Tags", "Tags", "Show/hide Tags")
	Mapping.Add(FName("Tags"), Tags.Id);

	PCGEX_DECL_ASSET_FILTER(Staging, "AssetEditor.Staging", "Staging", "Show/hide Staging")
	Mapping.Add(FName("Staging"), Staging.Id);

	PCGEX_DECL_ASSET_FILTER(Grammar, "AssetEditor.Grammar", "Grammar", "Show/hide Grammar")
	Mapping.Add(FName("GrammarSource"), Grammar.Id);
	Mapping.Add(FName("AssetGrammar"), Grammar.Id);
	Mapping.Add(FName("SubGrammarMode"), Grammar.Id);
	Mapping.Add(FName("CollectionGrammar"), Grammar.Id);

	PCGEX_DECL_ASSET_FILTER(Properties, "AssetEditor.Properties", "Properties", "Show/hide Property Overrides")
	Mapping.Add(FName("PropertyOverrides"), Properties.Id);

#undef PCGEX_DECL_ASSET_FILTER
}

FReply FPCGExAssetCollectionEditor::FilterShowAll() const
{
	TArray<FName> Keys;
	FilterInfos.GetKeys(Keys);
	UPCGExCollectionsEditorSettings* MutableSettings = GetMutableDefault<UPCGExCollectionsEditorSettings>();
	MutableSettings->ToggleHiddenAssetPropertyName(Keys, false);
	return FReply::Handled();
}

FReply FPCGExAssetCollectionEditor::FilterHideAll() const
{
	TArray<FName> Keys;
	FilterInfos.GetKeys(Keys);
	UPCGExCollectionsEditorSettings* MutableSettings = GetMutableDefault<UPCGExCollectionsEditorSettings>();
	MutableSettings->ToggleHiddenAssetPropertyName(Keys, true);
	return FReply::Handled();
}

FReply FPCGExAssetCollectionEditor::ToggleFilter(const PCGExAssetCollectionEditor::FilterInfos Filter) const
{
	UPCGExCollectionsEditorSettings* MutableSettings = GetMutableDefault<UPCGExCollectionsEditorSettings>();
	MutableSettings->ToggleHiddenAssetPropertyName(Filter.Id, MutableSettings->GetIsPropertyVisible(Filter.Id));
	return FReply::Handled();
}

void FPCGExAssetCollectionEditor::CreateTabs(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs)
{
	// Property editor module
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Details view arguments
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NotifyHook = nullptr;
	DetailsArgs.bAllowMultipleTopLevelObjects = false;

	// Create the details view
	TSharedPtr<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetIsPropertyVisibleDelegate(
		FIsPropertyVisible::CreateLambda(
			[](const FPropertyAndParent& PropertyAndParent)
			{
				return PropertyAndParent.Property.GetFName() != TEXT("Entries");
			}));

	// Set the asset to display
	DetailsView->SetObject(EditedCollection.Get());

	PCGExAssetCollectionEditor::TabInfos& Infos = OutTabs.Emplace_GetRef(FName("Collection"), DetailsView, FName("Collection Settings"));
	Infos.Icon = TEXT("Settings");

	CreateEntriesTab(OutTabs);
	CreateGridTab(OutTabs);
}

void FPCGExAssetCollectionEditor::CreateEntriesTab(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs)
{
	// Property editor module
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Details view arguments
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NotifyHook = nullptr;
	DetailsArgs.bAllowMultipleTopLevelObjects = false;

	// Create the details view
	TSharedPtr<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetIsPropertyVisibleDelegate(
		FIsPropertyVisible::CreateStatic(&FPCGExAssetCollectionEditor::IsPropertyUnderEntries));

	// Set the asset to display
	DetailsView->SetObject(EditedCollection.Get());
	PCGExAssetCollectionEditor::TabInfos& Infos = OutTabs.Emplace_GetRef(FName("Assets"), DetailsView);
	Infos.Icon = TEXT("Entries");

	FToolBarBuilder HeaderToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	HeaderToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetHeaderToolbar(HeaderToolbarBuilder);
	Infos.Header = HeaderToolbarBuilder.MakeWidget();

	FToolBarBuilder FooterToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	FooterToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetFooterToolbar(FooterToolbarBuilder);
	Infos.Footer = FooterToolbarBuilder.MakeWidget();
}

void FPCGExAssetCollectionEditor::CreateGridTab(TArray<PCGExAssetCollectionEditor::TabInfos>& OutTabs)
{
	if (!ThumbnailPool.IsValid())
	{
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
	}

	SAssignNew(GridView, SPCGExCollectionGridView)
		.Collection(EditedCollection.Get())
		.ThumbnailPool(ThumbnailPool)
		.OnGetPickerWidget(FOnGetTilePickerWidget::CreateSP(this, &FPCGExAssetCollectionEditor::BuildTilePickerWidget))
		.TileSize(128.f);

	PCGExAssetCollectionEditor::TabInfos& Infos = OutTabs.Emplace_GetRef(FName("Grid"), GridView, FName("Grid View"));
	Infos.Icon = TEXT("Entries");
	Infos.bIsDetailsView = false;

	// Reuse the same header toolbar as the Assets tab
	FToolBarBuilder HeaderToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	HeaderToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetHeaderToolbar(HeaderToolbarBuilder);
	Infos.Header = HeaderToolbarBuilder.MakeWidget();

	// Reuse the same footer toolbar (filter buttons)
	FToolBarBuilder FooterToolbarBuilder(GetToolkitCommands(), FMultiBoxCustomization::None);
	FooterToolbarBuilder.SetStyle(&FAppStyle::Get(), FName("Toolbar"));
	BuildAssetFooterToolbar(FooterToolbarBuilder);
	Infos.Footer = FooterToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> FPCGExAssetCollectionEditor::BuildTilePickerWidget(
	TWeakObjectPtr<UPCGExAssetCollection> InCollection,
	int32 EntryIndex,
	FSimpleDelegate OnAssetChanged)
{
	TWeakObjectPtr<UPCGExAssetCollection> WeakColl = InCollection;
	const int32 Idx = EntryIndex;

	// Resolve property metadata once — the struct type doesn't change at runtime
	const FName PickerPropName = GetTilePickerPropertyName();
	const UClass* AllowedClass = GetTilePickerAllowedClass();

	// Resolve SubCollection property class from reflection
	const UClass* SubCollectionClass = nullptr;
	if (UPCGExAssetCollection* Coll = WeakColl.Get())
	{
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
		if (ArrayProp)
		{
			FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
			if (InnerProp && InnerProp->Struct)
			{
				if (const FObjectPropertyBase* SubProp = CastField<FObjectPropertyBase>(InnerProp->Struct->FindPropertyByName(FName("SubCollection"))))
				{
					SubCollectionClass = SubProp->PropertyClass;
				}
			}
		}
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// SubCollection picker (visible when bIsSubCollection is true)
	Box->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([WeakColl, Idx]()
			{
				const UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll) { return EVisibility::Collapsed; }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				return (Result.IsValid() && Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(SubCollectionClass)
				.ObjectPath_Lambda([WeakColl, Idx]() -> FString
				{
					const UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return FString(); }
					const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
					if (!Result.IsValid()) { return FString(); }
					const UPCGExAssetCollection* SubColl = Result.Entry->GetSubCollectionPtr();
					return SubColl ? SubColl->GetPathName() : FString();
				})
				.OnObjectChanged_Lambda([WeakColl, Idx, OnAssetChanged](const FAssetData& AssetData)
				{
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }
					FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
					if (!Entry) { return; }
					FScopedTransaction Transaction(INVTEXT("Set SubCollection"));
					Coll->Modify();
					// Write the InternalSubCollection via the base class pointer
					Entry->InternalSubCollection = Cast<UPCGExAssetCollection>(AssetData.GetAsset());
					Coll->PostEditChange();
					OnAssetChanged.ExecuteIfBound();
				})
				.DisplayThumbnail(false)
			]
		];

	// Asset picker (visible when bIsSubCollection is false)
	// Detect property type once at construction to choose the right widget
	if (!PickerPropName.IsNone())
	{
		bool bIsClassProperty = false;
		if (UPCGExAssetCollection* Coll = WeakColl.Get())
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
			if (ArrayProp)
			{
				FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
				if (InnerProp && InnerProp->Struct)
				{
					bIsClassProperty = CastField<FSoftClassProperty>(InnerProp->Struct->FindPropertyByName(PickerPropName)) != nullptr;
				}
			}
		}

		if (bIsClassProperty)
		{
			// TSoftClassPtr<T> — use SClassPropertyEntryBox
			Box->AddSlot()
				.AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([WeakColl, Idx]()
					{
						const UPCGExAssetCollection* Coll = WeakColl.Get();
						if (!Coll) { return EVisibility::Collapsed; }
						const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
						return (Result.IsValid() && !Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SClassPropertyEntryBox)
						.MetaClass(AllowedClass)
						.SelectedClass_Lambda([WeakColl, Idx, PickerPropName]() -> const UClass*
						{
							UPCGExAssetCollection* Coll = WeakColl.Get();
							if (!Coll) { return nullptr; }
							FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
							if (!ArrayProp) { return nullptr; }
							FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
							if (!InnerProp || !InnerProp->Struct) { return nullptr; }
							void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
							FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
							if (Idx < 0 || Idx >= ArrayHelper.Num()) { return nullptr; }
							const uint8* EntryPtr = ArrayHelper.GetRawPtr(Idx);
							const FSoftClassProperty* ClassProp = CastField<FSoftClassProperty>(InnerProp->Struct->FindPropertyByName(PickerPropName));
							if (!ClassProp) { return nullptr; }
							const FSoftObjectPtr& SoftRef = *ClassProp->GetPropertyValuePtr_InContainer(EntryPtr);
							return Cast<UClass>(SoftRef.Get());
						})
						.OnSetClass_Lambda([WeakColl, Idx, PickerPropName, OnAssetChanged](const UClass* NewClass)
						{
							UPCGExAssetCollection* Coll = WeakColl.Get();
							if (!Coll) { return; }
							FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
							if (!ArrayProp) { return; }
							FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
							if (!InnerProp || !InnerProp->Struct) { return; }
							void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
							FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
							if (Idx < 0 || Idx >= ArrayHelper.Num()) { return; }
							uint8* EntryPtr = ArrayHelper.GetRawPtr(Idx);
							const FSoftClassProperty* ClassProp = CastField<FSoftClassProperty>(InnerProp->Struct->FindPropertyByName(PickerPropName));
							if (!ClassProp) { return; }

							FScopedTransaction Transaction(INVTEXT("Set Class"));
							Coll->Modify();
							FSoftObjectPtr& SoftRef = *const_cast<FSoftObjectPtr*>(ClassProp->GetPropertyValuePtr_InContainer(EntryPtr));
							SoftRef = NewClass ? FSoftObjectPath(NewClass) : FSoftObjectPath();
							Coll->PostEditChange();
							OnAssetChanged.ExecuteIfBound();
						})
					]
				];
		}
		else
		{
			// TSoftObjectPtr<T> or TObjectPtr<T> — use SObjectPropertyEntryBox
			Box->AddSlot()
				.AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([WeakColl, Idx]()
					{
						const UPCGExAssetCollection* Coll = WeakColl.Get();
						if (!Coll) { return EVisibility::Collapsed; }
						const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
						return (Result.IsValid() && !Result.Entry->bIsSubCollection) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(AllowedClass)
						.ObjectPath_Lambda([WeakColl, Idx, PickerPropName]() -> FString
						{
							UPCGExAssetCollection* Coll = WeakColl.Get();
							if (!Coll) { return FString(); }
							FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
							if (!ArrayProp) { return FString(); }
							FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
							if (!InnerProp || !InnerProp->Struct) { return FString(); }
							void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
							FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
							if (Idx < 0 || Idx >= ArrayHelper.Num()) { return FString(); }

							const uint8* EntryPtr = ArrayHelper.GetRawPtr(Idx);
							const FProperty* Prop = InnerProp->Struct->FindPropertyByName(PickerPropName);
							if (!Prop) { return FString(); }

							// Handle TSoftObjectPtr<T>
							if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
							{
								const FSoftObjectPtr& SoftRef = *SoftProp->GetPropertyValuePtr_InContainer(EntryPtr);
								return SoftRef.ToSoftObjectPath().ToString();
							}
							// Handle TObjectPtr<T>
							if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
							{
								const UObject* Obj = ObjProp->GetObjectPropertyValue_InContainer(EntryPtr);
								return Obj ? Obj->GetPathName() : FString();
							}
							return FString();
						})
						.OnObjectChanged_Lambda([WeakColl, Idx, PickerPropName, OnAssetChanged](const FAssetData& AssetData)
						{
							UPCGExAssetCollection* Coll = WeakColl.Get();
							if (!Coll) { return; }
							FArrayProperty* ArrayProp = CastField<FArrayProperty>(Coll->GetClass()->FindPropertyByName(FName("Entries")));
							if (!ArrayProp) { return; }
							FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
							if (!InnerProp || !InnerProp->Struct) { return; }
							void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
							FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
							if (Idx < 0 || Idx >= ArrayHelper.Num()) { return; }

							uint8* EntryPtr = ArrayHelper.GetRawPtr(Idx);
							const FProperty* Prop = InnerProp->Struct->FindPropertyByName(PickerPropName);
							if (!Prop) { return; }

							FScopedTransaction Transaction(INVTEXT("Set Asset"));
							Coll->Modify();

							if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
							{
								FSoftObjectPtr& SoftRef = *const_cast<FSoftObjectPtr*>(SoftProp->GetPropertyValuePtr_InContainer(EntryPtr));
								SoftRef = AssetData.GetSoftObjectPath();
							}
							else if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
							{
								ObjProp->SetObjectPropertyValue_InContainer(EntryPtr, AssetData.GetAsset());
							}

							Coll->PostEditChange();
							OnAssetChanged.ExecuteIfBound();
						})
						.DisplayThumbnail(false)
					]
				];
		}
	}

	return Box;
}

#define PCGEX_SLATE_ICON(_NAME) FSlateIcon(FAppStyle::GetAppStyleSetName(), "PCGEx.ActionIcon."#_NAME)
#define PCGEX_CURRENT_COLLECTION if (UPCGExAssetCollection* Collection = EditedCollection.Get())
#define PCGEX_SECTION_HEADER(_LABEL) \
ToolbarBuilder.AddWidget(\
SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(FMargin(8, 0))[\
SNew(STextBlock).Text(INVTEXT(_LABEL)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.8)))\
.Justification(ETextJustify::Center)]);

void FPCGExAssetCollectionEditor::BuildEditorToolbar(FToolBarBuilder& ToolbarBuilder)
{
#pragma region Staging

	ToolbarBuilder.BeginSection("StagingSection");
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						PCGEX_CURRENT_COLLECTION { Collection->EDITOR_RebuildStagingData(); }
					})
			),
			NAME_None,
			FText::FromString(TEXT("Rebuild")),
			INVTEXT("Rebuild Staging for this asset collection."),
			PCGEX_SLATE_ICON(RebuildStaging)
		);

		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						PCGEX_CURRENT_COLLECTION { Collection->EDITOR_RebuildStagingData_Recursive(); }
					})
			),
			NAME_None,
			FText::GetEmpty(),
			INVTEXT("Rebuild staging recursively (this and all subcollections)."),
			PCGEX_SLATE_ICON(RebuildStagingRecursive)
		);

		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						PCGEX_CURRENT_COLLECTION { Collection->EDITOR_RebuildStagingData_Project(); }
					})
			),
			NAME_None,
			FText::GetEmpty(),
			INVTEXT("Rebuild staging for the entire project. (Will go through all collection assets)"),
			PCGEX_SLATE_ICON(RebuildStagingProject)
		);
	}
	ToolbarBuilder.EndSection();

#pragma endregion
}

void FPCGExAssetCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
#pragma region Append

	ToolbarBuilder.BeginSection("ToolsSection");
	{
		ToolbarBuilder.AddWidget(
			SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4, 4))
			.ToolTipText(INVTEXT("Add entries\nAdd new entries to this collection."))
			.ButtonContent()
			[
				PCGEX_COMBOBOX_BUTTON_CONTENT("PCGEx.ActionIcon.AddContentBrowserSelection")
			]
			.OnGetMenuContent_Lambda(
				[this]() -> TSharedRef<SWidget>
				{
					TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);
					BuildAddMenuContent(MenuBox);
					return MenuBox;
				})
		);


		ToolbarBuilder.AddWidget(
			SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4, 4))
			.ToolTipText(INVTEXT("Weight tools\nBatch-edit entry weights."))
			.ButtonContent()
			[
				PCGEX_COMBOBOX_BUTTON_CONTENT("PCGEx.ActionIcon.NormalizeWeight")
			]
			.OnGetMenuContent_Lambda(
				[this]() -> TSharedRef<SWidget>
				{
					return
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						  .AutoHeight()
						  .Padding(4)
						[
							SNew(SButton)
							.Text(INVTEXT("Normalize to 100"))
							.OnClicked_Lambda(
								[this]()
								{
									PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::NormalizedWeightToSum(Collection); }
									return FReply::Handled();
								})
							.ToolTipText(INVTEXT("Normalize weight sum to 100"))
						]
						+ SVerticalBox::Slot()
						  .AutoHeight()
						  .Padding(4, 0, 4, 4)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(2, 2))
							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("= i")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::SetWeightIndex(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Set the weight index to the entry index."))
							]
							+ SUniformGridPanel::Slot(1, 0)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("100")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::WeightOne(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Reset all weights to 100"))
							]
							+ SUniformGridPanel::Slot(2, 0)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("+=1")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::PadWeight(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Add 1 to all weights"))
							]
							+ SUniformGridPanel::Slot(0, 1)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("\xD7""2")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::MultWeight(Collection, 2); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Multiply weights by 2"))
							]
							+ SUniformGridPanel::Slot(1, 1)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("\xD7""10")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::MultWeight(Collection, 10); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Multiply weights by 10"))
							]
							+ SUniformGridPanel::Slot(2, 1)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("???")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::WeightRandom(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Assign random weights"))
							]
						];
				})
		);

		ToolbarBuilder.AddWidget(
			SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4, 4))
			.ToolTipText(INVTEXT("Sort tools\nSort entries by weight."))
			.ButtonContent()
			[
				PCGEX_COMBOBOX_BUTTON_CONTENT_TEXT(TEXT("\u2195"), 10)
			]
			.OnGetMenuContent_Lambda(
				[this]() -> TSharedRef<SWidget>
					{
						return
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(2, 2))
							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("\u25B2 Ascending")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::SortByWeightAscending(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Sort collection by ascending weight"))
							]
							+ SUniformGridPanel::Slot(0, 1)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("\u25BC Descending")))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::SortByWeightDescending(Collection); }
										return FReply::Handled();
									})
								.ToolTipText(FText::FromString("Sort collection by descending weight"))
							]
						;
					})
		);
	}
	ToolbarBuilder.EndSection();

#pragma endregion
}

void FPCGExAssetCollectionEditor::BuildAddMenuContent(const TSharedRef<SVerticalBox>& MenuBox)
{
	MenuBox->AddSlot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SButton)
			.Text(INVTEXT("Add Content Browser Selection"))
			.OnClicked_Lambda(
				[this]()
				{
					PCGEX_CURRENT_COLLECTION { PCGExCollectionEditorUtils::AddBrowserSelection(Collection); }
					return FReply::Handled();
				})
			.ToolTipText(INVTEXT("Append the current content browser selection to this collection."))
		];
}

void FPCGExAssetCollectionEditor::BuildAssetFooterToolbar(FToolBarBuilder& ToolbarBuilder)
{
#pragma region Filters

	ToolbarBuilder.BeginSection("FilterSection");
	{
		PCGEX_SECTION_HEADER("Filters")

		TSharedRef<SUniformGridPanel> Grid =
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(2, 2));

		// Show all
		Grid->AddSlot(0, 0)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Show all")))
			.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
			.OnClicked_Raw(this, &FPCGExAssetCollectionEditor::FilterShowAll)
			.ToolTipText(FText::FromString(TEXT("Turns all filter off and show all properties.")))
		];

		// Hide all
		Grid->AddSlot(0, 1)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Hide all")))
			.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
			.OnClicked_Raw(this, &FPCGExAssetCollectionEditor::FilterHideAll)
			.ToolTipText(FText::FromString(TEXT("Turns all filter on and hide all properties.")))
		];

		int32 Index = 2;
		for (const TPair<FName, PCGExAssetCollectionEditor::FilterInfos>& Infos : FilterInfos)
		{
			const PCGExAssetCollectionEditor::FilterInfos& Filter = Infos.Value;

			Grid->AddSlot(Index / 2, Index % 2)
			[
				SNew(SButton)
				.OnClicked_Raw(this, &FPCGExAssetCollectionEditor::ToggleFilter, Filter)
				.ButtonColorAndOpacity_Lambda(
					[Filter]
					{
						return GetMutableDefault<UPCGExCollectionsEditorSettings>()->GetIsPropertyVisible(Filter.Id) ? FLinearColor(0.005, 0.005, 0.005, 0.5) : FLinearColor::Transparent;
					})
				.ToolTipText(Filter.ToolTip)
				[
					SNew(STextBlock)
					.Text(Filter.Label)
					.StrikeBrush_Lambda(
						[Filter]()
						{
							const bool bVisible = GetMutableDefault<UPCGExCollectionsEditorSettings>()->GetIsPropertyVisible(Filter.Id);
							return bVisible ? nullptr : FAppStyle::GetBrush("Common.StrikeThrough");
						})
				]
			];

			Index++;
		}

		ToolbarBuilder.AddWidget(Grid);
	}
	ToolbarBuilder.EndSection();

#pragma endregion
}

#undef PCGEX_SLATE_ICON
#undef PCGEX_CURRENT_COLLECTION
#undef PCGEX_SECTION_HEADER

void FPCGExAssetCollectionEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->SetCanDoDragOperation(false);

	for (PCGExAssetCollectionEditor::TabInfos& Tab : Tabs)
	{
		// Register tab spawner with our layout Id
		FTabSpawnerEntry& Entry =
			InTabManager->RegisterTabSpawner(
				            Tab.Id,
				            FOnSpawnTab::CreateLambda(
					            [Tab](const FSpawnTabArgs& Args)
					            {
						            return SNew(SDockTab)
							            .TabRole(Tab.Role)
							            .CanEverClose(false)
							            [
								            SNew(SVerticalBox)
								            + SVerticalBox::Slot()
								            .AutoHeight()
								            [
									            Tab.Header.IsValid() ? Tab.Header.ToSharedRef() : SNullWidget::NullWidget
								            ]
								            + SVerticalBox::Slot()
								            .FillHeight(1.f)
								            [
									            Tab.View.ToSharedRef()
								            ]
								            + SVerticalBox::Slot()
								            .AutoHeight()
								            [
									            Tab.Footer.IsValid() ? Tab.Footer.ToSharedRef() : SNullWidget::NullWidget
								            ]
							            ];
					            })
			            )
			            .SetDisplayName(FText::FromName(Tab.Label));

		Tab.WeakView = Tab.View;

		// Release shared ptr otherwise editor won't close
		Tab.View = nullptr;
		Tab.Header = nullptr;
		Tab.Footer = nullptr;

		if (!Tab.Icon.IsEmpty())
		{
			FString Icon = TEXT("PCGEx.ActionIcon.");
			Icon.Append(Tab.Icon);
			Entry.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), FName(Icon)));
		}
	}

	if (!Tabs.IsEmpty()) { InTabManager->SetMainTab(Tabs[0].Id); }

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FPCGExAssetCollectionEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	for (const PCGExAssetCollectionEditor::TabInfos& Tab : Tabs)
	{
		InTabManager->UnregisterTabSpawner(Tab.Id);
	}
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FPCGExAssetCollectionEditor::ForceRefreshTabs()
{
	for (const PCGExAssetCollectionEditor::TabInfos& Tab : Tabs)
	{
		if (!Tab.bIsDetailsView) { continue; }
		if (TSharedPtr<IDetailsView> DetailsView = StaticCastSharedPtr<IDetailsView>(Tab.WeakView.Pin()))
		{
			DetailsView->ForceRefresh();
		}
	}

	// Refresh grid view detail panel (responds to filter changes)
	if (GridView.IsValid())
	{
		GridView->RefreshDetailPanel();
	}
}
