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

	void UpdateSettingsCaches() const;
};
