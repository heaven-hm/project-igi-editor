# OpenIGI → project-igi-editor Port Audit

Reference root: `D:\Code\open-igi\src\` (vanilla clean-room implementation).
Status legend: `ported` (verified-reference + tests) · `partial` (some behavior,
gaps listed) · `stub` (placeholder) · `missing` · `n/a` (platform/format layer
the editor already owns or does not need).

Audit rule (per PORTING_SPECIFICATION.md §4): every `ported` row must name its
C++ symbol and its test. Rows move down only with evidence.

## 1. Engine core

| OpenIGI | C++ | Status |
|---|---|---|
| Engine/Time/GameClock.cs | game_clock.h/cpp | ported (absolute deadlines, exclusion, catch-up; RuntimeClockTest.*) |
| Engine/Flow/GameLoop.cs | app.cpp OnIdle/Frame + simulation_scheduler | partial — single-window loop done; render/tick split still shares Frame |
| Engine/Tasks/GameTask.cs + ContainerTask.cs | level/task_tree.h/cpp | ported (ownership/lifecycle tests) |
| Engine/Tasks/*.cs (24 files: FlowTask, RenderTask, ScriptTask…) | level/task_tree.cpp | partial — only task types used by slice |
| Scripting/Qvm/* (7) + Runtime/* (13) | level/qvm_parser, qvm_interpreter, qvm_native_registry | ported (bounded LOOP 8.5 subset; roundtrip tests) |
| Scripting/Expressions/* (12) | mission_expression.cpp/h | partial — objective expressions only |

## 2. AI (`OpenIGI.Game/Ai`, 45 files)

| OpenIGI | C++ | Status |
|---|---|---|
| AiPatrolRoute.cs | ai_system.cpp PatrolPeek/Start/Advance/Begin | ported (AiPatrolPortTest.*, A7.3 wrap-once) |
| AiPatrolPath.cs / AiPatrolCommand.cs | AiPatrolCommand + patrol_routes map | ported |
| AiMovementStep.cs | ai_system.cpp Advance/GoTo/StepTowardsNode | ported |
| AiVision.cs / AiViewCone.cs | ai_system.cpp CheckVision + config | partial — two cones yes; detection schedule/slots missing |
| AiDetectionSchedule.cs / AiDetectionSlot.cs | — | missing (staggered detection cadence) |
| AiEventQueues.cs / AiEvent*.cs | ai_events.h/cpp | partial — shared queue + owner filter; priorities/filters missing |
| AiSoldier.cs | ai_system.cpp Update/RunPatrolCommand | partial — patrol/vision/combat chase; gait anims partial |
| AiGuardCombat.cs / AiCombatStateMachine.cs (7 combat files) | ai_system combat branch + weapon path | partial — hitscan via WeaponSystem; burst/arm/channels missing |
| AiScriptApi.cs / AiScriptHost.cs / AiNative.cs | ai_script_host.cpp + qvm_ai_bindings.cpp | partial — CREATE/IDLE/ALERT/COMBAT/DEAD events, patrol/alarm bindings |
| HumanAiConfigRegistry/Tasks.cs | SetupLevelAiGuards | partial |
| AlarmSpeakerRegistry / AiAlarmControl* / SecurityCameraRegistry | script variables only | missing |
| AiStationaryGun.cs / AiGunnerCombat.cs | — | missing |
| AiRunPanicking.cs / AiFreePosition.cs / AiNodeOccupancy.cs | — | missing |
| AiArchetype / AiType / AiDifficulty / HumanAI configs | HumanAI child parsing | partial |

## 3. Player (`OpenIGI.Game/Player`, 61 files)

| OpenIGI | C++ | Status |
|---|---|---|
| PlayerBody.cs / HumanPlayerBody.cs | player_controller.cpp | ported (tick/ground/wall/ceiling tests) |
| HumanMotion.cs | player_motion.cpp | ported |
| HumanGroundQuery.cs | player_collision.cpp QueryGround | ported |
| HumanWallSweep.cs / HumanWallProbe.cs | player_collision.cpp Sweep | ported (multi-height) |
| HumanFallImpact.cs | player_fall_impact.cpp | ported (boundary tests) |
| HumanLadderClimb.cs | player_ladder.cpp | ported (4-phase machine) |
| HumanHealth.cs / HumanDamage.cs | player_controller ApplyDamage/DirectHealth | partial — armor 60% placeholder vs vanilla tables |
| HumanView.cs / HumanCameraMode.cs / DeathCameraVantage.cs | app_view + gameplay viewer | partial |
| HumanViewSway.cs | weapon_view_sway.cpp | ported |
| HumanViewWallPullback.cs | — | missing |
| HumanLocomotion* (5 files: graph/states/triggers/animator/actions) | player_animation_driver.cpp + animation_motion.cpp | partial — locomotion clips play; full state graph missing |
| HumanWeaponGraph.cs / HumanWeaponState.cs / HumanWeaponFlagWord.cs | weapon_system phases | partial |
| HumanShot.cs / HumanHitZoneTable.cs / HumanHitTest.cs | weapon ballistics | partial — zone multipliers missing |
| HumanInteractionDispatch.cs | interaction_query_ in runtime_world | partial |
| HumanSeparation.cs | — | missing (player-vs-enemy pushout) |
| HumanSlopeSlide.cs | AccumulateSlopeSlide | partial |
| HumanIdleTransitions.cs / SoldierDeath*.cs / HumanPlayerDeath.cs | death state basic | stub |
| Binoculars*/ DragunovScope / Mp5SightZoom | zoom flag only | missing scopes |
| MouseLookSettings.cs | profile sensitivity | partial |
| PlayerSpawn.cs | gameplay_spawn.cpp SelectAuthoredPlayerSpawn | ported |
| PlayerLoadout / PlayerWeapons / PlayerInput* | input router + weapon cycle | partial |

## 4. Weapons (`OpenIGI.Game/Weapons`, 32 files)

| OpenIGI | C++ | Status |
|---|---|---|
| GunFiring.cs / GunTiming.cs / GunState.cs | weapon_system.cpp | ported (cadence/burst/spread tests) |
| GunShotBallistics.cs / Ballistics/* (3) | weapon ballistics + projectile_system | partial — penetrations/deflection missing |
| AmmoTypeRegistry / AmmoPool / WeaponTypeRegistry / WeaponConfigRegistry | DefineWeapon parsing | partial — catalog present; full ammo pools partial |
| GrenadePhysics.cs / MissileFlight.cs / Flashbang.cs | projectile_system.cpp | partial — grenade bounce/fuse done; missile/flashbang missing |
| ExplosionDamage.cs | ApplyExplosionDamage | ported (LOS blast tests) |
| BulletImpact.cs / GunImpact.cs / GunCasing.cs / AmmoTracer.cs | impact sounds only | casings/tracers missing |
| WeaponAnimations.cs | animation driver requests | partial |
| HeldGun.cs / WeaponAction.cs | viewmodel draw | partial — user reports view not matching vanilla |
| StatusScreenLayout.cs / SightDisplayType / AmmoDisplayType | HUD | partial — health bar mismatch reported |

## 5. World (`OpenIGI.Game/World`, 76 files)

| OpenIGI | C++ | Status |
|---|---|---|
| DoorState.cs / DoorRegistry.cs | runtime/door_state.cpp | ported |
| ElevatorState.cs / VehiclePhysicsRegistry.cs | — | missing |
| ConditionalContainerGate/Task | runtime container gating | ported |
| CutsceneRuntime / CutSceneTask | removed by product decision | n/a (disabled) |
| Footstep.cs / FootstepSounds.cs | PlayFootstepIfNeeded | partial (surface materials missing) |
| GameMaterialRegistry.cs | material ids parsed | partial (sound mapping incomplete) |
| LadderPlacement / LadderClimbLine | ladder discovery | ported |
| MissionLoader / LoadedMission / GameMissionState | mission_state_loader + level_flow | partial |
| MissionProgression / MissionRegistry | level_flow progression | partial |
| TerrainData/RayTrace/SightBlock/etc (20+ terrain files) | editor terrain_query reuse | partial — sight blocking via static geometry proxy |
| MefMeshBuilder / MefSkinner / MeshGeometry | renderer mesh pipeline | n/a (editor renderer owns) |
| RailroadPath.cs | — | missing (level 3 train) |
| LightFixture* (3) | — | missing |
| WorldRayCast / RayCastBounds | FindProjectileCollision + LOS query | partial |
| BulletMarks.cs | — | missing |
| Pickup registries (Gun/Ammo/Generic) | interaction/pickup state | partial |

## Priority order (user-facing gaps first)

1. HUD health bar to vanilla layout (Weapons/StatusScreenLayout + UI widgets).
2. HeldGun view model presentation parity.
3. AiDetectionSchedule + vision cadence; guard gait animation requests.
4. Combat arm/burst/channels; hit-zone table.
5. HumanSeparation + wall pullback; death camera vantage.
6. Elevator/vehicle/railroad when missions require.
