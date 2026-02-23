// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExtendedToolkitEditor.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "PCGExGlobalSettings.h"
#include "PCGExModuleInterface.h"
#include "PCGGraph.h"
#include "PCGModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/PCGSpatialData.h"
#include "DataViz/PCGExSpatialDataVisualization.h"
#include "Details/PCGExDetailsCustomization.h"

#define LOCTEXT_NAMESPACE "FPCGExtendedToolkitEditorModule"

#pragma region CommandListEditorOnlyGraphs
namespace PCGExEditor
{
	static FAutoConsoleCommand CommandListEditorOnlyGraphs(
		TEXT("pcgex.ListEditorOnlyGraphs"),
		TEXT("Finds all graph marked as IsEditorOnly."),
		FConsoleCommandDelegate::CreateLambda(
			[]()
			{
				const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

				FARFilter Filter;
				Filter.ClassPaths.Add(UPCGGraph::StaticClass()->GetClassPathName());
				Filter.bRecursiveClasses = true;

				TArray<FAssetData> AssetDataList;
				AssetRegistry.GetAssets(Filter, AssetDataList);

				if (AssetDataList.IsEmpty())
				{
					UE_LOG(LogTemp, Warning, TEXT("No Editor-only graph found."));
					return;
				}

				const int32 NumTotalGraphs = AssetDataList.Num();
				int32 NumEditorOnlyGraphs = 0;
				for (const FAssetData& AssetData : AssetDataList)
				{
					if (UPCGGraph* Graph = Cast<UPCGGraph>(AssetData.GetAsset()))
					{
						if (Graph->IsEditorOnly())
						{
							NumEditorOnlyGraphs++;
							UE_LOG(LogTemp, Warning, TEXT("%s"), *Graph->GetPathName());
						}
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("Found %d EditorOnly graphs out of %d inspected graphs."), NumEditorOnlyGraphs, NumTotalGraphs);
			}));
}
#pragma endregion

void FPCGExtendedToolkitEditorModule::StartupModule()
{
	FModuleManager::Get().LoadModuleChecked<IModuleInterface>("PCG");

	// I know this is cursed
	FSlateStyleSet& AppStyle = const_cast<FSlateStyleSet&>(static_cast<const FSlateStyleSet&>(FAppStyle::Get()));

	Style = MakeShared<FSlateStyleSet>("PCGExStyleSet");
	Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("PCGExtendedToolkit"))->GetBaseDir() / TEXT("Resources") / TEXT("Icons"));

	{
		const TSharedPtr<FSlateStyleSet> InStyle = Style;

		PCGEX_ADD_CLASS_ICON(PCGExAssetCollection)
		PCGEX_ADD_CLASS_ICON(PCGExMeshCollection)
		PCGEX_ADD_CLASS_ICON(PCGExActorCollection)
		PCGEX_ADD_CLASS_ICON(PCGExLevelCollection)
		PCGEX_ADD_CLASS_ICON(PCGExPCGDataAssetCollection)
		PCGEX_ADD_CLASS_ICON(PCGExCustomGraphSettings)
		PCGEX_ADD_CLASS_ICON(PCGExCustomGraphBuilder)
		PCGEX_ADD_CLASS_ICON(PCGExCustomActorDataPacker)
		PCGEX_ADD_CLASS_ICON(PCGExBeacon)
		PCGEX_ADD_CLASS_ICON(PCGExBitmaskCollection)
		
		PCGEX_ADD_CLASS_ICON(PCGExValencyBondingRules)
		PCGEX_ADD_CLASS_ICON(PCGExValencyConnectorSet)
		PCGEX_ADD_CLASS_ICON(PCGExValencyOrbitalSet)

		PCGEX_REGISTER_PIN_ICON(OUT_Special)
		PCGEX_REGISTER_PIN_ICON(IN_Special)

		PCGEX_REGISTER_PIN_ICON(OUT_RecursionTracker)
		PCGEX_REGISTER_PIN_ICON(IN_RecursionTracker)
	}

	for (IPCGExModuleInterface* Module : IPCGExModuleInterface::RegisteredModules)
	{
		Module->RegisterToEditor(Style);
		Module->RegisterMenuExtensions();
	}

	// Store for late-loading addon modules that self-register
	IPCGExModuleInterface::EditorStyle = Style;

	FSlateStyleRegistry::RegisterSlateStyle(*Style.Get());
	PCGExDetailsCustomization::RegisterDetailsCustomization(Style);

	// Register data visualization
	if (GetDefault<UPCGExGlobalSettings>()->bPersistentDebug)
	{
		FPCGDataVisualizationRegistry& DataVisRegistry = FPCGModule::GetMutablePCGDataVisualizationRegistry();
		DataVisRegistry.RegisterPCGDataVisualization(UPCGSpatialData::StaticClass(), MakeUnique<const IPCGExSpatialDataVisualization>());
	}
}

#undef PCGEX_ADD_ICON

void FPCGExtendedToolkitEditorModule::ShutdownModule()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Style->GetStyleSetName());
	Style.Reset();
}

#undef PCGEX_FOREACH_CUSTOM_DATA_TYPE
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExtendedToolkitEditorModule, PCGExtendedToolkitEditor)
