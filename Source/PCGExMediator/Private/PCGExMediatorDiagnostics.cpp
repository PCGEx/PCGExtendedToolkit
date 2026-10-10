// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorDiagnostics.h"

#include "PCGExLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PCGExMediatorDiagnostics
{
	struct FThreadState
	{
		TArray<FPCGExMediatorDiagnostics*> Sinks;
		TArray<FString> Path;
	};

	FThreadState& State()
	{
		static thread_local FThreadState ThreadState;
		return ThreadState;
	}

	FString JoinPath(const TArray<FString>& Segments)
	{
		return FString::Join(Segments, TEXT("/"));
	}

	void Emit(const EPCGExMediatorSeverity Severity, const FString& Path, const FString& Message)
	{
		FThreadState& S = State();
		if (!S.Sinks.IsEmpty())
		{
			S.Sinks.Last()->Add(Severity, Path, Message);
			return;
		}

		const FString Line = Path.IsEmpty() ? Message : FString::Printf(TEXT("%s: %s"), *Path, *Message);
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

PCGExMediator::FScope::FScope(FPCGExMediatorDiagnostics& InSink)
{
	PCGExMediatorDiagnostics::State().Sinks.Add(&InSink);
}

PCGExMediator::FScope::~FScope()
{
	TArray<FPCGExMediatorDiagnostics*>& Sinks = PCGExMediatorDiagnostics::State().Sinks;
	Sinks.RemoveAt(Sinks.Num() - 1, EAllowShrinking::No);
}

PCGExMediator::FPathScope::FPathScope(const FString& InSegment)
{
	PCGExMediatorDiagnostics::State().Path.Add(InSegment);
}

PCGExMediator::FPathScope::FPathScope(const int32 InIndex)
{
	PCGExMediatorDiagnostics::State().Path.Add(FString::FromInt(InIndex));
}

PCGExMediator::FPathScope::~FPathScope()
{
	TArray<FString>& Path = PCGExMediatorDiagnostics::State().Path;
	Path.RemoveAt(Path.Num() - 1, EAllowShrinking::No);
}

bool PCGExMediator::HasScope()
{
	return !PCGExMediatorDiagnostics::State().Sinks.IsEmpty();
}

FString PCGExMediator::CurrentPath()
{
	return PCGExMediatorDiagnostics::JoinPath(PCGExMediatorDiagnostics::State().Path);
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
