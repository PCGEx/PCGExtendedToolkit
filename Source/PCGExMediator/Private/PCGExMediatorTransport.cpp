// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorTransport.h"

#include "PCGExMediatorConverter.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorReflection.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "PCGExMediatorTransport"

namespace PCGExMediatorTransport
{
	using namespace PCGExMediator;

	// Scratch storage for one property value, so a member decodes without touching the host.
	struct FPropertyScratch
	{
		FProperty* Property = nullptr;
		void* Memory = nullptr;

		explicit FPropertyScratch(FProperty* InProperty)
			: Property(InProperty)
		{
			Memory = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Memory);
		}

		~FPropertyScratch()
		{
			Property->DestroyValue(Memory);
			FMemory::Free(Memory);
		}

		FPropertyScratch(const FPropertyScratch&) = delete;
		FPropertyScratch& operator=(const FPropertyScratch&) = delete;
	};

	TSharedRef<FJsonObject> EnvelopeSchema(const FName FormatId, const int32 Version, const TSharedPtr<FJsonObject>& DataSchema)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Format, Schema::Const(FormatId.ToString()));
		Props->SetObjectField(Keys::Version, Schema::Const(Version));
		Props->SetObjectField(Keys::Data, DataSchema.IsValid() ? DataSchema.ToSharedRef() : MakeShared<FJsonObject>());

		const FString Required[] = {Keys::Format, Keys::Version, Keys::Data};
		TSharedRef<FJsonObject> S = Schema::Object(Props, FString(), Required);
		S->SetStringField(TEXT("$schema"), TEXT("https://json-schema.org/draft/2020-12/schema"));
		return S;
	}

	// Decodes one bound member into Scratch, starting from the live value so a merging codec sees current state.
	bool DecodeMember(const FPCGExMediatorBinding& Binding, UObject* Host, FProperty* Property, const TSharedPtr<FJsonValue>& Json, FPropertyScratch& Scratch, FPCGExMediatorDecodedMember Decoded)
	{
		FPathScope P(Property->GetName());
		Property->CopyCompleteValue(Scratch.Memory, Property->ContainerPtrToValuePtr<void>(Host));

		bool bOk = false;
		auto Import = [&]()
		{
			bOk = Reflect::DecodeProperty(Property, Scratch.Memory, Json);
		};

		if (Binding.WrapImport)
		{
			Binding.WrapImport(Host, Property->GetFName(), Decoded, Import);
		}
		else
		{
			Import();
		}
		return bOk;
	}
}

TSharedRef<FJsonObject> PCGExMediator::MakeEnvelope(const FName FormatId, const int32 Version, const TSharedPtr<FJsonValue>& Data)
{
	TSharedRef<FJsonObject> Doc = MakeShared<FJsonObject>();
	Doc->SetStringField(Keys::Format, FormatId.ToString());
	Doc->SetNumberField(Keys::Version, Version);
	Doc->SetField(Keys::Data, Data.IsValid() ? Data : MakeShared<FJsonValueNull>());
	return Doc;
}

bool PCGExMediator::ReadEnvelope(const FJsonObject& Doc, const FName ExpectedFormat, const int32 SupportedVersion, TSharedPtr<FJsonValue>& OutData)
{
	FString Format;
	if (!Doc.TryGetStringField(Keys::Format, Format))
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Format, TEXT("missing"));
		return false;
	}
	if (FName(*Format) != ExpectedFormat)
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Format, FString::Printf(TEXT("'%s' is not '%s'"), *Format, *ExpectedFormat.ToString()));
		return false;
	}

	double Version = 1;
	if (!Doc.TryGetNumberField(Keys::Version, Version))
	{
		Report(EPCGExMediatorSeverity::Warning, Keys::Version, TEXT("missing; reading as version 1"));
	}
	if (static_cast<int32>(Version) > SupportedVersion)
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Version, FString::Printf(TEXT("%d is newer than the supported %d"), static_cast<int32>(Version), SupportedVersion));
		return false;
	}

	const TSharedPtr<FJsonValue> Data = Doc.TryGetField(Keys::Data);
	if (!Data.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Data, TEXT("missing"));
		return false;
	}
	OutData = Data;
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::ExportStruct(const UScriptStruct* Struct, const void* Memory)
{
	check(IsInGameThread());

	const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct);
	if (!Format.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no format registered for %s"), *GetNameSafe(Struct)));
		return nullptr;
	}

	TSharedPtr<FJsonObject> Body;
	switch (ConverterToJson(*Format->Converter, Memory, Body))
	{
	case EConverterOutcome::Done:
		break;
	case EConverterOutcome::UseDefault:
		Body = MakeShared<FJsonObject>();
		if (!FJsonObjectConverter::UStructToJsonObject(Struct, Memory, Body.ToSharedRef()))
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("reflection export failed"));
			return nullptr;
		}
		break;
	default:
		return nullptr;
	}

	return MakeEnvelope(Format->Id, Format->Version, MakeShared<FJsonValueObject>(Body));
}

bool PCGExMediator::ImportStruct(const FJsonObject& Doc, const UScriptStruct* Struct, void* Memory)
{
	check(IsInGameThread());

	const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct);
	if (!Format.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no format registered for %s"), *GetNameSafe(Struct)));
		return false;
	}

	TSharedPtr<FJsonValue> Data;
	if (!ReadEnvelope(Doc, Format->Id, Format->Version, Data)) { return false; }
	if (Data->Type != EJson::Object)
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Data, TEXT("expected an object"));
		return false;
	}

	// Decode into a copy; the live struct changes only on success.
	FInstancedStruct Temp;
	Temp.InitializeAs(Struct, static_cast<const uint8*>(Memory));

	FPCGExMediatorDiagnostics Local;
	bool bOk = false;
	{
		FScope Scope(Local);
		FPathScope P(Keys::Data);
		switch (ConverterFromJson(*Format->Converter, Temp.GetMutableMemory(), Data->AsObject()))
		{
		case EConverterOutcome::Done:
			bOk = true;
			break;
		case EConverterOutcome::UseDefault:
			{
				FText FailReason;
				bOk = FJsonObjectConverter::JsonObjectToUStruct(Data->AsObject().ToSharedRef(), Struct, Temp.GetMutableMemory(), 0, 0, false, &FailReason);
				if (!bOk && !FailReason.IsEmpty()) { Report(EPCGExMediatorSeverity::Error, FailReason.ToString()); }
			}
			break;
		default:
			break;
		}
	}

	// Forward everything the converter reported, then decide on the whole.
	Forward(Local);
	if (!bOk || Local.HasErrors()) { return false; }

	Struct->CopyScriptStruct(Memory, Temp.GetMemory());
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::ExportObject(const UObject* Host)
{
	check(IsInGameThread());

	if (!Host)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no object"));
		return nullptr;
	}

	const TSharedPtr<const FPCGExMediatorBinding> Binding = FPCGExMediatorRegistry::FindBinding(Host->GetClass());
	if (!Binding.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no binding for %s"), *Host->GetClass()->GetName()));
		return nullptr;
	}

	TSharedRef<FJsonObject> Members = MakeShared<FJsonObject>();
	for (const FName& Member : Binding->Members)
	{
		FProperty* Property = FindFProperty<FProperty>(Host->GetClass(), Member);
		if (!Property)
		{
			Report(EPCGExMediatorSeverity::Error, Member.ToString(), TEXT("bound member not found on the host class"));
			return nullptr;
		}
		FPathScope P(Member.ToString());
		const TSharedPtr<FJsonValue> Value = Reflect::EncodeProperty(Property, Property->ContainerPtrToValuePtr<void>(Host));
		if (!Value.IsValid())
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("export failed"));
			return nullptr;
		}
		Members->SetField(Member.ToString(), Value);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(Keys::Class, Host->GetClass()->GetPathName());
	Data->SetObjectField(Keys::Members, Members);
	return MakeEnvelope(ObjectFormatId, ObjectFormatVersion, MakeShared<FJsonValueObject>(Data));
}

bool PCGExMediator::ImportObject(const FJsonObject& Doc, UObject* Host)
{
	using namespace PCGExMediatorTransport;

	check(IsInGameThread());

	if (!Host)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no object"));
		return false;
	}

	const TSharedPtr<const FPCGExMediatorBinding> Binding = FPCGExMediatorRegistry::FindBinding(Host->GetClass());
	if (!Binding.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no binding for %s"), *Host->GetClass()->GetName()));
		return false;
	}

	TSharedPtr<FJsonValue> DataValue;
	if (!ReadEnvelope(Doc, ObjectFormatId, ObjectFormatVersion, DataValue)) { return false; }
	if (DataValue->Type != EJson::Object)
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Data, TEXT("expected an object"));
		return false;
	}
	const TSharedPtr<FJsonObject> Data = DataValue->AsObject();

	FPathScope DataPath(Keys::Data);

	FString ClassPath;
	if (Data->TryGetStringField(Keys::Class, ClassPath))
	{
		const UClass* DocClass = FindType<UClass>(ClassPath);
		if (!DocClass || !Host->GetClass()->IsChildOf(DocClass))
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Class, FString::Printf(TEXT("'%s' does not match the target (%s)"), *ClassPath, *Host->GetClass()->GetPathName()));
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* Members = nullptr;
	if (!Data->TryGetObjectField(Keys::Members, Members))
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Members, TEXT("missing"));
		return false;
	}

	FPathScope MembersPath(Keys::Members);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Members)->Values)
	{
		if (!Binding->Members.Contains(FName(*Pair.Key)))
		{
			Report(EPCGExMediatorSeverity::Warning, Pair.Key, TEXT("not a bound member; ignored"));
		}
	}

	// Every member decodes into scratch before anything is applied: a later member reads earlier ones through
	// Decoded, and a failure anywhere leaves the host untouched.
	TArray<TUniquePtr<FPropertyScratch>> Decoded;
	Decoded.Reserve(Binding->Members.Num());
	auto DecodedMember = [&Binding, &Decoded](const FName Name) -> const void*
	{
		const int32 Index = Binding->Members.IndexOfByKey(Name);
		return (Decoded.IsValidIndex(Index) && Decoded[Index].IsValid()) ? Decoded[Index]->Memory : nullptr;
	};

	FPCGExMediatorDiagnostics Local;
	{
		FScope Scope(Local);
		for (const FName& Member : Binding->Members)
		{
			const TSharedPtr<FJsonValue> Json = (*Members)->TryGetField(Member.ToString());
			if (!Json.IsValid())
			{
				Decoded.Add(nullptr);
				continue;
			}

			FProperty* Property = FindFProperty<FProperty>(Host->GetClass(), Member);
			if (!Property)
			{
				Report(EPCGExMediatorSeverity::Error, Member.ToString(), TEXT("bound member not found on the host class"));
				break;
			}

			TUniquePtr<FPropertyScratch> Scratch = MakeUnique<FPropertyScratch>(Property);
			const bool bOk = DecodeMember(*Binding, Host, Property, Json, *Scratch, DecodedMember);
			Decoded.Add(MoveTemp(Scratch));
			if (!bOk) { break; }
		}
	}
	Forward(Local);
	if (Local.HasErrors()) { return false; }

	int32 Present = 0;
	for (const TUniquePtr<FPropertyScratch>& Scratch : Decoded) { if (Scratch.IsValid()) { ++Present; } }
	if (Present == 0)
	{
		Report(EPCGExMediatorSeverity::Warning, TEXT("no bound member present; nothing imported"));
		return true;
	}

	// Apply in binding order, each through the host's own edit hooks, inside one transaction.
	FScopedTransaction Transaction(LOCTEXT("ImportObject", "PCGEx Mediator Import"));
	Host->Modify();
	for (const TUniquePtr<FPropertyScratch>& Scratch : Decoded)
	{
		if (!Scratch.IsValid()) { continue; }
		FProperty* Property = Scratch->Property;
		Host->PreEditChange(Property);
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Host), Scratch->Memory);
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Host->PostEditChangeProperty(Event);
	}
	if (Binding->PostImport) { Binding->PostImport(Host); }
	(void)Host->MarkPackageDirty();
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeFormat(const FName FormatId)
{
	using namespace PCGExMediatorTransport;

	check(IsInGameThread());

	const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormat(FormatId);
	if (!Format.IsValid()) { return nullptr; }
	return EnvelopeSchema(Format->Id, Format->Version, Format->Describe ? Format->Describe() : nullptr);
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeObject(const UClass* HostClass)
{
	using namespace PCGExMediatorTransport;

	check(IsInGameThread());

	const TSharedPtr<const FPCGExMediatorBinding> Binding = FPCGExMediatorRegistry::FindBinding(HostClass);
	if (!Binding.IsValid()) { return nullptr; }

	TSharedRef<FJsonObject> MemberProps = MakeShared<FJsonObject>();
	for (const FName& Member : Binding->Members)
	{
		TSharedPtr<FJsonObject> MemberSchema;
		if (const FProperty* Property = FindFProperty<FProperty>(HostClass, Member)) { MemberSchema = Reflect::DescribeProperty(Property); }
		if (!MemberSchema.IsValid()) { MemberSchema = Schema::Typed(nullptr, TEXT("not a property of the host class")); }
		MemberProps->SetObjectField(Member.ToString(), MemberSchema.ToSharedRef());
	}

	TSharedRef<FJsonObject> DataProps = MakeShared<FJsonObject>();
	DataProps->SetObjectField(Keys::Class, Schema::Const(HostClass->GetPathName()));
	DataProps->SetObjectField(Keys::Members, Schema::Object(MemberProps));
	return EnvelopeSchema(ObjectFormatId, ObjectFormatVersion, Schema::Object(DataProps, Binding->Summary));
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeReflectedStruct(const FString& StructNameOrPath)
{
	check(IsInGameThread());

	const UScriptStruct* Struct = FindType<UScriptStruct>(StructNameOrPath);
	if (!Struct) { return nullptr; }

	TSharedPtr<FJsonObject> S = Reflect::DescribeStruct(Struct, &Reflect::IncludeAll, 1);
	if (S.IsValid())
	{
		S->SetStringField(TEXT("$schema"), TEXT("https://json-schema.org/draft/2020-12/schema"));
		Schema::Describe(S.ToSharedRef(), FString::Printf(TEXT("%s, reflected: every UPROPERTY by name in the dialect; nested structs one level deep, deeper ones collapsed with their own DescribeFormat path"), *Struct->GetPathName()));
	}
	return S;
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeAny(const FString& FormatIdOrClassOrStruct)
{
	const FString Trimmed = FormatIdOrClassOrStruct.TrimStartAndEnd();
	if (TSharedPtr<FJsonObject> S = DescribeFormat(FName(*Trimmed))) { return S; }
	if (const UClass* Class = FindType<UClass>(Trimmed))
	{
		if (TSharedPtr<FJsonObject> S = DescribeObject(Class)) { return S; }
	}
	return DescribeReflectedStruct(Trimmed);
}

TSharedRef<FJsonObject> PCGExMediator::ListAsJson()
{
	TArray<TSharedPtr<FJsonValue>> Formats;
	TArray<TSharedPtr<const FPCGExMediatorFormat>> RegisteredFormats;
	FPCGExMediatorRegistry::GetFormats(RegisteredFormats);
	for (const TSharedPtr<const FPCGExMediatorFormat>& Format : RegisteredFormats)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Format->Id.ToString());
		Entry->SetNumberField(TEXT("version"), Format->Version);
		Entry->SetStringField(TEXT("struct"), Format->Struct ? Format->Struct->GetPathName() : FString());
		Entry->SetStringField(TEXT("summary"), Format->Summary);
		Formats.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TArray<TSharedPtr<FJsonValue>> Bindings;
	TArray<TSharedPtr<const FPCGExMediatorBinding>> RegisteredBindings;
	FPCGExMediatorRegistry::GetBindings(RegisteredBindings);
	for (const TSharedPtr<const FPCGExMediatorBinding>& Binding : RegisteredBindings)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Binding->HostClass->GetPathName());
		TArray<FString> Members;
		for (const FName& Member : Binding->Members) { Members.Add(Member.ToString()); }
		Entry->SetArrayField(TEXT("members"), Schema::Strings(Members));
		Entry->SetStringField(TEXT("summary"), Binding->Summary);
		Bindings.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("formats"), Formats);
	Root->SetArrayField(TEXT("bindings"), Bindings);
	return Root;
}

FString PCGExMediator::ToString(const TSharedRef<FJsonObject>& Doc, const bool bPretty)
{
	FString Out;
	if (bPretty)
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Doc, Writer);
	}
	else
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Doc, Writer);
	}
	return Out;
}

TSharedPtr<FJsonObject> PCGExMediator::FromString(const FString& Text)
{
	TSharedPtr<FJsonObject> Doc;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Doc) || !Doc.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("not a JSON object: %s"), *Reader->GetErrorMessage()));
		return nullptr;
	}
	return Doc;
}

bool PCGExMediator::WriteFile(const FString& FilePath, const TSharedRef<FJsonObject>& Doc)
{
	if (!FFileHelper::SaveStringToFile(ToString(Doc), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("could not write '%s'"), *FilePath));
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::ReadFile(const FString& FilePath)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("could not read '%s'"), *FilePath));
		return nullptr;
	}
	return FromString(Text);
}

UObject* PCGExMediator::ResolveTarget(const FString& Target)
{
	check(IsInGameThread());

	if (UObject* Direct = ResolveObject(Target))
	{
		// "<graph path>:<node object name>" is itself a valid subobject path; the host is always the settings.
		if (const UPCGNode* Node = Cast<UPCGNode>(Direct))
		{
			if (UPCGSettings* Settings = Node->GetSettings()) { return Settings; }
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("node '%s' has no settings"), *Node->GetName()));
			return nullptr;
		}
		return Direct;
	}

	int32 Split = INDEX_NONE;
	if (Target.FindLastChar(TEXT(':'), Split) && Split > 0)
	{
		const FString GraphPath = Target.Left(Split);
		const FString NodeName = Target.Mid(Split + 1);
		if (const UPCGGraph* Graph = Cast<UPCGGraph>(ResolveObject(GraphPath)))
		{
			for (UPCGNode* Node : Graph->GetNodes())
			{
				if (!Node) { continue; }
				if (Node->GetAuthoredTitleName().ToString() == NodeName || Node->GetName() == NodeName || Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString() == NodeName)
				{
					if (UPCGSettings* Settings = Node->GetSettings()) { return Settings; }
				}
			}
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no node '%s' in %s"), *NodeName, *GraphPath));
			return nullptr;
		}
	}

	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' resolves to nothing (expected an object path, or <graph path>:<node>)"), *Target));
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
