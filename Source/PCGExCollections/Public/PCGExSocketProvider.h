// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"

class UStaticMeshComponent;

#include "PCGExSocketProvider.generated.h"

/**
 * Interface implemented by any actor or component that contributes a socket
 * to collection staging. Implementations are discovered during UpdateStaging:
 * - APCGExSocketActor placed in a level  -> socket on the level/PCGDataAsset entry
 * - UPCGExSocketComponent on an actor   -> socket on that actor class entry
 */
UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UPCGExSocketProvider : public UInterface
{
	GENERATED_BODY()
};

class PCGEXCOLLECTIONS_API IPCGExSocketProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Sockets")
	FName GetSocketName() const;

	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Sockets")
	FString GetSocketTag() const;

	/** World-space transform. UpdateStaging converts to relative as needed. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Sockets")
	FTransform GetSocketTransform() const;

	/** When true, this provider is excluded from bounds computation and level/data-asset export. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Sockets")
	bool ShouldStripFromExport() const;
};

/**
 * Placeable actor that marks a socket position within a level.
 * Discovered by level and PCGDataAsset collection staging to populate level-entry sockets.
 * Stripped from bounds and data-asset export by default.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "[PCGEx] Socket Actor", meta=(PCGExNodeLibraryDoc="staging/common-settings/socket-provider"))
class PCGEXCOLLECTIONS_API APCGExSocketActor : public AActor, public IPCGExSocketProvider
{
	GENERATED_BODY()

public:
	APCGExSocketActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCGEx Socket")
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCGEx Socket")
	FString SocketTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCGEx Socket")
	bool bStripFromExport = true;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual FName GetSocketName_Implementation() const override
	{
		return SocketName;
	}

	virtual FString GetSocketTag_Implementation() const override
	{
		return SocketTag;
	}

	virtual FTransform GetSocketTransform_Implementation() const override
	{
		return CachedTransform;
	}

	virtual bool ShouldStripFromExport_Implementation() const override
	{
		return bStripFromExport;
	}

private:
	/** World transform cached by OnConstruction. Read during staging from loaded levels where
	 *  ComponentToWorld may not be initialized. */
	UPROPERTY()
	FTransform CachedTransform = FTransform::Identity;
};

/**
 * Scene component that marks a socket position on an actor.
 * Discovered by actor collection staging (via temp-actor spawn) to populate actor-entry sockets.
 * Also picked up by level/PCGDataAsset staging when bGenerateCollections is enabled,
 * through the actor class entry's own UpdateStaging pass.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "[PCGEx] Socket Component",
	ClassGroup = (PCGEx), meta = (BlueprintSpawnableComponent))
class PCGEXCOLLECTIONS_API UPCGExSocketComponent : public USceneComponent, public IPCGExSocketProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FString SocketTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bStripFromExport = true;

	virtual FName GetSocketName_Implementation() const override
	{
		return SocketName;
	}

	virtual FString GetSocketTag_Implementation() const override
	{
		return SocketTag;
	}

	virtual FTransform GetSocketTransform_Implementation() const override
	{
		return GetComponentTransform();
	}

	virtual bool ShouldStripFromExport_Implementation() const override
	{
		return bStripFromExport;
	}
};
