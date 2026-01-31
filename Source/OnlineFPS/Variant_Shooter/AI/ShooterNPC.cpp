// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "ShooterGameState.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "OnlineFPS.h"

AShooterNPC::AShooterNPC()
{
	// Enable network replication for NPC
	bReplicates = true;
	SetReplicateMovement(true);

	// Configure movement for NPC
	if (GetCharacterMovement())
	{
		// Higher walk speed for NPCs
		GetCharacterMovement()->MaxWalkSpeed = 400.0f;
		GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
	}
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::BeginPlay - %s, HasAuthority: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"));

	// Store initial spawn location for respawning (only on server)
	if (HasAuthority())
	{
		InitialSpawnLocation = GetActorLocation();
		InitialSpawnRotation = GetActorRotation();
	}

	// Only spawn weapons on server (they will replicate to clients)
	if (HasAuthority())
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Spawning weapon of class: %s"), *GetNameSafe(WeaponClass));

		// spawn the weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (Weapon)
		{
			// Ensure that weapon is set to replicate
			Weapon->SetReplicates(true);
			Weapon->SetReplicateMovement(true);

			UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon spawned: %s, RemoteRole: %d, LocalRole: %d, bReplicates: %d"),
				*Weapon->GetName(), (int32)Weapon->GetRemoteRole(), (int32)Weapon->GetLocalRole(),
				(int32)Weapon->GetIsReplicated());

			// Force attach weapon meshes immediately on server
			// This ensures the weapon is visible and attached properly before replication
			AttachWeaponMeshes(Weapon);

			// Activate the weapon on server
			// On clients, weapon will be replicated and attached/activated via OnRep_Owner and OnRep_CurrentWeapon
			if (Weapon->GetOwner())
			{
				Weapon->ActivateWeapon();
				UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon activated successfully"));
			}
		}
		else
		{
			UE_LOG(LogOnlineFPS, Error, TEXT("  Failed to spawn weapon!"));
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Client - weapon will be replicated from server"));
	}
}

void AShooterNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate HP to all clients
	DOREPLIFETIME(AShooterNPC, CurrentHP);
	DOREPLIFETIME(AShooterNPC, TeamByte);
}

void AShooterNPC::OnRep_CurrentHP(float OldValue)
{
	// Handle HP replication on clients
	// Check if the character just died on the client
	// This happens when HP is replicated and drops to <= 0
	if (OldValue > 0.0f && CurrentHP <= 0.0f)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::OnRep_CurrentHP - NPC died, calling death effects on client"));

		// Handle client-side death effects (only on clients, not server)
		if (!HasAuthority())
		{
			// grant the death tag to the character
			Tags.Add(DeathTag);

			// disable capsule collision
			GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			// stop movement
			GetCharacterMovement()->StopMovementImmediately();
			GetCharacterMovement()->StopActiveMovement();

			// enable ragdoll physics on the third person mesh
			GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
			GetMesh()->SetSimulatePhysics(true);
			GetMesh()->SetPhysicsBlendWeight(1.0f);
		}
	}
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
	}
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::TakeDamage - %s, Damage: %.2f, Role: %d, HasAuthority: %s"),
		*GetName(), Damage, (int32)GetRemoteRole(), HasAuthority() ? TEXT("true") : TEXT("false"));

	// Only process damage on server
	if (!HasAuthority())
	{
		return 0.0f;
	}

	// ignore if already dead
	if (bIsDead)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Already dead, ignoring damage"));
		return 0.0f;
	}

	// Reduce HP
	CurrentHP -= Damage;

	UE_LOG(LogOnlineFPS, Warning, TEXT("  HP reduced from %.2f to %.2f"), CurrentHP + Damage, CurrentHP);

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		// Update KD statistics: record kill for attacker
		if (EventInstigator && EventInstigator != GetController())
		{
			if (AShooterGameState* GS = Cast<AShooterGameState>(GetWorld()->GetGameState()))
			{
				if (APlayerController* KillerPC = Cast<APlayerController>(EventInstigator))
				{
					GS->AddKill(KillerPC);
					UE_LOG(LogOnlineFPS, Warning, TEXT("  Player %s killed NPC %s"), 
						*KillerPC->GetPlayerState<APlayerState>()->GetPlayerName(), *GetName());
				}
			}
		}

		Die();
	}

	return Damage;
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::AttachWeaponMeshes - %s, Weapon: %s"),
		*GetName(), *GetNameSafe(WeaponToAttach));

	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);

	UE_LOG(LogOnlineFPS, Warning, TEXT("  NPC weapon meshes attached successfully"));
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// start aiming from the camera location
	const FVector AimSource = GetFirstPersonCameraComponent()->GetComponentLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have an aim target?
	if (CurrentAimTarget)
	{
		// target the actor location
		AimTarget = CurrentAimTarget->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// get the aim direction and apply randomness in a cone
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);

		
	} else {

		// no aim target, so just use the camera facing
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetFirstPersonCameraComponent()->GetForwardVector(), AimVarianceHalfAngle);

	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// are we still shooting?
	if (bIsShooting)
	{
		// fire the weapon
		Weapon->StartFiring();
	}
}

void AShooterNPC::Die()
{
	// Only run on server
	if (!HasAuthority())
	{
		return;
	}

	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::Die - %s"), *GetName());

	// raise the dead flag
	bIsDead = true;

	// grant the death tag to the character
	Tags.Add(DeathTag);

	// call the delegate
	OnPawnDeath.Broadcast();

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// Stop shooting
	StopShooting();

	// Deactivate weapon
	if (Weapon)
	{
		Weapon->DeactivateWeapon();
	}

	// disable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();

	// enable ragdoll physics on the third person mesh
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);

	// Schedule respawn (or destruction if RespawnTime is 0)
	if (RespawnTime > 0.0f)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Scheduling respawn in %.2f seconds"), RespawnTime);
		GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterNPC::Respawn, RespawnTime, false);
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  RespawnTime is 0, destroying actor"));
		Destroy();
	}
}

void AShooterNPC::Respawn()
{
	// Only run on server
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::Respawn - %s"), *GetName());

	// Reset HP to full
	CurrentHP = 100.0f;

	// Clear the dead flag
	bIsDead = false;

	// Remove death tag
	Tags.Remove(DeathTag);

	// Stop shooting flag and clear aim target
	bIsShooting = false;
	CurrentAimTarget = nullptr;

	// Teleport to spawn location
	SetActorLocation(InitialSpawnLocation);
	SetActorRotation(InitialSpawnRotation);

	// Disable ragdoll physics
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetPhysicsBlendWeight(0.0f);
	GetMesh()->SetCollisionProfileName(TEXT("CharacterMesh"));

	// Re-enable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Reset mesh relative location (ragdoll might have offset it)
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	// Re-enable movement
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// Reactivate weapon if it exists
	if (Weapon)
	{
		Weapon->ActivateWeapon();
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon reactivated"));
	}

	// Restart AI controller if available
	if (AShooterAIController* AIController = Cast<AShooterAIController>(GetController()))
	{
		// Use the public method to restart AI logic
		AIController->RestartAILogic();
		
		// Clear any lingering target
		AIController->ClearCurrentTarget();
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("  NPC respawned successfully"));
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::StartShooting - %s, Target: %s, HasAuthority: %s"),
		*GetName(), *GetNameSafe(ActorToShoot), HasAuthority() ? TEXT("true") : TEXT("false"));

	// Don't shoot if dead
	if (bIsDead)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  NPC is dead, ignoring shoot command"));
		return;
	}

	// save the aim target
	CurrentAimTarget = ActorToShoot;

	// raise the flag
	bIsShooting = true;

	// signal the weapon
	if (Weapon)
	{
		Weapon->StartFiring();
	}
	else
	{
		UE_LOG(LogOnlineFPS, Error, TEXT("  Weapon is NULL!"));
	}
}

void AShooterNPC::StopShooting()
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterNPC::StopShooting - %s"), *GetName());

	// lower the flag
	bIsShooting = false;

	// Clear aim target
	CurrentAimTarget = nullptr;

	// signal the weapon
	if (Weapon)
	{
		Weapon->StopFiring();
	}
}

bool AShooterNPC::IsAlive() const
{
	return !bIsDead;
}

AShooterWeapon* AShooterNPC::GetWeapon() const
{
	return Weapon;
}
