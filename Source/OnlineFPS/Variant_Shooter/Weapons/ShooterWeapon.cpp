// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "OnlineFPS.h"

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Enable replication for multiplayer
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::BeginPlay - %s, HasAuthority: %s, Owner: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetOwner()));

	// subscribe to the owner's destroyed delegate
	// Note: This must be done carefully to avoid crashes during network replication
	if (!bSubscribedToOwnerDestroyed)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			// Only add delegate if we're not already being destroyed
			// and the owner is valid and not being destroyed
			if (!IsPendingKillPending() && !OwnerActor->IsPendingKillPending())
			{
				OwnerActor->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
				bSubscribedToOwnerDestroyed = true;
				UE_LOG(LogOnlineFPS, Warning, TEXT("  Subscribed to %s destroyed event"), *GetNameSafe(OwnerActor));
			}
			else
			{
				UE_LOG(LogOnlineFPS, Warning, TEXT("  Skipped destroyed delegate subscription - actor pending kill"));
			}
		}
	}
	else
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Already subscribed to owner destroyed event, skipping"));
	}

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	UE_LOG(LogOnlineFPS, Warning, TEXT("  WeaponOwner cast: %s, PawnOwner cast: %s"),
		WeaponOwner ? TEXT("Success") : TEXT("Failed"),
		PawnOwner ? TEXT("Success") : TEXT("Failed"));

	// Only initialize bullets on server
	// On clients, bullets will be replicated from server
	if (HasAuthority())
	{
		// fill the first ammo clip
		CurrentBullets = MagazineSize;
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Server initialized bullets to %d"), CurrentBullets);
	}

	// attach the meshes to the owner
	if (HasAuthority())
	{
		// Server: attach immediately
		if (WeaponOwner)
		{
			WeaponOwner->AttachWeaponMeshes(this);
		}
	}
	else
	{
		// Client: attach if owner exists (might be called via OnRep_Owner)
		if (WeaponOwner)
		{
			WeaponOwner->AttachWeaponMeshes(this);
		}
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
	}

	// unsubscribe from owner's destroyed delegate to prevent crashes
	if (bSubscribedToOwnerDestroyed)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->OnDestroyed.RemoveDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
			UE_LOG(LogOnlineFPS, Warning, TEXT("  Unsubscribed from %s destroyed event"), *GetNameSafe(OwnerActor));
		}
		bSubscribedToOwnerDestroyed = false;
	}
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::OnRep_Owner()
{
	// Refresh owner references when owner is replicated to client
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::OnRep_Owner - %s, Owner: %s"),
		*GetName(), *GetNameSafe(GetOwner()));

	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	UE_LOG(LogOnlineFPS, Warning, TEXT("  WeaponOwner cast: %s, PawnOwner cast: %s"),
		WeaponOwner ? TEXT("Success") : TEXT("Failed"),
		PawnOwner ? TEXT("Success") : TEXT("Failed"));

	// On client, attach weapon meshes when owner is replicated
	if (!HasAuthority() && WeaponOwner)
	{
		WeaponOwner->AttachWeaponMeshes(this);
	}
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::StartFiring - %s, HasAuthority: %s, IsLocallyControlled: %s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"),
		GetOwner() ? (Cast<APawn>(GetOwner()) ? (Cast<APawn>(GetOwner())->IsLocallyControlled() ? TEXT("true") : TEXT("false")) : TEXT("N/A")) : TEXT("NULL"));

	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		// fire the weapon right away
		Fire();

	} else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// Only fire on server
	if (!HasAuthority())
	{
		return;
	}

	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		return;
	}

	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::Fire - Owner: %s, Instigator: %s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(PawnOwner));

	// fire a projectile at the target
	FireProjectile(WeaponOwner->GetWeaponTargetLocation());

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate, false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::FireProjectile - HasAuthority: %s, Owner: %s, Instigator: %s"),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(PawnOwner));

	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	// spawn the projectile
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);

	if (Projectile)
	{
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Projectile spawned: %s"), *Projectile->GetName());
		UE_LOG(LogOnlineFPS, Warning, TEXT("    Projectile Owner: %s, Instigator: %s"),
			*GetNameSafe(Projectile->GetOwner()), *GetNameSafe(Projectile->GetInstigator()));
	}
	else
	{
		UE_LOG(LogOnlineFPS, Error, TEXT("  Failed to spawn projectile!"));
	}

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume bullets
	--CurrentBullets;

	UE_LOG(LogOnlineFPS, Warning, TEXT("  Bullets after firing: %d (Magazine: %d)"),
		CurrentBullets, MagazineSize);

	// if the clip is depleted, reload it
	if (CurrentBullets <= 0)
	{
		CurrentBullets = MagazineSize;
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Magazine reloaded to %d"), CurrentBullets);
	}

	// On server, update HUD directly for local player
	// On clients, OnRep_CurrentBullets will handle HUD updates when bullet count is replicated
	if (HasAuthority() && PawnOwner && PawnOwner->IsLocallyControlled())
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
		UE_LOG(LogOnlineFPS, Warning, TEXT("  Server updating local player HUD"));
	}
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// find the muzzle location
	const FVector MuzzleLoc = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target 
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Note: Owner is already replicated by AActor base class, no need to redeclare

	// Replicate ammo to all clients
	// CurrentBullets is replicated to all clients but only updates HUD for the owner via OnRep
	DOREPLIFETIME(AShooterWeapon, MagazineSize);
	DOREPLIFETIME(AShooterWeapon, CurrentBullets);
}

void AShooterWeapon::OnRep_CurrentBullets(int32 OldValue)
{
	UE_LOG(LogOnlineFPS, Warning, TEXT("AShooterWeapon::OnRep_CurrentBullets - %s, Old: %d, New: %d"),
		*GetName(), OldValue, CurrentBullets);

	// On clients, WeaponOwner and PawnOwner might not be set in BeginPlay because GetOwner() was NULL
	// So we need to refresh them here when the replication happens
	if (!WeaponOwner || !PawnOwner)
	{
		WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
		PawnOwner = Cast<APawn>(GetOwner());
	}

	// Update HUD for the owner if they're the local player
	// On clients, WeaponOwner and PawnOwner point to the replicated owner actor
	// We need to check if this weapon is owned by a locally controlled pawn
	if (WeaponOwner && PawnOwner)
	{
		// Check if the owning pawn is locally controlled (works on both server and client)
		if (PawnOwner->IsLocallyControlled())
		{
			WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
		}
	}
}
