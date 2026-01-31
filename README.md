# OnlineFPS 多人游戏技术架构文档

## 项目概览

**项目名称**: OnlineFPS

**项目连接**: [ToyotaJiang/MultiplayerFPS-based-on-UE5.7-Variant_Shooter](https://github.com/ToyotaJiang/MultiplayerFPS-based-on-UE5.7-Variant_Shooter)

**引擎版本**: Unreal Engine 5.7  
**测试方式**: 监听服务器（Listen Server） + 两个客户端（主机玩家 + 远程玩家）  
**网络架构**: Client-Server模型，服务器权威（Server Authoritative）

---

## 目录

1. [网络复制架构](#1-网络复制架构)
2. [客户端UI同步机制](#2-客户端ui同步机制)
3. [角色动画同步](#3-角色动画同步)
4. [AI机器人设计](#4-ai机器人设计)
5. [K/D统计系统](#5-kd统计系统)
6. [测试指南](#6-测试指南)
7. [关键问题与解决方案](#7-关键问题与解决方案)

---

## 1. 网络复制架构

### 1.1 基础网络设置

**服务器权威原则**：所有游戏逻辑（伤害计算、射击、死亡、复活）只在服务器端执行，然后通过网络复制同步到客户端。

#### 核心复制类

```cpp
// ShooterCharacter.h/cpp - 玩家角色
class AShooterCharacter : public AOnlineFPSCharacter
{
    // 复制的属性
    UPROPERTY(Replicated) float MaxHP = 500.0f;
    UPROPERTY(ReplicatedUsing = OnRep_CurrentHP) float CurrentHP = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon) AShooterWeapon* CurrentWeapon;
    UPROPERTY(Replicated) uint8 TeamByte = 0;

    // 网络复制配置
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override
    {
        Super::GetLifetimeReplicatedProps(OutLifetimeProps);
        DOREPLIFETIME(AShooterCharacter, MaxHP);
        DOREPLIFETIME(AShooterCharacter, CurrentHP);
        DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
        DOREPLIFETIME(AShooterCharacter, TeamByte);
    }
};
```

**关键设置**：

- `SetReplicateMovement(true)` - 角色移动自动同步
- `bReplicates = true` - 启用Actor复制
- `DOREPLIFETIME` - 属性复制到所有客户端

### 1.2 武器系统复制

```cpp
// ShooterWeapon.h/cpp
class AShooterWeapon : public AActor
{
    // 武器弹药复制（从COND_OwnerOnly改为DOREPLIFETIME解决Client UI不更新问题）
    UPROPERTY(ReplicatedUsing = OnRep_CurrentBullets) int32 CurrentBullets = 0;
    UPROPERTY(Replicated) int32 MagazineSize = 10;

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override
    {
        Super::GetLifetimeReplicatedProps(OutLifetimeProps);
        DOREPLIFETIME(AShooterWeapon, CurrentBullets);  // 复制到所有客户端
        DOREPLIFETIME(AShooterWeapon, MagazineSize);
    }
};
```

**射击流程**（服务器权威）：

```cpp
// 客户端按下射击键
void AShooterCharacter::DoStartFiring()
{
    if (!HasAuthority())
    {
        ServerStartFiring();  // RPC调用服务器
        return;
    }

    // 服务器端执行
    if (CurrentWeapon && !IsDead())
    {
        CurrentWeapon->StartFiring();
    }
}

// 服务器RPC
UFUNCTION(Server, Reliable, WithValidation)
void ServerStartFiring();
```

### 1.3 AI机器人复制

```cpp
// ShooterNPC.h/cpp - AI机器人继承自角色基类
class AShooterNPC : public AOnlineFPSCharacter
{
    // 复制的属性
    UPROPERTY(ReplicatedUsing = OnRep_CurrentHP) float CurrentHP = 100.0f;
    UPROPERTY(Replicated) uint8 TeamByte = 1;

    AShooterNPC()
    {
        // 关键：启用网络复制
        bReplicates = true;
        SetReplicateMovement(true);  // 移动自动同步
    }
};
```

**AI网络特性**：

- AI逻辑**只在服务器运行**（`if (!HasAuthority()) return;`）
- AI移动通过`SetReplicateMovement(true)`自动同步到客户端
- AI射击、死亡、复活通过属性复制同步

---

## 2. 客户端UI同步机制

### 2.1 UI同步架构

**委托（Delegate）驱动模式**：服务器修改属性 → `OnRep_`函数调用 → 触发委托 → UI更新

### 2.2 子弹数UI同步

#### 问题历程：

1. **初始问题**：客户端子弹UI不更新
2. **原因**：`CurrentBullets`使用`COND_OwnerOnly`条件复制，远程客户端收不到数据
3. **解决方案**：改为`DOREPLIFETIME`复制到所有客户端

#### 完整流程：

```cpp
// 1. 服务器射击，子弹数减少
void AShooterWeapon::Fire()
{
    --CurrentBullets;  // 服务器修改

    // 服务器端立即更新本地UI（监听服务器的玩家）
    if (HasAuthority() && PawnOwner && PawnOwner->IsLocallyControlled())
    {
        WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
    }
}

// 2. 网络复制触发OnRep回调（客户端）
void AShooterWeapon::OnRep_CurrentBullets(int32 OldValue)
{
    // 客户端收到复制后更新UI
    WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

// 3. 通过委托通知UI
void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
    OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

// 4. PlayerController监听委托更新UI
void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
    if (BulletCounterUI)
    {
        BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
    }
}
```

### 2.3 血量UI同步

```cpp
// 1. 服务器处理伤害
float AShooterCharacter::TakeDamage(...)
{
    if (!HasAuthority()) return 0.0f;  // 只在服务器执行

    CurrentHP -= Damage;

    // 服务器端立即更新本地UI
    if (IsLocallyControlled())
    {
        OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
    }

    return Damage;
}

// 2. 客户端通过OnRep回调更新UI
void AShooterCharacter::OnRep_CurrentHP(float OldValue)
{
    float LifePercent = FMath::Max(0.0f, CurrentHP / MaxHP);
    OnDamaged.Broadcast(LifePercent);  // 触发委托

    // 处理死亡特效（客户端）
    if (OldValue > 0.0f && CurrentHP <= 0.0f && !HasAuthority())
    {
        // 客户端死亡效果
        GetCharacterMovement()->StopMovementImmediately();
        DisableInput(nullptr);
        BP_OnDeath();  // 蓝图死亡动画
    }
}
```

### 2.4 武器切换UI同步

**关键机制**：`OnRep_Pawn`强制更新

```cpp
// 问题：客户端加入游戏时，可能在武器复制前就创建UI，导致UI不更新
void AShooterPlayerController::OnRep_Pawn()
{
    Super::OnRep_Pawn();

    if (IsLocalPlayerController() && GetPawn())
    {
        if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(NewPawn))
        {
            // 强制更新武器UI（即使OnRep还没触发）
            if (ShooterCharacter->GetCurrentWeapon())
            {
                ShooterCharacter->OnBulletCountUpdated.Broadcast(
                    ShooterCharacter->GetCurrentWeapon()->GetMagazineSize(),
                    ShooterCharacter->GetCurrentWeapon()->GetBulletCount()
                );
            }

            // 强制更新血量UI
            float LifePercent = ShooterCharacter->GetCurrentHP() / ShooterCharacter->GetMaxHP();
            ShooterCharacter->OnDamaged.Broadcast(LifePercent);
        }
    }
}
```

---

## 3. 角色动画同步

### 3.1 死亡动画同步

#### 玩家死亡动画

```cpp
void AShooterCharacter::Die()
{
    // 服务器端死亡逻辑
    if (HasAuthority())
    {
        CurrentWeapon->DeactivateWeapon();
        GetCharacterMovement()->StopMovementImmediately();
        DisableInput(nullptr);
        Tags.Add(DeathTag);  // 添加"Dead"标签

        OnBulletCountUpdated.Broadcast(0, 0);  // 清空UI
        BP_OnDeath();  // 调用蓝图死亡动画

        // 5秒后复活
        GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, 5.0f, false);
    }
}

// 客户端通过OnRep_CurrentHP触发死亡动画
void AShooterCharacter::OnRep_CurrentHP(float OldValue)
{
    if (OldValue > 0.0f && CurrentHP <= 0.0f && !HasAuthority())
    {
        // 客户端死亡效果
        BP_OnDeath();  // 蓝图实现：播放死亡动画、Ragdoll等
    }
}
```

#### AI机器人死亡动画

```cpp
void AShooterNPC::Die()
{
    if (!HasAuthority()) return;

    bIsDead = true;
    Tags.Add(DeathTag);

    // 停止射击和移动
    StopShooting();
    GetCharacterMovement()->StopMovementImmediately();

    // 启用Ragdoll物理（自动同步到客户端）
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
    GetMesh()->SetSimulatePhysics(true);
    GetMesh()->SetPhysicsBlendWeight(1.0f);

    // 5秒后复活
    GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterNPC::Respawn, 5.0f, false);
}

// 客户端通过OnRep同步Ragdoll效果
void AShooterNPC::OnRep_CurrentHP(float OldValue)
{
    if (OldValue > 0.0f && CurrentHP <= 0.0f && !HasAuthority())
    {
        // 客户端启用Ragdoll
        GetMesh()->SetSimulatePhysics(true);
    }
}
```

### 3.2 动画网络优化

**UE自动同步机制**：

- **骨骼网格动画**：通过`AnimInstance`自动同步（基于移动速度、旋转等）
- **Ragdoll物理**：通过`SetSimulatePhysics(true)`自动同步物理状态
- **Montage动画**：通过RPC调用同步（射击动画等）

---

## 4. AI机器人设计

### 4.1 AI架构概览

**双层AI设计**：

1. **主要AI**：StateTree（UE5行为树替代品）
2. **后备AI**：Fallback Timer Logic（StateTree失效时的降级方案）

### 4.2 AI控制器设计

```cpp
// ShooterAIController.h/cpp
class AShooterAIController : public AAIController
{
    // StateTree AI组件（主要AI逻辑）
    UPROPERTY() UStateTreeAIComponent* StateTreeAI;

    // AI感知组件（视觉、听觉）
    UPROPERTY() UAIPerceptionComponent* AIPerception;

    // 当前攻击目标
    UPROPERTY() AActor* TargetEnemy;

    // Fallback AI Timer（降级方案）
    FTimerHandle FallbackAITimer;
    TOptional<FVector> CurrentWanderTarget;

    AShooterAIController()
    {
        bReplicates = true;  // AI控制器不复制（只在服务器运行）
    }
};
```

### 4.3 AI生命周期

#### 初始化

```cpp
void AShooterAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    // 只在服务器运行AI
    if (!HasAuthority()) return;

    if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
    {
        // 订阅NPC死亡事件
        NPC->OnPawnDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);

        // 启动StateTree AI（延迟0.5秒确保初始化完成）
        GetWorld()->GetTimerManager().SetTimer(StartLogicTimer, [this, NPC]()
        {
            StateTreeAI->SetComponentTickEnabled(true);
            StateTreeAI->StartLogic();

            // 如果StateTree Tick被禁用，启用Fallback AI
            if (!StateTreeAI->IsComponentTickEnabled())
            {
                StartFallbackAILogic(NPC);
            }
        }, 0.5f, false);
    }
}
```

#### Fallback AI逻辑

```cpp
void AShooterAIController::StartFallbackAILogic(AShooterNPC* NPC)
{
    // 每0.5秒执行一次AI Tick
    GetWorld()->GetTimerManager().SetTimer(FallbackAITimer, [this, NPC]()
    {
        // 安全检查
        if (!HasAuthority() || !NPC || !NPC->IsAlive()) return;

        AActor* TargetActor = GetCurrentTarget();

        // 寻找目标（通过AI感知系统）
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

        // 有目标：追击和射击
        if (TargetActor)
        {
            const float Distance = FVector::Dist(NPC->GetActorLocation(), TargetActor->GetActorLocation());

            // 距离>300：移动接近
            if (Distance > 300.0f)
            {
                MoveToActor(TargetActor, 300.0f);
            }

            // 距离<800 且有视线：射击
            if (Distance <= 800.0f && HasLineOfSight(NPC, TargetActor))
            {
                NPC->StartShooting(TargetActor);
            }
            else
            {
                NPC->StopShooting();
            }
        }
        // 无目标：随机游走
        else
        {
            NPC->StopShooting();

            if (!CurrentWanderTarget.IsSet())
            {
                // 在1000单位半径内随机选择点
                FNavLocation RandomLocation;
                if (NavSystem->GetRandomReachablePointInRadius(NPC->GetActorLocation(), 1000.0f, RandomLocation))
                {
                    CurrentWanderTarget = RandomLocation.Location;
                }
            }

            if (CurrentWanderTarget.IsSet())
            {
                MoveToLocation(CurrentWanderTarget.GetValue());
            }
        }

    }, 0.5f, true);  // 每0.5秒循环
}
```

#### AI死亡和复活

```cpp
void AShooterAIController::OnPawnDeath()
{
    ClearCurrentTarget();

    if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
    {
        NPC->StopShooting();
    }

    // 停止Fallback AI（关键：防止死后继续射击）
    StopFallbackAILogic();

    // 停止移动
    if (GetPathFollowingComponent())
    {
        GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);
    }

    // 停止StateTree
    if (StateTreeAI)
    {
        StateTreeAI->StopLogic(FString("Pawn Death"));
    }
}

// NPC复活时重启AI
void AShooterNPC::Respawn()
{
    if (!HasAuthority()) return;

    // 重置状态
    CurrentHP = 100.0f;
    bIsDead = false;
    Tags.Remove(DeathTag);

    // 禁用Ragdoll，恢复移动
    GetMesh()->SetSimulatePhysics(false);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);

    // 重启AI
    if (AShooterAIController* AIController = Cast<AShooterAIController>(GetController()))
    {
        AIController->RestartAILogic();
    }
}
```

### 4.4 AI网络同步

**关键原则**：

- AI逻辑**只在服务器运行**
- AI移动通过`SetReplicateMovement(true)`自动同步
- AI射击、死亡通过属性复制同步
- 客户端只负责渲染AI的移动和动画

---

## 5. K/D统计系统

### 5.1 架构设计

**数据流向**：  
`服务器：伤害计算` → `GameState：K/D统计` → `网络复制` → `客户端PlayerController` → `UI更新`

### 5.2 GameState统计中心

```cpp
// ShooterGameState.h/cpp - 游戏状态（复制到所有客户端）
USTRUCT(BlueprintType)
struct FPlayerStats
{
    GENERATED_BODY()

    UPROPERTY() FString PlayerName;
    UPROPERTY() int32 Kills = 0;
    UPROPERTY() int32 Deaths = 0;
};

class AShooterGameState : public AGameState
{
    // K/D统计数组（复制到所有客户端）
    UPROPERTY(ReplicatedUsing = OnRep_PlayerStats) 
    TArray<FPlayerStats> PlayerStats;

    // 添加击杀记录
    void AddKill(APlayerController* KillerPC)
    {
        FPlayerStats& Stats = GetPlayerStats(KillerPC);
        Stats.Kills++;

        // 触发复制（手动标记为脏数据）
        PlayerStats = PlayerStats;
    }

    // 添加死亡记录
    void AddDeath(APlayerController* VictimPC)
    {
        FPlayerStats& Stats = GetPlayerStats(VictimPC);
        Stats.Deaths++;

        PlayerStats = PlayerStats;
    }
};
```

### 5.3 死亡时记录K/D

```cpp
void AShooterCharacter::Die()
{
    if (HasAuthority())
    {
        if (AShooterGameState* GS = Cast<AShooterGameState>(GetWorld()->GetGameState()))
        {
            // 记录受害者死亡
            if (APlayerController* VictimPC = Cast<APlayerController>(GetController()))
            {
                GS->AddDeath(VictimPC);
            }

            // 记录击杀者击杀（排除自杀）
            if (LastDamageInstigator && LastDamageInstigator != GetController())
            {
                if (APlayerController* KillerPC = Cast<APlayerController>(LastDamageInstigator))
                {
                    GS->AddKill(KillerPC);
                }
            }
        }
    }
}

// AI被击杀时
void AShooterNPC::TakeDamage(...)
{
    if (CurrentHP <= 0.0f && HasAuthority())
    {
        // 只记录玩家击杀AI（不记录AI死亡）
        if (EventInstigator && Cast<APlayerController>(EventInstigator))
        {
            if (AShooterGameState* GS = Cast<AShooterGameState>(GetWorld()->GetGameState()))
            {
                GS->AddKill(Cast<APlayerController>(EventInstigator));
            }
        }

        Die();
    }
}
```

### 5.4 客户端UI更新

#### 定时器更新（解决客户端启动时GameState未复制问题）

```cpp
// ShooterPlayerController.cpp
void AShooterPlayerController::BeginPlay()
{
    Super::BeginPlay();

    if (IsLocalPlayerController())
    {
        // 延迟2秒启动K/D更新Timer（等待GameState复制）
        FTimerHandle DelayedStartTimer;
        GetWorld()->GetTimerManager().SetTimer(DelayedStartTimer, [this]()
        {
            // 启动每秒更新的Timer
            GetWorld()->GetTimerManager().SetTimer(
                KDStatsUpdateTimer, 
                this, 
                &AShooterPlayerController::UpdateKDStatsUI, 
                1.0f,  // 每秒更新
                true   // 循环
            );
        }, 2.0f, false);
    }
}
```

#### 100% 纯C++的K/D UI实现

```cpp
// ShooterBulletCounterUI.cpp
void UShooterBulletCounterUI::NativeConstruct()
{
    Super::NativeConstruct();

    // 如果蓝图没有绑定KDStatsText，纯C++创建
    if (!KDStatsText)
    {
        CPPKDStatsText = NewObject<UTextBlock>(this, UTextBlock::StaticClass());

        // 设置字体和样式
        FSlateFontInfo FontInfo = CPPKDStatsText->GetFont();
        FontInfo.Size = 22;
        CPPKDStatsText->SetFont(FontInfo);
        CPPKDStatsText->SetColorAndOpacity(FLinearColor::Yellow);
        CPPKDStatsText->SetShadowOffset(FVector2D(2.0f, 2.0f));
        CPPKDStatsText->SetShadowColorAndOpacity(FLinearColor::Black);

        // 添加到Canvas（位置：左上角20, 80）
        if (CanvasPanel)
        {
            UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(CPPKDStatsText);
            CanvasSlot->SetPosition(FVector2D(20.0f, 80.0f));
            CanvasSlot->SetSize(FVector2D(300.0f, 40.0f));
        }
    }
}

void UShooterBulletCounterUI::UpdateKDStats(const TArray<FPlayerStats>& PlayerStats)
{
    UTextBlock* TextToUpdate = KDStatsText ? KDStatsText : CPPKDStatsText;
    if (!TextToUpdate) return;

    // 查找本地玩家的K/D数据
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        FString PlayerName = PC->GetPlayerState<APlayerState>()->GetPlayerName();

        int32 Kills = 0, Deaths = 0;
        for (const FPlayerStats& Stats : PlayerStats)
        {
            if (Stats.PlayerName == PlayerName)
            {
                Kills = Stats.Kills;
                Deaths = Stats.Deaths;
                break;
            }
        }

        // 计算K/D比率
        float KDRatio = Deaths > 0 ? (float)Kills / (float)Deaths : (float)Kills;

        // 更新文本："K/D: 5/3 (1.67)"
        FString KDText = FString::Printf(TEXT("K/D: %d/%d (%.2f)"), Kills, Deaths, KDRatio);
        TextToUpdate->SetText(FText::FromString(KDText));

        // 根据K/D比率改变颜色
        if (KDRatio >= 2.0f)
            TextToUpdate->SetColorAndOpacity(FLinearColor::Green);
        else if (KDRatio >= 1.0f)
            TextToUpdate->SetColorAndOpacity(FLinearColor::Yellow);
        else
            TextToUpdate->SetColorAndOpacity(FLinearColor::Red);
    }
}
```

#### GameState复制触发UI更新

```cpp
void AShooterGameState::OnRep_PlayerStats()
{
    // 统计数据复制到客户端时，自动更新所有本地玩家的UI
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
```

---

## 6. 测试指南

### 6.1 测试环境配置

**引擎**: Unreal Engine 5.7  
**模式**: 监听服务器（Listen Server）  
**玩家数**: 2人（主机玩家 + 1个远程客户端）

### 6.2 启动测试步骤

#### 方法1：编辑器内测试（推荐）

1. **打开项目编辑器**
  
2. **设置多人游戏参数**：
  
  - 点击顶部工具栏的 `▼` （Play按钮旁边）
  - 选择 `Advanced Settings`
  - 设置：
    - `Number of Players`: **2**
    - `Net Mode`: **Play As Listen Server**
    - `Run Under One Process`: **勾选**（性能更好）
3. **启动测试**：
  
  - 点击 `Play` 按钮（或按 `Alt + P`）
  - 编辑器会自动打开2个窗口：
    - **窗口1**：监听服务器（主机玩家）
    - **窗口2**：客户端（远程玩家）

#### 方法2：独立程序测试

1. **打包游戏**：
  
  - `File` → `Package Project` → `Windows` → 选择输出目录
2. **启动服务器**：
  
  ```cmd
  OnlineFPS.exe -log
  ```
  
3. **启动客户端**：
  
  ```cmd
  OnlineFPS.exe 127.0.0.1 -log
  ```
  

### 6.3 测试检查项

#### ✅ 基础连接测试

- [ ] 客户端成功连接到监听服务器
- [ ] 两个玩家都能在对方屏幕上看到角色
- [ ] AI机器人在服务器生成后，客户端可见

#### ✅ 移动和输入测试

- [ ] 主机玩家移动正常
- [ ] 客户端玩家移动正常
- [ ] 对方玩家的移动实时同步，无明显延迟
- [ ] AI机器人移动在客户端同步

#### ✅ 射击和伤害测试

- [ ] 主机玩家射击，子弹发射正常
- [ ] 客户端玩家射击，子弹发射正常
- [ ] 射击对方后，对方血量正确减少
- [ ] 射击AI后，AI血量减少且客户端同步死亡动画

#### ✅ UI同步测试

- [ ] **子弹数UI**：射击后，本地玩家子弹数实时更新
- [ ] **血量UI**：受伤后，血条立即更新（主机和客户端）
- [ ] **K/D统计UI**：
  - [ ] 击杀敌人后，K值+1（1秒内更新）
  - [ ] 死亡后，D值+1（1秒内更新）
  - [ ] K/D比率正确计算（K/D: 5/3 (1.67)）
  - [ ] 颜色正确变化（绿色≥2.0，黄色≥1.0，红色<1.0）

#### ✅ 死亡和复活测试

- [ ] 玩家死亡后：
  - [ ] 播放死亡动画（两端同步）
  - [ ] 停止移动和射击
  - [ ] UI子弹数清零
  - [ ] 5秒后自动复活
- [ ] AI死亡后：
  - [ ] 启用Ragdoll物理（客户端同步）
  - [ ] 停止射击和移动
  - [ ] 5秒后原地复活
  - [ ] 复活后AI重新开始巡逻/攻击

#### ✅ AI行为测试

- [ ] AI生成后自动巡逻
- [ ] AI发现玩家后追击
- [ ] AI进入射程后开始射击
- [ ] AI死亡后停止射击（不会尸体射击）
- [ ] AI复活后恢复正常行为

### 6.4 常见问题排查

#### 问题1：客户端UI不更新

**症状**：客户端玩家射击后子弹数不变

**排查**：

```cpp
// 检查武器复制设置
void AShooterWeapon::GetLifetimeReplicatedProps(...)
{
    // ❌ 错误：COND_OwnerOnly - 只复制给拥有者
    DOREPLIFETIME_CONDITION(AShooterWeapon, CurrentBullets, COND_OwnerOnly);

    // ✅ 正确：复制到所有客户端
    DOREPLIFETIME(AShooterWeapon, CurrentBullets);
}
```

#### 问题2：客户端看不到武器

**症状**：客户端玩家手里没有武器模型

**原因**：武器未正确复制或未在`OnRep_Pawn`中强制更新

**解决方案**：

```cpp
void AShooterPlayerController::OnRep_Pawn()
{
    // 强制更新武器显示
    if (AShooterCharacter* Character = Cast<AShooterCharacter>(GetPawn()))
    {
        if (Character->GetCurrentWeapon())
        {
            // 强制触发UI更新
            Character->OnBulletCountUpdated.Broadcast(...);
        }
    }
}
```

#### 问题3：AI死后继续射击

**症状**：AI机器人死亡后子弹还在发射

**原因**：Fallback AI Timer未清除

**解决方案**：

```cpp
void AShooterAIController::OnPawnDeath()
{
    StopFallbackAILogic();  // 清除Timer

    if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
    {
        NPC->StopShooting();
    }
}
```

#### 问题4：K/D UI启动时不显示

**症状**：客户端进入游戏前2秒K/D UI显示错误

**原因**：GameState尚未从服务器复制

**解决方案**：

```cpp
// 延迟2秒启动K/D更新Timer
GetWorld()->GetTimerManager().SetTimer(DelayedStartTimer, [this]()
{
    GetWorld()->GetTimerManager().SetTimer(KDStatsUpdateTimer, ...);
}, 2.0f, false);

// 在更新函数中优雅处理未就绪状态
void UpdateKDStatsUI()
{
    if (!GetWorld()->GetGameState())
    {
        return;  // 静默跳过，下次Timer再试
    }
    // ... 正常更新
}
```

---

## 7. 关键问题与解决方案

### 7.1 网络复制问题

| 问题  | 原因  | 解决方案 |
| --- | --- | --- |
| 客户端子弹UI不更新 | `CurrentBullets`使用`COND_OwnerOnly` | 改为`DOREPLIFETIME`复制到所有客户端 |
| 客户端看不到武器 | 武器复制延迟，UI在武器复制前创建 | `OnRep_Pawn`中强制更新UI |
| AI不移动 | 未设置`SetReplicateMovement(true)` | NPC构造函数添加移动复制设置 |

### 7.2 AI问题

| 问题  | 原因  | 解决方案 |
| --- | --- | --- |
| AI死后继续射击 | Fallback AI Timer未清除 | `OnPawnDeath()`中调用`StopFallbackAILogic()` |
| AI复活后不动 | AI逻辑未重启 | `Respawn()`中调用`AIController->RestartAILogic()` |
| StateTree不工作 | Tick被禁用 | 使用Fallback AI作为降级方案 |

### 7.3 UI问题

| 问题  | 原因  | 解决方案 |
| --- | --- | --- |
| K/D UI启动时不显示 | GameState未复制 | 延迟2秒启动Timer，优雅处理未就绪状态 |
| 受伤反馈消失 | 使用`BindWidget`（必需绑定） | 改为`BindWidgetOptional`，纯C++自动创建 |

---

## 8. 架构优势

### 8.1 服务器权威

**优点**：

- 防止作弊（所有逻辑在服务器验证）
- 一致性保证（单一真实源）
- 易于调试（逻辑集中）

**实现**：

```cpp
if (!HasAuthority())
{
    ServerRPC();  // 客户端调用RPC
    return;
}
// 服务器端逻辑
```

### 8.2 委托驱动UI

**优点**：

- 低耦合（UI和游戏逻辑分离）
- 自动同步（OnRep触发委托）
- 易于扩展（添加监听者）

**模式**：

```
服务器修改数据 → OnRep_XXX → Delegate.Broadcast() → UI更新
```

### 8.3 纯C++实现

**优点**：

- 无需蓝图配置（自动创建UI元素）
- 版本控制友好（代码diff清晰）
- 性能更好（编译期优化）

**示例**：K/D UI完全由C++代码创建和更新，无需任何蓝图操作

---

## 9. 性能优化建议

### 9.1 网络优化

1. **复制频率控制**：
  
  ```cpp
  // 降低非关键属性的复制频率
  DOREPLIFETIME_CONDITION(AShooterCharacter, TeamByte, COND_InitialOnly);
  ```
  
2. **带宽优化**：
  
  - 使用`uint8`而非`int32`（TeamByte）
  - 只复制变化的数据
3. **延迟补偿**：
  
  - 客户端预测（移动、射击）
  - 服务器校正（Hit Validation）

### 9.2 AI优化

1. **感知系统优化**：
  
  ```cpp
  // 降低AI感知更新频率
  AIPerception->SetSenseConfig(UAISense_Sight::StaticClass(), SightConfig);
  SightConfig->SetMaxAge(3.0f);  // 3秒后遗忘目标
  ```
  
2. **Fallback AI频率**：
  
  ```cpp
  // 0.5秒更新一次（足够流畅，性能友好）
  SetTimer(FallbackAITimer, ..., 0.5f, true);
  ```
  

### 9.3 UI优化

1. **定时更新而非实时**：
  
  ```cpp
  // K/D UI每秒更新（而非每次击杀）
  SetTimer(KDStatsUpdateTimer, ..., 1.0f, true);
  ```
  
2. **只更新本地玩家UI**：
  
  ```cpp
  if (IsLocalPlayerController())
  {
      UpdateUI();
  }
  ```
