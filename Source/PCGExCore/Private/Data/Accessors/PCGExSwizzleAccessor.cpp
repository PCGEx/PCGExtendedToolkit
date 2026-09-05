// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExSwizzleAccessor.h"

#include "Types/PCGExTypeOpsImpl.h"

namespace PCGExData
{
	namespace
	{
		// Try to interpret a single character as a component index (0..3).
		// Returns true on success.
		bool ComponentIndexFromChar(TCHAR Ch, uint8& OutIndex)
		{
			switch (Ch)
			{
			case TEXT('X'):
				OutIndex = 0;
				return true;
			case TEXT('Y'):
				OutIndex = 1;
				return true;
			case TEXT('Z'):
				OutIndex = 2;
				return true;
			case TEXT('W'):
				OutIndex = 3;
				return true;
			default:
				return false;
			}
		}

		// Extract the i-th component of a typed source as a double.
		// Components beyond the type's native count return 0.
		template <typename T>
		double GetComponent(const T& Value, uint8 Index);

		template <>
		FORCEINLINE double GetComponent<FVector2D>(const FVector2D& V, uint8 Index)
		{
			switch (Index)
			{
			case 0:
				return V.X;
			case 1:
				return V.Y;
			default:
				return 0.0;
			}
		}

		template <>
		FORCEINLINE double GetComponent<FVector>(const FVector& V, uint8 Index)
		{
			switch (Index)
			{
			case 0:
				return V.X;
			case 1:
				return V.Y;
			case 2:
				return V.Z;
			default:
				return 0.0;
			}
		}

		template <>
		FORCEINLINE double GetComponent<FVector4>(const FVector4& V, uint8 Index)
		{
			switch (Index)
			{
			case 0:
				return V.X;
			case 1:
				return V.Y;
			case 2:
				return V.Z;
			case 3:
				return V.W;
			default:
				return 0.0;
			}
		}

		template <>
		FORCEINLINE double GetComponent<FQuat>(const FQuat& Q, uint8 Index)
		{
			// Match FSingleFieldAccessor's semantics: route through Rotator so X=Roll,
			// Y=Yaw, Z=Pitch. Keeps consistency with field extraction on quats.
			const FRotator R = Q.Rotator();
			switch (Index)
			{
			case 0:
				return R.Roll;
			case 1:
				return R.Yaw;
			case 2:
				return R.Pitch;
			default:
				return 0.0;
			}
		}

		template <>
		FORCEINLINE double GetComponent<FRotator>(const FRotator& R, uint8 Index)
		{
			switch (Index)
			{
			case 0:
				return R.Roll;
			case 1:
				return R.Yaw;
			case 2:
				return R.Pitch;
			default:
				return 0.0;
			}
		}

		// Scalar sources: every index reads the same value.
		template <typename T>
		FORCEINLINE double GetScalarComponent(const T& Value, uint8 /*Index*/)
		{
			return static_cast<double>(Value);
		}

		void WriteVector2(void* OutValue, double X, double Y)
		{
			*static_cast<FVector2D*>(OutValue) = FVector2D(X, Y);
		}

		void WriteVector(void* OutValue, double X, double Y, double Z)
		{
			*static_cast<FVector*>(OutValue) = FVector(X, Y, Z);
		}

		void WriteVector4(void* OutValue, double X, double Y, double Z, double W)
		{
			*static_cast<FVector4*>(OutValue) = FVector4(X, Y, Z, W);
		}

		// Dispatch helper: read Length components from typed source, write to OutValue.
		template <typename T>
		void SwizzleStep(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			const T& Src = *static_cast<const T*>(Parent);
			const double C0 = GetComponent<T>(Src, Parsed.SwizzleMask[0]);
			const double C1 = GetComponent<T>(Src, Parsed.SwizzleMask[1]);
			switch (Parsed.SwizzleLength)
			{
			case 2:
				WriteVector2(ChildOut, C0, C1);
				break;
			case 3:
				WriteVector(ChildOut, C0, C1, GetComponent<T>(Src, Parsed.SwizzleMask[2]));
				break;
			case 4:
				WriteVector4(ChildOut, C0, C1,
				             GetComponent<T>(Src, Parsed.SwizzleMask[2]),
				             GetComponent<T>(Src, Parsed.SwizzleMask[3]));
				break;
			default:
				break;
			}
		}

		// Scalar source dispatch (bool, int32, int64, float, double): every component
		// reads the same scalar value. Kept as a separate template because scalar types
		// aren't indexable like vectors.
		template <typename T>
		void SwizzleStepScalar(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			const T& Src = *static_cast<const T*>(Parent);
			const double V = static_cast<double>(Src);
			switch (Parsed.SwizzleLength)
			{
			case 2:
				WriteVector2(ChildOut, V, V);
				break;
			case 3:
				WriteVector(ChildOut, V, V, V);
				break;
			case 4:
				WriteVector4(ChildOut, V, V, V, V);
				break;
			default:
				break;
			}
		}
	}

	bool FSwizzleAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		const int32 Len = UpperToken.Len();
		if (Len < 2 || Len > 4)
		{
			return false;
		}

		uint8 Mask[4] = {0, 0, 0, 0};
		for (int32 i = 0; i < Len; ++i)
		{
			uint8 Index = 0;
			if (!ComponentIndexFromChar(UpperToken[i], Index))
			{
				return false;
			}
			Mask[i] = Index;
		}

		// A 2-char token like "XY" is a valid swizzle but also collides with
		// no existing SingleField alias (which are 1-char or word-like).
		// The registry tries accessors in order (Axis -> TransformPart ->
		// SingleField -> Swizzle), so SingleField gets first crack at "XY".
		// SingleField only has 1-char "X"/"Y"/etc. entries, so no collision.
		//
		// We also reject single-char tokens here because "X" alone should
		// match SingleField, not degrade into a length-1 swizzle.

		OutParsed.SwizzleMask[0] = Mask[0];
		OutParsed.SwizzleMask[1] = Mask[1];
		OutParsed.SwizzleMask[2] = Mask[2];
		OutParsed.SwizzleMask[3] = Mask[3];
		OutParsed.SwizzleLength = static_cast<uint8>(Len);
		// Hint: swizzles always produce a vector of some length.
		switch (Len)
		{
		case 2:
			OutParsed.SourceTypeHint = EPCGMetadataTypes::Vector2;
			break;
		case 3:
			OutParsed.SourceTypeHint = EPCGMetadataTypes::Vector;
			break;
		case 4:
			OutParsed.SourceTypeHint = EPCGMetadataTypes::Vector4;
			break;
		default:
			break;
		}
		return true;
	}

	bool FSwizzleAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                         const FAccessorParseResult& Parsed,
	                                         EPCGMetadataTypes& OutType) const
	{
		(void)InType;
		switch (Parsed.SwizzleLength)
		{
		case 2:
			OutType = EPCGMetadataTypes::Vector2;
			return true;
		case 3:
			OutType = EPCGMetadataTypes::Vector;
			return true;
		case 4:
			OutType = EPCGMetadataTypes::Vector4;
			return true;
		default:
			OutType = EPCGMetadataTypes::Unknown;
			return false;
		}
	}

	void FSwizzleAccessor::ApplyGet(EPCGMetadataTypes InType,
	                                const void* Source,
	                                EPCGMetadataTypes OutType,
	                                void* OutValue,
	                                const FAccessorParseResult& Parsed) const
	{
		(void)OutType;
		check(Source != nullptr);
		check(OutValue != nullptr);

		switch (InType)
		{
		case EPCGMetadataTypes::Vector2:
			SwizzleStep<FVector2D>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Vector:
			SwizzleStep<FVector>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Vector4:
			SwizzleStep<FVector4>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Quaternion:
			SwizzleStep<FQuat>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Rotator:
			SwizzleStep<FRotator>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Float:
			SwizzleStepScalar<float>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Double:
			SwizzleStepScalar<double>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Integer32:
			SwizzleStepScalar<int32>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Integer64:
			SwizzleStepScalar<int64>(Source, OutValue, Parsed);
			break;
		case EPCGMetadataTypes::Boolean:
			SwizzleStepScalar<bool>(Source, OutValue, Parsed);
			break;
		default:
			// Unsupported source type: leave OutValue uninitialized. Chain
			// compiler should have dropped this step via ClassifyForInType
			// before the hot path is reached.
			break;
		}
	}

	FStepGetFn FSwizzleAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		switch (InType)
		{
		case EPCGMetadataTypes::Vector2:
			return &SwizzleStep<FVector2D>;
		case EPCGMetadataTypes::Vector:
			return &SwizzleStep<FVector>;
		case EPCGMetadataTypes::Vector4:
			return &SwizzleStep<FVector4>;
		case EPCGMetadataTypes::Quaternion:
			return &SwizzleStep<FQuat>;
		case EPCGMetadataTypes::Rotator:
			return &SwizzleStep<FRotator>;
		case EPCGMetadataTypes::Float:
			return &SwizzleStepScalar<float>;
		case EPCGMetadataTypes::Double:
			return &SwizzleStepScalar<double>;
		case EPCGMetadataTypes::Integer32:
			return &SwizzleStepScalar<int32>;
		case EPCGMetadataTypes::Integer64:
			return &SwizzleStepScalar<int64>;
		case EPCGMetadataTypes::Boolean:
			return &SwizzleStepScalar<bool>;
		default:
			return nullptr;
		}
	}

	ISubAccessor::ECompileAction FSwizzleAccessor::ClassifyForInType(
		EPCGMetadataTypes InType, const FAccessorParseResult& Parsed,
		const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)Parsed;
		(void)SourceDesc;
		switch (InType)
		{
		case EPCGMetadataTypes::Vector2:
		case EPCGMetadataTypes::Vector:
		case EPCGMetadataTypes::Vector4:
		case EPCGMetadataTypes::Quaternion:
		case EPCGMetadataTypes::Rotator:
		case EPCGMetadataTypes::Float:
		case EPCGMetadataTypes::Double:
		case EPCGMetadataTypes::Integer32:
		case EPCGMetadataTypes::Integer64:
		case EPCGMetadataTypes::Boolean:
			return ECompileAction::Keep;

		// Transform source: auto-promote to Position like other accessors.
		// `.xy` on a Transform attribute -> `.Position.xy` -> Vector2 of (Pos.X, Pos.Y).
		case EPCGMetadataTypes::Transform:
			return ECompileAction::PromoteWithPosition;

		case EPCGMetadataTypes::String:
		case EPCGMetadataTypes::Name:
		case EPCGMetadataTypes::SoftObjectPath:
		case EPCGMetadataTypes::SoftClassPath:
			return ECompileAction::Drop;

		default:
			return ECompileAction::Drop;
		}
	}

	FString FSwizzleAccessor::GetDisplayName() const
	{
		return TEXT("Swizzle");
	}
}
