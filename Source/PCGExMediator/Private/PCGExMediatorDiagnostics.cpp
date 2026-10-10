// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorDiagnostics.h"

#include "PCGExLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PCGExMediatorDiagnostics
{
	void LogLine(const EPCGExMediatorSeverity Severity, const FString& Line)
	{
		switch (Severity)
		{
		case EPCGExMediatorSeverity::Error:
			UE_LOG(LogPCGEx, Error, TEXT("[Mediator] %s"), *Line);
			break;
		case EPCGExMediatorSeverity::Warning:
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] %s"), *Line);
			break;
		default:
			UE_LOG(LogPCGEx, Log, TEXT("[Mediator] %s"), *Line);
			break;
		}
	}

	void Emit(const EPCGExMediatorSeverity Severity, const FString& Path, const FString& Message)
	{
		if (FPCGExMediatorDiagnostics* const* Sink = PCGExMediator::FScope::Current())
		{
			(*Sink)->Add(Severity, Path, Message);
			return;
		}
		LogLine(Severity, Path.IsEmpty() ? Message : FString::Printf(TEXT("%s: %s"), *Path, *Message));
	}
}

#pragma region FPCGExMediatorDiagnostic

FString FPCGExMediatorDiagnostic::ToString() const
{
	if (Path.IsEmpty())
	{
		return FString::Printf(TEXT("[%s] %s"), PCGExMediator::SeverityToString(Severity), *Message);
	}
	return FString::Printf(TEXT("[%s] %s: %s"), PCGExMediator::SeverityToString(Severity), *Path, *Message);
}

#pragma endregion

#pragma region FPCGExMediatorDiagnostics

bool FPCGExMediatorDiagnostics::HasErrors() const
{
	return Num(EPCGExMediatorSeverity::Error) > 0;
}

int32 FPCGExMediatorDiagnostics::Num(const EPCGExMediatorSeverity Severity) const
{
	int32 Count = 0;
	for (const FPCGExMediatorDiagnostic& Item : Items)
	{
		if (Item.Severity == Severity)
		{
			++Count;
		}
	}
	return Count;
}

void FPCGExMediatorDiagnostics::Add(const EPCGExMediatorSeverity Severity, const FString& Path, const FString& Message)
{
	FPCGExMediatorDiagnostic& Item = Items.AddDefaulted_GetRef();
	Item.Severity = Severity;
	Item.Path = Path;
	Item.Message = Message;
}

void FPCGExMediatorDiagnostics::Reset()
{
	Items.Reset();
}

FString FPCGExMediatorDiagnostics::ToString() const
{
	TArray<FString> Lines;
	Lines.Reserve(Items.Num());
	for (const FPCGExMediatorDiagnostic& Item : Items)
	{
		Lines.Add(Item.ToString());
	}
	return FString::Join(Lines, LINE_TERMINATOR);
}

TSharedRef<FJsonValue> FPCGExMediatorDiagnostics::ToJson() const
{
	TArray<TSharedPtr<FJsonValue>> Array;
	Array.Reserve(Items.Num());
	for (const FPCGExMediatorDiagnostic& Item : Items)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("severity"), PCGExMediator::SeverityToString(Item.Severity));
		Obj->SetStringField(TEXT("path"), Item.Path);
		Obj->SetStringField(TEXT("message"), Item.Message);
		Array.Add(MakeShared<FJsonValueObject>(Obj));
	}
	return MakeShared<FJsonValueArray>(Array);
}

#pragma endregion

#pragma region Scopes

TArray<FPCGExMediatorDiagnostics*>& FPCGExMediatorSinkStack::Get()
{
	static thread_local TArray<FPCGExMediatorDiagnostics*> Stack;
	return Stack;
}

TArray<FString>& FPCGExMediatorPathStack::Get()
{
	static thread_local TArray<FString> Stack;
	return Stack;
}

bool PCGExMediator::HasScope()
{
	return FScope::IsActive();
}

FString PCGExMediator::CurrentPath()
{
	return FString::Join(FPCGExMediatorPathStack::Get(), TEXT("/"));
}

void PCGExMediator::Report(const EPCGExMediatorSeverity Severity, const FString& Message)
{
	PCGExMediatorDiagnostics::Emit(Severity, CurrentPath(), Message);
}

void PCGExMediator::Report(const EPCGExMediatorSeverity Severity, const FString& Segment, const FString& Message)
{
	const FString Base = CurrentPath();
	PCGExMediatorDiagnostics::Emit(Severity, Base.IsEmpty() ? Segment : Base + TEXT("/") + Segment, Message);
}

void PCGExMediator::Forward(const FPCGExMediatorDiagnostics& Collected)
{
	for (const FPCGExMediatorDiagnostic& Item : Collected.Items)
	{
		PCGExMediatorDiagnostics::Emit(Item.Severity, Item.Path, Item.Message);
	}
}

void PCGExMediator::LogDiagnostics(const FPCGExMediatorDiagnostics& Diagnostics)
{
	for (const FPCGExMediatorDiagnostic& Item : Diagnostics.Items)
	{
		PCGExMediatorDiagnostics::LogLine(Item.Severity, Item.Path.IsEmpty() ? Item.Message : FString::Printf(TEXT("%s: %s"), *Item.Path, *Item.Message));
	}
}

const TCHAR* PCGExMediator::SeverityToString(const EPCGExMediatorSeverity Severity)
{
	switch (Severity)
	{
	case EPCGExMediatorSeverity::Error: return TEXT("error");
	case EPCGExMediatorSeverity::Warning: return TEXT("warning");
	default: return TEXT("info");
	}
}

#pragma endregion
