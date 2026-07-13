// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Types/PCGExAttributeIdentity.h"

#include "PCGExPathsCommon.generated.h"

UENUM()
enum class EPCGExSplinePointTypeRedux : uint8
{
	Linear       = 0 UMETA(DisplayName = "Linear (0)", Tooltip="Linear (0)."),
	Curve        = 1 UMETA(DisplayName = "Curve (1)", Tooltip="Curve (1)."),
	Constant     = 2 UMETA(DisplayName = "Constant (2)", Tooltip="Constant (2)."),
	CurveClamped = 3 UMETA(DisplayName = "CurveClamped (3)", Tooltip="CurveClamped (3).")
};

UENUM()
enum class EPCGExInlinePathProcessingOrder : uint8
{
	FromStart       = 0 UMETA(DisplayName = "From Start", ToolTip="Start at the index 0 of the path. If inverted, start at the last index."),
	EndpointCompare = 2 UMETA(DisplayName = "Endpoint Comparison", ToolTip="Compare an attribute on start and end point to determine which endpoint to start with. If the comparison returns true, start with first point."),
	TaggedAny       = 3 UMETA(DisplayName = "Tagged (Any)", ToolTip="Check for a tag match on the input data. If the tag is found, start with first point."),
	TaggedAll       = 4 UMETA(DisplayName = "Tagged (All)", ToolTip="Check for all tag matches on the input data. If all tags are found, start with first point."),
};

UENUM()
enum class EPCGExInputScope : uint8
{
	All          = 0 UMETA(DisplayName = "All", Tooltip="All paths are considered to have the same open or closed status."),
	AllButTagged = 2 UMETA(DisplayName = "All but tagged", Tooltip="All paths are considered open or closed by default, except the ones with the specified tags which will use the opposite value."),
};

UENUM()
enum class EPCGExPathNormalDirection : uint8
{
	Normal            = 0 UMETA(DisplayName = "Normal", ToolTip="..."),
	Binormal          = 1 UMETA(DisplayName = "Binormal", ToolTip="..."),
	AverageNormal     = 2 UMETA(DisplayName = "Average Normal", ToolTip="..."),
	ParallelTransport = 3 UMETA(DisplayName = "Parallel Transport", ToolTip="Torsion-minimizing normal carried along the path from the first edge. Stable through vertical segments where Up-based normals degenerate; on closed loops the residual seam twist is distributed along the path."),
	Custom            = 10 UMETA(DisplayName = "Custom", ToolTip="..."),
};

UENUM()
enum class EPCGExSplineMeshUpMode : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Constant up vector"),
	Attribute = 1 UMETA(DisplayName = "Attribute", Tooltip="Per-point attribute value"),
	Tangents  = 2 UMETA(DisplayName = "From Tangents (Gimbal fix)", Tooltip="Automatically computed up vector from tangents to enforce gimbal fix")
};

UENUM(BlueprintType)
enum class EPCGExSplineMeshAxis : uint8
{
	Default = 0 UMETA(Hidden),
	X       = 1 UMETA(DisplayName = "X", ToolTip="X Axis"),
	Y       = 2 UMETA(DisplayName = "Y", ToolTip="Y Axis"),
	Z       = 3 UMETA(DisplayName = "Z", ToolTip="Z Axis"),
};

UENUM()
enum class EPCGExSplinePointType : uint8
{
	Linear             = 0 UMETA(DisplayName = "Linear (0)", Tooltip="Linear (0)."),
	Curve              = 1 UMETA(DisplayName = "Curve (1)", Tooltip="Curve (1)."),
	Constant           = 2 UMETA(DisplayName = "Constant (2)", Tooltip="Constant (2)."),
	CurveClamped       = 3 UMETA(DisplayName = "CurveClamped (3)", Tooltip="CurveClamped (3)."),
	CurveCustomTangent = 4 UMETA(DisplayName = "CurveCustomTangent (4)", Tooltip="CurveCustomTangent (4).")
};

namespace PCGExPaths
{
	namespace Labels
	{
		PCGEX_CTX_STATE(State_BuildingPaths)

		const FName SourcePathsLabel = TEXT("Paths");
		const FName OutputPathsLabel = TEXT("Paths");

		const FName SourceCanCutFilters = TEXT("Can Cut Conditions");
		const FName SourceCanBeCutFilters = TEXT("Can Be Cut Conditions");
		const FName SourceTriggerFilters = TEXT("Trigger Conditions");
		const FName SourceShiftFilters = TEXT("Shift Conditions");

		const FPCGAttributeIdentifier ClosedLoopIdentifier = FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data);
		const FPCGAttributeIdentifier HoleIdentifier = FPCGAttributeIdentifier(FName("IsHole"), PCGMetadataDomainID::Data);
	}

	struct PCGEXCORE_API FPathMetrics
	{
		FPathMetrics() = default;
		explicit FPathMetrics(const FVector& InStart);

		FVector Start = FVector::ZeroVector;
		FVector Last = FVector::ZeroVector;
		double Length = -1;
		int32 Count = 0;

		void Reset(const FVector& InStart);

		double Add(const FVector& Location);
		double Add(const FVector& Location, double& OutDistToLast);

		bool IsValid() const
		{
			return Length > 0;
		}

		double GetTime(const double Distance) const
		{
			return (!Distance || !Length) ? 0 : Distance / Length;
		}

		double DistToLast(const FVector& Location) const
		{
			return FVector::Dist(Last, Location);
		}

		bool IsLastWithinRange(const FVector& Location, const double Range) const
		{
			return DistToLast(Location) < Range;
		}
	};
}
