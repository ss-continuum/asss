
/* dist: public */

#include <string.h>

#include "asss.h"

#include "letters.inc"


local void Cbrickwrite(const char *params, Player *p, const Target *target);

local Iplayerdata *pd;
local Igame *game;
local Icmdman *cmd;


EXPORT int MM_bricklayer(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd->AddCommand("brickwrite", Cbrickwrite, NULL);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("brickwrite", Cbrickwrite);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


void Cbrickwrite(const char *params, Player *p, const Target *target)
{
	int i, wid;
	Arena *arena = p->arena;
	int freq = p->p_freq;
	int x = p->position.x >> 4;
	int y = p->position.y >> 4;

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

