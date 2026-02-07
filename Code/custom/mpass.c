#include <libdl/player.h>
#include <libdl/utils.h>
#include <libdl/game.h>
#include <libdl/net.h>
#include <libdl/stdio.h>
#include <libdl/random.h>
#include <libdl/string.h>
#include <libdl/area.h>
#include <libdl/spawnpoint.h>
#include <libdl/ui.h>
#include "utils.h"
#include "maputils.h"
#include "shared.h"
#include "bigal.h"
#include "statue.h"
#include "blessings.h"

#define MOB_HAS_DROP_PROBABILITY_LUCKY (0.05)

void frameTick(void);
void mapReturnPlayersToMap(void);
int mboxGetItems(Moby *moby, int forPlayerId, struct MysteryBoxItemWeight **out);
int mobMobyProcessHitFlags(Moby *moby, Moby *hitMoby, float damage, int reactToThorns);

#if BLESSINGS
void blessingsFrameTick(void);
#endif

char HasEquippedNewBlessing[GAME_MAX_PLAYERS];
extern Moby *reactorActiveMoby;
Moby *TeleporterMoby = NULL;
int StatuesActivated = 0;
int BlessingSlotsUnlocked = 0;

// blessings
const char *INTERACT_BLESSING_MESSAGE = "\x11 %s";
const char *BLESSING_NAMES[] = {
		[BLESSING_ITEM_MULTI_JUMP] "Blessing of the Hare",
		[BLESSING_ITEM_LUCK] "Blessing of the Clover",
		[BLESSING_ITEM_BULL] "Blessing of the Bull",
		//[BLESSING_ITEM_ELEM_IMMUNITY] "Blessing of Corrosion",
		[BLESSING_ITEM_THORNS] "Blessing of the Rose",
		[BLESSING_ITEM_HEALTH_REGEN] "Blessing of Vitality",
		[BLESSING_ITEM_AMMO_REGEN] "Blessing of the Hunt",
};

const char *BLESSING_DESCRIPTIONS[BLESSING_ITEM_COUNT] = {
		[BLESSING_ITEM_MULTI_JUMP] "Suddenly the walls seem.. shorter...",
		[BLESSING_ITEM_LUCK] "Vox appreciates your business...",
		[BLESSING_ITEM_BULL] "You feel as though you could charge forever...",
		[BLESSING_ITEM_THORNS] "You feel.. protected...",
		[BLESSING_ITEM_HEALTH_REGEN] "The nanomites inside you begin to change...",
		[BLESSING_ITEM_AMMO_REGEN] "The nanomites inside your weapons begin to change...",
};

const int BLESSINGS_ENABLED[] = {
		BLESSING_ITEM_MULTI_JUMP,
		BLESSING_ITEM_LUCK,
		BLESSING_ITEM_BULL,
		BLESSING_ITEM_THORNS,
		BLESSING_ITEM_HEALTH_REGEN,
		BLESSING_ITEM_AMMO_REGEN,
		// BLESSING_ITEM_ELEM_IMMUNITY,
};

extern const char blessingTexIds[];

VECTOR BLESSING_POSITIONS[BLESSING_ITEM_COUNT] = {
		[BLESSING_ITEM_MULTI_JUMP]
		{ 449.3509, 829.1361, 404.1362, (-90) * MATH_DEG2RAD },
		[BLESSING_ITEM_LUCK]
		{ 439.1009, 811.3826, 404.1362, (-30) * MATH_DEG2RAD },
		[BLESSING_ITEM_BULL]
		{ 418.6009, 811.3826, 404.1362, (30) * MATH_DEG2RAD },
		[BLESSING_ITEM_THORNS]
		{ 408.3509, 829.1361, 404.1362, (90) * MATH_DEG2RAD },
		[BLESSING_ITEM_HEALTH_REGEN]
		{ 418.6009, 846.8896, 404.1362, (150) * MATH_DEG2RAD },
		[BLESSING_ITEM_AMMO_REGEN]
		{ 439.1009, 846.8896, 404.1362, (-150) * MATH_DEG2RAD },
};

//--------------------------------------------------------------------------
// Locations of where to spawn statues
VECTOR statueSpawnPositionRotations[] = {
		{543.98, 757.12, 505.52, 0},
		{0, 0, 180 * MATH_DEG2RAD, 0},
		{412.54, 772.13, 515.49, 0},
		{0, -3.382 * MATH_DEG2RAD, -215.379 * MATH_DEG2RAD, 0},
		{612.77, 940.64, 510.43, 0},
		{7.23 * MATH_DEG2RAD, 0, -44.605 * MATH_DEG2RAD, 0},
};
const int statueSpawnPositionRotationsCount = sizeof(statueSpawnPositionRotations) / (sizeof(VECTOR) * 2);

//--------------------------------------------------------------------------
struct MysteryBoxItemWeight MysteryBoxItemProbabilitiesLucky[] = {
		{7, 0.01},
		{12, 0.03030303},
		{8, 0.04165365},
		{9, 0.07593113},
		{10, 0.08164638},
		{4, 0.1254169},
		{11, 0.1393521},
		{5, 0.1548356},
		{6, 0.2064475},
		{2, 0.2932493},
		{3, 0.7589982},
		{0, 1},
};
const int MysteryBoxItemProbabilitiesLuckyCount = COUNT_OF(MysteryBoxItemProbabilitiesLucky);
extern struct MysteryBoxItemWeight MysteryBoxItemProbabilities[];
extern const int MysteryBoxItemProbabilitiesCount;

//--------------------------------------------------------------------------
void gambitsNoSpeedTick(void)
{
	static int done = 0;
	if (done)
		return;

	Moby *m = mobyListGetStart();
	while ((m = mobyFindNextByOClass(m, UPGRADE_MOBY_OCLASS)))
	{
		if (m->PVar)
		{
			struct UpgradePVar *pvars = m->PVar;
			if (pvars->Type == UPGRADE_SPEED)
			{
				m->Position[2] = 0;
				done = 1;
				DPRINTF("set free %08X\n", m);
			}
		}
		++m;
	}
}

//--------------------------------------------------------------------------
int mpassMboxGetItems(Moby *moby, int forPlayerId, struct MysteryBoxItemWeight **out)
{
	if (!out)
		return 0;

	int count = MysteryBoxItemProbabilitiesCount;
	struct MysteryBoxItemWeight *items = MysteryBoxItemProbabilities;

	// use lucky probabilities if player has luck blessing
	if (forPlayerId >= 0 && blessingsPlayerHasBlessing(forPlayerId, BLESSING_ITEM_LUCK))
	{
		*out = MysteryBoxItemProbabilitiesLucky;
		return MysteryBoxItemProbabilitiesLuckyCount;
	}

	// otherwise use default probabilities
	*out = MysteryBoxItemProbabilities;
	return MysteryBoxItemProbabilitiesCount;
}

//--------------------------------------------------------------------------
int mpassOnHasPlayerUsedTeleporter(Moby *moby, Player *player)
{
	// pointer to player is in $s1
	asm volatile(
			".set noreorder;\n"
			"move %0, $s1"
			: : "r"(player));

	// only filter for the top teleporter moby
	if (moby != TeleporterMoby)
		return 0;

	return HasEquippedNewBlessing[player->PlayerId];
}

//--------------------------------------------------------------------------
void mpassSetPlayerHasEquippedNewBlessing(Player *player, int hasEntered)
{
	HasEquippedNewBlessing[player->PlayerId] = hasEntered;
}

//--------------------------------------------------------------------------
void mpassDrawBlessingTotemQuads(void)
{
	int count = sizeof(BLESSINGS_ENABLED) / sizeof(int);
	int i;

	for (i = 0; i < count; ++i)
	{
		int blessing = BLESSINGS_ENABLED[i];

		// draw
		VECTOR p = {0, 0, 3, 0};
		vector_add(p, BLESSING_POSITIONS[blessing], p);
		// HOOK_JAL(0x205b64dc, 0x004e4d70);
		gfxDrawBillboardQuad(vector_read(p), 1, 0x80FFFFFF, 0x80 << 24, -MATH_PI / 2, blessingTexIds[blessing], 1, 0);
		// HOOK_JAL(0x205b64dc, 0x004c4200);
	}
}

//--------------------------------------------------------------------------
void mpassUpdateBlessingTotems(void)
{
	int local, i, j;
	int count = sizeof(BLESSINGS_ENABLED) / sizeof(int);
	VECTOR dt;
	static char buf[4][64];

	if (!MapConfig.State)
		return;

	for (i = 0; i < count; ++i)
	{
		int blessing = BLESSINGS_ENABLED[i];

		// handle pickup
		for (local = 0; local < GAME_MAX_LOCALS; ++local)
		{
			Player *player = playerGetFromSlot(local);
			if (!player)
				continue;

			int playerId = player->PlayerId;
			struct SurvivalPlayer *playerData = &MapConfig.State->PlayerStates[playerId];
			int slots = blessingsGetPlayerSlots(playerId);
			if (!slots)
				continue;
			if (blessingsPlayerHasBlessing(playerId, blessing))
				continue;

			vector_subtract(dt, player->PlayerPosition, (float *)BLESSING_POSITIONS[blessing]);
			if (vector_sqrmag(dt) < (4 * 4))
			{

				// draw help popup
				snprintf(buf[local], sizeof(buf[local]), INTERACT_BLESSING_MESSAGE, BLESSING_NAMES[blessing]);
				uiShowPopup(local, buf[local]);
				playerData->MessageCooldownTicks = 2;

				if (padGetButtonDown(local, PAD_CIRCLE) > 0)
				{
					for (j = 0; j < slots;)
						if (blessingsGetPlayerBlessingAt(playerId, j++) == BLESSING_ITEM_NONE)
							break;

					blessingsSetPlayerBlessingAt(playerId, j - 1, blessing);
					pushSnack(local, BLESSING_DESCRIPTIONS[blessing], TPS * 2);
					mpassSetPlayerHasEquippedNewBlessing(player, 1);
				}
				break;
			}
		}
	}
}

//--------------------------------------------------------------------------
int mpassStatuesAreActivated(void)
{
	return StatuesActivated == statueSpawnPositionRotationsCount;
}

//--------------------------------------------------------------------------
void mpassUpdateUnlockedBlessingSlots(void)
{
	static int unlockedThisRound = 0;
	static int lastRoundNumber = 0;
	static int roundWasSpecial = 0;
	if (!MapConfig.State)
		return;

#if DEBUG
	BlessingSlotsUnlocked = 3;
#endif

	if (BlessingSlotsUnlocked < 3 && MapConfig.State->RoundCompleteTime && !unlockedThisRound && roundWasSpecial && mpassStatuesAreActivated())
	{
		BlessingSlotsUnlocked += 1;
		unlockedThisRound = 1;
		DPRINTF("unlocked blessing slots %d\n", BlessingSlotsUnlocked);
	}

	// force
	int i;
	for (i = 0; i < GAME_MAX_PLAYERS; ++i)
	{
		blessingsSetPlayerSlots(i, BlessingSlotsUnlocked);
	}

	if (lastRoundNumber != MapConfig.State->RoundNumber)
	{
		lastRoundNumber = MapConfig.State->RoundNumber;
		roundWasSpecial = MapConfig.State->RoundIsSpecial;
		unlockedThisRound = 0;
	}
}

//--------------------------------------------------------------------------
void mpassUpdateBlessingTeleporter(void)
{
	if (!TeleporterMoby)
		return;

	gfxRegisterDrawFunction((void **)0x0022251C, &mpassDrawBlessingTotemQuads, TeleporterMoby);
	if (mpassStatuesAreActivated() && !reactorActiveMoby && MapConfig.State->RoundCompleteTime)
	{
		mobySetState(TeleporterMoby, 1, -1);
		TeleporterMoby->DrawDist = 64;
		TeleporterMoby->UpdateDist = 64;
		TeleporterMoby->CollActive = 0;
	}
	else
	{
#if !DEBUG
		mobySetState(TeleporterMoby, 2, -1);
		TeleporterMoby->DrawDist = 0;
		TeleporterMoby->UpdateDist = 0;
		TeleporterMoby->CollActive = -1;
#endif
	}
}

//--------------------------------------------------------------------------
void mpassReturnPlayersToMap(void)
{
	int i;
	VECTOR p, r, o;

	// if we're in between rounds, don't return
	if (!MapConfig.State)
		return;
	if (MapConfig.State->RoundEndTime)
		return;
	if (!TeleporterMoby)
		return;
	if (TeleporterMoby->State == 1)
		return;

	for (i = 0; i < GAME_MAX_LOCALS; ++i)
	{
		Player *player = playerGetFromSlot(i);
		if (!player || !player->SkinMoby)
			continue;

		// if we're under the map, then we must be dead or in the bunker
		// teleport back up either way
		if (player->PlayerPosition[2] < 460)
		{

			// use player start
			playerGetSpawnpoint(player, p, r, 0);
			vector_fromyaw(o, (player->PlayerId / (float)GAME_MAX_PLAYERS) * MATH_TAU - MATH_PI);
			vector_scale(o, o, 2.5);
			vector_add(p, p, o);
			playerSetPosRot(player, p, r);
			// playerSetHealth(player, maxf(0, player->Health - player->MaxHealth*0.5));
		}

		// reset so they can enter next time the teleporter appears
		mpassSetPlayerHasEquippedNewBlessing(player, 0);
	}
}

//--------------------------------------------------------------------------
int mpassGetDropTypeOnMobKilled(Player *killedByPlayer, Moby *mob, int gadgetId)
{
	float randomValue = randRange(0.0, 1.0);
	float probability = blessingsPlayerHasBlessing(killedByPlayer->PlayerId, BLESSING_ITEM_LUCK) ? MOB_HAS_DROP_PROBABILITY_LUCKY : MOB_HAS_DROP_PROBABILITY;

	// wait for drop cooldown
	if (MapConfig.State && MapConfig.State->DropCooldownTicks > 0)
		return -1;

	// return negative to not spawn
	if (randomValue >= probability)
		return -1;

	// return random drop type
	return randRangeInt(0, DROP_COUNT - 1);
}

//--------------------------------------------------------------------------
int mpassMobMobyProcessHitFlags(Moby *moby, Moby *hitMoby, float damage, int reactToThorns)
{
	int result = 0;
	struct MobPVar *pvars = (struct MobPVar *)moby->PVar;

	Player *player = guberMobyGetPlayerDamager(hitMoby);
	if (player)
		result |= MOB_DO_DAMAGE_HIT_FLAG_HIT_PLAYER;
	if (hitMoby == pvars->MobVars.Target)
		result |= MOB_DO_DAMAGE_HIT_FLAG_HIT_TARGET;
	if (mobyIsMob(hitMoby))
		result |= MOB_DO_DAMAGE_HIT_FLAG_HIT_MOB;

#if BLESSINGS
	if (player && player->timers.postHitInvinc == 0 && blessingsPlayerHasBlessing(player->PlayerId, BLESSING_ITEM_THORNS))
	{
		result |= MOB_DO_DAMAGE_HIT_FLAG_HIT_PLAYER_THORNS;
		if (reactToThorns)
			blessingsMobReactToThorns(moby, damage, player->PlayerId);
	}
#endif

	return result;
}

//--------------------------------------------------------------------------
void mpassOnFrameTick(void)
{
	// call base survival frame tick
	frameTick();

#if BLESSINGS
	blessingsFrameTick();
#endif
}

//--------------------------------------------------------------------------
void mpassTick(void)
{
	if (MapConfig.ClientsReady || !netGetDmeServerConnection())
	{
		bigalSpawn();
		statueSpawn();
	}

	// set msg string for when a player has equipped a new blessing this round
	strncpy(uiMsgString(0x25f9), "The gods have blessed you plenty.", 34);

	mpassUpdateUnlockedBlessingSlots();
	mpassUpdateBlessingTotems();
	mpassUpdateBlessingTeleporter();
}

//--------------------------------------------------------------------------
void mpassInit(void)
{
	HOOK_JAL(0x003dfd18, &mpassOnHasPlayerUsedTeleporter);
	HOOK_J_OP(&mapReturnPlayersToMap, &mpassReturnPlayersToMap, 0);
	HOOK_J_OP(&mobMobyProcessHitFlags, &mpassMobMobyProcessHitFlags, 0);
	HOOK_J_OP(&mboxGetItems, &mpassMboxGetItems, 0);
	MapConfig.Functions.GetDropTypeOnMobKilledFunc = &mpassGetDropTypeOnMobKilled;
	MapConfig.Functions.OnFrameTickFunc = &mpassOnFrameTick;

	// reset player equipped blessings this round
	memset(HasEquippedNewBlessing, 0, sizeof(HasEquippedNewBlessing));

	// enable replaying dialog
	// POKE_U32(0x004E3E2C, 0);

	// change DrawBillboardQuad to use frame tex
	HOOK_JAL(0x005b64dc, 0x004e4d70);

	// find teleporters
	TeleporterMoby = mobyFindNextByOClass(mobyListGetStart(), MOBY_ID_TELEPORT_PAD);
	DPRINTF("teleporter %08X\n", (u32)TeleporterMoby);

	bigalInit();
	statueInit();

#if DEBUG
	blessingsSetPlayerSlots(0, 3);
	// blessingsSetPlayerBlessingAt(0, 0, BLESSING_ITEM_BULL);
	// blessingsSetPlayerBlessingAt(0, 1, BLESSING_ITEM_LUCK);
	// blessingsSetPlayerBlessingAt(0, 2, BLESSING_ITEM_MULTI_JUMP);
#endif
}
