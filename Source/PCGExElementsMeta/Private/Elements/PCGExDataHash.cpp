// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDataHash.h"

#include "PCGContext.h"
#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/Crc.h"

#define LOCTEXT_NAMESPACE "PCGExDataHashElement"
#define PCGEX_NAMESPACE DataHash

namespace PCGExDataHash
{
	EPCGMetadataTypes ToMetadataType(EPCGExDataHashType InType)
	{
		switch (InType)
		{
		case EPCGExDataHashType::Bool:
			return EPCGMetadataTypes::Boolean;
		case EPCGExDataHashType::Int32:
			return EPCGMetadataTypes::Integer32;
		case EPCGExDataHashType::Int64:
			return EPCGMetadataTypes::Integer64;
		case EPCGExDataHashType::Float:
			return EPCGMetadataTypes::Float;
		case EPCGExDataHashType::Double:
			return EPCGMetadataTypes::Double;
		case EPCGExDataHashType::Vector2:
			return EPCGMetadataTypes::Vector2;
		case EPCGExDataHashType::Vector:
			return EPCGMetadataTypes::Vector;
		case EPCGExDataHashType::Vector4:
			return EPCGMetadataTypes::Vector4;
		case EPCGExDataHashType::Quaternion:
			return EPCGMetadataTypes::Quaternion;
		case EPCGExDataHashType::Rotator:
			return EPCGMetadataTypes::Rotator;
		case EPCGExDataHashType::Transform:
			return EPCGMetadataTypes::Transform;
		default:
			return EPCGMetadataTypes::Unknown;
		}
	}

	// Deliberately not GetTypeHash / HashCombine: TypeHash.h states its results are "not expected
	// to leave the running process", so the seed would be hostage to engine internals. Everything
	// below is integer-only and self-contained, so the same input shape yields the same seed on
	// every session, platform, build and engine version.
	constexpr uint64 FnvOffsetBasis = 14695981039346656037ULL;
	constexpr uint64 FnvPrime = 1099511628211ULL;

	// Word-wise rather than byte-wise so byte order never enters the result.
	FORCEINLINE uint64 Mix(const uint64 InHash, const uint64 InValue)
	{
		return (InHash ^ InValue) * FnvPrime;
	}

	// Murmur3 finalizer. FNV alone diffuses poorly into FRandomStream's single-step LCG, which
	// would let near-identical inputs produce near-identical first draws.
	FORCEINLINE uint64 Avalanche(uint64 InHash)
	{
		InHash ^= InHash >> 33;
		InHash *= 0xff51afd7ed558ccdULL;
		InHash ^= InHash >> 33;
		InHash *= 0xc4ceb9fe1a85ec53ULL;
		InHash ^= InHash >> 33;
		return InHash;
	}

	// Hashed instead of the concrete class name: pcg.EnablePointArrayData swaps UPCGPointData for
	// UPCGPointArrayData, which would otherwise silently re-roll every value.
	constexpr uint64 CategoryNull = 0;
	constexpr uint64 CategoryPoint = 1;
	constexpr uint64 CategoryPolyLine = 2;
	constexpr uint64 CategoryParam = 3;
	constexpr uint64 CategorySpatial = 4;
	constexpr uint64 CategoryOther = 5;

	// Bounds are FP-derived, so hashing raw bits would make the seed sensitive to one-ULP upstream
	// drift. The scale MUST stay a power of two: multiplying only shifts the exponent, so it is
	// exact and MSVC's /fp:fast has no alternative form to pick. A division, or any
	// non-power-of-two scale, would reintroduce cross-compiler divergence.
	constexpr double QuantumScale = 1024.0;

	// Past this the double -> int64 conversion would be undefined; +/-Inf rails out here too.
	constexpr double QuantumRail = 9.0e18;

	// Placed in the gap real quantized values can never occupy (|value| <= QuantumRail).
	constexpr uint64 SentinelNaN = 0x8000000000000001ULL;
	constexpr uint64 SentinelOverflowPos = 0x8000000000000002ULL;
	constexpr uint64 SentinelOverflowNeg = 0x8000000000000003ULL;

	uint64 QuantizeCoord(const double InValue)
	{
		// Bit-pattern test, not (A != A), so /fp:fast cannot fold it away.
		if (FMath::IsNaN(InValue))
		{
			return SentinelNaN;
		}

		const double Scaled = InValue * QuantumScale;

		if (Scaled >= QuantumRail)
		{
			return SentinelOverflowPos;
		}

		if (Scaled <= -QuantumRail)
		{
			return SentinelOverflowNeg;
		}

		// Also normalizes -0.0 to 0.
		return static_cast<uint64>(FMath::FloorToInt64(Scaled));
	}

	uint64 HashBox(uint64 InHash, const FBox& InBox)
	{
		// Distinguishes "no bounds" from "a degenerate box at the origin".
		InHash = Mix(InHash, InBox.IsValid ? 1ULL : 0ULL);
		InHash = Mix(InHash, QuantizeCoord(InBox.Min.X));
		InHash = Mix(InHash, QuantizeCoord(InBox.Min.Y));
		InHash = Mix(InHash, QuantizeCoord(InBox.Min.Z));
		InHash = Mix(InHash, QuantizeCoord(InBox.Max.X));
		InHash = Mix(InHash, QuantizeCoord(InBox.Max.Y));
		InHash = Mix(InHash, QuantizeCoord(InBox.Max.Z));
		return InHash;
	}

	uint64 HashInput(uint64 InHash, const UPCGData* Data)
	{
		if (!Data)
		{
			return Mix(InHash, CategoryNull);
		}

		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Data))
		{
			InHash = Mix(InHash, CategoryPoint);
			InHash = Mix(InHash, static_cast<uint64>(PointData->GetNumPoints()));
			return HashBox(InHash, PointData->GetBounds());
		}

		if (const UPCGPolyLineData* PolyLineData = Cast<UPCGPolyLineData>(Data))
		{
			InHash = Mix(InHash, CategoryPolyLine);
			InHash = Mix(InHash, static_cast<uint64>(PolyLineData->GetNumSegments()));
			return HashBox(InHash, PolyLineData->GetBounds());
		}

		if (const UPCGParamData* ParamData = Cast<UPCGParamData>(Data))
		{
			const UPCGMetadata* Metadata = ParamData->ConstMetadata();
			InHash = Mix(InHash, CategoryParam);
			return Mix(InHash, static_cast<uint64>(Metadata ? Metadata->GetLocalItemCount() : 0));
		}

		if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Data))
		{
			InHash = Mix(InHash, CategorySpatial);
			return HashBox(InHash, SpatialData->GetBounds());
		}

		// Nothing shape-like to read, so fall back to the class name. FCrc::StrCrc32 is a table CRC
		// over code points and treats every char width as 32-bit, so unlike GetTypeHash(FName) it
		// depends on neither pool insertion order nor platform.
		InHash = Mix(InHash, CategoryOther);
		return Mix(InHash, static_cast<uint64>(FCrc::StrCrc32(*Data->GetClass()->GetName())));
	}

	// Computes [Min,Max] given the user's range settings and the value type.
	// When bUseRange is OFF, defaults to:
	//   - integer types: full type range
	//   - everything else: [-1, 1]
	// Scale components of Transform get their own [0, 1] default and use the
	// shared resolved range when bUseRange is on (handled inline at the call site).
	void GetResolvedRange(const UPCGExDataHashSettings* Settings, EPCGExDataHashType ForType, double& OutMin, double& OutMax)
	{
		if (Settings->bUseRange)
		{
			OutMin = FMath::Min(Settings->RangeMin, Settings->RangeMax);
			OutMax = FMath::Max(Settings->RangeMin, Settings->RangeMax);
			return;
		}

		switch (ForType)
		{
		case EPCGExDataHashType::Int32:
			OutMin = static_cast<double>(MIN_int32);
			OutMax = static_cast<double>(MAX_int32);
			return;
		case EPCGExDataHashType::Int64:
			// Full int64 range; handled specially in the random path.
			OutMin = static_cast<double>(MIN_int64);
			OutMax = static_cast<double>(MAX_int64);
			return;
		default:
			OutMin = -1.0;
			OutMax = 1.0;
			return;
		}
	}

	// Uniform random unit quaternion (Shoemake).
	FQuat RandomUnitQuat(FRandomStream& Stream)
	{
		const double U1 = static_cast<double>(Stream.GetFraction());
		const double U2 = static_cast<double>(Stream.GetFraction());
		const double U3 = static_cast<double>(Stream.GetFraction());

		const double S1 = FMath::Sqrt(1.0 - U1);
		const double S2 = FMath::Sqrt(U1);
		const double T2 = 2.0 * UE_DOUBLE_PI * U2;
		const double T3 = 2.0 * UE_DOUBLE_PI * U3;

		return FQuat(
			S1 * FMath::Sin(T2),
			S1 * FMath::Cos(T2),
			S2 * FMath::Sin(T3),
			S2 * FMath::Cos(T3));
	}

	// Quaternion from per-axis Euler degrees in [Min,Max] (used when bUseRange is on).
	FQuat RandomEulerQuat(FRandomStream& Stream, double Min, double Max)
	{
		const FRotator R(Stream.FRandRange(Min, Max), Stream.FRandRange(Min, Max), Stream.FRandRange(Min, Max));
		return R.Quaternion();
	}

	// FRandomStream::RandRange(MIN_int32, MAX_int32) overflows when computing
	// (Max - Min + 1), so the full-range case needs its own path.
	int32 RandomInt32(FRandomStream& Stream, int32 Min, int32 Max)
	{
		if (Min == MIN_int32 && Max == MAX_int32)
		{
			return static_cast<int32>(Stream.GetUnsignedInt());
		}
		if (Max <= Min)
		{
			return Min;
		}
		return Stream.RandRange(Min, Max);
	}

	int64 RandomInt64(FRandomStream& Stream, int64 Min, int64 Max)
	{
		const uint64 Raw =
			(static_cast<uint64>(static_cast<uint32>(Stream.GetUnsignedInt())) << 32) |
			static_cast<uint64>(static_cast<uint32>(Stream.GetUnsignedInt()));

		if (Min == MIN_int64 && Max == MAX_int64)
		{
			return static_cast<int64>(Raw);
		}

		if (Max <= Min)
		{
			return Min;
		}

		const uint64 Span = static_cast<uint64>(Max - Min) + 1ULL;
		return Min + static_cast<int64>(Raw % Span);
	}
}

#if WITH_EDITOR
FString UPCGExDataHashSettings::GetEnumDisplayName() const
{
	const UEnum* EnumPtr = StaticEnum<EPCGExDataHashType>();
	if (!EnumPtr)
	{
		return FString();
	}
	return EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(OutputType)).ToString();
}
#endif

#if PCGEX_ENGINE_VERSION >= 507
FPCGDataTypeIdentifier UPCGExDataHashSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (!InPin->IsOutputPin() || InPin->Properties.Label != PCGExDataHash::OutputValueLabel)
	{
		return Super::GetCurrentPinTypesID(InPin);
	}

	FPCGDataTypeIdentifier Id = FPCGDataTypeInfoParam::AsId();
	Id.CustomSubtype = static_cast<int32>(PCGExDataHash::ToMetadataType(OutputType));
	return Id;
}
#endif

TArray<FPCGPinProperties> UPCGExDataHashSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGPinConstants::DefaultInputLabel, "Any combination of data. The output value is a deterministic function of the inputs' kind, element count, spatial bounds, plus the Salt.", Normal)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExDataHashSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(PCGExDataHash::OutputValueLabel, "Single random value derived from the input data.", Required)
	return PinProperties;
}

FPCGElementPtr UPCGExDataHashSettings::CreateElement() const
{
	return MakeShared<FPCGExDataHashElement>();
}

bool FPCGExDataHashElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGExDataHashSettings* Settings = Context->GetInputSettings<UPCGExDataHashSettings>();
	check(Settings);

	if (!PCGExMetaHelpers::IsWritableAttributeName(Settings->OutputAttributeName))
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(FTEXT("\"{0}\" is not a valid attribute name."), FText::FromName(Settings->OutputAttributeName)));
		return true;
	}

	// Hash all inputs on the default pin. Order matters: changing connection order
	// changes the value, consistent with how PCG iterates pin inputs.
	uint64 Hash = PCGExDataHash::FnvOffsetBasis;

	// Via uint32 so a negative salt doesn't sign-extend into the high word.
	Hash = PCGExDataHash::Mix(Hash, static_cast<uint64>(static_cast<uint32>(Settings->Salt)));

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	Hash = PCGExDataHash::Mix(Hash, static_cast<uint64>(Inputs.Num()));

	for (const FPCGTaggedData& Tagged : Inputs)
	{
		Hash = PCGExDataHash::HashInput(Hash, Tagged.Data);
	}

	Hash = PCGExDataHash::Avalanche(Hash);

	FRandomStream Stream(static_cast<int32>(static_cast<uint32>(Hash ^ (Hash >> 32))));

	UPCGParamData* OutputData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	check(OutputData && OutputData->Metadata);

	const FName AttrName = Settings->OutputAttributeName;
	const PCGMetadataEntryKey Entry = OutputData->Metadata->AddEntry();

	double RMin = 0.0;
	double RMax = 0.0;
	PCGExDataHash::GetResolvedRange(Settings, Settings->OutputType, RMin, RMax);

	switch (Settings->OutputType)
	{
	case EPCGExDataHashType::Bool:
	{
		const bool Value = Stream.RandRange(0, 1) != 0;
		FPCGMetadataAttribute<bool>* Attr = OutputData->Metadata->CreateAttribute<bool>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Int32:
	{
		const int32 Min32 = Settings->bUseRange ? static_cast<int32>(FMath::Clamp(RMin, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32))) : MIN_int32;
		const int32 Max32 = Settings->bUseRange ? static_cast<int32>(FMath::Clamp(RMax, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32))) : MAX_int32;
		const int32 Value = PCGExDataHash::RandomInt32(Stream, Min32, Max32);
		FPCGMetadataAttribute<int32>* Attr = OutputData->Metadata->CreateAttribute<int32>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Int64:
	{
		const int64 Min64 = Settings->bUseRange ? static_cast<int64>(FMath::Clamp(RMin, static_cast<double>(MIN_int64), static_cast<double>(MAX_int64))) : MIN_int64;
		const int64 Max64 = Settings->bUseRange ? static_cast<int64>(FMath::Clamp(RMax, static_cast<double>(MIN_int64), static_cast<double>(MAX_int64))) : MAX_int64;
		const int64 Value = PCGExDataHash::RandomInt64(Stream, Min64, Max64);
		FPCGMetadataAttribute<int64>* Attr = OutputData->Metadata->CreateAttribute<int64>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Float:
	{
		const float Value = static_cast<float>(Stream.FRandRange(RMin, RMax));
		FPCGMetadataAttribute<float>* Attr = OutputData->Metadata->CreateAttribute<float>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Double:
	{
		const double Value = Stream.FRandRange(RMin, RMax);
		FPCGMetadataAttribute<double>* Attr = OutputData->Metadata->CreateAttribute<double>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Vector2:
	{
		const FVector2D Value(Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax));
		FPCGMetadataAttribute<FVector2D>* Attr = OutputData->Metadata->CreateAttribute<FVector2D>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Vector:
	{
		const FVector Value(Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax));
		FPCGMetadataAttribute<FVector>* Attr = OutputData->Metadata->CreateAttribute<FVector>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Vector4:
	{
		const FVector4 Value(Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax));
		FPCGMetadataAttribute<FVector4>* Attr = OutputData->Metadata->CreateAttribute<FVector4>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Quaternion:
	{
		const FQuat Value = Settings->bUseRange
			? PCGExDataHash::RandomEulerQuat(Stream, RMin, RMax)
			: PCGExDataHash::RandomUnitQuat(Stream);
		FPCGMetadataAttribute<FQuat>* Attr = OutputData->Metadata->CreateAttribute<FQuat>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Rotator:
	{
		const FRotator Value(Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax));
		FPCGMetadataAttribute<FRotator>* Attr = OutputData->Metadata->CreateAttribute<FRotator>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	case EPCGExDataHashType::Transform:
	{
		const FVector Location(Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax), Stream.FRandRange(RMin, RMax));
		const FQuat Rotation = Settings->bUseRange
			? PCGExDataHash::RandomEulerQuat(Stream, RMin, RMax)
			: PCGExDataHash::RandomUnitQuat(Stream);
		// Scale: special case [0,1] when range is off, otherwise share the user's range.
		const double ScaleMin = Settings->bUseRange ? RMin : 0.0;
		const double ScaleMax = Settings->bUseRange ? RMax : 1.0;
		const FVector Scale(Stream.FRandRange(ScaleMin, ScaleMax), Stream.FRandRange(ScaleMin, ScaleMax), Stream.FRandRange(ScaleMin, ScaleMax));
		const FTransform Value(Rotation, Location, Scale);
		FPCGMetadataAttribute<FTransform>* Attr = OutputData->Metadata->CreateAttribute<FTransform>(AttrName, Value, true, true);
		Attr->SetValue(Entry, Value);
		break;
	}
	}

	FPCGTaggedData& Staged = Context->OutputData.TaggedData.Emplace_GetRef();
	Staged.Pin = PCGExDataHash::OutputValueLabel;
	Staged.Data = OutputData;

	return true;
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
