// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorTransport.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "JsonObjectConverter.h"
#include "JsonObjectStructInterface.h"
#include "Misc/FileHelper.h"
#include "PCGCommon.h"
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

	TSharedRef<FJsonObject> ConstSchema(const FString& Value)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("const"), Value);
		return S;
	}

	TSharedRef<FJsonObject> ConstSchema(const int32 Value)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("const"), Value);
		return S;
	}

	TSharedRef<FJsonObject> EnvelopeSchema(const FName FormatId, const int32 Version, const TSharedPtr<FJsonObject>& DataSchema)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("$schema"), TEXT("https://json-schema.org/draft/2020-12/schema"));
		S->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Format, ConstSchema(FormatId.ToString()));
		Props->SetObjectField(Keys::Version, ConstSchema(Version));
		Props->SetObjectField(Keys::Data, DataSchema.IsValid() ? DataSchema.ToSharedRef() : MakeShared<FJsonObject>());
		S->SetObjectField(TEXT("properties"), Props);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(Keys::Format));
		Required.Add(MakeShared<FJsonValueString>(Keys::Version));
		Required.Add(MakeShared<FJsonValueString>(Keys::Data));
		S->SetArrayField(TEXT("required"), Required);
		return S;
	}

	// Decodes one bound member into Scratch. Null Json = member absent from the document (not an error).
	bool DecodeMember(const FPCGExMediatorBinding& Binding, UObject* Host, FProperty* Property, const TSharedPtr<FJsonValue>& Json, FPropertyScratch& Scratch)
	{
		FPathScope P(Property->GetName());

		// Start from the live value so a converter that merges (or a plain reflected member) sees current state.
		Property->CopyCompleteValue(Scratch.Memory, Property->ContainerPtrToValuePtr<void>(Host));

		bool bOk = false;
		auto Import = [&]()
		{
			FText FailReason;
			bOk = FJsonObjectConverter::JsonValueToUProperty(Json, Property, Scratch.Memory, 0, 0, false, &FailReason);
			if (!bOk && !FailReason.IsEmpty())
			{
				Report(EPCGExMediatorSeverity::Error, FailReason.ToString());
			}
		};

		if (Binding.WrapImport)
		{
			Binding.WrapImport(Host, Property->GetFName(), Import);
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
	const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct);
	if (!Format.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no format registered for %s"), *GetNameSafe(Struct)));
		return nullptr;
	}

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	switch (Format->Converter->ConvertToJson(Memory, Body))
	{
	case EJsonObjectConvertResult::Converted:
		break;
	case EJsonObjectConvertResult::UseDefaultConverter:
		if (!FJsonObjectConverter::UStructToJsonObject(Struct, Memory, Body.ToSharedRef()))
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("reflection export failed"));
			return nullptr;
		}
		break;
	case EJsonObjectConvertResult::IgnoreAndContinue:
		Body = MakeShared<FJsonObject>();
		break;
	default:
		Report(EPCGExMediatorSeverity::Error, TEXT("export failed"));
		return nullptr;
	}

	return MakeEnvelope(Format->Id, Format->Version, MakeShared<FJsonValueObject>(Body));
}

bool PCGExMediator::ImportStruct(const FJsonObject& Doc, const UScriptStruct* Struct, void* Memory)
{
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
		switch (Format->Converter->ConvertFromJson(Temp.GetMutableMemory(), Data->AsObject()))
		{
		case EJsonObjectConvertResult::Converted:
		case EJsonObjectConvertResult::IgnoreAndContinue:
			bOk = true;
			break;
		case EJsonObjectConvertResult::UseDefaultConverter:
			{
				FText FailReason;
				bOk = FJsonObjectConverter::JsonObjectToUStruct(Data->AsObject().ToSharedRef(), Struct, Temp.GetMutableMemory(), 0, 0, false, &FailReason);
				if (!bOk && !FailReason.IsEmpty()) { Report(EPCGExMediatorSeverity::Error, FailReason.ToString()); }
			}
			break;
		default:
			bOk = false;
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
		const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(Property, Property->ContainerPtrToValuePtr<void>(Host));
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
		const UClass* DocClass = FindObject<UClass>(nullptr, *ClassPath);
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

	// Members apply in binding order, each decoded into scratch and then pushed through the host's own edit
	// hooks before the next one decodes -- a later member may depend on an earlier one (rows on a schema). The
	// transaction is cancelled on the first failure, which restores everything Modify() recorded.
	FScopedTransaction Transaction(LOCTEXT("ImportObject", "PCGEx Mediator Import"));
	Host->Modify();

	int32 Applied = 0;
	FPCGExMediatorDiagnostics Local;
	{
		FScope Scope(Local);
		for (const FName& Member : Binding->Members)
		{
			const TSharedPtr<FJsonValue> Json = (*Members)->TryGetField(Member.ToString());
			if (!Json.IsValid()) { continue; }

			FProperty* Property = FindFProperty<FProperty>(Host->GetClass(), Member);
			if (!Property)
			{
				Report(EPCGExMediatorSeverity::Error, Member.ToString(), TEXT("bound member not found on the host class"));
				break;
			}

			FPropertyScratch Scratch(Property);
			if (!DecodeMember(*Binding, Host, Property, Json, Scratch)) { break; }

			Host->PreEditChange(Property);
			Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Host), Scratch.Memory);
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Host->PostEditChangeProperty(Event);
			++Applied;
		}
	}
	Forward(Local);

	if (Local.HasErrors())
	{
		Transaction.Cancel();
		return false;
	}
	if (Applied == 0)
	{
		Transaction.Cancel();
		Report(EPCGExMediatorSeverity::Warning, TEXT("no bound member present; nothing imported"));
		return true;
	}

	if (Binding->PostImport) { Binding->PostImport(Host); }
	(void)Host->MarkPackageDirty();
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeFormat(const FName FormatId)
{
	using namespace PCGExMediatorTransport;

	const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormat(FormatId);
	if (!Format.IsValid()) { return nullptr; }
	return EnvelopeSchema(Format->Id, Format->Version, Format->Describe ? Format->Describe() : nullptr);
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeObject(const UClass* HostClass)
{
	using namespace PCGExMediatorTransport;

	const TSharedPtr<const FPCGExMediatorBinding> Binding = FPCGExMediatorRegistry::FindBinding(HostClass);
	if (!Binding.IsValid()) { return nullptr; }

	TSharedRef<FJsonObject> MemberProps = MakeShared<FJsonObject>();
	for (const FName& Member : Binding->Members)
	{
		TSharedPtr<FJsonObject> MemberSchema;
		if (const FProperty* Property = FindFProperty<FProperty>(HostClass, Member))
		{
			const FProperty* Inner = Property;
			const bool bArray = Property->IsA<FArrayProperty>();
			if (bArray) { Inner = CastField<FArrayProperty>(Property)->Inner; }

			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Inner))
			{
				if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(StructProperty->Struct); Format.IsValid() && Format->Describe)
				{
					MemberSchema = Format->Describe();
					if (MemberSchema.IsValid()) { MemberSchema->SetStringField(TEXT("$comment"), FString::Printf(TEXT("format %s v%d"), *Format->Id.ToString(), Format->Version)); }
				}
			}
			if (bArray)
			{
				TSharedRef<FJsonObject> ArraySchema = MakeShared<FJsonObject>();
				ArraySchema->SetStringField(TEXT("type"), TEXT("array"));
				if (MemberSchema.IsValid()) { ArraySchema->SetObjectField(TEXT("items"), MemberSchema.ToSharedRef()); }
				MemberSchema = ArraySchema;
			}
		}
		if (!MemberSchema.IsValid())
		{
			MemberSchema = MakeShared<FJsonObject>();
			MemberSchema->SetStringField(TEXT("description"), TEXT("reflected as-is (no registered format)"));
		}
		MemberProps->SetObjectField(Member.ToString(), MemberSchema.ToSharedRef());
	}

	TSharedRef<FJsonObject> MembersSchema = MakeShared<FJsonObject>();
	MembersSchema->SetStringField(TEXT("type"), TEXT("object"));
	MembersSchema->SetObjectField(TEXT("properties"), MemberProps);

	TSharedRef<FJsonObject> DataProps = MakeShared<FJsonObject>();
	DataProps->SetObjectField(Keys::Class, ConstSchema(HostClass->GetPathName()));
	DataProps->SetObjectField(Keys::Members, MembersSchema);

	TSharedRef<FJsonObject> DataSchema = MakeShared<FJsonObject>();
	DataSchema->SetStringField(TEXT("type"), TEXT("object"));
	DataSchema->SetObjectField(TEXT("properties"), DataProps);
	if (!Binding->Summary.IsEmpty()) { DataSchema->SetStringField(TEXT("description"), Binding->Summary); }

	return EnvelopeSchema(ObjectFormatId, ObjectFormatVersion, DataSchema);
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
	auto LoadPath = [](const FString& PathString) -> UObject*
	{
		const FSoftObjectPath Path(PathString);
		if (Path.IsNull()) { return nullptr; }
		if (UObject* Found = Path.ResolveObject()) { return Found; }
		PCGExHelpers::LoadBlocking_AnyThread(Path);
		return Path.ResolveObject();
	};

	if (UObject* Direct = LoadPath(Target))
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
		if (const UPCGGraph* Graph = Cast<UPCGGraph>(LoadPath(GraphPath)))
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

const UClass* PCGExMediator::FindClass(const FString& NameOrPath)
{
	if (const UClass* ByPath = FindObject<UClass>(nullptr, *NameOrPath)) { return ByPath; }
	if (const UClass* ByName = FindFirstObject<UClass>(*NameOrPath, EFindFirstObjectOptions::ExactClass)) { return ByName; }
	// UClass names carry no U / A prefix; accept the C++ spelling too.
	if (NameOrPath.Len() > 1 && (NameOrPath[0] == TEXT('U') || NameOrPath[0] == TEXT('A')) && FChar::IsUpper(NameOrPath[1]))
	{
		return FindFirstObject<UClass>(*NameOrPath.Mid(1), EFindFirstObjectOptions::ExactClass);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
