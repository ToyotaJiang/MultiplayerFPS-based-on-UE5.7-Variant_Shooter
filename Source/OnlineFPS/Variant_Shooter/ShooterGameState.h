// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "ShooterGameState.generated.h"

/**
 *  Player statistics for KD tracking
 */
USTRUCT(BlueprintType)
struct FPlayerStats
{
	GENERATED_BODY()

	/** Player's name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FString PlayerName;

	/** Number of kills */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Kills;

	/** Number of deaths */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Deaths;

	FPlayerStats()
		: PlayerName(TEXT(""))
		, Kills(0)
		, Deaths(0)
	{
	}
};

/**
 *  GameState for multiplayer FPS game
 *  Stores replicated game state that all clients need to know about
 */
UCLASS()
class ONLINEFPS_API AShooterGameState : public AGameState
{
	GENERATED_BODY()

public:

	/** Array of player statistics (replicated) */
	UPROPERTY(ReplicatedUsing = OnRep_PlayerStats, VisibleAnywhere, BlueprintReadWrite, Category = "Stats")
	TArray<FPlayerStats> PlayerStats;

	AShooterGameState();

	/** Register a kill for a player */
	void AddKill(APlayerController* KillerPC);

	/** Register a death for a player */
	void AddDeath(APlayerController* VictimPC);

	/** Get or create player stats for a controller */
	FPlayerStats& GetPlayerStats(APlayerController* PC);

protected:

	/** Called when PlayerStats is replicated */
	UFUNCTION()
	void OnRep_PlayerStats();

protected:

	/** Configure replication */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
