// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "OnlineFPSCharacter.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "NavigationSystem.h"
#include "OnlineFPS.h"
#include "TimerManager.h"

AShooterAIController::AShooterAIController()
{
	// Enable network replication for AI Controller
	bReplicates = true;

	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	StateTreeAI->SetStartLogicAutomatically(false);

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Only run AI logic on server
	if (!HasAuthority())
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterAIController::OnPossess - Client skipping AI logic for %s"), *GetNameSafe(InPawn));
		return;
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterAIController::OnPossess - Server possessing %s"), *GetNameSafe(InPawn));

	// ensure we're possessing an NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
	{
		// add the team tag to the pawn
		NPC->Tags.Add(TeamTag);

		// subscribe to the pawn's OnDeath delegate
		NPC->OnPawnDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);

		// start AI logic with delay to ensure everything is initialized
		if (StateTreeAI)
		{
			// Force enable tick before starting - more aggressive approach
			StateTreeAI->SetComponentTickEnabled(true);
			StateTreeAI->PrimaryComponentTick.bCanEverTick = true;
			StateTreeAI->PrimaryComponentTick.bStartWithTickEnabled = true;
			StateTreeAI->PrimaryComponentTick.bRunOnAnyThread = false;

			// Force the component to register for ticking
			StateTreeAI->RegisterComponent();
			StateTreeAI->MarkRenderStateDirty();

			// Mark as replicate
			StateTreeAI->SetIsReplicated(true);

			UE_LOG(LogOnlineFPS, Warning, TEXT("  StateTreeAI tick enabled: CanEverTick=%d, StartWithTickEnabled=%d, IsTickEnabled=%d"),
				StateTreeAI->PrimaryComponentTick.bCanEverTick,
				StateTreeAI->PrimaryComponentTick.bStartWithTickEnabled,
				StateTreeAI->IsComponentTickEnabled());

			// Use a timer to start the StateTree logic after a brief delay
			// This ensures the pawn and weapon are fully initialized
			FTimerHandle StartLogicTimer;
			GetWorld()->GetTimerManager().SetTimer(StartLogicTimer, [this, NPC]()
			{
				if (StateTreeAI)
				{
					// Force enable tick again before starting
					StateTreeAI->SetComponentTickEnabled(true);
					StateTreeAI->StartLogic();

					// Check if StateTree tick is actually enabled after start
					bool bTickEnabled = StateTreeAI->IsComponentTickEnabled();
					UE_LOG(LogOnlineFPS, Warning, TEXT("  StateTreeAI started successfully for %s"), *NPC->GetName());
					UE_LOG(LogOnlineFPS, Warning, TEXT("    Tick enabled after start: %d"), bTickEnabled);

					// If StateTree tick was disabled, enable fallback movement logic
					if (!bTickEnabled)
					{
						UE_LOG(LogOnlineFPS, Warning, TEXT("    StateTree tick disabled - activating fallback AI logic"));
						StartFallbackAILogic(NPC);
					}
				}
			}, 0.5f, false);
		}
		else
		{
			UE_LOG(LogOnlineFPS, Error, TEXT("  StateTreeAI component is NULL!"));
		}
	}
}

void AShooterAIController::OnPawnDeath()
{
	// Clear current target to stop attacking
	ClearCurrentTarget();

	// Stop any active shooting
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
	{
		NPC->StopShooting();
	}

	// Stop fallback AI logic if running
	StopFallbackAILogic();

	// stop movement
	if (GetPathFollowingComponent())
	{
		GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);
	}

	// stop StateTree logic
	if (StateTreeAI)
	{
		StateTreeAI->StopLogic(FString("Pawn Death"));
	}

	// Note: We don't unpossess or destroy the controller anymore
	// This allows the AI to respawn and continue using the same controller
	// The controller will be repossessed when the NPC respawns
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = Target;
}

void AShooterAIController::ClearCurrentTarget()
{
	TargetEnemy = nullptr;
}

void AShooterAIController::RestartAILogic()
{
	// Stop any fallback AI first
	StopFallbackAILogic();

	if (StateTreeAI)
	{
		// Re-enable tick and restart the StateTree logic
		StateTreeAI->SetComponentTickEnabled(true);
		StateTreeAI->StartLogic();
		UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterAIController::RestartAILogic - AI logic restarted"));

		// Check if StateTree tick was disabled again
		if (!StateTreeAI->IsComponentTickEnabled())
		{
			UE_LOG(LogOnlineFPS, Warning, TEXT("  StateTree tick disabled after restart - activating fallback AI"));
			if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
			{
				StartFallbackAILogic(NPC);
			}
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Error, TEXT("AShooterAIController::RestartAILogic - StateTreeAI is NULL!"));
	}
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}

void AShooterAIController::StartFallbackAILogic(AShooterNPC* NPC)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("  Starting fallback AI logic for %s"), *NPC->GetName());

	// Ensure we're still possessing the NPC and have authority
	if (!HasAuthority() || !NPC || GetPawn() != NPC)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("    Cannot start fallback - invalid conditions"));
		return;
	}

	// Stop any existing fallback AI timer
	StopFallbackAILogic();

	// Set up periodic AI tick timer for basic movement and shooting
	GetWorld()->GetTimerManager().SetTimer(FallbackAITimer, [this, NPC]()
	{
		// Safety checks: stop if NPC is dead, invalid, or we no longer possess it
		if (!HasAuthority() || !NPC || GetPawn() != NPC || !NPC->IsAlive())
		{
			UE_LOG(LogOnlineFPS, Verbose, TEXT("    Fallback AI skipping tick - NPC invalid or dead"));
			return;
		}

		// Try to get target from perception
		AActor* TargetActor = GetCurrentTarget();

		// If no target, try to find one
		if (!TargetActor && AIPerception)
		{
			TArray<AActor*> SensedActors;
			AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SensedActors);

			for (AActor* Sensed : SensedActors)
			{
				if (Sensed && Sensed->ActorHasTag(FName("Player")))
				{
					TargetActor = Sensed;
					SetCurrentTarget(TargetActor);
					break;
				}
			}
		}

		// If we have a target, move towards it and shoot
		if (TargetActor)
		{
			// Check if target is still alive
			bool bTargetIsAlive = true;
			if (AOnlineFPSCharacter* TargetCharacter = Cast<AOnlineFPSCharacter>(TargetActor))
			{
				bTargetIsAlive = !TargetCharacter->ActorHasTag(FName("Dead"));
			}

			// If target is dead, clear it and find a new one
			if (!bTargetIsAlive)
			{
				UE_LOG(LogOnlineFPS, Verbose, TEXT("    Target %s is dead, clearing target"), *TargetActor->GetName());
				ClearCurrentTarget();
				TargetActor = nullptr;
				// Stop shooting since target is dead
				NPC->StopShooting();
			}
			else
			{
				// Target is alive, continue with attack logic
				const float AttackRange = 800.0f;
				const float StopDistance = 300.0f;

				// Calculate distance to target
				const float Distance = FVector::Dist(NPC->GetActorLocation(), TargetActor->GetActorLocation());

				// Move towards target if far away
				if (Distance > StopDistance)
				{
					MoveToActor(TargetActor, StopDistance);
				}

				// Shoot if in range and have line of sight
				if (Distance <= AttackRange)
				{
					// Simple line of sight check
					FHitResult HitResult;
					FCollisionQueryParams QueryParams;
					QueryParams.AddIgnoredActor(NPC);
					QueryParams.AddIgnoredActor(TargetActor);

					const bool bHasLOS = !GetWorld()->LineTraceSingleByChannel(
						HitResult,
						NPC->GetActorLocation(),
						TargetActor->GetActorLocation(),
						ECC_Visibility,
						QueryParams
					);

					if (bHasLOS && NPC->GetWeapon())
					{
						NPC->StartShooting(TargetActor);
					}
					else
					{
						NPC->StopShooting();
					}
				}
				else
				{
					NPC->StopShooting();
				}
			}
		}
		// No target - wander randomly
		else
		{
			// Make sure we stop shooting
			NPC->StopShooting();

			// Simple wander behavior
			if (!CurrentWanderTarget.IsSet())
			{
				// Pick a random point nearby
				const FVector Origin = NPC->GetActorLocation();
				const float WanderRadius = 1000.0f;
				FNavLocation RandomLocation;

				if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetNavigationSystem(GetWorld()))
				{
					if (NavSystem->GetRandomReachablePointInRadius(Origin, WanderRadius, RandomLocation))
					{
						CurrentWanderTarget = RandomLocation.Location;
					}
				}
			}

			// Move to wander target
			if (CurrentWanderTarget.IsSet())
			{
				const float DistanceToWanderTarget = FVector::Dist(NPC->GetActorLocation(), CurrentWanderTarget.GetValue());

				if (DistanceToWanderTarget > 100.0f)
				{
					MoveToLocation(CurrentWanderTarget.GetValue());
				}
				else
				{
					// Reached wander target, pick new one next tick
					CurrentWanderTarget.Reset();
				}
			}
		}

	}, 0.5f, true); // Update every 0.5 seconds

	UE_LOG(LogOnlineFPS, Warning, TEXT("  Fallback AI logic activated"));
}

void AShooterAIController::StopFallbackAILogic()
{
	if (GetWorld() && FallbackAITimer.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(FallbackAITimer);
		FallbackAITimer.Invalidate();
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Fallback AI logic stopped"));
	}
}
