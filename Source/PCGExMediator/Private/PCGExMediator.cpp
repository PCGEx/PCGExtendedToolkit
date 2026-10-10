// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediator.h"

#include "PCGExLog.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorTransport.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "FPCGExMediatorModule"

namespace PCGExMediatorCommands
{
	using namespace PCGExMediator;

	void LogDiagnostics(const FPCGExMediatorDiagnostics& Diagnostics)
	{
		for (const FPCGExMediatorDiagnostic& D : Diagnostics.Items)
		{
			switch (D.Severity)
			{
			case EPCGExMediatorSeverity::Error: UE_LOG(LogPCGEx, Error, TEXT("[Mediator] %s"), *D.ToString()); break;
			case EPCGExMediatorSeverity::Warning: UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] %s"), *D.ToString()); break;
			default: UE_LOG(LogPCGEx, Log, TEXT("[Mediator] %s"), *D.ToString()); break;
			}
		}
	}

	void List(const TArray<FString>& Args)
	{
		TArray<TSharedPtr<const FPCGExMediatorFormat>> Formats;
		FPCGExMediatorRegistry::GetFormats(Formats);
		UE_LOG(LogPCGEx, Log, TEXT("[Mediator] %d format(s):"), Formats.Num());
		for (const TSharedPtr<const FPCGExMediatorFormat>& F : Formats)
		{
			UE_LOG(LogPCGEx, Log, TEXT("  %s v%d  (%s)  %s"), *F->Id.ToString(), F->Version, *GetNameSafe(F->Struct), *F->Summary);
		}

		TArray<TSharedPtr<const FPCGExMediatorBinding>> Bindings;
		FPCGExMediatorRegistry::GetBindings(Bindings);
		UE_LOG(LogPCGEx, Log, TEXT("[Mediator] %d binding(s):"), Bindings.Num());
		for (const TSharedPtr<const FPCGExMediatorBinding>& B : Bindings)
		{
			TArray<FString> Members;
			for (const FName& M : B->Members) { Members.Add(M.ToString()); }
			UE_LOG(LogPCGEx, Log, TEXT("  %s  [%s]  %s"), *B->HostClass->GetPathName(), *FString::Join(Members, TEXT(", ")), *B->Summary);
		}
	}

	void Describe(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.describe <format id | class path>"));
			return;
		}

		TSharedPtr<FJsonObject> Schema = DescribeFormat(FName(*Args[0]));
		if (!Schema.IsValid())
		{
			if (const UClass* Class = FindObject<UClass>(nullptr, *Args[0]))
			{
				Schema = DescribeObject(Class);
			}
			else if (const UClass* ShortClass = FindFirstObject<UClass>(*Args[0], EFindFirstObjectOptions::ExactClass))
			{
				Schema = DescribeObject(ShortClass);
			}
		}
		if (!Schema.IsValid())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] '%s' is neither a registered format nor a bound class."), *Args[0]);
			return;
		}
		UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Schema.ToSharedRef()));
	}

	void Export(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.export <target> [file]"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		TSharedPtr<FJsonObject> Doc;
		{
			FScope Scope(Diagnostics);
			if (UObject* Target = ResolveTarget(Args[0]))
			{
				Doc = ExportObject(Target);
				if (Doc.IsValid() && Args.Num() > 1) { WriteFile(Args[1], Doc.ToSharedRef()); }
			}
		}
		LogDiagnostics(Diagnostics);
		if (Doc.IsValid() && Args.Num() == 1)
		{
			UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Doc.ToSharedRef()));
		}
	}

	void Import(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.import <target> <file>"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		bool bOk = false;
		{
			FScope Scope(Diagnostics);
			if (UObject* Target = ResolveTarget(Args[0]))
			{
				if (const TSharedPtr<FJsonObject> Doc = ReadFile(Args[1]))
				{
					bOk = ImportObject(*Doc, Target);
				}
			}
		}
		LogDiagnostics(Diagnostics);
		UE_LOG(LogPCGEx, Log, TEXT("[Mediator] import %s"), bOk ? TEXT("succeeded") : TEXT("failed"));
	}
}

void FPCGExMediatorModule::StartupModule()
{
	IPCGExModuleInterface::StartupModule();

	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.list"), TEXT("List the registered JSON formats and object bindings."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::List), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.describe"), TEXT("Print the JSON Schema of a format id or of a bound class. Usage: pcgex.mediator.describe <format id | class>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Describe), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.export"), TEXT("Export a bound object as JSON, to the log or a file. Usage: pcgex.mediator.export <asset path | graph path:node> [file]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Export), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.import"), TEXT("Import a JSON file into a bound object (undoable). Usage: pcgex.mediator.import <asset path | graph path:node> <file>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Import), ECVF_Default));
}

void FPCGExMediatorModule::ShutdownModule()
{
	IConsoleManager& Console = IConsoleManager::Get();
	for (IConsoleObject* Command : ConsoleCommands)
	{
		if (Command) { Console.UnregisterConsoleObject(Command); }
	}
	ConsoleCommands.Reset();

	IPCGExModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExMediatorModule, PCGExMediator)
