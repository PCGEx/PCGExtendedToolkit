// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "PCGExAssemblyRoot.generated.h"

class AActor;

UINTERFACE(MinimalAPI)
class UPCGExAssemblyRoot : public UInterface
{
	GENERATED_BODY()
};

/**
 * An actor that can be exported as if it were a level: its attached subtree is the content, its
 * transform is the frame. Implement to bound the descent (owned actors, nested containers, ignored
 * subtrees...) -- a root without the interface exports every attached descendant.
 * Consumed by FPCGExLevelExportSource::FromActorSubtree.
 */
class PCGEXCOLLECTIONS_API IPCGExAssemblyRoot
{
	GENERATED_BODY()

public:
	/** False prunes Node AND everything attached below it. Null is never content. */
	virtual bool IsAssemblyContent(const AActor* Node) const
	{
		return Node != nullptr;
	}

	/** Frame the exported points are expressed in. Default: the implementing actor's transform. */
	virtual FTransform GetAssemblyFrame() const;
};
