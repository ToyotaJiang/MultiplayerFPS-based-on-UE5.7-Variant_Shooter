// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterUI.h"
#include "ShooterPlayerController.h"
#include "ShooterCharacter.h"
#include "ShooterGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AShooterGameMode::AShooterGameMode()
{
	// Use ShooterGameState by default (required for AGameMode compatibility)
	GameStateClass = AShooterGameState::StaticClass();

	// Use ShooterPlayerController by default
	PlayerControllerClass = AShooterPlayerController::StaticClass();

	// Use ShooterCharacter as default pawn
	DefaultPawnClass = AShooterCharacter::StaticClass();
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Initialize the replicated array with default team scores
	// Assuming 2 teams (0 and 1), can be expanded as needed
	ReplicatedTeamScores.SetNum(2, EAllowShrinking::No);
	ReplicatedTeamScores[0] = 0;
	ReplicatedTeamScores[1] = 0;

	// Initialize previous scores
	PreviousTeamScores.SetNum(2, EAllowShrinking::No);
	PreviousTeamScores[0] = 0;
	PreviousTeamScores[1] = 0;

	// Only create UI on server or listen server
	// In network games, each client will create their own UI
	if (GetNetMode() != NM_DedicatedServer)
	{
		// create the UI for the local player
		ShooterUI = CreateWidget<UShooterUI>(GetWorld(), ShooterUIClass);
		if (ShooterUI)
		{
			ShooterUI->AddToViewport(0);
		}
	}
}

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// Ensure the new player has their UI created
	// This will be called for both host and clients
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	// Ensure the array is large enough
	if (TeamByte >= ReplicatedTeamScores.Num())
	{
		ReplicatedTeamScores.SetNum(TeamByte + 1, EAllowShrinking::Yes);
	}

	// increment the score for the given team
	++ReplicatedTeamScores[TeamByte];

	// Also update the local map for easy access
	TeamScores.Emplace(TeamByte, ReplicatedTeamScores[TeamByte]);

	// UI will be updated automatically via OnRep_ReplicatedTeamScores on clients
}

void AShooterGameMode::OnRep_ReplicatedTeamScores()
{
	// Update UI when scores are replicated to clients
	// Compare with previous scores to only update changed teams
	for (int32 i = 0; i < ReplicatedTeamScores.Num() && i < PreviousTeamScores.Num(); i++)
	{
		if (ReplicatedTeamScores[i] != PreviousTeamScores[i])
		{
			// Update local map
			TeamScores.Emplace(i, ReplicatedTeamScores[i]);

			// Update UI if we're a listen server (ShooterUI is created in GameMode)
			if (ShooterUI && GetNetMode() == NM_ListenServer)
			{
				ShooterUI->BP_UpdateScore(i, ReplicatedTeamScores[i]);
			}

			// Also update all player controllers' UI (for listen server and clients)
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					if (AShooterPlayerController* ShooterPC = Cast<AShooterPlayerController>(PC))
					{
						ShooterPC->UpdateScoreUI(i, ReplicatedTeamScores[i]);
					}
				}
			}
		}
	}

	// Update previous scores
	PreviousTeamScores = ReplicatedTeamScores;
}

int32 AShooterGameMode::GetTeamScore(uint8 TeamByte) const
{
	// Try to get from replicated array first
	if (TeamByte < ReplicatedTeamScores.Num())
	{
		return ReplicatedTeamScores[TeamByte];
	}

	// Fall back to local map
	int32 Score = 0;
	if (const int32* FoundScore = TeamScores.Find(TeamByte))
	{
		Score = *FoundScore;
	}
	return Score;
}

void AShooterGameMode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate team scores to all clients
	DOREPLIFETIME(AShooterGameMode, ReplicatedTeamScores);
}
