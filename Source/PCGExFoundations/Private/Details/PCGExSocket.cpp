// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExSocket.h"

FPCGExSocket::FPCGExSocket() {}
FPCGExSocket::~FPCGExSocket() {}
FPCGExSocket::FPCGExSocket(const FPCGExSocket& Other)
	: bManaged(Other.bManaged),
	SocketName(Other.SocketName), 
	RelativeTransform(Other.RelativeTransform),
	Tag(Other.Tag)
{
}

FPCGExSocket& FPCGExSocket::operator=(const FPCGExSocket& Other)
{
	if (this != &Other)
	{
		bManaged = Other.bManaged;
		SocketName = Other.SocketName;
		RelativeTransform = Other.RelativeTransform;
		Tag = Other.Tag;
	}
	return *this;
}

FPCGExSocket::FPCGExSocket(const FName& InSocketName, const FVector& InRelativeLocation, const FRotator& InRelativeRotation, const FVector& InRelativeScale, FString InTag)
	: SocketName(InSocketName),
	RelativeTransform(FTransform(InRelativeRotation.Quaternion(), InRelativeLocation, InRelativeScale)), 
	Tag(InTag)
{
}

FPCGExSocket::FPCGExSocket(const FName& InSocketName, const FTransform& InRelativeTransform, const FString& InTag)
	: SocketName(InSocketName), 
	RelativeTransform(InRelativeTransform), 
	Tag(InTag)
{
}
