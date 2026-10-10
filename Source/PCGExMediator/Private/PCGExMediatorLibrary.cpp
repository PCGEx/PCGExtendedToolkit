// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorLibrary.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
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
	return PCGExMediator::ToString(PCGExMediator::ListAsJson(), false);
}

FString UPCGExMediatorLibrary::DescribeFormat(const FString& FormatIdOrClass)
{
	const TSharedPtr<FJsonObject> Schema = PCGExMediator::DescribeAny(FormatIdOrClass);
	if (!Schema.IsValid())
	{
		return PCGExMediatorLibrary::Failure(FString::Printf(TEXT("'%s' is neither a registered format, a bound class nor a struct; see ListFormats."), *FormatIdOrClass.TrimStartAndEnd()));
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

	UClass* Class = PCGExMediator::FindType<UClass>(ClassNameOrPath);
	if (!Class) { return Failure(FString::Printf(TEXT("class '%s' not found"), *ClassNameOrPath)); }
	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return Failure(FString::Printf(TEXT("class '%s' is abstract or deprecated"), *Class->GetName()));
	}

	const FString Folder = PackagePath.TrimStartAndEnd();
	const FString Name = AssetName.TrimStartAndEnd();
	if (!FPackageName::IsValidLongPackageName(Folder)) { return Failure(FString::Printf(TEXT("'%s' is not a long package path such as /Game/Folder"), *Folder)); }
	if (Name.IsEmpty() || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS)) { return Failure(FString::Printf(TEXT("'%s' is not a valid asset name"), *Name)); }

	// Every refusal the asset tools would raise as a modal dialog is checked here first.
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	const FString PackageName = Folder / Name;
	if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
	{
		return Failure(FString::Printf(TEXT("'%s' already exists"), *PackageName));
	}
	if (FEditorFileUtils::IsMapPackageAsset(PackageName))
	{
		return Failure(FString::Printf(TEXT("a map named '%s' already exists in that folder"), *Name));
	}
	FText Reason;
	if (!AssetTools.IsObjectPathAllowed(PackageName, &Reason))
	{
		return Failure(FString::Printf(TEXT("'%s' is not allowed: %s"), *PackageName, *Reason.ToString()));
	}

	// The class's own factory when one exists (type-specific setup), the plain NewObject path otherwise.
	UFactory* FactoryTemplate = nullptr;
	for (UFactory* Candidate : AssetTools.GetNewAssetFactories())
	{
		if (!Candidate || !Candidate->DoesSupportClass(Class)) { continue; }
		if (Candidate->GetSupportedClass() == Class)
		{
			FactoryTemplate = Candidate;
			break;
		}
		if (!FactoryTemplate) { FactoryTemplate = Candidate; }
	}
	UFactory* Factory = FactoryTemplate ? NewObject<UFactory>(GetTransientPackage(), FactoryTemplate->GetClass()) : nullptr;

	UObject* Asset = AssetTools.CreateAsset(Name, Folder, Class, Factory, FName(TEXT("PCGExMediator")));
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
	// CanUndo fills its text only with the refusal reason; the title lives in the undo context.
	FText Reason;
	if (!GEditor->Trans->CanUndo(&Reason)) { return Failure(FString::Printf(TEXT("nothing to undo %s"), *Reason.ToString())); }
	const FText Title = GEditor->Trans->GetUndoContext(false).Title;
	if (!GEditor->UndoTransaction()) { return Failure(FString::Printf(TEXT("undo of '%s' was refused"), *Title.ToString())); }

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), true);
	Result->SetStringField(TEXT("title"), Title.ToString());
	return PCGExMediator::ToString(Result, false);
}
