// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediator.h"

#include "PCGExLog.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorTransport.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FPCGExMediatorModule"

namespace PCGExMediatorCommands
{
	using namespace PCGExMediator;

	void List(const TArray<FString>& Args)
	{
		UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(ListAsJson()));
	}

	void Describe(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.describe <format id | class | struct>"));
			return;
		}

		const TSharedPtr<FJsonObject> Schema = DescribeAny(Args[0]);
		if (!Schema.IsValid())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] '%s' is neither a registered format, a bound class nor a struct."), *Args[0]);
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
		TEXT("pcgex.mediator.describe"), TEXT("Print the JSON Schema of a format id, a bound class or a struct. Usage: pcgex.mediator.describe <format id | class | struct>"),
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
