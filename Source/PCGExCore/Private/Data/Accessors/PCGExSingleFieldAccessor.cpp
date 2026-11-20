// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExSingleFieldAccessor.h"

#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Types/PCGExTypeOps.h"

namespace PCGExData
{
	namespace PCGExSingleFieldAccessor
	{
		// Mirrors STRMAP_SINGLE_FIELD from PCGExSubSelection.h. The third tuple
		// element ("FieldIndex" 0..3) is what SetFieldIndex would normalize to;
		// derived fields (Length/Volume/Sum/...) all carry index 0 in the legacy
		// table -- matching that exactly is required for parity.
		struct FFieldEntry
		{
			PCGExTypeOps::ESingleField Field;
			EPCGMetadataTypes Hint;
			int32 FieldIndex;
		};

		const TMap<FString, FFieldEntry>& GetFieldTable()
		{
			static const TMap<FString, FFieldEntry> Table = {
				{TEXT("X"), {PCGExTypeOps::ESingleField::X, EPCGMetadataTypes::Vector, 0}},
				{TEXT("R"), {PCGExTypeOps::ESingleField::X, EPCGMetadataTypes::Quaternion, 0}},
				{TEXT("ROLL"), {PCGExTypeOps::ESingleField::X, EPCGMetadataTypes::Quaternion, 0}},
				{TEXT("RX"), {PCGExTypeOps::ESingleField::X, EPCGMetadataTypes::Quaternion, 0}},
				{TEXT("Y"), {PCGExTypeOps::ESingleField::Y, EPCGMetadataTypes::Vector, 1}},
				{TEXT("G"), {PCGExTypeOps::ESingleField::Y, EPCGMetadataTypes::Vector4, 1}},
				{TEXT("YAW"), {PCGExTypeOps::ESingleField::Y, EPCGMetadataTypes::Quaternion, 1}},
				{TEXT("RY"), {PCGExTypeOps::ESingleField::Y, EPCGMetadataTypes::Quaternion, 1}},
				{TEXT("Z"), {PCGExTypeOps::ESingleField::Z, EPCGMetadataTypes::Vector, 2}},
				{TEXT("B"), {PCGExTypeOps::ESingleField::Z, EPCGMetadataTypes::Vector4, 2}},
				{TEXT("P"), {PCGExTypeOps::ESingleField::Z, EPCGMetadataTypes::Quaternion, 2}},
				{TEXT("PITCH"), {PCGExTypeOps::ESingleField::Z, EPCGMetadataTypes::Quaternion, 2}},
				{TEXT("RZ"), {PCGExTypeOps::ESingleField::Z, EPCGMetadataTypes::Quaternion, 2}},
				{TEXT("W"), {PCGExTypeOps::ESingleField::W, EPCGMetadataTypes::Vector4, 3}},
				{TEXT("A"), {PCGExTypeOps::ESingleField::W, EPCGMetadataTypes::Vector4, 3}},
				{TEXT("L"), {PCGExTypeOps::ESingleField::Length, EPCGMetadataTypes::Vector, 0}},
				{TEXT("LEN"), {PCGExTypeOps::ESingleField::Length, EPCGMetadataTypes::Vector, 0}},
				{TEXT("LENGTH"), {PCGExTypeOps::ESingleField::Length, EPCGMetadataTypes::Vector, 0}},
				{TEXT("SQUAREDLENGTH"), {PCGExTypeOps::ESingleField::SquaredLength, EPCGMetadataTypes::Vector, 0}},
				{TEXT("LENSQR"), {PCGExTypeOps::ESingleField::SquaredLength, EPCGMetadataTypes::Vector, 0}},
				{TEXT("VOL"), {PCGExTypeOps::ESingleField::Volume, EPCGMetadataTypes::Vector, 0}},
				{TEXT("VOLUME"), {PCGExTypeOps::ESingleField::Volume, EPCGMetadataTypes::Vector, 0}},
				{TEXT("SUM"), {PCGExTypeOps::ESingleField::Sum, EPCGMetadataTypes::Vector, 0}},
			};
			return Table;
		}

		//
		// Per-type ExtractField / InjectField logic. These private templates +
		// specializations are the sole implementations of field
		// extraction/injection.
		//

		using PCGExTypeOps::ESingleField;

		// --- ExtractField ---

		// Scalars: Field enum ignored; cast to double.
		template <typename T>
		FORCEINLINE double ExtractFieldFromValue(const T& Value, ESingleField /*Field*/)
		{
			return static_cast<double>(Value);
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FVector2D>(const FVector2D& V, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				return V.X;
			case ESingleField::Y:
				return V.Y;
			case ESingleField::Length:
				return V.Length();
			case ESingleField::SquaredLength:
				return V.SquaredLength();
			case ESingleField::Volume:
				return V.X * V.Y;
			case ESingleField::Sum:
				return V.X + V.Y;
			default:
				return V.X;
			}
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FVector>(const FVector& V, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				return V.X;
			case ESingleField::Y:
				return V.Y;
			case ESingleField::Z:
				return V.Z;
			case ESingleField::Length:
				return V.Length();
			case ESingleField::SquaredLength:
				return V.SquaredLength();
			case ESingleField::Volume:
				return V.X * V.Y * V.Z;
			case ESingleField::Sum:
				return V.X + V.Y + V.Z;
			default:
				return V.X;
			}
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FVector4>(const FVector4& V, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				return V.X;
			case ESingleField::Y:
				return V.Y;
			case ESingleField::Z:
				return V.Z;
			case ESingleField::W:
				return V.W;
			case ESingleField::Length:
				return FVector(V.X, V.Y, V.Z).Length();
			case ESingleField::SquaredLength:
				return FVector(V.X, V.Y, V.Z).SquaredLength();
			case ESingleField::Volume:
				return V.X * V.Y * V.Z * V.W;
			case ESingleField::Sum:
				return V.X + V.Y + V.Z + V.W;
			default:
				return V.X;
			}
		}

		// FRotator quirk: X=Roll, Y=Yaw, Z=Pitch.
		template <>
		FORCEINLINE double ExtractFieldFromValue<FRotator>(const FRotator& R, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				return R.Roll;
			case ESingleField::Y:
				return R.Yaw;
			case ESingleField::Z:
				return R.Pitch;
			default:
				return R.Roll;
			}
		}

		// FQuat: convert to FRotator, then extract.
		template <>
		FORCEINLINE double ExtractFieldFromValue<FQuat>(const FQuat& Q, ESingleField Field)
		{
			return ExtractFieldFromValue<FRotator>(Q.Rotator(), Field);
		}

		// FTransform: extract location, then FVector field logic.
		template <>
		FORCEINLINE double ExtractFieldFromValue<FTransform>(const FTransform& T, ESingleField Field)
		{
			return ExtractFieldFromValue<FVector>(T.GetLocation(), Field);
		}

		// String types: return 0. Unreachable via ClassifyForInType (Drop on strings).
		template <>
		FORCEINLINE double ExtractFieldFromValue<FString>(const FString& /*V*/, ESingleField /*F*/)
		{
			return 0.0;
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FName>(const FName& /*V*/, ESingleField /*F*/)
		{
			return 0.0;
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FSoftObjectPath>(const FSoftObjectPath& /*V*/, ESingleField /*F*/)
		{
			return 0.0;
		}

		template <>
		FORCEINLINE double ExtractFieldFromValue<FSoftClassPath>(const FSoftClassPath& /*V*/, ESingleField /*F*/)
		{
			return 0.0;
		}

		// --- InjectField ---

		// Scalars: Field enum ignored; cast from double.
		template <typename T>
		FORCEINLINE void InjectFieldIntoValue(T& Value, double FieldValue, ESingleField /*Field*/)
		{
			Value = static_cast<T>(FieldValue);
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FVector2D>(FVector2D& V, double Value, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				V.X = Value;
				break;
			case ESingleField::Y:
				V.Y = Value;
				break;
			case ESingleField::Length:
				V = V.GetSafeNormal() * Value;
				break;
			case ESingleField::SquaredLength:
				V = V.GetSafeNormal() * FMath::Sqrt(Value);
				break;
			default:
				break;
			}
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FVector>(FVector& V, double Value, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				V.X = Value;
				break;
			case ESingleField::Y:
				V.Y = Value;
				break;
			case ESingleField::Z:
				V.Z = Value;
				break;
			case ESingleField::Length:
				V = V.GetSafeNormal() * Value;
				break;
			case ESingleField::SquaredLength:
				V = V.GetSafeNormal() * FMath::Sqrt(Value);
				break;
			default:
				break;
			}
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FVector4>(FVector4& V, double Value, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				V.X = Value;
				break;
			case ESingleField::Y:
				V.Y = Value;
				break;
			case ESingleField::Z:
				V.Z = Value;
				break;
			case ESingleField::W:
				V.W = Value;
				break;
			case ESingleField::Length:
			{
				FVector Vec(V.X, V.Y, V.Z);
				Vec = Vec.GetSafeNormal() * Value;
				V = FVector4(Vec.X, Vec.Y, Vec.Z, V.W);
			}
			break;
			case ESingleField::SquaredLength:
			{
				FVector Vec(V.X, V.Y, V.Z);
				Vec = Vec.GetSafeNormal() * FMath::Sqrt(Value);
				V = FVector4(Vec.X, Vec.Y, Vec.Z, V.W);
			}
			break;
			default:
				break;
			}
		}

		// FRotator quirk: X=Roll, Y=Yaw, Z=Pitch.
		template <>
		FORCEINLINE void InjectFieldIntoValue<FRotator>(FRotator& R, double Value, ESingleField Field)
		{
			switch (Field)
			{
			case ESingleField::X:
				R.Roll = Value;
				break;
			case ESingleField::Y:
				R.Yaw = Value;
				break;
			case ESingleField::Z:
				R.Pitch = Value;
				break;
			case ESingleField::Length:
				R = R.GetNormalized() * Value;
				break;
			case ESingleField::SquaredLength:
				R = R.GetNormalized() * FMath::Sqrt(Value);
				break;
			default:
				break;
			}
		}

		// FQuat: convert to FRotator, inject, convert back.
		template <>
		FORCEINLINE void InjectFieldIntoValue<FQuat>(FQuat& Q, double Value, ESingleField Field)
		{
			FRotator R = Q.Rotator();
			InjectFieldIntoValue<FRotator>(R, Value, Field);
			Q = R.Quaternion();
		}

		// FTransform: extract location, inject into FVector, write back.
		// Correctly injects into Location rather than passing &T as FVector*.
		template <>
		FORCEINLINE void InjectFieldIntoValue<FTransform>(FTransform& T, double Value, ESingleField Field)
		{
			FVector Pos = T.GetLocation();
			InjectFieldIntoValue<FVector>(Pos, Value, Field);
			T.SetLocation(Pos);
		}

		// String types: no-op. Unreachable via ClassifyForInType (Drop on strings).
		template <>
		FORCEINLINE void InjectFieldIntoValue<FString>(FString& /*V*/, double /*Val*/, ESingleField /*F*/)
		{
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FName>(FName& /*V*/, double /*Val*/, ESingleField /*F*/)
		{
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FSoftObjectPath>(FSoftObjectPath& /*V*/, double /*Val*/, ESingleField /*F*/)
		{
		}

		template <>
		FORCEINLINE void InjectFieldIntoValue<FSoftClassPath>(FSoftClassPath& /*V*/, double /*Val*/, ESingleField /*F*/)
		{
		}

		//
		// Typed fn pointers for compiled-chain hot path
		//

		template <typename T>
		void FieldGetStep(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			*static_cast<double*>(ChildOut) = ExtractFieldFromValue<T>(*static_cast<const T*>(Parent), Parsed.Field);
		}

		template <typename T>
		void FieldSetStep(void* ParentInOut, const void* NewChild, const FAccessorParseResult& Parsed)
		{
			const double Value = *static_cast<const double*>(NewChild);
			InjectFieldIntoValue<T>(*static_cast<T*>(ParentInOut), Value, Parsed.Field);
		}
	}

	bool FSingleFieldAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		if (const PCGExSingleFieldAccessor::FFieldEntry* Entry = PCGExSingleFieldAccessor::GetFieldTable().Find(UpperToken))
		{
			OutParsed.Field = Entry->Field;
			OutParsed.FieldIndex = Entry->FieldIndex;
			OutParsed.SourceTypeHint = Entry->Hint;
			return true;
		}
		return false;
	}

	bool FSingleFieldAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                             const FAccessorParseResult& Parsed,
	                                             EPCGMetadataTypes& OutType) const
	{
		(void)InType;
		(void)Parsed;
		OutType = EPCGMetadataTypes::Double;
		return true;
	}

	void FSingleFieldAccessor::ApplyGet(EPCGMetadataTypes InType,
	                                    const void* Source,
	                                    EPCGMetadataTypes OutType,
	                                    void* OutValue,
	                                    const FAccessorParseResult& Parsed) const
	{
		(void)OutType; // always Double; caller already sized OutValue
		check(Source != nullptr);
		check(OutValue != nullptr);

#define PCGEX_FIELD_EXTRACT(_TYPE, _NAME) { *static_cast<double*>(OutValue) = PCGExSingleFieldAccessor::ExtractFieldFromValue<_TYPE>(*static_cast<const _TYPE*>(Source), Parsed.Field); }
		PCGEX_EXECUTEWITHRIGHTTYPE(InType, PCGEX_FIELD_EXTRACT)
#undef PCGEX_FIELD_EXTRACT
	}

	void FSingleFieldAccessor::ApplySet(EPCGMetadataTypes InType,
	                                    void* TargetInOut,
	                                    EPCGMetadataTypes SourceType,
	                                    const void* Source,
	                                    const FAccessorParseResult& Parsed) const
	{
		check(TargetInOut != nullptr);
		check(Source != nullptr);

		// Convert source -> double, then inject into target's field slot.
		double Scalar = 0.0;
		if (SourceType == EPCGMetadataTypes::Double)
		{
			Scalar = *static_cast<const double*>(Source);
		}
		else
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, EPCGMetadataTypes::Double, &Scalar);
		}

#define PCGEX_FIELD_INJECT(_TYPE, _NAME) { PCGExSingleFieldAccessor::InjectFieldIntoValue<_TYPE>(*static_cast<_TYPE*>(TargetInOut), Scalar, Parsed.Field); }
		PCGEX_EXECUTEWITHRIGHTTYPE(InType, PCGEX_FIELD_INJECT)
#undef PCGEX_FIELD_INJECT
	}

	FString FSingleFieldAccessor::GetDisplayName() const
	{
		return TEXT("SingleField");
	}

	FStepGetFn FSingleFieldAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		switch (InType)
		{
#define PCGEX_CASE(_TYPE, _NAME, ...) case EPCGMetadataTypes::_NAME: return &PCGExSingleFieldAccessor::FieldGetStep<_TYPE>;
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_CASE)
#undef PCGEX_CASE
		default:
			return nullptr;
		}
	}

	FStepSetFn FSingleFieldAccessor::GetStepSetFn(EPCGMetadataTypes InType) const
	{
		switch (InType)
		{
#define PCGEX_CASE(_TYPE, _NAME, ...) case EPCGMetadataTypes::_NAME: return &PCGExSingleFieldAccessor::FieldSetStep<_TYPE>;
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_CASE)
#undef PCGEX_CASE
		default:
			return nullptr;
		}
	}

	ISubAccessor::ECompileAction FSingleFieldAccessor::ClassifyForInType(
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
			return ECompileAction::Keep;

		case EPCGMetadataTypes::Float:
		case EPCGMetadataTypes::Double:
		case EPCGMetadataTypes::Integer32:
		case EPCGMetadataTypes::Integer64:
		case EPCGMetadataTypes::Boolean:
			return ECompileAction::Keep;

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
}
