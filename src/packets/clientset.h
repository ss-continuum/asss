
#ifndef __PACKETS_CLIENTSET_H
#define __PACKETS_CLIENTSET_H


/* structs for packet types and data */

struct WeaponBits
{
	unsigned ShrapnelMax 	: 5;
	unsigned ShrapnelRate	: 5;
	unsigned AntiWarpStatus : 2;
	unsigned CloakStatus    : 2;
	unsigned StealthStatus  : 2;
	unsigned XRadarStatus   : 2;
	unsigned InitialGuns	: 2;
	unsigned MaxGuns		: 2;
	unsigned InitialBombs	: 2;
	unsigned MaxBombs		: 2;
	unsigned DoubleBarrel	: 1;
	unsigned EmpBomb		: 1;
	unsigned SeeMines		: 1;
	unsigned Padding		: 3;
};


struct ShipSettings /* 144 bytes */
{
	i32 SuperTime, ShieldsTime;
	i16 Gravity, GravityTopSpeed;
	i16 BulletFireEnergy, MultiFireEnergy;
	i16 BombFireEnergy, BombFireEnergyUpgrade;
	i16 LandmineFireEnergy, LandmineFireEnergyUpgrade;
	i16 BulletSpeed;
	i32 BombSpeed;
	i16 MultiFireAngle;
	i16 CloakEnergy, StealthEnergy, AntiWarpEnergy, XRadarEnergy;
	i16 MaximumRotation, MaximumThrust, MaximumSpeed, MaximumRecharge, MaximumEnergy;
	i16 InitialRotation, InitialThrust, InitialSpeed, InitialRecharge, InitialEnergy;
	i16 UpgradeRotation, UpgradeThrust, UpgradeSpeed, UpgradeRecharge, UpgradeEnergy;
	i16 AfterburnerEnergy, BombThrust, BurstSpeed;
	i16 TurrstThrstPenalty, TurrstSpeedPenalty;
	i16 BulletFireDelay, MultiFireDelay, BombFireDelay, LandmineFireDelay;
	i16 RocketTime, InitialBounty, DamageFactor;
	i16 PrizeShareLimit, AttachBounty, SoccerThrowTime;
	i16 SoccerBallFriction, SoccerBallProximity, SoccerBallSpeed;
	i8  TurretLimit, BurstShrapnel, MaxMines;
	i8  RepelMax, BurstMax, DecoyMax, ThorMax, BrickMax, RocketMax, PortalMax;
	i8  InitialRepel, InitialBurst, InitialBrick, InitialRocket, InitialThor;
	i8  InitialDecoy, InitialPortal;
	i8  BombBounceCount;
	struct WeaponBits Weapons;
	byte Padding[16];
};


struct PrizeWeightFields
{
	i8 QuickCharge, Energy, Rotation, Stealth, Cloak, XRadar;
	i8 Warp, Gun, Bomb, BouncingBillets, Thruster, TopSpeed, Recharge;
	i8 Glue, MultiFire, Proximity, AllWeapons, Shields, Shrapnel;
	i8 AntiWarp, Repel, Burst, Decoy, Thor, MultiPrize, Brick, Rocket, Portal;
};


struct ClientSettings
{
	i32 type; /* 0x0F */
	struct ShipSettings ships[8];
	i32 BulletDamageLevel, BombDamageLevel; /* * 1000 */
	i32 BulletAliveTime, BombAliveTime;
	i32 DecoyAliveTime, SafetyLimit, FrequencyShift;
	i32 MaxFrequency, RepelSpeed, MineAliveTime;
	i32 BurstDamageLevel, BulletDamageUpgrade; /* * 1000 */
	i32 FlagDropDelay, EnterGameFlaggingDelay;
	i32 RocketThrust, RocketSpeed;
	i32 InactiveShrapDamage; /* * 1000 */
	i32 WormholeSwitchTime, ActiveAppShutdownTime, ShrapnelSpeed;
	i32 Unknown_32_a[4];
	i16 SendRoutePercent;
	i16 BombExplodeDelay;
	i16 SendPositionDelay;
	i16 BombExplodePixels;
	i16 DeathPrizeTime, JitterTime, EnterDelay, EngineShutdownTime;
	i16 ProximityDistance;
	i16 BountyIncreaseForKill;
	i16 BounceFactor, MapZoomFactor;
	i16 MaxBonus, MaxPenalty, RewardBase;
	i16 RepelTime, RepelDistance;
	i16 TickerDelay;
	i16 FlaggerOnRadar;
	i16 FlaggerKillMultiplier;
	i16 PrizeFactor, PrizeDelay;
	i16 MinimumVirtual, UpgradeVirtual, PrizeMaxExist, PrizeMinExist, PrizeNegativeFactor;
	i16 DoorDelay, AntiWarpPixels, DoorMode;
	i16 FlagBlankDelay, NoDataFlagDropDelay;
	i16 MultiPrizeCount;
	i16 BrickTime;
	i16 WarpRadiusLimit;
	i16 EBombShutdownTime, EBombDamagePercent;
	i16 RadarNeutralSize;
	i16 WarpPointDelay;
	i16 NearDeathLevel;
	i16 BBombDamagePercent, ShrapnelDamagePercent;
	i16 ClientSlowPacketTime;
	i16 FlagDropResetReward;
	i16 FlaggerFireCostPercent, FlaggerDamagePercent, FlaggerBombFireDelay;
	i16 PassDelay, BallBlankDelay;
	i16 S2CNoDataKickoutDelay;
	i16 FlaggerThrustAdjustment, FlaggerSpeedAdjustment;
	i32 ClientSlowPacketSampleSize;
	i32 Unknowns_32_b[2];
	i8 RandomShrap, BallBounce, AllowBombs, AllowGuns;
	i8 SoccerMode, MaxPerTeam, MaxPerPrivateTeam, TeamMaxMines;
	i8 GravityBombs, BombSafety, MessageReliable, TakePrizeReliable;
	i8 AllowAudioMessages, PrizeHideCount, ExtraPositionData, SlowFrameCheck;
	i8 CarryFlags, AllowSavedShips, RadarMode, VictoryMusic;
	i8 FlaggerGunUpgrade, FlaggerBombUpgrade, UseFlagger, BallLocation;
	i32 Unknown_32_c[2];
	struct PrizeWeightFields PrizeWeights; /* 28 bytes	 */
};

/* all of the 32 bit unknowns (8) are 0x00000000 */

#endif

