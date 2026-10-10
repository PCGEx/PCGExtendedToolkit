// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorLibrary.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorTransport.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor/Transactor.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

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

FString UPCGExMediatorLibrary::CreateAsset(const FString& ClassNameOrPath, const FString& PackagePath, const FString& AssetName)
{
	using namespace PCGExMediatorLibrary;

	const UClass* Class = PCGExMediator::FindClass(ClassNameOrPath.TrimStartAndEnd());
	if (!Class) { return Failure(FString::Printf(TEXT("class '%s' not found"), *ClassNameOrPath)); }
	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return Failure(FString::Printf(TEXT("class '%s' is abstract or deprecated"), *Class->GetName()));
	}

	const FString Folder = PackagePath.TrimStartAndEnd();
	const FString Name = AssetName.TrimStartAndEnd();
	if (!FPackageName::IsValidLongPackageName(Folder)) { return Failure(FString::Printf(TEXT("'%s' is not a long package path such as /Game/Folder"), *Folder)); }
	if (Name.IsEmpty() || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS)) { return Failure(FString::Printf(TEXT("'%s' is not a valid asset name"), *Name)); }

	// Checked here: the asset tools prompt (modal) on a conflict instead of failing.
	const FString PackageName = Folder / Name;
	if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
	{
		return Failure(FString::Printf(TEXT("'%s' already exists"), *PackageName));
	}

	// The class's own factory when one exists (type-specific setup), the plain NewObject path otherwise.
	UFactory* Factory = nullptr;
	UClass* ExactFactoryClass = nullptr;
	UClass* AnyFactoryClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!Candidate->IsChildOf(UFactory::StaticClass()) || Candidate->HasAnyClassFlags(CLASS_Abstract)) { continue; }
		// Non-const only because DoesSupportClass is declared so; the default object is read, never written.
		UFactory* Default = Candidate->GetDefaultObject<UFactory>();
		if (!Default->CanCreateNew() || !Default->DoesSupportClass(const_cast<UClass*>(Class))) { continue; }
		if (Default->GetSupportedClass() == Class)
		{
			ExactFactoryClass = Candidate;
			break;
		}
		if (!AnyFactoryClass) { AnyFactoryClass = Candidate; }
	}
	if (UClass* FactoryClass = ExactFactoryClass ? ExactFactoryClass : AnyFactoryClass)
	{
		Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	}

	UObject* Asset = FAssetToolsModule::GetModule().Get().CreateAsset(Name, Folder, const_cast<UClass*>(Class), Factory, FName(TEXT("PCGExMediator")));
	if (!Asset) { return Failure(FString::Printf(TEXT("the asset tools refused to create %s '%s'"), *Class->GetName(), *PackageName)); }

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), true);
	Result->SetStringField(TEXT("path"), Asset->GetPathName());
	Result->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
	return PCGExMediator::ToString(Result, false);
}

FString UPCGExMediatorLibrary::SaveAsset(const FString& ObjectPath)
{
	using namespace PCGExMediatorLibrary;

	FPCGExMediatorDiagnostics Diagnostics;
	UObject* Object = nullptr;
	{
		PCGExMediator::FScope Scope(Diagnostics);
		Object = PCGExMediator::ResolveTarget(ObjectPath.TrimStartAndEnd());
	}
	if (!Object) { return Failure(Diagnostics); }

	UPackage* Package = Object->GetOutermost();
	if (!Package || Package == GetTransientPackage()) { return Failure(FString::Printf(TEXT("'%s' is transient; nothing to save"), *Object->GetPathName())); }
	if (!UEditorLoadingAndSavingUtils::SavePackages({Package}, false)) { return Failure(FString::Printf(TEXT("saving '%s' failed"), *Package->GetName())); }

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), true);
	Result->SetStringField(TEXT("path"), Object->GetPathName());
	return PCGExMediator::ToString(Result, false);
}

FString UPCGExMediatorLibrary::Undo()
{
	using namespace PCGExMediatorLibrary;

	if (!GEditor || !GEditor->Trans) { return Failure(TEXT("no editor transaction buffer")); }
	FText Title;
	if (!GEditor->Trans->CanUndo(&Title)) { return Failure(TEXT("nothing to undo")); }
	if (!GEditor->UndoTransaction()) { return Failure(FString::Printf(TEXT("undo of '%s' was refused"), *Title.ToString())); }

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), true);
	Result->SetStringField(TEXT("title"), Title.ToString());
	return PCGExMediator::ToString(Result, false);
}
