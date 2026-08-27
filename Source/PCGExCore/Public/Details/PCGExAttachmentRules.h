// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "PCGExAttachmentRules.generated.h"

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/transform-details/attachment-rules"))
struct PCGEXCORE_API FPCGExAttachmentRules
{
	GENERATED_BODY()

	FPCGExAttachmentRules() = default;
	explicit FPCGExAttachmentRules(EAttachmentRule InLoc, EAttachmentRule InRot = EAttachmentRule::KeepWorld, EAttachmentRule InScale = EAttachmentRule::KeepWorld);
	~FPCGExAttachmentRules() = default;

	/** The rule to apply to location when attaching */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EAttachmentRule LocationRule = EAttachmentRule::KeepWorld;

	/** The rule to apply to rotation when attaching */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EAttachmentRule RotationRule = EAttachmentRule::KeepWorld;

	/** The rule to apply to scale when attaching */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EAttachmentRule ScaleRule = EAttachmentRule::KeepWorld;

	/** Whether to weld simulated bodies together when attaching */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bWeldSimulatedBodies = false;

	FAttachmentTransformRules GetRules() const;
};
