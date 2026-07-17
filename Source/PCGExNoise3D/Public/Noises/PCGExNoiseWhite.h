// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseWhite.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigWhite : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigWhite()
		: FPCGExNoise3DConfigBase()
	{
	}
};

/**
 * White noise - pure random with no spatial correlation
 * Extremely fast, grainy appearance
 */
class PCGEXNOISE3D_API FPCGExNoiseWhite : public FPCGExNoise3DOperation
{
public:
	virtual ~FPCGExNoiseWhite() override = default;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryWhite : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigWhite Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-white"))
class UPCGExNoise3DWhiteProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DWhite, "Noise : White", "White noise - fast, pure random.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigWhite Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
