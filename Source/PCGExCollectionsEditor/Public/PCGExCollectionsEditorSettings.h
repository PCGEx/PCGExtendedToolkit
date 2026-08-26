// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "Layout/Visibility.h"


#include "PCGExCollectionsEditorSettings.generated.h"

UCLASS(Config=EditorUser, DefaultConfig, meta=(DisplayName="PCGEx | Collections", Description="PCGEx Editor Collections Settings"))
class PCGEXCOLLECTIONSEDITOR_API UPCGExCollectionsEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;

	virtual FName GetContainerName() const override
	{
		return "Editor";
	}

	virtual FName GetCategoryName() const override
	{
		return "Plugins";
	}

	virtual FName GetSectionName() const override
	{
		return "PCGEx | Collections";
	}

	static FSimpleMulticastDelegate OnHiddenAssetPropertyNamesChanged;

	/** Map a property internal name to a property name, so multiple property visibility can be toggled by a single flag */
	TMap<FName, FName> PropertyNamesMap;

	UPROPERTY(Config)
	TSet<FName> HiddenPropertyNames;

	/** Grid tab splitter fractions, written on drag end. Config-only, not a Project Settings knob. */
	UPROPERTY(Config)
	float GridPaneSplit = 0.65f;

	UPROPERTY(Config)
	float DetailPaneSplit = 0.35f;

	/** When a collection tile references a Blueprint class that isn't loaded yet, kick an async load so the picker shows the class name instead of "None". Disable if this causes editor stalls on open. */
	UPROPERTY(EditAnywhere, config, Category = Settings)
	bool bAsyncLoadPickerClasses = true;

	/** Auto-rebuild an entry's staging when its referenced asset is saved. Per-entry scope (only the affected entry rebuilds). Disable if you prefer manual rebuilds only. */
	UPROPERTY(EditAnywhere, config, Category = Settings)
	bool bAutoRebuildOnStale = true;

	/** When a collection loads in the editor, re-stage entries whose source packages' content digest no longer matches the fingerprint recorded at their last rebuild (content, not file dates -- VCS checkouts rewrite mtimes). Catches changes made offline between sessions. Entries with no recorded fingerprint (never rebuilt since fingerprints landed) are skipped until a manual rebuild sets the baseline. */
	UPROPERTY(EditAnywhere, config, Category = Settings)
	bool bRebuildStaleEntriesOnOpen = true;

	void ToggleHiddenAssetPropertyName(const FName PropertyName, const bool bHide);
	void ToggleHiddenAssetPropertyName(const TArray<FName> Properties, const bool bHide);
	EVisibility GetPropertyVisibility(const FName PropertyName) const;

	bool GetIsPropertyVisible(const FName PropertyName) const;

protected:
	/** Internal version tracking. */
	UPROPERTY(EditAnywhere, config, Category = Settings, AdvancedDisplay)
	int64 PCGExDataVersion = -1;
};
