
#include "asss.h"

#include "letters.inc"


local void Cbrickwrite(const char *params, int pid, int target);

local Iplayerdata *pd;
local Igame *game;
local Icmdman *cmd;


int MM_bricklayer(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_GAME, &game);
		mm->RegInterest(I_PLAYERDATA, &pd);
		cmd->AddCommand("brickwrite", Cbrickwrite);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("brickwrite", Cbrickwrite);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_GAME, &game);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void Cbrickwrite(const char *params, int pid, int target)
{
	int i, wid;
	int arena = pd->players[pid].arena, freq = pd->players[pid].freq;
	int x = pd->players[pid].position.x >> 4;
	int y = pd->players[pid].position.y >> 4;

	if (!game) return;

	wid = 0;
	for (i = 0; i < strlen(params); i++)
		if (params[i] >= ' ' && params[i] <= '~')
			wid += letterdata[(int)params[i] - ' '].width + 1;

	x -= wid / 2;
	y -= letterheight / 2;

	for (i = 0; i < strlen(params); i++)
		if (params[i] >= ' ' && params[i] <= '~')
		{
			int c = params[i] - ' ';
			int bnum = letterdata[c].bricknum;
			struct bl_brick *brk = letterdata[c].bricks;
			for ( ; bnum ; bnum--, brk++)
			{
				game->DropBrick(
						arena,
						freq,
						x + brk->x1,
						y + brk->y1,
						x + brk->x2,
						y + brk->y2);
			}
			x += letterdata[c].width + 1;
		}
}

