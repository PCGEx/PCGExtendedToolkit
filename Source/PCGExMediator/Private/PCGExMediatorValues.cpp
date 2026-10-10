// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorValues.h"

#include "PCGExMediatorDiagnostics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UObject/Class.h"

namespace PCGExMediatorValues
{
	using namespace PCGExMediator;

	// Largest integer a JSON number carries exactly.
	constexpr int64 ExactIntegerLimit = 9007199254740992LL;

	FString ShapeName(const TSharedPtr<FJsonValue>& Json)
	{
		if (!Json.IsValid()) { return TEXT("missing"); }
		switch (Json->Type)
		{
		case EJson::Null: return TEXT("null");
		case EJson::String: return TEXT("string");
		case EJson::Number: return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array: return TEXT("array");
		case EJson::Object: return TEXT("object");
		default: return TEXT("none");
		}
	}

	bool Fail(const FString& Expected, const TSharedPtr<FJsonValue>& Json)
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("expected %s, got %s"), *Expected, *ShapeName(Json)));
		return false;
	}

	TSharedRef<FJsonValue> Num(const double V)
	{
		return MakeShared<FJsonValueNumber>(V);
	}

	TSharedRef<FJsonValue> NumArray(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Values.size());
		for (const double V : Values)
		{
			Array.Add(Num(V));
		}
		return MakeShared<FJsonValueArray>(Array);
	}

	bool ReadNumber(const TSharedPtr<FJsonValue>& Json, double& Out)
	{
		if (!Json.IsValid() || Json->Type != EJson::Number) { return false; }
		Out = Json->AsNumber();
		return true;
	}

	// N numbers from an array, or from an object with the given keys.
	bool ReadComponents(const TSharedPtr<FJsonValue>& Json, const TArrayView<const TCHAR* const> Keys, TArrayView<double> Out)
	{
		if (!Json.IsValid()) { return false; }
		if (Json->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array = Json->AsArray();
			if (Array.Num() != Out.Num()) { return false; }
			for (int32 i = 0; i < Out.Num(); ++i)
			{
				if (!ReadNumber(Array[i], Out[i])) { return false; }
			}
			return true;
		}
		if (Json->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& Obj = Json->AsObject();
			for (int32 i = 0; i < Out.Num(); ++i)
			{
				const TSharedPtr<FJsonValue>* Field = Obj->Values.Find(Keys[i]);
				if (!Field || !ReadNumber(*Field, Out[i])) { return false; }
			}
			return true;
		}
		return false;
	}

	constexpr const TCHAR* XYZW[] = {TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w")};
	constexpr const TCHAR* PYR[] = {TEXT("pitch"), TEXT("yaw"), TEXT("roll")};

	// A rotation slot accepts a quat (4 components, x/y/z/w) or a rotator (3 components, pitch/yaw/roll).
	bool ReadRotation(const TSharedPtr<FJsonValue>& Json, FQuat& Out)
	{
		double Q[4];
		if (ReadComponents(Json, XYZW, Q))
		{
			Out = FQuat(Q[0], Q[1], Q[2], Q[3]).GetNormalized();
			return true;
		}
		double R[3];
		if (ReadComponents(Json, PYR, R))
		{
			Out = FRotator(R[0], R[1], R[2]).Quaternion();
			return true;
		}
		return false;
	}

	TSharedRef<FJsonValue> WriteQuat(const FQuat& Q)
	{
		return NumArray({Q.X, Q.Y, Q.Z, Q.W});
	}

	TSharedRef<FJsonValue> WriteRotator(const FRotator& R)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("pitch"), R.Pitch);
		Obj->SetNumberField(TEXT("yaw"), R.Yaw);
		Obj->SetNumberField(TEXT("roll"), R.Roll);
		return MakeShared<FJsonValueObject>(Obj);
	}

	bool ReadInteger(const TSharedPtr<FJsonValue>& Json, int64& Out)
	{
		if (!Json.IsValid()) { return false; }
		if (Json->Type == EJson::Number)
		{
			const double D = Json->AsNumber();
			if (FMath::Frac(D) != 0.0)
			{
				Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("%g truncated to an integer"), D));
			}
			Out = static_cast<int64>(D);
			return true;
		}
		if (Json->Type == EJson::String)
		{
			return LexTryParseString(Out, *Json->AsString());
		}
		return false;
	}

	TSharedRef<FJsonValue> WriteInteger(const int64 V)
	{
		if (V > ExactIntegerLimit || V < -ExactIntegerLimit)
		{
			return MakeShared<FJsonValueString>(LexToString(V));
		}
		return Num(static_cast<double>(V));
	}

	TSharedRef<FJsonObject> NumberSchema(const TCHAR* Description)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("number"));
		S->SetStringField(TEXT("description"), Description);
		return S;
	}

	TSharedRef<FJsonObject> ArraySchema(const int32 Count, const TCHAR* Description)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("array"));
		TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
		Items->SetStringField(TEXT("type"), TEXT("number"));
		S->SetObjectField(TEXT("items"), Items);
		S->SetNumberField(TEXT("minItems"), Count);
		S->SetNumberField(TEXT("maxItems"), Count);
		S->SetStringField(TEXT("description"), Description);
		return S;
	}

	TSharedRef<FJsonObject> StringSchema(const TCHAR* Description)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("string"));
		S->SetStringField(TEXT("description"), Description);
		return S;
	}

	bool IsBitflags(const UEnum* Enum)
	{
		return Enum && Enum->HasMetaData(TEXT("Bitflags"));
	}

	// Enumerators worth naming: every entry but the generated _MAX.
	void ForEachEnumerator(const UEnum* Enum, TFunctionRef<void(int32 Index, int64 Value, const FString& Name)> Fn)
	{
		const int32 Count = Enum->ContainsExistingMax() ? Enum->NumEnums() - 1 : Enum->NumEnums();
		for (int32 i = 0; i < Count; ++i)
		{
			Fn(i, Enum->GetValueByIndex(i), Enum->GetNameStringByIndex(i));
		}
	}
}

bool PCGExMediator::Values::IsSupported(const EPCGMetadataTypes Type)
{
	switch (Type)
	{
	case EPCGMetadataTypes::Float:
	case EPCGMetadataTypes::Double:
	case EPCGMetadataTypes::Integer32:
	case EPCGMetadataTypes::Integer64:
	case EPCGMetadataTypes::Vector2:
	case EPCGMetadataTypes::Vector:
	case EPCGMetadataTypes::Vector4:
	case EPCGMetadataTypes::Quaternion:
	case EPCGMetadataTypes::Transform:
	case EPCGMetadataTypes::String:
	case EPCGMetadataTypes::Boolean:
	case EPCGMetadataTypes::Rotator:
	case EPCGMetadataTypes::Name:
	case EPCGMetadataTypes::SoftObjectPath:
	case EPCGMetadataTypes::SoftClassPath:
		return true;
	default:
		return false;
	}
}

TSharedPtr<FJsonValue> PCGExMediator::Values::Encode(const EPCGMetadataTypes Type, const void* Value)
{
	using namespace PCGExMediatorValues;

	switch (Type)
	{
	case EPCGMetadataTypes::Float: return Num(*static_cast<const float*>(Value));
	case EPCGMetadataTypes::Double: return Num(*static_cast<const double*>(Value));
	case EPCGMetadataTypes::Integer32: return Num(*static_cast<const int32*>(Value));
	case EPCGMetadataTypes::Integer64: return WriteInteger(*static_cast<const int64*>(Value));
	case EPCGMetadataTypes::Boolean: return MakeShared<FJsonValueBoolean>(*static_cast<const bool*>(Value));
	case EPCGMetadataTypes::Vector2:
		{
			const FVector2D& V = *static_cast<const FVector2D*>(Value);
			return NumArray({V.X, V.Y});
		}
	case EPCGMetadataTypes::Vector:
		{
			const FVector& V = *static_cast<const FVector*>(Value);
			return NumArray({V.X, V.Y, V.Z});
		}
	case EPCGMetadataTypes::Vector4:
		{
			const FVector4& V = *static_cast<const FVector4*>(Value);
			return NumArray({V.X, V.Y, V.Z, V.W});
		}
	case EPCGMetadataTypes::Quaternion: return WriteQuat(*static_cast<const FQuat*>(Value));
	case EPCGMetadataTypes::Rotator: return WriteRotator(*static_cast<const FRotator*>(Value));
	case EPCGMetadataTypes::Transform:
		{
			const FTransform& T = *static_cast<const FTransform*>(Value);
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			const FVector L = T.GetLocation();
			const FVector S = T.GetScale3D();
			Obj->SetField(TEXT("location"), NumArray({L.X, L.Y, L.Z}));
			Obj->SetField(TEXT("rotation"), WriteQuat(T.GetRotation()));
			Obj->SetField(TEXT("scale"), NumArray({S.X, S.Y, S.Z}));
			return MakeShared<FJsonValueObject>(Obj);
		}
	case EPCGMetadataTypes::String: return MakeShared<FJsonValueString>(*static_cast<const FString*>(Value));
	case EPCGMetadataTypes::Name: return MakeShared<FJsonValueString>(static_cast<const FName*>(Value)->ToString());
	case EPCGMetadataTypes::SoftObjectPath: return MakeShared<FJsonValueString>(static_cast<const FSoftObjectPath*>(Value)->ToString());
	case EPCGMetadataTypes::SoftClassPath: return MakeShared<FJsonValueString>(static_cast<const FSoftClassPath*>(Value)->ToString());
	default:
		return nullptr;
	}
}

bool PCGExMediator::Values::Decode(const TSharedPtr<FJsonValue>& Json, const EPCGMetadataTypes Type, void* OutValue)
{
	using namespace PCGExMediatorValues;

	switch (Type)
	{
	case EPCGMetadataTypes::Float:
	case EPCGMetadataTypes::Double:
		{
			double D = 0;
			if (!ReadNumber(Json, D)) { return Fail(TEXT("a number"), Json); }
			if (Type == EPCGMetadataTypes::Float) { *static_cast<float*>(OutValue) = static_cast<float>(D); }
			else { *static_cast<double*>(OutValue) = D; }
			return true;
		}
	case EPCGMetadataTypes::Integer32:
	case EPCGMetadataTypes::Integer64:
		{
			int64 I = 0;
			if (!ReadInteger(Json, I)) { return Fail(TEXT("an integer (number or numeric string)"), Json); }
			if (Type == EPCGMetadataTypes::Integer32)
			{
				if (I > MAX_int32 || I < MIN_int32)
				{
					Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%lld does not fit a 32-bit integer"), I));
					return false;
				}
				*static_cast<int32*>(OutValue) = static_cast<int32>(I);
			}
			else { *static_cast<int64*>(OutValue) = I; }
			return true;
		}
	case EPCGMetadataTypes::Boolean:
		{
			if (!Json.IsValid() || Json->Type != EJson::Boolean) { return Fail(TEXT("a boolean"), Json); }
			*static_cast<bool*>(OutValue) = Json->AsBool();
			return true;
		}
	case EPCGMetadataTypes::Vector2:
		{
			double C[2];
			if (!ReadComponents(Json, XYZW, C)) { return Fail(TEXT("[x, y]"), Json); }
			*static_cast<FVector2D*>(OutValue) = FVector2D(C[0], C[1]);
			return true;
		}
	case EPCGMetadataTypes::Vector:
		{
			double C[3];
			if (!ReadComponents(Json, XYZW, C)) { return Fail(TEXT("[x, y, z]"), Json); }
			*static_cast<FVector*>(OutValue) = FVector(C[0], C[1], C[2]);
			return true;
		}
	case EPCGMetadataTypes::Vector4:
		{
			double C[4];
			if (!ReadComponents(Json, XYZW, C)) { return Fail(TEXT("[x, y, z, w]"), Json); }
			*static_cast<FVector4*>(OutValue) = FVector4(C[0], C[1], C[2], C[3]);
			return true;
		}
	case EPCGMetadataTypes::Quaternion:
		{
			FQuat Q;
			if (!ReadRotation(Json, Q)) { return Fail(TEXT("a quat [x, y, z, w] or a rotator {pitch, yaw, roll}"), Json); }
			*static_cast<FQuat*>(OutValue) = Q;
			return true;
		}
	case EPCGMetadataTypes::Rotator:
		{
			double R[3];
			if (ReadComponents(Json, PYR, R))
			{
				*static_cast<FRotator*>(OutValue) = FRotator(R[0], R[1], R[2]);
				return true;
			}
			FQuat Q;
			if (!ReadRotation(Json, Q)) { return Fail(TEXT("a rotator {pitch, yaw, roll} or a quat [x, y, z, w]"), Json); }
			*static_cast<FRotator*>(OutValue) = Q.Rotator();
			return true;
		}
	case EPCGMetadataTypes::Transform:
		{
			if (!Json.IsValid() || Json->Type != EJson::Object) { return Fail(TEXT("{location, rotation, scale}"), Json); }
			const TSharedPtr<FJsonObject>& Obj = Json->AsObject();
			FVector Location = FVector::ZeroVector;
			FQuat Rotation = FQuat::Identity;
			FVector Scale = FVector::OneVector;

			if (const TSharedPtr<FJsonValue>* F = Obj->Values.Find(TEXT("location")))
			{
				FPathScope P(TEXT("location"));
				if (!Decode(*F, EPCGMetadataTypes::Vector, &Location)) { return false; }
			}
			if (const TSharedPtr<FJsonValue>* F = Obj->Values.Find(TEXT("rotation")))
			{
				FPathScope P(TEXT("rotation"));
				if (!ReadRotation(*F, Rotation)) { return Fail(TEXT("a quat [x, y, z, w] or a rotator {pitch, yaw, roll}"), *F); }
			}
			if (const TSharedPtr<FJsonValue>* F = Obj->Values.Find(TEXT("scale")))
			{
				FPathScope P(TEXT("scale"));
				if (!Decode(*F, EPCGMetadataTypes::Vector, &Scale)) { return false; }
			}
			*static_cast<FTransform*>(OutValue) = FTransform(Rotation, Location, Scale);
			return true;
		}
	case EPCGMetadataTypes::String:
		{
			if (!Json.IsValid() || Json->Type != EJson::String) { return Fail(TEXT("a string"), Json); }
			*static_cast<FString*>(OutValue) = Json->AsString();
			return true;
		}
	case EPCGMetadataTypes::Name:
		{
			if (!Json.IsValid() || Json->Type != EJson::String) { return Fail(TEXT("a string"), Json); }
			const FString S = Json->AsString();
			if (S.Len() >= NAME_SIZE)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("name longer than the FName limit"));
				return false;
			}
			*static_cast<FName*>(OutValue) = S.IsEmpty() ? NAME_None : FName(*S);
			return true;
		}
	case EPCGMetadataTypes::SoftObjectPath:
	case EPCGMetadataTypes::SoftClassPath:
		{
			if (!Json.IsValid() || Json->Type != EJson::String) { return Fail(TEXT("an object path string"), Json); }
			const FString S = Json->AsString();
			if (!S.IsEmpty() && !FPackageName::IsValidObjectPath(S))
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not an object path"), *S));
				return false;
			}
			if (Type == EPCGMetadataTypes::SoftObjectPath) { *static_cast<FSoftObjectPath*>(OutValue) = FSoftObjectPath(S); }
			else { *static_cast<FSoftClassPath*>(OutValue) = FSoftClassPath(S); }
			return true;
		}
	default:
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("type %s has no JSON representation"), *TypeToString(Type)));
		return false;
	}
}

TSharedPtr<FJsonValue> PCGExMediator::Values::EncodeEnum(const UEnum* Enum, const int64 Value)
{
	using namespace PCGExMediatorValues;

	if (!Enum) { return WriteInteger(Value); }

	const FString Exact = Enum->GetNameStringByValue(Value);
	if (!Exact.IsEmpty()) { return MakeShared<FJsonValueString>(Exact); }

	if (IsBitflags(Enum))
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		int64 Remaining = Value;
		ForEachEnumerator(Enum, [&](int32, const int64 V, const FString& Name)
		{
			if (V != 0 && FMath::CountBits64(static_cast<uint64>(V)) == 1 && (Remaining & V) == V)
			{
				Names.Add(MakeShared<FJsonValueString>(Name));
				Remaining &= ~V;
			}
		});
		if (Remaining == 0) { return MakeShared<FJsonValueArray>(Names); }
	}

	return WriteInteger(Value);
}

bool PCGExMediator::Values::DecodeEnum(const UEnum* Enum, const TSharedPtr<FJsonValue>& Json, int64& OutValue)
{
	using namespace PCGExMediatorValues;

	if (!Json.IsValid()) { return Fail(TEXT("an enumerator name, an array of names or a number"), Json); }

	auto ByName = [&](const FString& Name, int64& Out) -> bool
	{
		if (!Enum)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' cannot be resolved: no enum class"), *Name));
			return false;
		}
		const int64 V = Enum->GetValueByNameString(Name);
		if (V == INDEX_NONE)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not an enumerator of %s"), *Name, *Enum->GetName()));
			return false;
		}
		Out = V;
		return true;
	};

	if (Json->Type == EJson::String)
	{
		return ByName(Json->AsString(), OutValue);
	}
	if (Json->Type == EJson::Array)
	{
		int64 Combined = 0;
		const TArray<TSharedPtr<FJsonValue>>& Array = Json->AsArray();
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			FPathScope P(i);
			if (!Array[i].IsValid() || Array[i]->Type != EJson::String) { return Fail(TEXT("an enumerator name"), Array[i]); }
			int64 V = 0;
			if (!ByName(Array[i]->AsString(), V)) { return false; }
			Combined |= V;
		}
		OutValue = Combined;
		return true;
	}

	int64 I = 0;
	if (!ReadInteger(Json, I)) { return Fail(TEXT("an enumerator name, an array of names or a number"), Json); }
	if (Enum && !Enum->IsValidEnumValueOrBitfield(I))
	{
		Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("%lld is not a value of %s; stored as-is"), I, *Enum->GetName()));
	}
	OutValue = I;
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::Values::DescribeShape(const EPCGMetadataTypes Type)
{
	using namespace PCGExMediatorValues;

	switch (Type)
	{
	case EPCGMetadataTypes::Float: return NumberSchema(TEXT("32-bit float"));
	case EPCGMetadataTypes::Double: return NumberSchema(TEXT("64-bit float"));
	case EPCGMetadataTypes::Integer32:
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("type"), TEXT("integer"));
			S->SetStringField(TEXT("description"), TEXT("32-bit integer"));
			return S;
		}
	case EPCGMetadataTypes::Integer64:
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Types;
			Types.Add(MakeShared<FJsonValueString>(TEXT("integer")));
			Types.Add(MakeShared<FJsonValueString>(TEXT("string")));
			S->SetArrayField(TEXT("type"), Types);
			S->SetStringField(TEXT("description"), TEXT("64-bit integer; a decimal string beyond 2^53"));
			return S;
		}
	case EPCGMetadataTypes::Boolean:
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("type"), TEXT("boolean"));
			return S;
		}
	case EPCGMetadataTypes::Vector2: return ArraySchema(2, TEXT("[x, y]"));
	case EPCGMetadataTypes::Vector: return ArraySchema(3, TEXT("[x, y, z]"));
	case EPCGMetadataTypes::Vector4: return ArraySchema(4, TEXT("[x, y, z, w]"));
	case EPCGMetadataTypes::Quaternion: return ArraySchema(4, TEXT("quat [x, y, z, w]; a rotator {pitch, yaw, roll} is also accepted"));
	case EPCGMetadataTypes::Rotator:
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("pitch"), NumberSchema(TEXT("degrees")));
			Props->SetObjectField(TEXT("yaw"), NumberSchema(TEXT("degrees")));
			Props->SetObjectField(TEXT("roll"), NumberSchema(TEXT("degrees")));
			S->SetObjectField(TEXT("properties"), Props);
			S->SetStringField(TEXT("description"), TEXT("rotator in degrees; a quat [x, y, z, w] is also accepted"));
			return S;
		}
	case EPCGMetadataTypes::Transform:
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("location"), ArraySchema(3, TEXT("[x, y, z], default zero")));
			Props->SetObjectField(TEXT("rotation"), ArraySchema(4, TEXT("quat [x, y, z, w] or rotator {pitch, yaw, roll}, default identity")));
			Props->SetObjectField(TEXT("scale"), ArraySchema(3, TEXT("[x, y, z], default one")));
			S->SetObjectField(TEXT("properties"), Props);
			return S;
		}
	case EPCGMetadataTypes::String: return StringSchema(TEXT("string"));
	case EPCGMetadataTypes::Name: return StringSchema(TEXT("name; \"None\" is the empty name"));
	case EPCGMetadataTypes::SoftObjectPath: return StringSchema(TEXT("object path, e.g. /Game/Folder/Asset.Asset; \"\" is null"));
	case EPCGMetadataTypes::SoftClassPath: return StringSchema(TEXT("class path, e.g. /Script/Engine.StaticMesh or /Game/BP.BP_C; \"\" is null"));
	default:
		return nullptr;
	}
}

TSharedPtr<FJsonObject> PCGExMediator::Values::DescribeEnumShape(const UEnum* Enum)
{
	using namespace PCGExMediatorValues;

	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	if (!Enum)
	{
		S->SetStringField(TEXT("type"), TEXT("string"));
		S->SetStringField(TEXT("description"), TEXT("enumerator name"));
		return S;
	}

	TArray<TSharedPtr<FJsonValue>> Names;
	ForEachEnumerator(Enum, [&](int32, int64, const FString& Name)
	{
		Names.Add(MakeShared<FJsonValueString>(Name));
	});

	TSharedRef<FJsonObject> NameSchema = MakeShared<FJsonObject>();
	NameSchema->SetStringField(TEXT("type"), TEXT("string"));
	NameSchema->SetArrayField(TEXT("enum"), Names);

	if (!IsBitflags(Enum))
	{
		S->SetStringField(TEXT("type"), TEXT("string"));
		S->SetArrayField(TEXT("enum"), Names);
		S->SetStringField(TEXT("description"), FString::Printf(TEXT("enumerator of %s"), *Enum->GetName()));
		return S;
	}

	TSharedRef<FJsonObject> ArrayForm = MakeShared<FJsonObject>();
	ArrayForm->SetStringField(TEXT("type"), TEXT("array"));
	ArrayForm->SetObjectField(TEXT("items"), NameSchema);

	TArray<TSharedPtr<FJsonValue>> OneOf;
	OneOf.Add(MakeShared<FJsonValueObject>(NameSchema));
	OneOf.Add(MakeShared<FJsonValueObject>(ArrayForm));
	S->SetArrayField(TEXT("oneOf"), OneOf);
	S->SetStringField(TEXT("description"), FString::Printf(TEXT("flag of %s, or an array of flags"), *Enum->GetName()));
	return S;
}

FString PCGExMediator::Values::TypeToString(const EPCGMetadataTypes Type)
{
	switch (Type)
	{
	case EPCGMetadataTypes::Float: return TEXT("Float");
	case EPCGMetadataTypes::Double: return TEXT("Double");
	case EPCGMetadataTypes::Integer32: return TEXT("Integer32");
	case EPCGMetadataTypes::Integer64: return TEXT("Integer64");
	case EPCGMetadataTypes::Vector2: return TEXT("Vector2");
	case EPCGMetadataTypes::Vector: return TEXT("Vector");
	case EPCGMetadataTypes::Vector4: return TEXT("Vector4");
	case EPCGMetadataTypes::Quaternion: return TEXT("Quaternion");
	case EPCGMetadataTypes::Transform: return TEXT("Transform");
	case EPCGMetadataTypes::String: return TEXT("String");
	case EPCGMetadataTypes::Boolean: return TEXT("Boolean");
	case EPCGMetadataTypes::Rotator: return TEXT("Rotator");
	case EPCGMetadataTypes::Name: return TEXT("Name");
	case EPCGMetadataTypes::SoftObjectPath: return TEXT("SoftObjectPath");
	case EPCGMetadataTypes::SoftClassPath: return TEXT("SoftClassPath");
	default: return TEXT("Unknown");
	}
}
