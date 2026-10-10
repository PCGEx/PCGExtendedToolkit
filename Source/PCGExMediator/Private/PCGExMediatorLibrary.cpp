// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorLibrary.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorTransport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"

namespace PCGExMediatorLibrary
{
	FString Failure(const FPCGExMediatorDiagnostics& Diagnostics)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("ok"), false);
		Result->SetField(TEXT("diagnostics"), Diagnostics.ToJson());
		return PCGExMediator::ToString(Result, false);
	}

	FString Failure(const FString& Message)
	{
		FPCGExMediatorDiagnostics Diagnostics;
		Diagnostics.Add(EPCGExMediatorSeverity::Error, FString(), Message);
		return Failure(Diagnostics);
	}
}

FString UPCGExMediatorLibrary::ListFormats()
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
		TArray<TSharedPtr<FJsonValue>> Members;
		for (const FName& Member : Binding->Members) { Members.Add(MakeShared<FJsonValueString>(Member.ToString())); }
		Entry->SetArrayField(TEXT("members"), Members);
		Entry->SetStringField(TEXT("summary"), Binding->Summary);
		Bindings.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("formats"), Formats);
	Root->SetArrayField(TEXT("bindings"), Bindings);
	return PCGExMediator::ToString(Root, false);
}

FString UPCGExMediatorLibrary::DescribeFormat(const FString& FormatIdOrClass)
{
	const FString Trimmed = FormatIdOrClass.TrimStartAndEnd();
	TSharedPtr<FJsonObject> Schema = PCGExMediator::DescribeFormat(FName(*Trimmed));
	if (!Schema.IsValid())
	{
		if (const UClass* Class = PCGExMediator::FindClass(Trimmed)) { Schema = PCGExMediator::DescribeObject(Class); }
	}
	if (!Schema.IsValid())
	{
		return PCGExMediatorLibrary::Failure(FString::Printf(TEXT("'%s' is neither a registered format nor a bound class; see ListFormats."), *Trimmed));
	}
	return PCGExMediator::ToString(Schema.ToSharedRef(), false);
}

FString UPCGExMediatorLibrary::ExportJson(const FString& Target)
{
	FPCGExMediatorDiagnostics Diagnostics;
	TSharedPtr<FJsonObject> Doc;
	{
		PCGExMediator::FScope Scope(Diagnostics);
		if (UObject* Object = PCGExMediator::ResolveTarget(Target.TrimStartAndEnd()))
		{
			Doc = PCGExMediator::ExportObject(Object);
		}
	}
	if (!Doc.IsValid()) { return PCGExMediatorLibrary::Failure(Diagnostics); }
	return PCGExMediator::ToString(Doc.ToSharedRef(), false);
}

FString UPCGExMediatorLibrary::ImportJson(const FString& Target, const FString& Json)
{
	FPCGExMediatorDiagnostics Diagnostics;
	bool bOk = false;
	{
		PCGExMediator::FScope Scope(Diagnostics);
		if (UObject* Object = PCGExMediator::ResolveTarget(Target.TrimStartAndEnd()))
		{
			if (const TSharedPtr<FJsonObject> Doc = PCGExMediator::FromString(Json))
			{
				bOk = PCGExMediator::ImportObject(*Doc, Object);
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), bOk);
	Result->SetField(TEXT("diagnostics"), Diagnostics.ToJson());
	return PCGExMediator::ToString(Result, false);
}
