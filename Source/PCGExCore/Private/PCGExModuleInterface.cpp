// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExModuleInterface.h"

#include "CoreMinimal.h"
#include "PCGExLog.h"
#include "UObject/Class.h"
#include "UObject/CoreRedirects.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ToolMenus.h"
#endif

TArray<IPCGExModuleInterface*> IPCGExModuleInterface::RegisteredModules;

#if WITH_EDITOR
TWeakPtr<FSlateStyleSet> IPCGExModuleInterface::EditorStyle;
#endif

void IPCGExModuleInterface::StartupModule()
{
	UE_LOG(LogPCGEx, Log, TEXT("IPCGExModuleInterface::StartupModule >> %s"), *GetModuleName());
	RegisteredModules.Add(this);

	if (OldBaseModules.Num() > 0)
	{
		RegisterRedirectors();
	}
}

void IPCGExModuleInterface::ShutdownModule()
{
	RegisteredModules.Remove(this);

#if WITH_EDITOR
	UnregisterMenuExtensions();
#endif
}

void IPCGExModuleInterface::RegisterRedirectors() const
{
	TArray<FCoreRedirect> Redirects;
	int32 NumSkipped = 0;

	const FString ThisModuleName = GetModuleName();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;

		FString ClassPath = Class->GetPathName();
		if (!ClassPath.StartsWith(FString::Printf(TEXT("/Script/%s."), *ThisModuleName)))
		{
			continue;
		}

		FString ClassName = Class->GetName();

		for (const FString& OldModuleName : OldBaseModules)
		{
			const FString OldPath = FString::Printf(TEXT("/Script/%s.%s"), *OldModuleName, *ClassName);

			// An ini redirect for this old name wins: adding a second one with another target is an Error and is dropped.
			TArray<const FCoreRedirect*> Existing;
			if (FCoreRedirects::GetMatchingRedirects(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(OldPath), Existing))
			{
				NumSkipped++;
				continue;
			}

			Redirects.Emplace(ECoreRedirectFlags::Type_Class, *OldPath, *FString::Printf(TEXT("/Script/%s.%s"), *ThisModuleName, *ClassName));
		}
	}

	if (Redirects.Num() > 0)
	{
		FCoreRedirects::AddRedirectList(Redirects, *ThisModuleName);
		UE_LOG(LogPCGEx, Log, TEXT("%s: Registered %d class redirects (%d left to explicit redirects)"), *ThisModuleName, Redirects.Num(), NumSkipped);
	}
}

#if WITH_EDITOR
void IPCGExModuleInterface::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
}

void IPCGExModuleInterface::RegisterMenuExtensions()
{
}

void IPCGExModuleInterface::UnregisterMenuExtensions()
{
	UToolMenus::UnregisterOwner(this);
}
#endif
