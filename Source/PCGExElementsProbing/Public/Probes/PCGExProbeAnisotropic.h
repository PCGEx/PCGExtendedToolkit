// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"


#include "PCGExProbeAnisotropic.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigAnisotropic : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigAnisotropic()
		: FPCGExProbeConfigBase()
	{
	}

	/** Max angle to search within. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, ClampMax=11.25))
	double MaxAngle = 5;

	/** Transform the direction with the point's */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTransformDirection = true;
};

/**
 * 
 */
class FPCGExProbeAnisotropic : public FPCGExProbeOperation
{
public:
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigAnisotropic Config;

protected:
	double MinDot = 0;
	const FVector Directions[16] = {
		FVector(1, 0, 0),             // 0 degrees (East)
		FVector(0.9239, 0.3827, 0),   // 22.5 degrees
		FVector(0.7071, 0.7071, 0),   // 45 degrees (Northeast)
		FVector(0.3827, 0.9239, 0),   // 67.5 degrees
		FVector(0, 1, 0),             // 90 degrees (North)
		FVector(-0.3827, 0.9239, 0),  // 112.5 degrees
		FVector(-0.7071, 0.7071, 0),  // 135 degrees (Northwest)
		FVector(-0.9239, 0.3827, 0),  // 157.5 degrees
		FVector(-1, 0, 0),            // 180 degrees (West)
		FVector(-0.9239, -0.3827, 0), // 202.5 degrees
		FVector(-0.7071, -0.7071, 0), // 225 degrees (Southwest)
		FVector(-0.3827, -0.9239, 0), // 247.5 degrees
		FVector(0, -1, 0),            // 270 degrees (South)
		FVector(0.3827, -0.9239, 0),  // 292.5 degrees
		FVector(0.7071, -0.7071, 0),  // 315 degrees (Southeast)
		FVector(0.9239, -0.3827, 0)   // 337.5 degrees
	};
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryAnisotropic : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigAnisotropic Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-anisotropic"))
class UPCGExProbeAnisotropicProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeAnisotropic, "Probe : Anisotropic", "Probe in 16 directions over the X/Y axis. It's recommended to use internal projection to get the best results", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigAnisotropic Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
