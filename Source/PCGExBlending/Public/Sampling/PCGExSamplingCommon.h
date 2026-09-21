// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExMetaHelpers.h"

#include "PCGExSamplingCommon.generated.h"

UENUM()
enum class EPCGExRangeType : uint8
{
	FullRange      = 0 UMETA(DisplayName = "Full Range", ToolTip="Remap the declared [Min..Max] range to [0..1]."),
	EffectiveRange = 1 UMETA(DisplayName = "Effective Range", ToolTip="Remap the sampled [Min..Max] range to [0..1]."),
};

UENUM()
enum class EPCGExSurfaceSource : uint8
{
	All             = 0 UMETA(DisplayName = "Any surface", ToolTip="Any surface within range will be tested"),
	ActorReferences = 1 UMETA(DisplayName = "Actor Reference", ToolTip="Only a list of actor surfaces will be included."),
	Primitives      = 2 UMETA(DisplayName = "Primitives", ToolTip="Only the provided primitive components will be tested."),
};


UENUM()
enum class EPCGExSampleMethod : uint8
{
	WithinRange    = 0 UMETA(DisplayName = "All (Within range)", ToolTip="Use RangeMax = 0 to include all targets (where supported)"),
	ClosestTarget  = 1 UMETA(DisplayName = "Closest Target", ToolTip="Picks & process the closest target only"),
	FarthestTarget = 2 UMETA(DisplayName = "Farthest Target", ToolTip="Picks & process the farthest target only"),
	BestCandidate  = 3 UMETA(DisplayName = "Best Candidate", ToolTip="Picks & process the best candidate based on sorting rules"),
};

UENUM()
enum class EPCGExSampleSource : uint8
{
	Source   = 0 UMETA(DisplayName = "In", ToolTip="Read value on main inputs"),
	Target   = 1 UMETA(DisplayName = "Target", ToolTip="Read value on target"),
	Constant = 2 UMETA(DisplayName = "Constant", ToolTip="Read constant", ActionIcon="Constant"),
};

UENUM()
enum class EPCGExAngleRange : uint8
{
	URadians               = 0 UMETA(DisplayName = "Radians (0..+PI)", ToolTip="0..+PI"),
	PIRadians              = 1 UMETA(DisplayName = "Radians (-PI..+PI)", ToolTip="-PI..+PI, signed by winding"),
	TAURadians             = 2 UMETA(DisplayName = "Radians (0..+TAU)", ToolTip="0..TAU"),
	UDegrees               = 3 UMETA(DisplayName = "Degrees (0..+180)", ToolTip="0..+180"),
	PIDegrees              = 4 UMETA(DisplayName = "Degrees (-180..+180)", ToolTip="-180..+180, signed by winding"),
	TAUDegrees             = 5 UMETA(DisplayName = "Degrees (0..+360)", ToolTip="0..+360"),
	NormalizedHalf         = 6 UMETA(DisplayName = "Normalized Half (0..180 -> 0..1)", ToolTip="0..180 -> 0..1"),
	Normalized             = 7 UMETA(DisplayName = "Normalized (0..+360 -> 0..1)", ToolTip="0..+360 -> 0..1"),
	InvertedNormalizedHalf = 8 UMETA(DisplayName = "Inv. Normalized Half (0..180 -> 1..0)", ToolTip="0..180 -> 1..0"),
	InvertedNormalized     = 9 UMETA(DisplayName = "Inv. Normalized (0..+360 -> 1..0)", ToolTip="0..+360 -> 1..0"),
};

UENUM()
enum class EPCGExSampleWeightMode : uint8
{
	Distance      = 0 UMETA(DisplayName = "Distance", ToolTip="Weight is computed using distance to targets"),
	Attribute     = 1 UMETA(DisplayName = "Attribute", ToolTip="Uses a fixed attribute value on the target as weight"),
	AttributeMult = 2 UMETA(DisplayName = "Att x Dist", ToolTip="Uses a fixed attribute value on the target as a multiplier to distance-based weight"),
};

UENUM()
enum class EPCGExInsideWeighting : uint8
{
	Distance = 0 UMETA(DisplayName = "Distance", ToolTip="Weight falls off with the distance to the closest edge, inside or out."),
	Full     = 1 UMETA(DisplayName = "Full", ToolTip="Inside targets always get full weight. Outside targets fall off with distance."),
	Depth    = 2 UMETA(DisplayName = "Depth", ToolTip="Inside targets weigh more the deeper they are, saturating at Depth Range. Outside targets fall off with distance."),
};

/**
 * Inside-aware distance weighting shared by the path samplers. GetWeight yields the unscaled weight
 * the blend union and the weight curve both consume: 1 at the closest edge, 0 at the far end of the
 * resolved range. GetScale is applied after the curve so a scale above 1 is not clamped away.
 */
USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExInsideWeightingDetails
{
	GENERATED_BODY()

	/** How targets lying inside a closed shape are weighted relative to those near its edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInsideWeighting Mode = EPCGExInsideWeighting::Distance;

	/** Distance from the edge at which depth weight saturates. 0 uses the weighting range's max. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, EditCondition="Mode == EPCGExInsideWeighting::Depth", EditConditionHides))
	double DepthRange = 0;

	/** Multiplier applied to inside targets' weight, for blending and weighted outputs alike. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double InsideWeightScale = 1;

	/** Multiplier applied to outside targets' weight, for blending and weighted outputs alike. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double OutsideWeightScale = 1;

	/** Unscaled weight in [0..1]. Min/Max is the resolved weighting range; a degenerate range yields 1. */
	FORCEINLINE double GetWeight(const double Dist, const bool bInside, const double Min, const double Max) const
	{
		const double Width = Max - Min;
		const double T = Width > 0 ? FMath::Clamp((Dist - Min) / Width, 0.0, 1.0) : 0.0;

		switch (Mode)
		{
		case EPCGExInsideWeighting::Distance:
			return 1.0 - T;
		case EPCGExInsideWeighting::Full:
			return bInside ? 1.0 : 1.0 - T;
		case EPCGExInsideWeighting::Depth:
			if (!bInside)
			{
				return 1.0 - T;
			}
			{
				const double Range = DepthRange > 0 ? DepthRange : Max;
				return Range > 0 ? FMath::Clamp(Dist / Range, 0.0, 1.0) : 1.0;
			}
		default:
			checkNoEntry();
			return 1.0 - T;
		}
	}

	FORCEINLINE double GetScale(const bool bInside) const
	{
		return bInside ? InsideWeightScale : OutsideWeightScale;
	}
};

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Component Flags"))
enum class EPCGExApplySampledComponentFlags : uint8
{
	None = 0,
	X    = 1 << 0 UMETA(DisplayName = "X", ToolTip="Apply X Component", ActionIcon="X"),
	Y    = 1 << 1 UMETA(DisplayName = "Y", ToolTip="Apply Y Component", ActionIcon="Y"),
	Z    = 1 << 2 UMETA(DisplayName = "Z", ToolTip="Apply Z Component", ActionIcon="Z"),
	All  = X | Y | Z UMETA(Hidden, DisplayName = "All", ToolTip="Apply all Component"),
};

ENUM_CLASS_FLAGS(EPCGExApplySampledComponentFlags)
using EPCGExApplySampledComponentFlagsBitmask = TEnumAsByte<EPCGExApplySampledComponentFlags>;

namespace PCGExSampling::Labels
{
	const FName SourceSourceLabel = TEXT("Sources");
	const FName SourceIgnoreActorsLabel = TEXT("InIgnoreActors");
	const FName SourceActorReferencesLabel = TEXT("ActorReferences");
	const FName OutputSampledActorsLabel = TEXT("OutSampledActors");
}

// The output set every target sampler shares; a node appends its own fields after it.
#define PCGEX_FOREACH_FIELD_SAMPLING_COMMON(MACRO)\
MACRO(Success, bool, false)\
MACRO(Transform, FTransform, FTransform::Identity)\
MACRO(LookAtTransform, FTransform, FTransform::Identity)\
MACRO(Distance, double, 0)\
MACRO(SignedDistance, double, 0)\
MACRO(ComponentWiseDistance, FVector, FVector::ZeroVector)\
MACRO(Angle, double, 0)\
MACRO(NumSamples, int32, 0)

// PCGExSampling::FCommonOutputConfig fields, and their fill from a node (expects Config, Settings and Context in scope)
#define PCGEX_OUTPUT_CONFIG_DECL(_NAME, _TYPE, _DEFAULT_VALUE) FName _NAME##AttributeName = NAME_None; bool bWrite##_NAME = false;
#define PCGEX_OUTPUT_CONFIG_FWD(_NAME, _TYPE, _DEFAULT_VALUE) Config._NAME##AttributeName = Settings->_NAME##AttributeName; Config.bWrite##_NAME = Context->bWrite##_NAME;

// Everything FCommonOutputConfig copies from a sampler's settings except the per-node failure policy
#define PCGEX_OUTPUT_CONFIG_FWD_COMMON \
PCGEX_FOREACH_FIELD_SAMPLING_COMMON(PCGEX_OUTPUT_CONFIG_FWD) \
Config.DistanceScale = Settings->DistanceScale; \
Config.SignedDistanceScale = Settings->SignedDistanceScale; \
Config.bOutputNormalizedDistance = Settings->bOutputNormalizedDistance; \
Config.bOutputOneMinusDistance = Settings->bOutputOneMinusDistance; \
Config.bAbsoluteComponentWiseDistance = Settings->bAbsoluteComponentWiseDistance; \
Config.AngleRange = Settings->AngleRange;

// Declaration & use pair, boolean will be set by name validation
#define PCGEX_OUTPUT_DECL_TOGGLE(_NAME, _TYPE, _DEFAULT_VALUE) bool bWrite##_NAME = false;
#define PCGEX_OUTPUT_DECL(_NAME, _TYPE, _DEFAULT_VALUE) TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Writer;
#define PCGEX_OUTPUT_DECL_AND_TOGGLE(_NAME, _TYPE, _DEFAULT_VALUE) PCGEX_OUTPUT_DECL_TOGGLE(_NAME, _TYPE, _DEFAULT_VALUE) PCGEX_OUTPUT_DECL(_NAME, _TYPE, _DEFAULT_VALUE)

// Simply validate name from settings
#define PCGEX_OUTPUT_VALIDATE_NAME(_NAME, _TYPE, _DEFAULT_VALUE)\
Context->bWrite##_NAME = Settings->bWrite##_NAME; \
if(Context->bWrite##_NAME && !PCGExMetaHelpers::IsWritableAttributeName(Settings->_NAME##AttributeName))\
{ PCGE_LOG(Warning, GraphAndLog, FTEXT("Invalid output attribute name for " #_NAME )); Context->bWrite##_NAME = false; }

#define PCGEX_OUTPUT_INIT(_NAME, _TYPE, _DEFAULT_VALUE) if(Context->bWrite##_NAME){ _NAME##Writer = OutputFacade->GetWritable<_TYPE>(Settings->_NAME##AttributeName, _DEFAULT_VALUE, true, PCGExData::EBufferInit::Inherit); }
#define PCGEX_OUTPUT_VALUE(_NAME, _INDEX, _VALUE) if(_NAME##Writer){_NAME##Writer->SetValue(_INDEX, _VALUE); }
