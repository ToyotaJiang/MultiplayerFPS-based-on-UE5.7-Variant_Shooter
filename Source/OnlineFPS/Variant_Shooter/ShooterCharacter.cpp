// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "ShooterGameState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "OnlineFPS.h"

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// Enable network replication for movement
	SetReplicateMovement(true);

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Only initialize HP on server
	// On clients, HP will be replicated from server
	// However, for single-player or listen server, we need to ensure HP is set
	if (HasAuthority())
	{
		// reset HP to max
		CurrentHP = MaxHP;

		// update the HUD only for the local player
		if (IsLocallyControlled())
		{
			OnDamaged.Broadcast(1.0f);
		}
	}
	else if (IsLocallyControlled())
	{
		// For clients, if HP hasn't been replicated yet (network lag), use max HP temporarily
		if (CurrentHP <= 0.0f)
		{
			CurrentHP = MaxHP;
			OnDamaged.Broadcast(1.0f);
		}
	}
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);
	}

}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Only process damage on server
	if (!HasAuthority())
	{
		return 0.0f;
	}

	// Log damage event
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::TakeDamage - Damage: %.2f, HasAuthority: %s, IsLocallyControlled: %s, Controller: %s"),
		Damage,
		HasAuthority() ? TEXT("true") : TEXT("false"),
		IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		GetController() ? *GetController()->GetName() : TEXT("NULL"));

	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		// Store damage instigator for KD tracking before dying
		LastDamageInstigator = EventInstigator;
		Die();
	}

	// update the HUD for the local player (host player)
	// OnRep_CurrentHP will handle updating on clients
	if (IsLocallyControlled())
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Broadcasting OnDamaged - LifePercent: %.2f"), FMath::Max(0.0f, CurrentHP / MaxHP));
		OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
	}

	return Damage;
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

void AShooterCharacter::DoStartFiring()
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::DoStartFiring - %s, HasAuthority: %s, IsLocallyControlled: %s, Weapon: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"), IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("NULL"));

	// If on client, call server RPC
	if (!HasAuthority())
	{
		ServerStartFiring();
		return;
	}

	// fire the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
	else
	{
		if (!CurrentWeapon)
		{
			UE_LOG(LogOnlineFPS, Error, TEXT("  No weapon equipped!"));
		}
		if (IsDead())
		{
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Character is dead!"));
		}
	}
}

void AShooterCharacter::ServerStartFiring_Implementation()
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::ServerStartFiring_Implementation - %s"), *GetName());

	// fire the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
}

bool AShooterCharacter::ServerStartFiring_Validate()
{
	return true;
}

void AShooterCharacter::DoStopFiring()
{
	// If on client, call server RPC
	if (!HasAuthority())
	{
		ServerStopFiring();
		return;
	}

	// stop firing the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}

void AShooterCharacter::ServerStopFiring_Implementation()
{
	// stop firing the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}

bool AShooterCharacter::ServerStopFiring_Validate()
{
	return true;
}

void AShooterCharacter::DoSwitchWeapon()
{
	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && !IsDead())
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::AttachWeaponMeshes - %s, Weapon: %s"),
		*GetName(), *GetNameSafe(Weapon));

	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);

	UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon meshes attached successfully"));
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// stub
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// apply the recoil as pitch input
	AddControllerPitchInput(Recoil);
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// trace ahead from the camera viewpoint
	FHitResult OutHit;

	const FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	const FVector End = Start + (GetFirstPersonCameraComponent()->GetForwardVector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// Only server should add weapons
	if (!HasAuthority())
	{
		ServerAddWeaponClass(WeaponClass);
		return;
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::AddWeaponClass - %s, HasAuthority: %s, IsLocallyControlled: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"), IsLocallyControlled() ? TEXT("true") : TEXT("false"));

	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
	// spawn the new weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// Ensure the weapon is set to replicate
			AddedWeapon->SetReplicates(true);
			AddedWeapon->SetReplicateMovement(true);

			UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon spawned: %s, RemoteRole: %d, LocalRole: %d, bReplicates: %d, Owner: %s"),
				*AddedWeapon->GetName(), (int32)AddedWeapon->GetRemoteRole(), (int32)AddedWeapon->GetLocalRole(),
				(int32)AddedWeapon->GetIsReplicated(), *GetNameSafe(AddedWeapon->GetOwner()));

			// add the weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// switch to the new weapon
			// OnRep_CurrentWeapon will handle activation on clients via replication
			CurrentWeapon = AddedWeapon;
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Set CurrentWeapon to: %s"), *AddedWeapon->GetName());

			// Activate weapon on server (client activation handled by OnRep)
			if (HasAuthority())
			{
				CurrentWeapon->ActivateWeapon();
			}
		}
		else
		{
			UE_LOG(LogOnlineFPS, Error, TEXT("  Failed to spawn weapon!"));
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Already own this weapon: %s"), *OwnedWeapon->GetName());
	}
}

void AShooterCharacter::ServerAddWeaponClass_Implementation(TSubclassOf<AShooterWeapon> WeaponClass)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::ServerAddWeaponClass_Implementation"));

	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// spawn new weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// Ensure that weapon is set to replicate
			AddedWeapon->SetReplicates(true);
			AddedWeapon->SetReplicateMovement(true);

			UE_LOG(LogOnlineFPS, Warning, TEXT("  Weapon spawned in ServerAddWeaponClass: %s, RemoteRole: %d, LocalRole: %d, bReplicates: %d, Owner: %s"),
				*AddedWeapon->GetName(), (int32)AddedWeapon->GetRemoteRole(), (int32)AddedWeapon->GetLocalRole(),
				(int32)AddedWeapon->GetIsReplicated(), *GetNameSafe(AddedWeapon->GetOwner()));

			// add weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// switch to new weapon
			// OnRep_CurrentWeapon will handle activation on clients via replication
			CurrentWeapon = AddedWeapon;
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Set CurrentWeapon to: %s"), *AddedWeapon->GetName());

			// Activate weapon on server (client activation handled by OnRep)
			if (HasAuthority())
			{
				CurrentWeapon->ActivateWeapon();
			}
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Already own this weapon: %s"), *OwnedWeapon->GetName());
	}
}

bool AShooterCharacter::ServerAddWeaponClass_Validate(TSubclassOf<AShooterWeapon> WeaponClass)
{
	return true;
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// update the bullet counter
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die()
{
	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// Update KD statistics on server
	if (HasAuthority())
	{
		if (AShooterGameState* GS = Cast<AShooterGameState>(GetWorld()->GetGameState()))
		{
			// Record death for victim
			if (APlayerController* VictimPC = Cast<APlayerController>(GetController()))
			{
				GS->AddDeath(VictimPC);
			}

			// Record kill for killer (if killed by another player)
			if (LastDamageInstigator && LastDamageInstigator != GetController())
			{
				if (APlayerController* KillerPC = Cast<APlayerController>(LastDamageInstigator))
				{
					GS->AddKill(KillerPC);
					APlayerState* KillerState = KillerPC->PlayerState.Get();
					FString KillerName = KillerState ? KillerState->GetPlayerName() : TEXT("Unknown");

					APlayerController* VictimPC = Cast<APlayerController>(GetController());
					APlayerState* VictimState = VictimPC ? VictimPC->PlayerState.Get() : nullptr;
					FString VictimName = VictimState ? VictimState->GetPlayerName() : TEXT("Unknown");

					UE_LOG(LogOnlineFPS, Warning, TEXT("Die: %s killed %s"), *KillerName, *VictimName);
				}
			}

			// Reset damage instigator
			LastDamageInstigator = nullptr;
		}

		// increment the team score
		if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->IncrementTeamScore(TeamByte);
		}
	}

	// grant the death tag to the character
	Tags.Add(DeathTag);

	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();

	// disable controls
	DisableInput(nullptr);

	// reset the bullet counter UI
	OnBulletCountUpdated.Broadcast(0, 0);

	// call the BP handler
	BP_OnDeath();

	// schedule character respawn (only on server)
	if (HasAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, RespawnTime, false);
	}
}

void AShooterCharacter::OnRespawn()
{
	// destroy the character to force the PC to respawn
	Destroy();
}

bool AShooterCharacter::IsDead() const
{
	// the character is dead if their current HP drops to zero
	return CurrentHP <= 0.0f;
}

void AShooterCharacter::OnRep_CurrentHP(float OldValue)
{
	// Update the HUD when HP is replicated
	float LifePercent = FMath::Max(0.0f, CurrentHP / MaxHP);
	OnDamaged.Broadcast(LifePercent);

	// Check if the character just died on the client
	// This happens when HP is replicated and drops to <= 0
	if (OldValue > 0.0f && CurrentHP <= 0.0f)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::OnRep_CurrentHP - Character died, calling death effects on client"));

		// Handle client-side death effects (only on clients, not server)
		if (!HasAuthority())
		{
			// deactivate the weapon
			if (IsValid(CurrentWeapon))
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// stop character movement
			GetCharacterMovement()->StopMovementImmediately();

			// disable controls
			DisableInput(nullptr);

			// reset the bullet counter UI
			OnBulletCountUpdated.Broadcast(0, 0);

			// call the BP handler for death effects
			BP_OnDeath();
		}
	}
}

void AShooterCharacter::OnRep_CurrentWeapon(AShooterWeapon* OldWeapon)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterCharacter::OnRep_CurrentWeapon - %s, HasAuthority: %s, IsLocallyControlled: %s, OldWeapon: %s, NewWeapon: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"), IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(OldWeapon), *GetNameSafe(CurrentWeapon));

	// Deactivate the old weapon
	if (OldWeapon)
	{
		OldWeapon->DeactivateWeapon();
	}

	// Activate the new weapon
	if (CurrentWeapon)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Activating new weapon: %s, Owner: %s, RemoteRole: %d"),
			*CurrentWeapon->GetName(), *GetNameSafe(CurrentWeapon->GetOwner()), (int32)CurrentWeapon->GetRemoteRole());

		// Attach weapon meshes to the owner
		AttachWeaponMeshes(CurrentWeapon);

		// Activate the weapon to show it and update UI
		CurrentWeapon->ActivateWeapon();

		// Update bullet counter UI for local players
		if (IsLocallyControlled())
		{
			OnBulletCountUpdated.Broadcast(CurrentWeapon->GetMagazineSize(), CurrentWeapon->GetBulletCount());
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Updated bullet UI: %d/%d"), CurrentWeapon->GetBulletCount(), CurrentWeapon->GetMagazineSize());
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  CurrentWeapon is NULL after replication!"));
	}
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate HP, team byte, and current weapon to all clients
	DOREPLIFETIME(AShooterCharacter, MaxHP);
	DOREPLIFETIME(AShooterCharacter, CurrentHP);
	DOREPLIFETIME(AShooterCharacter, TeamByte);
	DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
}
