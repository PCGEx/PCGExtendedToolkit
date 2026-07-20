// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinition.h"
#include "AssetRegistry/AssetData.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Toolkits/IToolkit.h"

class IToolkitHost;
class UPCGExAssetCollection;

/**
 * Editor-side type info for a registered collection type. Mirrors the runtime FTypeInfo but
 * carries UI/editor metadata: display strings, color, default new-asset name prefix, plus
 * detection and create/update/open closures so the menu utils + asset definition base can
 * operate generically.
 *
 * Populate via PCGEX_REGISTER_COLLECTION_EDITOR_TYPE in each collection's actions cpp.
 */
struct PCGEXCOLLECTIONSEDITOR_API FCollectionEditorTypeInfo
{
	PCGExAssetCollection::FTypeId Id = NAME_None;
	TWeakObjectPtr<UClass> CollectionClass = nullptr;

	// Source asset class used by the content-browser menu to detect candidate assets.
	// For types whose source detection is non-trivial (e.g. Actor BPs whose parent class
	// must be checked), DetectSourceAsset is overridden and SourceAssetClass may be nullptr.
	TWeakObjectPtr<UClass> SourceAssetClass = nullptr;

	FString DefaultAssetNamePrefix;

	FText DisplayName;
	FText AssetDescription;
	FLinearColor AssetColor = FLinearColor::White;

	// Asset-only collections like PCGDataAsset surface in the asset definition but don't
	// participate in the "Create from selection" right-click flow.
	bool bSupportsMenuCreation = true;

	// Tile-picker contribution: the entry-struct property holding the primary asset ref +
	// its picker class filter. Resolved PER ROW on heterogeneous grids; typed editors
	// default to them. Registered via Customize alongside the main registration.
	FName TilePickerPropertyName = NAME_None;
	TWeakObjectPtr<const UClass> TilePickerAllowedClass = nullptr;

	TFunction<bool(const FAssetData&)> DetectSourceAsset;
	TFunction<bool(const FAssetData&)> DetectCollectionAsset;

	TFunction<void(UPCGExAssetCollection*, EToolkitMode::Type, const TSharedPtr<IToolkitHost>&)> OpenEditor;

	TFunction<void(const TArray<FAssetData>&)> CreateCollection;
	TFunction<void(const TArray<TObjectPtr<UPCGExAssetCollection>>&, const TArray<FAssetData>&)> UpdateCollections;

	bool IsValid() const
	{
		return Id != NAME_None && CollectionClass.IsValid();
	}
};

/**
 * Editor-side singleton registry, mirroring the runtime FTypeRegistry's two-phase init.
 */
class PCGEXCOLLECTIONSEDITOR_API FCollectionEditorTypeRegistry
{
public:
	static FCollectionEditorTypeRegistry& Get();

	PCGExAssetCollection::FTypeId Register(FCollectionEditorTypeInfo&& Info);

	const FCollectionEditorTypeInfo* Find(PCGExAssetCollection::FTypeId Id) const;
	const FCollectionEditorTypeInfo* FindByCollectionClass(const UClass* Class) const;

	void GetAll(TArray<const FCollectionEditorTypeInfo*>& OutInfos) const;

	/**
	 * Apply a mutator to a registered entry. Use this when a type needs to override the
	 * defaults the registration macro sets (e.g. Actor needs a custom DetectSourceAsset
	 * that walks Blueprint parent classes).
	 *
	 * Mutator must NOT re-enter the registry -- FRWLock is non-recursive and would deadlock.
	 * Logs a warning if Id isn't registered.
	 */
	void Customize(PCGExAssetCollection::FTypeId Id, TFunctionRef<void(FCollectionEditorTypeInfo&)> Mutator);

	template <typename Func>
	void ForEach(Func&& Callback) const
	{
		FReadScopeLock Lock(RegistryLock);
		for (const auto& Pair : Types)
		{
			Callback(Pair.Value);
		}
	}

	static void AddPendingRegistration(TFunction<void()>&& Func);
	static void ProcessPendingRegistrations();

private:
	FCollectionEditorTypeRegistry() = default;

	static TArray<TFunction<void()>>& GetPendingRegistrations();
	static bool& IsProcessed();

	mutable FRWLock RegistryLock;
	TMap<PCGExAssetCollection::FTypeId, FCollectionEditorTypeInfo> Types;
	TMap<TWeakObjectPtr<UClass>, PCGExAssetCollection::FTypeId> ClassToType;
};

/**
 * Register a collection type with the editor. Invoke once per type from the collection's
 * Actions cpp file. CPP-ONLY -- the macro defines an unnamed-namespace auto-register struct;
 * invoking it from a header would cause ODR collisions across translation units.
 *
 * Pass _AssetNamePrefix / _DisplayName / _Description as bare string literals (the macro
 * applies TEXT()/INVTEXT() itself).
 */
#define PCGEX_REGISTER_COLLECTION_EDITOR_TYPE(_TypeId, _CollectionClass, _SourceAssetClass, _AssetNamePrefix, _Color, _DisplayName, _Description, _EditorToolkit) \
namespace { \
struct FAutoRegisterEditor##_CollectionClass { \
	FAutoRegisterEditor##_CollectionClass() { \
		FCollectionEditorTypeRegistry::AddPendingRegistration([]() { \
			FCollectionEditorTypeInfo Info; \
			Info.Id = PCGExAssetCollection::TypeIds::_TypeId; \
			Info.CollectionClass = _CollectionClass::StaticClass(); \
			Info.SourceAssetClass = _SourceAssetClass::StaticClass(); \
			Info.DefaultAssetNamePrefix = TEXT(_AssetNamePrefix); \
			Info.AssetColor = _Color; \
			Info.DisplayName = INVTEXT(_DisplayName); \
			Info.AssetDescription = INVTEXT(_Description); \
			Info.DetectSourceAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<_SourceAssetClass>(); }; \
			Info.DetectCollectionAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<_CollectionClass>(); }; \
			Info.OpenEditor = [](UPCGExAssetCollection* Collection, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host) { \
				TSharedRef<_EditorToolkit> Editor = MakeShared<_EditorToolkit>(); \
				Editor->InitEditor(Collection, Mode, Host); \
			}; \
			Info.CreateCollection = [](const TArray<FAssetData>& Assets) { \
				PCGExCollectionEditorHelpers::CreateCollectionFromTyped(Assets, _CollectionClass::StaticClass(), TEXT(_AssetNamePrefix)); \
			}; \
			Info.UpdateCollections = &PCGExCollectionEditorHelpers::UpdateCollectionsFromTyped; \
			FCollectionEditorTypeRegistry::Get().Register(MoveTemp(Info)); \
		}); \
	} \
} GAutoRegisterEditor##_CollectionClass; \
}
