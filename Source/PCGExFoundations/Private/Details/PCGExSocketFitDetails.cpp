// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExSocketFitDetails.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"
#include "Details/PCGExSocket.h"


bool FPCGExSocketFitDetails::Init(const TSharedPtr<PCGExData::FFacade>& InFacade)
{
	if (!bEnabled ||
		(SocketNameValue.Input == EPCGExInputValueType::Constant && SocketNameValue.Constant.IsNone()) ||
		(SocketNameValue.Input == EPCGExInputValueType::Attribute && SocketNameValue.Attribute.IsNone()))
	{
		bMutate = false;
		return true;
	}

	bMutate = true;
	SocketNameBuffer = SocketNameValue.GetValueSetting();
	if (!SocketNameBuffer->Init(InFacade))
	{
		return false;
	}

	return true;
}

void FPCGExSocketFitDetails::Mutate(const int32 Index, const TArray<FPCGExSocket>& InSockets, FTransform& InOutTransform) const
{
	if (!bMutate)
	{
		return;
	}

	const FName SName = SocketNameBuffer->Read(Index);
	for (const FPCGExSocket& Socket : InSockets)
	{
		if (Socket.SocketName == SName)
		{
			InOutTransform = InOutTransform * Socket.RelativeTransform;
			return;
		}
	}
}
