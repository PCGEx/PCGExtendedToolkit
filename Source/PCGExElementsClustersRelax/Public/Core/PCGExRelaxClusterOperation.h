// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExInstancedFactory.h"

#include "Clusters/PCGExCluster.h"
#include "PCGExRelaxClusterOperation.generated.h"

/**
 * 
 */
UCLASS(Abstract)
class UPCGExRelaxClusterOperation : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	/** Under the hood updates are operated on a FInt64Vector3. The regular FVector value is multiplied by this factor, and later divided by it. Default value of 100 means .00 precision. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Floating Point Precision"), AdvancedDisplay)
	double Precision = 100;

#if WITH_EDITOR
	/** Value-forward legacy triplet values into their shorthand replacements. No pin renaming: instanced
	 *  operation properties generate no override pins (engine recurses the base class, not the subclass). */
	virtual void ApplyShorthandDeprecation()
	{
	}
#endif

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override
	{
		Super::CopySettingsFrom(Other);
		//if (const UPCGExRelaxClusterOperation* TypedOther = Cast<UPCGExRelaxClusterOperation>(Other))		{		}
	}

	virtual bool PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster)
	{
		Cluster = InCluster;
		return true;
	}

	virtual int32 GetNumSteps()
	{
		return 1;
	}

	virtual EPCGExClusterElement GetStepSource(const int32 InStep)
	{
		return EPCGExClusterElement::Vtx;
	}


	virtual EPCGExClusterElement PrepareNextStep(const int32 InStep)
	{
		if (InStep == 0)
		{
			std::swap(ReadBuffer, WriteBuffer);
		}
		return EPCGExClusterElement::Vtx;
	}

	// Node steps

	virtual void Step1(const PCGExClusters::FNode& Node)
	{
	}

	virtual void Step2(const PCGExClusters::FNode& Node)
	{
	}

	virtual void Step3(const PCGExClusters::FNode& Node)
	{
	}

	// Edge steps

	virtual void Step1(const PCGExGraphs::FEdge& Edge)
	{
	}

	virtual void Step2(const PCGExGraphs::FEdge& Edge)
	{
	}

	virtual void Step3(const PCGExGraphs::FEdge& Edge)
	{
	}

	TSharedPtr<PCGExClusters::FCluster> Cluster;
	TArray<FTransform>* ReadBuffer = nullptr;
	TArray<FTransform>* WriteBuffer = nullptr;


	virtual void Cleanup() override
	{
		Cluster = nullptr;
		ReadBuffer = nullptr;
		WriteBuffer = nullptr;

		Super::Cleanup();
	}

protected:
	TArray<FInt64Vector3> Deltas;

	FVector GetDelta(const int32 Index)
	{
		const FInt64Vector3& P = Deltas[Index];
		return FVector(P.X, P.Y, P.Z) / Precision;
	}

	void AddDelta(const int32 Index, const FVector& Delta)
	{
		FPlatformAtomics::InterlockedAdd(&Deltas[Index].X, FMath::RoundToInt64(Delta.X * Precision));
		FPlatformAtomics::InterlockedAdd(&Deltas[Index].Y, FMath::RoundToInt64(Delta.Y * Precision));
		FPlatformAtomics::InterlockedAdd(&Deltas[Index].Z, FMath::RoundToInt64(Delta.Z * Precision));
	}

	void AddDelta(const int32 AddIndex, const int32 SubtractIndex, const FVector& Delta)
	{
		AddDelta(AddIndex, Delta);
		AddDelta(SubtractIndex, -Delta);
	}
};
