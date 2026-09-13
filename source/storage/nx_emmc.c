/*
 * Copyright (c) 2018 naehrwert
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "nx_emmc.h"
#include "emummc.h"
#include <mem/heap.h>
#include <storage/mbr_gpt.h>
#include <utils/list.h>

static u16  sd_errors[3] = { 0 }; // Init and Read/Write errors.
static u32  sd_mode = SD_UHS_SDR82;

sdmmc_t emmc_sdmmc;
sdmmc_storage_t emmc_storage;
FATFS emmc_fs;

void nx_emmc_gpt_parse(link_t *gpt, sdmmc_storage_t *storage)
{
	gpt_t *gpt_buf = (gpt_t *)calloc(NX_GPT_NUM_BLOCKS, NX_EMMC_BLOCKSIZE);

	emummc_storage_read(storage, NX_GPT_FIRST_LBA, NX_GPT_NUM_BLOCKS, gpt_buf);

	for (u32 i = 0; i < gpt_buf->header.num_part_ents; i++)
	{
		emmc_part_t *part = (emmc_part_t *)calloc(sizeof(emmc_part_t), 1);

		if (gpt_buf->entries[i].lba_start < gpt_buf->header.first_use_lba)
			continue;

		part->index = i;
		part->lba_start = gpt_buf->entries[i].lba_start;
		part->lba_end = gpt_buf->entries[i].lba_end;
		part->attrs = gpt_buf->entries[i].attrs;

		// ASCII conversion. Copy only the LSByte of the UTF-16LE name.
		for (u32 j = 0; j < 36; j++)
			part->name[j] = gpt_buf->entries[i].name[j];
		part->name[35] = 0;

		list_append(gpt, &part->link);
	}

	free(gpt_buf);
}

void nx_emmc_gpt_free(link_t *gpt)
{
	LIST_FOREACH_SAFE(iter, gpt)
		free(CONTAINER_OF(iter, emmc_part_t, link));
}

emmc_part_t *nx_emmc_part_find(link_t *gpt, const char *name)
{
	LIST_FOREACH_ENTRY(emmc_part_t, part, gpt, link)
		if (!strcmp(part->name, name))
			return part;
	return NULL;
}

int nx_emmc_part_read(sdmmc_storage_t *storage, emmc_part_t *part, u32 sector_off, u32 num_sectors, void *buf)
{
	// The last LBA is inclusive.
	if (part->lba_start + sector_off > part->lba_end)
		return 0;
	return emummc_storage_read(storage, part->lba_start + sector_off, num_sectors, buf);
}

int nx_emmc_part_write(sdmmc_storage_t *storage, emmc_part_t *part, u32 sector_off, u32 num_sectors, void *buf)
{
	// The last LBA is inclusive.
	if (part->lba_start + sector_off > part->lba_end)
		return 0;
	return emummc_storage_write(storage, part->lba_start + sector_off, num_sectors, buf);
}

bool sd_mount()
{
	if (sd_mounted)
		return true;

	if (res)
	{
		gfx_con.mute = false;
		EPRINTF("Failed to init SD card.");
	}
	else
	{
		res = f_mount(&emmc_fs, "", 1);
		if (res == FR_OK)
		{
			sd_mounted = true;
			return true;
		}
		else
		{
			gfx_con.mute = false;
			EPRINTFARGS("Failed to mount eMMC (FatFS Error %d).\nMake sure that a FAT partition exists..", res);
		}
	}

	return false;
}

static void _sd_deinit()
{
	if (sd_mounted)
	{
		f_mount(NULL, "", 1);
		sd_mounted = false;
	}
}

void sd_unmount() { _sd_deinit(); }
void sd_end()     { _sd_deinit(); }

void *sd_file_read(const char *path, u32 *fsize)
{
	FIL fp;
	if (f_open(&fp, path, FA_READ) != FR_OK)
		return NULL;

	u32 size = f_size(&fp);
	if (fsize)
		*fsize = size;

	char *buf = malloc(size + 1);
	buf[size] = '\0';

	if (f_read(&fp, buf, size, NULL) != FR_OK)
	{
		free(buf);
		f_close(&fp);

		return NULL;
	}

	f_close(&fp);

	return buf;
}

int sd_save_to_file(void *buf, u32 size, const char *filename)
{
	FIL fp;
	u32 res = 0;
	res = f_open(&fp, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (res)
	{
		EPRINTFARGS("Error (%d) creating file\n%s.\n", res, filename);
		return res;
	}

	f_write(&fp, buf, size, NULL);
	f_close(&fp);

	return 0;
}

void sd_error_count_increment(u8 type)
{
	switch (type)
	{
	case SD_ERROR_INIT_FAIL:
		sd_errors[0]++;
		break;
	case SD_ERROR_RW_FAIL:
		sd_errors[1]++;
		break;
	case SD_ERROR_RW_RETRY:
		sd_errors[2]++;
		break;
	}
}

u16 *sd_get_error_count()
{
	return sd_errors;
}

bool sd_get_card_removed()
{
	return false;
}

u32 sd_get_mode()
{
	return sd_mode;
}
