// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;
class AShooterGameState;

/**
 *  Simple GameMode for a first person shooter game
 *  Manages game UI
 *  Keeps track of team scores
 *  Supports network replication for multiplayer
 */
UCLASS()
class ONLINEFPS_API AShooterGameMode : public AGameMode
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	AShooterGameMode();

protected:

	/** Type of UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter")
	TSubclassOf<UShooterUI> ShooterUIClass;

	/** Pointer to the UI widget */
	TObjectPtr<UShooterUI> ShooterUI;

	/** Map of scores by team ID */
	UPROPERTY()
	TMap<uint8, int32> TeamScores;

	/** Array of team scores for replication (easier to replicate than TMap) */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedTeamScores)
	TArray<int32> ReplicatedTeamScores;

	/** Previous team scores for OnRep comparison */
	UPROPERTY()
	TArray<int32> PreviousTeamScores;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Called when a player joins the game */
	virtual void PostLogin(APlayerController* NewPlayer) override;

public:

	/** Increases the score for the given team */
	UFUNCTION(BlueprintCallable, Category="Shooter")
	void IncrementTeamScore(uint8 TeamByte);

	/** Gets the current score for a team */
	UFUNCTION(BlueprintCallable, Category="Shooter")
	int32 GetTeamScore(uint8 TeamByte) const;

protected:

	/** Called when ReplicatedTeamScores is replicated */
	UFUNCTION()
	void OnRep_ReplicatedTeamScores();

protected:

	/** Replicates team scores to all clients */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
