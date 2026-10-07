// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Utils/PCGExUniqueNameGenerator.h"
#include "CoreMinimal.h"

FName FPCGExUniqueNameGenerator::Get(const FString& BaseName)
{
	const int32 UniqueIdx = FPlatformAtomics::InterlockedIncrement(&Idx) - 1;
	return FName(BaseName + "_" + FString::Printf(TEXT("%d"), UniqueIdx));
}

FName FPCGExUniqueNameGenerator::Get(const FName& BaseName)
{
	return Get(BaseName.ToString());
}
