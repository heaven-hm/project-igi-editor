# Per-AI-type config research (agent-verified)

## qvm inventory
level1: 37 (1205,1506,1512,1566,1667,1768,1898,1989,2022,2200-2219,2225-2226,3001,3032,3091,3099,3201,3333)
level2: 26 | level3: 63 | level4: 72 | level5: 41 | level6: 55 | level7: 55 | level8: 54
level9: 36 | level10: 42 | level11: 68 | level12: 55 | level13: 73 | level14: 179 (incl 57.qvm, unique 2500-block)
Pattern: dense soldier block (2200s/500s) + scattered specials.

## Type system (igi.exe evidence addresses in comments)
- AiType: 29 archetypes, table walk 0x44EEB0 over strings 0x53C9C0..0x53CA34.
  Order: Rpg0 Gunner1 Sniper2 Anya3 Ekk4 Priboi5 Civilian6 Patrol{Uzi,Ak,Spas,Pistol}7-10
  Guard{...}11-14 SecurityPatrol15-16 MafiaPatrol17-19 MafiaGuard20-22 SpetnazPatrol23-25 SpetnazGuard26-28.
- AiArchetype: 9 behavior scripts, collapse map 0x53C94C, dispatched by AIFunction_DefaultHandler 0x44E060.
  All PATROL*->script 7, all GUARD*->script 8; Rpg/Gunner/Sniper/Anya/Ekk/Priboi/Civilian identity.
  Script index order is REVERSE of rdata string order. observer.qvm/radioguard.qvm ship dead.
- Config registry: LOCAL:common/ai/settings.qvm, container HumanAIConfig + HumanAIConfigItem.
  29 types x 3 difficulties = 87 rows; index = difficulty + 3*aiType (memset 348B at sub_406DF0).
  Difficulty from player profile via sub_406F70; defaults sub_406B70; last declaration wins.

## Row fields (HumanAIConfigItemTask, 34 params)
AiTypeName +296, DifficultyName +40, DamageScale +672(1.0), RecoilThreshold +676(0.33),
InitialMovementDelay +556(0.33), MovementDelay +560(1.0), TargetingTimeout +616(10),
TrackingDistanceM +624(15), LieDownMinDistM +620(15), EvasiveActionProb +564(0.3),
EvasiveStand/KneelRollOrLieDown +568/+572(0.5), RollSide +576/+580(0.5),
HitProbMin/MaxRange +584/+588(1.0/0.25), MaxRoundsPerSequence +552(30,i32),
AimPrecision +628(0.2), MinClipFrac +592(0.1), RandClipFrac +596(0.2),
MinFireDelay +600(1.5), RandFireDelay +604(1.0), MinShotDelay +608(0.1), RandShotDelay +612(0),
GrenadeThrowProb +632(0.33)/WhenReloading +636(0.1), GrenadeDist +640/+644(10/60),
GrenadeFlyTime +648/+652(0.5/0.5), GrenadeExplodeTime +656/+660(0.3/0.7),
CloseCombatHitProb +664(0.4), CloseCombatDamage +668(0.5).

## igi.exe verification points
sub_406450 container/item registration; sub_406B70 defaults; sub_406DF0 shape;
sub_406F70 resolution+difficulty; sub_406330 GD names; 0x44EEB0+0x53C9C0 type table;
0x53C94C archetype map; 0x44E060 dispatch; 0x48D8E0 aim precision consumer;
0x48D8B0 stand-fire; 0x48D000/0x48D080 guard arm; sub_48BFD0 patrol outcomes;
0x48BC00 evasive roll. Full write-ups: open-igi docs/re-notes/ai.md A10-A10.3.
