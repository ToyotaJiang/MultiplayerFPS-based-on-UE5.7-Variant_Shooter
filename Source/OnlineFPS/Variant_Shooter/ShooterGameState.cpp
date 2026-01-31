// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterGameState.h"
#include "Variant_Shooter/ShooterPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "OnlineFPS.h"

AShooterGameState::AShooterGameState()
{
	// Enable replication
	bReplicates = true;
}

void AShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterGameState, PlayerStats);
}

FPlayerStats& AShooterGameState::GetPlayerStats(APlayerController* PC)
{
	if (!PC || !PC->PlayerState)
	{
		static FPlayerStats EmptyStats;
		return EmptyStats;
	}

	FString PlayerName = PC->PlayerState->GetPlayerName();

	// Find existing stats for this player
	for (FPlayerStats& Stats : PlayerStats)
	{
		if (Stats.PlayerName == PlayerName)
		{
			return Stats;
		}
	}

	// Create new stats entry
	FPlayerStats NewStats;
	NewStats.PlayerName = PlayerName;
	NewStats.Kills = 0;
	NewStats.Deaths = 0;
	PlayerStats.Add(NewStats);

	return PlayerStats.Last();
}

void AShooterGameState::AddKill(APlayerController* KillerPC)
{
	if (!KillerPC)
	{
		return;
	}

	FPlayerStats& Stats = GetPlayerStats(KillerPC);
	Stats.Kills++;

	UE_LOG(LogOnlineFPS, Warning, TEXT("AddKill: %s kills now: %d"), *Stats.PlayerName, Stats.Kills);

	// Trigger replication manually to ensure immediate update
	if (HasAuthority())
	{
		// Force a replication update by modifying the array
		PlayerStats = PlayerStats;
	}
}

void AShooterGameState::AddDeath(APlayerController* VictimPC)
{
	if (!VictimPC)
	{
		return;
	}

	FPlayerStats& Stats = GetPlayerStats(VictimPC);
	Stats.Deaths++;

	UE_LOG(LogOnlineFPS, Warning, TEXT("AddDeath: %s deaths now: %d"), *Stats.PlayerName, Stats.Deaths);

	// Trigger replication manually to ensure immediate update
	if (HasAuthority())
	{
		// Force a replication update by modifying the array
		PlayerStats = PlayerStats;
	}
}

void AShooterGameState::OnRep_PlayerStats()
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterGameState::OnRep_PlayerStats - Stats replicated to client"));

	// Notify all local player controllers to update their UI
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (PC->IsLocalPlayerController())
				{
					if (AShooterPlayerController* ShooterPC = Cast<AShooterPlayerController>(PC))
					{
						ShooterPC->UpdateKDStatsUI();
					}
				}
			}
		}
	}
}
