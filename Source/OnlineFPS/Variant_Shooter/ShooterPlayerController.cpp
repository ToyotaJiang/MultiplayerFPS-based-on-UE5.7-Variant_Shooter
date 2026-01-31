// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterBulletCounterUI.h"
#include "ShooterGameMode.h"
#include "ShooterUI.h"
#include "ShooterGameState.h"
#include "OnlineFPS.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Net/UnrealNetwork.h"

AShooterPlayerController::AShooterPlayerController()
{
	// Set default character class for respawn
	CharacterClass = AShooterCharacter::StaticClass();
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogOnlineFPS, Warning, TEXT("ShooterPlayerController::BeginPlay - IsLocalPlayerController: %s, HasAuthority: %s"),
		IsLocalPlayerController() ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController())
	{
		if (ShouldUseTouchControls())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			} else {

				UE_LOG(LogOnlineFPS, Error, TEXT("Could not spawn mobile controls widget."));

			}
		}

		// create the bullet counter widget and add it to the screen
		UE_LOG(LogOnlineFPS, Warning, TEXT("Creating BulletCounterUI - Class: %s"),
			BulletCounterUIClass ? *BulletCounterUIClass->GetName() : TEXT("None"));
		BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);

		if (BulletCounterUI)
		{
			BulletCounterUI->AddToPlayerScreen(0);
			UE_LOG(LogOnlineFPS, Warning, TEXT("BulletCounterUI created successfully"));

		} else {

			UE_LOG(LogOnlineFPS, Error, TEXT("Could not spawn bullet counter widget."));

		}

		// Create the shooter UI widget for team scores
		UE_LOG(LogOnlineFPS, Warning, TEXT("Creating ShooterUI - Class: %s"),
			ShooterUIClass ? *ShooterUIClass->GetName() : TEXT("None"));
		if (ShooterUIClass)
		{
			ShooterUI = CreateWidget<UShooterUI>(this, ShooterUIClass);
			if (ShooterUI)
			{
				ShooterUI->AddToPlayerScreen(0);
				UE_LOG(LogOnlineFPS, Warning, TEXT("ShooterUI created successfully"));
			}
		}

		// Set up periodic KD stats update timer
		// Use a delayed start to ensure GameState is replicated
		if (GetWorld())
		{
			FTimerHandle DelayedStartTimer;
			GetWorld()->GetTimerManager().SetTimer(DelayedStartTimer, [this]()
			{
				// Start the periodic update timer
				if (GetWorld())
				{
					GetWorld()->GetTimerManager().SetTimer(
						KDStatsUpdateTimer, 
						this, 
						&AShooterPlayerController::UpdateKDStatsUI, 
						1.0f, 
						true
					);
					UE_LOG(LogOnlineFPS, Warning, TEXT("KD Stats update timer started (1 second interval)"));
				}
			}, 2.0f, false); // Delay 2 seconds to ensure GameState is ready
			
			UE_LOG(LogOnlineFPS, Warning, TEXT("KD Stats update timer will start in 2 seconds"));
		}
	}
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

void AShooterPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();

	UE_LOG(LogOnlineFPS, Warning, TEXT("ShooterPlayerController::OnRep_Pawn - IsLocalPlayerController: %s, Pawn: %s, HasAuthority: %s"),
		IsLocalPlayerController() ? TEXT("true") : TEXT("false"),
		GetPawn() ? *GetPawn()->GetName() : TEXT("None"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	// Called when pawn is replicated to clients
	// Set up UI and delegates
	if (IsLocalPlayerController() && GetPawn())
	{
		APawn* NewPawn = GetPawn();

		// Remove all existing delegates for this pawn to avoid duplicates
		NewPawn->OnDestroyed.RemoveAll(this);
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(NewPawn))
		{
			ShooterCharacter->OnBulletCountUpdated.RemoveAll(this);
			ShooterCharacter->OnDamaged.RemoveAll(this);

			// Force update UI with current weapon state immediately
			// This ensures UI is updated even if OnRep is called before delegates are set
			if (ShooterCharacter->GetCurrentWeapon())
			{
				UE_LOG(LogOnlineFPS, Warning, TEXT("  Force updating weapon HUD in OnRep_Pawn"));
				ShooterCharacter->OnBulletCountUpdated.Broadcast(
					ShooterCharacter->GetCurrentWeapon()->GetMagazineSize(),
					ShooterCharacter->GetCurrentWeapon()->GetBulletCount());
			}

			// Force update health UI
			float LifePercent = 1.0f;
			if (ShooterCharacter->GetMaxHP() > 0)
			{
				LifePercent = ShooterCharacter->GetCurrentHP() / ShooterCharacter->GetMaxHP();
			}
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Force updating health UI in OnRep_Pawn: %.2f"), LifePercent);
			ShooterCharacter->OnDamaged.Broadcast(LifePercent);
		}

		SetupPawnUI(NewPawn);

		// Create UI if not already created (in case BeginPlay was not called on client)
		if (!BulletCounterUI && BulletCounterUIClass)
		{
			UE_LOG(LogOnlineFPS, Warning, TEXT("Creating BulletCounterUI in OnRep_Pawn - Class: %s"),
				*BulletCounterUIClass->GetName());
			BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);
			if (BulletCounterUI)
			{
				BulletCounterUI->AddToPlayerScreen(0);
				UE_LOG(LogOnlineFPS, Warning, TEXT("BulletCounterUI created successfully in OnRep_Pawn"));
			}
		}

		// Create ShooterUI if not already created
		if (!ShooterUI && ShooterUIClass)
		{
			UE_LOG(LogOnlineFPS, Warning, TEXT("Creating ShooterUI in OnRep_Pawn - Class: %s"),
				*ShooterUIClass->GetName());
			ShooterUI = CreateWidget<UShooterUI>(this, ShooterUIClass);
			if (ShooterUI)
			{
				ShooterUI->AddToPlayerScreen(0);
				UE_LOG(LogOnlineFPS, Warning, TEXT("ShooterUI created successfully in OnRep_Pawn"));
			}
		}

		// Set up input mapping contexts for this pawn
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// add the input mapping contexts
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
					}
				}
			}
		}
	}
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// subscribe to the pawn's OnDestroyed delegate
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	// is this a shooter character?
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// add the player tag
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// subscribe to the pawn's delegates
		ShooterCharacter->OnBulletCountUpdated.AddDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnPawnDamaged);

		// For local player controllers (including host), set up input mapping contexts
		if (IsLocalPlayerController())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
			{
				// add the input mapping contexts
				for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}

				// only add these IMCs if we're not using mobile touch input
				if (!ShouldUseTouchControls())
				{
					for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
					}
				}
			}
		}

		// force update the life bar on server or local player
		if (HasAuthority() || IsLocalPlayerController())
		{
			float LifePercent = 1.0f; // Default to full health
			if (ShooterCharacter->GetMaxHP() > 0)
			{
				LifePercent = ShooterCharacter->GetCurrentHP() / ShooterCharacter->GetMaxHP();
			}
			ShooterCharacter->OnDamaged.Broadcast(LifePercent);
		}
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// reset the bullet counter HUD
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}

	// find the player start
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() > 0)
	{
		// select a random player start
		AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];

		// spawn a character at the player start
		const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

		if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
		{
			// possess the character
			Possess(RespawnedCharacter);
		}
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
	}
}

bool AShooterPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AShooterPlayerController::UpdateScoreUI(int32 TeamIndex, int32 Score)
{
	// Update the shooter UI if it exists
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateScore(TeamIndex, Score);
	}
}

void AShooterPlayerController::SetupPawnUI(APawn* InPawn)
{
	// Set up UI for the given pawn (used on clients when pawn is replicated)
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// subscribe to the pawn's delegates
		ShooterCharacter->OnBulletCountUpdated.AddDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnPawnDamaged);

		// subscribe to the pawn's OnDestroyed delegate
		InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

		// add the player tag
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// update the life bar with current HP
		float LifePercent = 1.0f; // Default
		if (ShooterCharacter->GetMaxHP() > 0)
		{
			LifePercent = ShooterCharacter->GetCurrentHP() / ShooterCharacter->GetMaxHP();
		}
		ShooterCharacter->OnDamaged.Broadcast(LifePercent);
	}
}

void AShooterPlayerController::UpdateKDStatsUI()
{
	// Only update for local player controllers
	if (!IsLocalPlayerController())
	{
		return;
	}

	// Get game state and player stats
	AShooterGameState* GS = Cast<AShooterGameState>(GetWorld()->GetGameState());
	
	if (!GS)
	{
		// GameState not ready yet, will retry next timer tick
		UE_LOG(LogTemp, Verbose, TEXT("UpdateKDStatsUI: GameState not ready yet"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("AShooterPlayerController::UpdateKDStatsUI - PlayerStats count: %d"), GS->PlayerStats.Num());

	// Update ShooterUI if it exists (original location) - Blueprint version
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateKDStats(GS->PlayerStats);
	}

	// Update BulletCounterUI with PURE C++ implementation - no blueprint needed!
	if (BulletCounterUI)
	{
		BulletCounterUI->UpdateKDStats(GS->PlayerStats);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  BulletCounterUI is NULL!"));
	}
}
