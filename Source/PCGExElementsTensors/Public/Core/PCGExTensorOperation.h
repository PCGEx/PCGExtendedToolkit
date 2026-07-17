// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTensor.h"
#include "Elements/PCGExExtrudeTensors.h"
#include "Factories/PCGExOperation.h"
#include "UObject/Object.h"

namespace PCGExMath
{
	class IDistances;
}

namespace PCGExDetails
{
	class FDistances;
}

class UPCGExTensorFactoryData;
/**
 * 
 */
class PCGEXELEMENTSTENSORS_API PCGExTensorOperation : public FPCGExOperation
{
protected:
	PCGExFloatLUT PotencyFalloffLUT = nullptr;
	PCGExFloatLUT WeightFalloffLUT = nullptr;

public:
	TSharedPtr<PCGExTensor::FEffectorsArray> Effectors;
	TObjectPtr<const UPCGExTensorFactoryData> Factory = nullptr;
	FPCGExTensorConfigBase BaseConfig;

	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory);

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const;

	virtual bool PrepareForData(const TSharedPtr<PCGExData::FFacade>& InDataFacade);

	template <bool bFast = false>
	const PCGExTensor::FPackedEffector* ComputeFactor(
		const FVector& InPosition,
		const int32 InEffectorIndex,
		PCGExTensor::FEffectorMetrics& OutMetrics) const
	{
		auto* E = Effectors->GetPackedEffectorPtr(InEffectorIndex);

		const double RadiusSquared = E->RadiusSquared;
		const double DistSquared = FVector::DistSquared(InPosition, E->Location);

		if (DistSquared > RadiusSquared)
		{
			return nullptr;
		}

		const double OutFactor = DistSquared / RadiusSquared;
		OutMetrics.Factor = OutFactor;

		if constexpr (bFast)
		{
			OutMetrics.Guide = FVector::ForwardVector;
		}
		else
		{
			OutMetrics.Guide = BaseConfig.LocalGuideCurve.GetValue(OutFactor);
		}

		OutMetrics.Potency = E->Potency * PotencyFalloffLUT->Eval(OutFactor);
		OutMetrics.Weight = E->Weight * WeightFalloffLUT->Eval(OutFactor);

		return E;
	}

	template <bool bFast = false>
	bool ComputeFactor(
		const FVector& InPosition,
		const FPCGSplineStruct& InEffector,
		const double Radius, FTransform& OutTransform,
		PCGExTensor::FEffectorMetrics& OutMetrics) const
	{
		OutTransform = PCGExPaths::Helpers::GetClosestTransform(InEffector, InPosition, true);

		const FVector Scale = OutTransform.GetScale3D();

		const double RadiusSquared = FMath::Square(FVector2D(Scale.Y, Scale.Z).Length() * Radius);
		const double DistSquared = FVector::DistSquared(InPosition, OutTransform.GetLocation());

		if (DistSquared > RadiusSquared)
		{
			return false;
		}

		const double OutFactor = DistSquared / RadiusSquared;
		OutMetrics.Factor = OutFactor;

		if constexpr (bFast)
		{
			OutMetrics.Guide = FVector::ForwardVector;
		}
		else
		{
			OutMetrics.Guide = BaseConfig.LocalGuideCurve.GetValue(OutFactor);
		}

		OutMetrics.Potency = BaseConfig.PotencyValue.Constant * PotencyFalloffLUT->Eval(OutFactor);
		OutMetrics.Weight = BaseConfig.WeightValue.Constant * WeightFalloffLUT->Eval(OutFactor);

		return true;
	}
};

/**
 * 
 */
class PCGEXELEMENTSTENSORS_API PCGExTensorPointOperation : public PCGExTensorOperation
{
public:
	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;
	TSharedPtr<PCGExMath::IDistances> DistanceDetails;
};
