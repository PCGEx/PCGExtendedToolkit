// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "UObject/Object.h"

#include "PCGExOctree.h"
#include "Curves/CurveVector.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExSettingsMacros.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathAxis.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExTensor.generated.h"

struct FPCGExContext;
class UPCGSettings;
class UPCGNode;
class UPCGExTensorPointFactoryData;
class UPCGExTensorFactoryData;

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExTensor
{
	struct FTensorSample;
}

UENUM()
enum class EPCGExTensorSamplingMode : uint8
{
	Weighted       = 0 UMETA(DisplayName = "Weighted", ToolTip="Compute a weighted average of the sampled tensors"),
	OrderedInPlace = 1 UMETA(DisplayName = "Ordered (in place)", ToolTip="Applies tensor one after another in order, using the same original position"),
	OrderedMutated = 2 UMETA(DisplayName = "Ordered (mutated)", ToolTip="Applies tensor & update sampling position one after another in order"),
};

UENUM()
enum class EPCGExEffectorFlattenMode : uint8
{
	Weighted         = 0 UMETA(DisplayName = "Weighted", ToolTip="Compute a weighted average of the sampled effectors"),
	Closest          = 1 UMETA(DisplayName = "Closest", ToolTip="Uses the closest effector only"),
	StrongestWeight  = 2 UMETA(DisplayName = "Strongest (Weight)", ToolTip="Uses the effector with the highest weight only"),
	StrongestPotency = 3 UMETA(DisplayName = "Strongest (Potency)", ToolTip="Uses the effector with the highest potency only"),
};

UENUM()
enum class EPCGExEffectorInfluenceShape : uint8
{
	Box    = 0 UMETA(DisplayName = "Box", Tooltip="Point' bounds"),
	Sphere = 1 UMETA(DisplayName = "Sphere", Tooltip="Sphere which radius is defined by the bounds' extents size"),
};

UENUM()
enum class EPCGExTensorStopConditionHandling : uint8
{
	Exclude = 0 UMETA(DisplayName = "Exclude", Tooltip="Ignore the stopping sample and don't add it to the path."),
	Include = 1 UMETA(DisplayName = "Include", Tooltip="Include the stopping sample to the path.")
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSTENSORS_API FPCGExTensorSamplingMutationsDetails
{
	GENERATED_BODY()

	FPCGExTensorSamplingMutationsDetails()
	{
	}

	virtual ~FPCGExTensorSamplingMutationsDetails()
	{
	}

	/** If enabled, sample will be mirrored. Computed before bidirectional. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bInvert = false;

	/** If enabled, perform a dot product with the direction of the input transform and the resuting sample. If that dot product is < 0, the sampled direction and size is reversed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bBidirectional = false;

	/** Local axis from input transform used to test if the sampled direction should be inverted */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName = " └─ Reference Axis", EditCondition="bBidirectional", EditConditionHides))
	EPCGExAxis BidirectionalAxisReference = EPCGExAxis::Forward;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bScaleDirectionAndSize = false;

	/** If enabled, applies a scale factor to the direction and size. This is handy for "flattening" tensors (scale by 1,1,0) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="bScaleDirectionAndSize"))
	FVector Scale = FVector::OneVector;

	PCGExTensor::FTensorSample Mutate(const FTransform& InProbe, PCGExTensor::FTensorSample InSample) const;
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSTENSORS_API FPCGExTensorConfigBase
{
	GENERATED_BODY()

	explicit FPCGExTensorConfigBase(const bool SupportAttributes = true, const bool SupportMutations = true);

	virtual ~FPCGExTensorConfigBase() = default;

	UPROPERTY()
	bool bSupportAttributes = true;

	UPROPERTY()
	bool bSupportMutations = false;

	/** Base weight multiplier for this tensor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayPriority=-1))
	double TensorWeight = 1;

	/** How individual effectors on that tensor are composited */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayPriority=-1))
	EPCGExEffectorFlattenMode Blending = EPCGExEffectorFlattenMode::Weighted;

	// Guide falloff

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Guide", meta=(PCG_NotOverridable))
	bool bUseLocalGuideCurve = true;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Per-point Guide curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Guide", meta = (PCG_NotOverridable, DisplayName="Guide Curve", EditCondition = "bUseLocalGuideCurve", EditConditionHides))
	FRuntimeVectorCurve LocalGuideCurve;

	/** Per-point Weight falloff curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Guide", meta=(PCG_Overridable, DisplayName="Guide Curve", EditCondition="!bUseLocalGuideCurve", EditConditionHides))
	TSoftObjectPtr<UCurveVector> GuideCurve;

	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Guide", meta=(PCG_NotOverridable))
	//FPCGExCurveLookupDetails GuideCurveLookup;

	//PCGExFloatLUT GuideCurveObj = nullptr;

	// Potency Falloff

	/** Per-point Potency. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_Overridable, DisplayName="Potency", DisplayPriority=-1))
	FPCGExInputShorthandSelectorDouble PotencyValue = FPCGExInputShorthandSelectorDouble(FName("$Density"), 1.0, true);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType PotencyInput_DEPRECATED = EPCGExInputValueType::Attribute;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector PotencyAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Potency_DEPRECATED = 1;

#pragma endregion

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_NotOverridable, DisplayPriority=-1))
	bool bUseLocalPotencyFalloffCurve = true;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Per-point Potency falloff curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta = (PCG_NotOverridable, DisplayName="Potency Falloff Curve", EditCondition = "bUseLocalPotencyFalloffCurve", EditConditionHides, DisplayPriority=-1))
	FRuntimeFloatCurve LocalPotencyFalloffCurve;

	/** Per-point Potency falloff curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_Overridable, DisplayName="Potency Falloff Curve", EditCondition="!bUseLocalPotencyFalloffCurve", EditConditionHides, DisplayPriority=-1))
	TSoftObjectPtr<UCurveFloat> PotencyFalloffCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails PotencyFalloffCurveLookup;

	PCGExFloatLUT PotencyFalloffLUT = nullptr;

	/** A multiplier applied to Potency after it's computed. Makes it easy to scale entire tensors up or down, or invert their influence altogether. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_Overridable, DisplayName="Potency Scale", DisplayPriority=-1))
	double PotencyScale = 1;

	// Weight Falloff

	/** Per-point internal Weight. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, DisplayName="Weight", DisplayPriority=-1))
	FPCGExInputShorthandSelectorDoubleAbs WeightValue = FPCGExInputShorthandSelectorDoubleAbs(FName("Steepness"), 1.0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType WeightInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector WeightAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Weight_DEPRECATED = 1;

#pragma endregion

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable, DisplayPriority=-1))
	bool bUseLocalWeightFalloffCurve = true;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Per-point Weight falloff curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta = (PCG_NotOverridable, DisplayName="Weight Falloff Curve", EditCondition = "bUseLocalWeightFalloffCurve", EditConditionHides, DisplayPriority=-1))
	FRuntimeFloatCurve LocalWeightFalloffCurve;

	/** Per-point Weight falloff curve sampled using distance to effector origin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, DisplayName="Weight Falloff Curve", EditCondition="!bUseLocalWeightFalloffCurve", EditConditionHides, DisplayPriority=-1))
	TSoftObjectPtr<UCurveFloat> WeightFalloffCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails WeightFalloffCurveLookup;

	PCGExFloatLUT WeightFalloffLUT = nullptr;

	/** How should overlapping effector influence be flattened (not implemented yet)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Potency", meta=(PCG_NotOverridable, DisplayPriority=-1))
	EPCGExEffectorFlattenMode EffectorFlattenMode = EPCGExEffectorFlattenMode::Weighted;

	/** Tensor mutations settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Sampling Mutations", EditCondition="bSupportMutations", EditConditionHides, HideEditConditionToggle))
	FPCGExTensorSamplingMutationsDetails Mutations;

	virtual void Init(FPCGExContext* InContext);

#if WITH_EDITOR
	virtual void ApplyDeprecation();
	virtual void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

namespace PCGExTensor
{
	const FName OutputTensorLabel = TEXT("Tensor");
	const FName SourceTensorsLabel = TEXT("Tensors");
	const FName SourceEffectorsLabel = TEXT("Effectors");
	const FName SourceTensorConfigSourceLabel = TEXT("Parent Tensor");

	struct FPackedEffector
	{
		FVector Location = FVector::ZeroVector;
		float RadiusSquared = 1;
		float Potency = 1;
		float Weight = 1;
	};

	class FEffectorsArray : public TSharedFromThis<FEffectorsArray>
	{
	protected:
		TArray<FPackedEffector> PackedEffectors;
		TArray<FQuat> Rotations;

		TSharedPtr<PCGExOctree::FItemOctree> Octree;

	public:
		FEffectorsArray() = default;
		virtual ~FEffectorsArray() = default;

		virtual bool Init(FPCGExContext* InContext, const UPCGExTensorPointFactoryData* InFactory);

	protected:
		virtual void PrepareSinglePoint(const int32 Index, const FTransform& InTransform, FPackedEffector& OutPackedEffector);

	public:
		FORCEINLINE const PCGExOctree::FItemOctree* GetOctree() const
		{
			return Octree.Get();
		}

		FORCEINLINE const FPackedEffector& GetPackedEffector(const int32 Index) const
		{
			return PackedEffectors[Index];
		}

		FORCEINLINE const FPackedEffector* GetPackedEffectorPtr(const int32 Index) const
		{
			return (PackedEffectors.GetData() + Index);
		}

		FORCEINLINE const FQuat& GetRotation(const int32 Index) const
		{
			return Rotations[Index];
		}
	};

	struct FTensorSample
	{
		FVector DirectionAndSize = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		int32 Effectors = 0; // number of things that affected this sample
		double Weight = 0;   // total weights applied to this sample

		FTensorSample() = default;

		FTensorSample(const FVector& InDirectionAndSize, const FQuat& InRotation, const int32 InEffectors, const double InWeight);

		~FTensorSample() = default;

		FTensorSample operator+(const FTensorSample& Other) const;
		FTensorSample& operator+=(const FTensorSample& Other);
		FTensorSample operator*(const double Factor) const;
		FTensorSample& operator*=(const double Factor);
		FTensorSample operator/(const double Factor) const;
		FTensorSample& operator/=(const double Factor);

		void Transform(FTransform& InTransform, double InWeight = 1) const;
		FTransform GetTransformed(const FTransform& InTransform, double InWeight = 1) const;
	};

	struct FEffectorMetrics
	{
		double Distance = 0;
		double Factor = 0;
		double Potency = 0;
		double Weight = 0;
		FVector Guide = FVector::ForwardVector;

		FEffectorMetrics() = default;
		~FEffectorMetrics() = default;
	};

	struct FEffectorSample
	{
		FVector Direction = FVector::ZeroVector; // effector direction
		double Potency = 0;                      // i.e, length
		double Weight = 0;                       // weight of this sample

		FEffectorSample() = default;

		FEffectorSample(const FVector& InDirection, const double InPotency, const double InWeight);

		~FEffectorSample() = default;
	};

	struct FEffectorSamples
	{
		FTensorSample TensorSample = FTensorSample();
		TArray<FEffectorSample, TInlineAllocator<8>> Samples;

		double TotalPotency = 0;

		FEffectorSamples() = default;
		~FEffectorSamples() = default;

		FEffectorSample& Emplace_GetRef(const FVector& InDirection, const double InPotency, const double InWeight);

		template <EPCGExEffectorFlattenMode Mode = EPCGExEffectorFlattenMode::Weighted>
		FTensorSample Flatten(double InWeight)
		{
			TensorSample.Effectors = Samples.Num();

			if constexpr (Mode == EPCGExEffectorFlattenMode::Closest)
			{
				FVector DirectionAndSize = FVector::ZeroVector;

				for (const FEffectorSample& EffectorSample : Samples)
				{
					const double S = (EffectorSample.Potency * (EffectorSample.Weight / TensorSample.Weight));
					DirectionAndSize += EffectorSample.Direction * S;
				}

				TensorSample.DirectionAndSize = DirectionAndSize;
				TensorSample.Rotation = FRotationMatrix::MakeFromX(DirectionAndSize.GetSafeNormal()).ToQuat();
				TensorSample.Weight = InWeight;
			}
			else
			{
				// defaults to weighted 
				FVector DirectionAndSize = FVector::ZeroVector;

				for (const FEffectorSample& EffectorSample : Samples)
				{
					const double S = (EffectorSample.Potency * (EffectorSample.Weight / TensorSample.Weight));
					DirectionAndSize += EffectorSample.Direction * S;
				}

				TensorSample.DirectionAndSize = DirectionAndSize;
				TensorSample.Rotation = FRotationMatrix::MakeFromX(DirectionAndSize.GetSafeNormal()).ToQuat();
				TensorSample.Weight = InWeight;
			}

			return TensorSample;
		}
	};
}
