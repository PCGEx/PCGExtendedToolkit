// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMathAxis.generated.h"

UENUM()
enum class EPCGExMinimalAxis : uint8
{
	None = 0 UMETA(DisplayName = "None", ToolTip="None", ActionIcon="STF_None"),
	X    = 1 UMETA(DisplayName = "X", ToolTip="X Axis", ActionIcon="X"),
	Y    = 2 UMETA(DisplayName = "Y", ToolTip="Y Axis", ActionIcon="Y"),
	Z    = 3 UMETA(DisplayName = "Z", ToolTip="Z Axis", ActionIcon="Z"),
};


UENUM()
enum class EPCGExAxis : uint8
{
	Forward  = 0 UMETA(DisplayName = "Forward", ToolTip="Forward (X+)."),
	Backward = 1 UMETA(DisplayName = "Backward", ToolTip="Backward (X-)."),
	Right    = 2 UMETA(DisplayName = "Right", ToolTip="Right (Y+)"),
	Left     = 3 UMETA(DisplayName = "Left", ToolTip="Left (Y-)"),
	Up       = 4 UMETA(DisplayName = "Up", ToolTip="Up (Z+)"),
	Down     = 5 UMETA(DisplayName = "Down", ToolTip="Down (Z-)"),
};

UENUM(BlueprintType)
enum class EPCGExAxisOrder : uint8
{
	XYZ = 0 UMETA(DisplayName = "X > Y > Z", ToolTip="(0) X > Y > Z", ActionIcon="AxisOrder_XYZ"),
	YZX = 1 UMETA(DisplayName = "Y > Z > X", ToolTip="(1) Y > Z > X", ActionIcon="AxisOrder_YZX"),
	ZXY = 2 UMETA(DisplayName = "Z > X > Y", ToolTip="(2) Z > X > Y", ActionIcon="AxisOrder_ZXY"),
	YXZ = 3 UMETA(DisplayName = "Y > X > Z", ToolTip="(3) Y > X > Z", ActionIcon="AxisOrder_YXZ"),
	ZYX = 4 UMETA(DisplayName = "Z > Y > X", ToolTip="(4) Z > Y > X", ActionIcon="AxisOrder_ZYX"),
	XZY = 5 UMETA(DisplayName = "X > Z > Y", ToolTip="(5) X > Z > Y", ActionIcon="AxisOrder_XZY")
};

UENUM()
enum class EPCGExAxisAlign : uint8
{
	Forward  = 0 UMETA(DisplayName = "Forward", ToolTip="..."),
	Backward = 1 UMETA(DisplayName = "Backward", ToolTip="..."),
	Right    = 2 UMETA(DisplayName = "Right", ToolTip="..."),
	Left     = 3 UMETA(DisplayName = "Left", ToolTip="..."),
	Up       = 4 UMETA(DisplayName = "Up", ToolTip="..."),
	Down     = 5 UMETA(DisplayName = "Down", ToolTip="..."),
};

UENUM(BlueprintType)
enum class EPCGExMakeRotAxis : uint8
{
	X  = 0 UMETA(DisplayName = "X", ToolTip="(0) Main direction used for X (Forward)", ActionIcon="RotOrder_X"),
	XY = 1 UMETA(DisplayName = "X > Y", ToolTip="(1) Main direction used for X (Forward), second axis for Y (Right)", ActionIcon="RotOrder_XY"),
	XZ = 2 UMETA(DisplayName = "X > Z", ToolTip="(2) Main direction used for X (Forward), second axis for Z (Up)", ActionIcon="RotOrder_XZ"),
	Y  = 3 UMETA(DisplayName = "Y", ToolTip="(3) Main direction used for Y (Right)", ActionIcon="RotOrder_Y"),
	YX = 4 UMETA(DisplayName = "Y > X", ToolTip="(4) Main direction used for Y (Right), second axis for X (Forward)", ActionIcon="RotOrder_YX"),
	YZ = 5 UMETA(DisplayName = "Y > Z", ToolTip="(5) Main direction used for Y (Right), second axis for Z (Up)", ActionIcon="RotOrder_YZ"),
	Z  = 6 UMETA(DisplayName = "Z", ToolTip="(6) Main direction used for Z (Up)", ActionIcon="RotOrder_Z"),
	ZX = 7 UMETA(DisplayName = "Z > X", ToolTip="(7) Main direction used for Z (Up), second axis for X (Forward)", ActionIcon="RotOrder_ZX"),
	ZY = 8 UMETA(DisplayName = "Z > Y", ToolTip="(8) Main direction used for Z (Up), second axis for Y (Right)", ActionIcon="RotOrder_ZY")
};

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Rotation Component Flags"))
enum class EPCGExAbsoluteRotationFlags : uint8
{
	None = 0 UMETA(Hidden),
	X    = 1 << 0 UMETA(DisplayName = "Pitch", ToolTip = "Pitch", ActionIcon="X"),
	Y    = 1 << 1 UMETA(DisplayName = "Yaw", ToolTip = "Yaw", ActionIcon="Y"),
	Z    = 1 << 2 UMETA(DisplayName = "Roll", ToolTip = "Roll", ActionIcon="Z")
};

namespace PCGExMath
{
	static const int32 AxisOrders[6][3] = {
		{0, 1, 2}, // X > Y > Z
		{1, 2, 0}, // Y > Z > X
		{2, 0, 1}, // Z > X > Y
		{1, 0, 2}, // Y > X > Z
		{2, 1, 0}, // Z > Y > X
		{0, 2, 1}  // X > Z > Y
	};

	FORCEINLINE void GetAxesOrder(EPCGExAxisOrder Order, int32 (&OutArray)[3])
	{
		const int32 Index = static_cast<uint8>(Order);
		OutArray[0] = AxisOrders[Index][0];
		OutArray[1] = AxisOrders[Index][1];
		OutArray[2] = AxisOrders[Index][2];
	}

	FORCEINLINE void GetAxesOrder(EPCGExAxisOrder Order, int32& A, int32& B, int32& C)
	{
		const int32 Index = static_cast<uint8>(Order);
		A = AxisOrders[Index][0];
		B = AxisOrders[Index][1];
		C = AxisOrders[Index][2];
	}

	FORCEINLINE void ReorderAxes(EPCGExAxisOrder Order, FVector& X, FVector& Y, FVector& Z)
	{
		const FVector Axes[3] = {X, Y, Z};
		const int32 Index = static_cast<uint8>(Order);
		X = Axes[AxisOrders[Index][0]];
		Y = Axes[AxisOrders[Index][1]];
		Z = Axes[AxisOrders[Index][2]];
	}

	PCGEXCORE_API void GetAxesOrder(EPCGExMakeRotAxis Order, int32& A, int32& B, int32& C);

	FORCEINLINE void GetAxesOrder(EPCGExMakeRotAxis Order, int32 (&OutArray)[3])
	{
		GetAxesOrder(Order, OutArray[0], OutArray[1], OutArray[2]);
	}

	/** Builds a rotation from the given axes according to the selected construction.
	 * Degenerate inputs degrade gracefully: a two-axis construction whose secondary axis is zero
	 * falls back to its single-axis variant, a zero primary axis falls back to the secondary,
	 * and fully degenerate inputs return identity. */
	PCGEXCORE_API FQuat MakeRot(const EPCGExMakeRotAxis Order, const FVector& X, const FVector& Y, const FVector& Z);

	/** Two-axes shorthand: A is the construction's primary axis, B its secondary. Same degeneracy fallbacks as above. */
	PCGEXCORE_API FQuat MakeRot(const EPCGExMakeRotAxis Order, const FVector& A, const FVector& B);

	/** For each reference axis (X/Y/ZAxis), outputs the index (0=X, 1=Y, 2=Z) of the quaternion's local
	 * axis best aligned with it, sign-insensitive. With bPermute the mapping is guaranteed bijective
	 * (highest-scoring assignment); without it each reference axis picks independently and may duplicate. */
	PCGEXCORE_API void FindOrderMatch(const FQuat& Quat, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis, int32& X, int32& Y, int32& Z, const bool bPermute = true);

	/** Normalized Cross(Up, Dir). When Dir is (nearly) parallel to Up the cross vanishes; this falls
	 * back to an alternate reference axis so the result is never zero for a non-zero Dir. */
	PCGEXCORE_API FVector SafeCrossNormal(const FVector& Up, const FVector& Dir);

	template <const EPCGExAxis Dir = EPCGExAxis::Forward>
	FORCEINLINE static FVector GetDirection(const FQuat& Quat)
	{
		if constexpr (Dir == EPCGExAxis::Backward)
		{
			return Quat.GetForwardVector() * -1;
		}
		else if constexpr (Dir == EPCGExAxis::Right)
		{
			return Quat.GetRightVector();
		}
		else if constexpr (Dir == EPCGExAxis::Left)
		{
			return Quat.GetRightVector() * -1;
		}
		else if constexpr (Dir == EPCGExAxis::Up)
		{
			return Quat.GetUpVector();
		}
		else if constexpr (Dir == EPCGExAxis::Down)
		{
			return Quat.GetUpVector() * -1;
		}
		else
		{
			return Quat.GetForwardVector();
		}
	}

	PCGEXCORE_API FVector GetDirection(const FQuat& Quat, const EPCGExAxis Dir);
	PCGEXCORE_API FVector GetDirection(const EPCGExAxis Dir);
	PCGEXCORE_API FTransform GetIdentity(const EPCGExAxisOrder Order);

	PCGEXCORE_API void Swizzle(FVector& Vector, const EPCGExAxisOrder Order);

	PCGEXCORE_API void Swizzle(FVector& Vector, const int32 (&Order)[3]);

	PCGEXCORE_API FQuat MakeDirection(const EPCGExAxis Dir, const FVector& InForward);
	PCGEXCORE_API FQuat MakeDirection(const EPCGExAxis Dir, const FVector& InForward, const FVector& InUp);

	PCGEXCORE_API FVector GetNormal(const FVector& A, const FVector& B, const FVector& C);
	PCGEXCORE_API FVector GetNormalUp(const FVector& A, const FVector& B, const FVector& Up);

	PCGEXCORE_API FTransform MakeLookAtTransform(const FVector& LookAt, const FVector& LookUp, const EPCGExAxisAlign AlignAxis);

	PCGEXCORE_API double GetAngle(const FVector& A, const FVector& B);

	// Expects normalized vectors
	PCGEXCORE_API double GetRadiansBetweenVectors(const FVector& A, const FVector& B, const FVector& UpVector = FVector::UpVector);
	PCGEXCORE_API double GetRadiansBetweenVectors(const FVector2D& A, const FVector2D& B);
	PCGEXCORE_API double GetDegreesBetweenVectors(const FVector& A, const FVector& B, const FVector& UpVector = FVector::UpVector);
}

#define PCGEX_AXIS_X FVector::ForwardVector
#define PCGEX_AXIS_Y FVector::RightVector
#define PCGEX_AXIS_Z FVector::UpVector
#define PCGEX_AXIS_X_N FVector::BackwardVector
#define PCGEX_AXIS_Y_N FVector::LeftVector
#define PCGEX_AXIS_Z_N FVector::DownVector

#define PCGEX_FOREACH_XYZ(MACRO)\
MACRO(X)\
MACRO(Y)\
MACRO(Z)
