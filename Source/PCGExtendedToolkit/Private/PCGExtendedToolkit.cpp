// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExtendedToolkit.h"
#if WITH_EDITOR
#include "Editor/PCGExSubModules.generated.h"
#else
#include "PCGExSubModules.generated.h"
#endif
#include "PCGExVersion.h"

#if WITH_EDITOR
#include "PCGExCoreSettingsCache.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerTypes.h"
#include "UObject/ICookInfo.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Helpers/PCGExCookDependencyProvider.h"
#endif

#include "PCGExGlobalSettings.h"
#include "PCGExModuleInterface.h"
#include "AssetRegistry/AssetData.h"

#define LOCTEXT_NAMESPACE "FPCGExtendedToolkitModule"

void FPCGExtendedToolkitModule::StartupModule()
{
	FModuleManager::Get().LoadModuleChecked<IModuleInterface>("PCG");

#if WITH_EDITOR

	ModifyCookDelegateHandle = UE::Cook::FDelegates::ModifyCook.AddLambda(
		[](UE::Cook::ICookInfo& CookInfo, TArray<UE::Cook::FPackageCookRule>& InOutPackageCookRules)
		{
			IAssetRegistry& Registry = UAssetManager::Get().GetAssetRegistry();
			const FName FolderInstigator(TEXT("PCGExAlwaysCookAssets"));
			const FName ProviderInstigator(TEXT("PCGExCookDependencyProvider"));

			// Thanks @jenkinsgage
			// Force-cook every asset shipped inside this plugin's content folder.
			TArray<FAssetData> PCGExAssets;
			Registry.GetAssetsByPath(TEXT("/PCGExtendedToolkit"), PCGExAssets, true);
			for (const FAssetData& Asset : PCGExAssets)
			{
				InOutPackageCookRules.Emplace(Asset.PackageName, FolderInstigator, UE::Cook::EPackageCookRule::AddToCook);
			}

			// Collect concrete UClasses implementing IPCGExCookDependencyProvider for a single batched registry query.
			TArray<FTopLevelAssetPath> ProviderClassPaths;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					continue;
				}
				if (Class->ImplementsInterface(UPCGExCookDependencyProvider::StaticClass()))
				{
					ProviderClassPaths.Add(Class->GetClassPathName());
				}
			}

			if (ProviderClassPaths.IsEmpty())
			{
				return;
			}

			FARFilter Filter;
			Filter.ClassPaths = MoveTemp(ProviderClassPaths);
			Filter.bRecursiveClasses = false; // leaves already enumerated

			TArray<FAssetData> ProviderAssets;
			Registry.GetAssets(Filter, ProviderAssets);

			for (const FAssetData& Asset : ProviderAssets)
			{
				// Soft refs into /Game won't drag the provider asset into the cook on their own.
				InOutPackageCookRules.Emplace(Asset.PackageName, ProviderInstigator, UE::Cook::EPackageCookRule::AddToCook);

				UObject* Obj;
				{
					FCookLoadScope CookLoadScope(ECookLoadType::EditorOnly);
					Obj = Asset.GetAsset();
				}
				if (!Obj) { continue; }

				IPCGExCookDependencyProvider* Provider = Cast<IPCGExCookDependencyProvider>(Obj);
				if (!Provider) { continue; }

				TSet<FSoftObjectPath> Paths;
				Provider->GetCookDependencyAssetPaths(Paths);

				for (const FSoftObjectPath& Path : Paths)
				{
					const FName PackageName = Path.GetLongPackageFName();
					if (!PackageName.IsNone())
					{
						InOutPackageCookRules.Emplace(PackageName, ProviderInstigator, UE::Cook::EPackageCookRule::AddToCook);
					}
				}
			}
		}
		);

#endif

	const TMap<FString, TArray<FString>>& Dependencies = PCGExSubModules::GetModuleDependencies();
	TSet<FString> Loaded;

	// Recursive loader
	TFunction<void(const FString&)> LoadWithDeps = [&](const FString& ModuleName)
	{
		if (Loaded.Contains(ModuleName))
		{
			return;
		}

		// Load dependencies first
		if (const TArray<FString>* Deps = Dependencies.Find(ModuleName))
		{
			for (const FString& Dep : *Deps)
			{
				LoadWithDeps(Dep);
			}
		}

		// Now load this module
		if (!FModuleManager::Get().IsModuleLoaded(*ModuleName))
		{
			FModuleManager::Get().LoadModule(*ModuleName);
		}
		Loaded.Add(ModuleName);
	};

	// Load all enabled modules (dependencies will be loaded first)
	for (const FString& ModuleName : PCGExSubModules::GetEnabledModules())
	{
		LoadWithDeps(ModuleName);
	}

	GetDefault<UPCGExGlobalSettings>()->UpdateSettingsCaches();

#pragma region Push Pins

#if WITH_EDITOR

	int32 PinIndex = -1;

#pragma region OUT

	PCGEX_EMPLACE_PIN_OUT(OUT_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_OUT("Vtx")
	PCGEX_MAP_PIN_OUT("Unmatched Vtx")

	PCGEX_EMPLACE_PIN_OUT(OUT_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_OUT("Edges")
	PCGEX_MAP_PIN_OUT("Unmatched Edges")

#pragma endregion

#pragma region IN

	PCGEX_EMPLACE_PIN_IN(IN_Vtx, "Point collection formatted for use as cluster vtx.");
	PCGEX_MAP_PIN_IN("Vtx")

	PCGEX_EMPLACE_PIN_IN(IN_Edges, "Point collection formatted for use as cluster edges.");
	PCGEX_MAP_PIN_IN("Edges")

	PCGEX_EMPLACE_PIN_IN(IN_Special, "Attribute set whose values will be used to override a specific internal module.");
	PCGEX_MAP_PIN_IN("Overrides : Blending")
	PCGEX_MAP_PIN_IN("Overrides : Refinement")
	PCGEX_MAP_PIN_IN("Overrides : Graph Builder")
	PCGEX_MAP_PIN_IN("Overrides : Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Start Tangents")
	PCGEX_MAP_PIN_IN("Overrides : End Tangents")
	PCGEX_MAP_PIN_IN("Overrides : Goal Picker")
	PCGEX_MAP_PIN_IN("Overrides : Search")
	PCGEX_MAP_PIN_IN("Overrides : Orient")
	PCGEX_MAP_PIN_IN("Overrides : Smoothing")
	PCGEX_MAP_PIN_IN("Overrides : Packer")

#pragma endregion

#endif

#pragma endregion
}

void FPCGExtendedToolkitModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module

#if WITH_EDITOR
	UE::Cook::FDelegates::ModifyCook.Remove(ModifyCookDelegateHandle);
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExtendedToolkitModule, PCGExtendedToolkit)
