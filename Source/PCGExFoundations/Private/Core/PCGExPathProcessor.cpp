// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPathProcessor.h"

#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExPathProcessorElement"

UPCGExPathProcessorSettings::UPCGExPathProcessorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FName UPCGExPathProcessorSettings::GetMainInputPin() const
{
	return PCGExPaths::Labels::SourcePathsLabel;
}

FName UPCGExPathProcessorSettings::GetMainOutputPin() const
{
	return PCGExPaths::Labels::OutputPathsLabel;
}

#undef LOCTEXT_NAMESPACE
