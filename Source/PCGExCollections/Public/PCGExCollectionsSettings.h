// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "PCGExCollectionsSettings.generated.h"

class UPCGPin;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PCGEx | Collections", Description="Configure PCG Extended Toolkit' Collections module settings"))
class PCGEXCOLLECTIONS_API UPCGExCollectionsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	/** Disable collision on new entries */
	UPROPERTY(EditAnywhere, config, Category = "Collections")
	bool bDisableCollisionByDefault = true;

	/** Default level data exporter class used when no exporter is explicitly assigned.
	 *  If None, falls back to UPCGExDefaultLevelDataExporter. */
	UPROPERTY(EditAnywhere, config, Category = "Defaults", meta=(MetaClass="/Script/PCGExCollections.PCGExLevelDataExporter"))
	FSoftClassPath DefaultLevelExporterClass;

	/** Default actor content filter class used on new collections and exporters.
	 *  If None, falls back to UPCGExDefaultActorContentFilter. */
	UPROPERTY(EditAnywhere, config, Category = "Defaults", meta=(MetaClass="/Script/PCGExCollections.PCGExActorContentFilter"))
	FSoftClassPath DefaultContentFilterClass;

	/** Default bounds evaluator class used on new collections and exporters.
	 *  If None, falls back to UPCGExDefaultBoundsEvaluator. */
	UPROPERTY(EditAnywhere, config, Category = "Defaults", meta=(MetaClass="/Script/PCGExCollections.PCGExBoundsEvaluator"))
	FSoftClassPath DefaultBoundsEvaluatorClass;

	/** Default actor mesh classificator class used on new exporters.
	 *  If None, falls back to UPCGExDefaultActorMeshClassificator. */
	UPROPERTY(EditAnywhere, config, Category = "Defaults", meta=(MetaClass="/Script/PCGExCollections.PCGExActorMeshClassificator"))
	FSoftClassPath DefaultMeshClassificatorClass;

	/** Additional actor class names to unconditionally exclude from all level exports,
	 *  on top of the hardcoded list in UPCGExActorContentFilter::KnownSystemActorClasses.
	 *  Use the C++ class name without the A prefix (e.g. "ChaosDebugDrawActor"). */
	UPROPERTY(EditAnywhere, config, Category = "Defaults")
	TArray<FName> AdditionalSystemActorClasses;

	/** Pushes the settings into the module cache. bLoadClasses false only re-resolves classes already in memory. */
	void UpdateSettingsCaches(bool bLoadClasses = true);

private:
	/** Roots the classes the cache points at: unreferenced Blueprint classes are collectable outside the editor. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UClass>> ResolvedDefaultClasses;
};
