/*MESS ROM notes :
        
        - pc2386 BIOS is corrupt (JMP at F000:FFF0 points to RAM)
        - pc2386 video BIOS is underdumped (16k instead of 24k)
        - c386sx16 BIOS fails checksum
*/

#include <stdlib.h>
#include <string.h>
#include "ibm.h"

#include "config.h"
#include "mem.h"
#include "video.h"
#include "x86.h"
#include "cpu.h"
#include "rom.h"
#include "x86_ops.h"
#include "codegen.h"

page_t *pages;
page_t **page_lookup;

static uint8_t         (*_mem_read_b[0x40000])(uint32_t addr, void *priv);
static uint16_t        (*_mem_read_w[0x40000])(uint32_t addr, void *priv);
static uint32_t        (*_mem_read_l[0x40000])(uint32_t addr, void *priv);
static void           (*_mem_write_b[0x40000])(uint32_t addr, uint8_t  val, void *priv);
static void           (*_mem_write_w[0x40000])(uint32_t addr, uint16_t val, void *priv);
static void           (*_mem_write_l[0x40000])(uint32_t addr, uint32_t val, void *priv);
static uint8_t            *_mem_exec[0x40000];
static void             *_mem_priv_r[0x40000];
static void             *_mem_priv_w[0x40000];
static mem_mapping_t *_mem_mapping_r[0x40000];
static mem_mapping_t *_mem_mapping_w[0x40000];
static int                _mem_state[0x40000];

static mem_mapping_t base_mapping;
mem_mapping_t ram_low_mapping;
mem_mapping_t ram_high_mapping;
mem_mapping_t ram_mid_mapping;
mem_mapping_t ram_remapped_mapping;
mem_mapping_t bios_mapping[8];
mem_mapping_t bios_high_mapping[8];
static mem_mapping_t romext_mapping;

int shadowbios,shadowbios_write;

unsigned char isram[0x10000];

static uint8_t ff_array[0x1000];

int mem_size;
uint32_t biosmask;
int readlnum=0,writelnum=0;
int cachesize=256;

uint8_t *ram, *rom = NULL;
uint8_t romext[32768];

static void romfread(uint8_t *buf, size_t size, size_t count, FILE *fp)
{
	int result = fread(buf,size,count,fp);
	if (result < count)
	{
		pclog("ROM read failed: Expected %d, read %d\n", count, result);
	}
}

static int mem_load_basic(char *path)
{
        char s[256];
        FILE *f;
        
        sprintf(s, "%s/ibm-basic-1.10.rom", path);
        f = romfopen(s, "rb");
        if (!f)
        {
                sprintf(s, "%s/basicc11.f6", path);
                f = romfopen(s, "rb");
                if (!f) return 1; /*I don't really care if BASIC is there or not*/
                romfread(rom + 0x6000, 8192, 1, f);
                fclose(f);
                sprintf(s, "%s/basicc11.f8", path);
                f = romfopen(s, "rb");
                if (!f) return 0; /*But if some of it is there, then all of it must be*/
                romfread(rom + 0x8000, 8192, 1, f);
                fclose(f);
                sprintf(s, "%s/basicc11.fa", path);
                f = romfopen(s, "rb");
                if (!f) return 0;
                romfread(rom + 0xA000, 8192, 1, f);
                fclose(f);
                sprintf(s, "%s/basicc11.fc", path);
                f = romfopen(s, "rb");
                if (!f) return 0;
                romfread(rom + 0xC000, 8192, 1, f);
                fclose(f);
        }
        else
        {
                romfread(rom + 0x6000, 32768, 1, f);
                fclose(f);
        }

        return 1;
}
        
int loadbios()
{
        FILE *f=NULL,*ff=NULL;
        int c;
       
        loadfont("mda.rom", 0);
	loadfont("wy700.rom", 3);
	loadfont("8x12.bin", 4);
        
        biosmask = 0xffff;
        
        if (!rom)
                rom = malloc(0x20000);
        memset(romext,0x63,0x4000);
        memset(rom, 0xff, 0x20000);
        
        pclog("Starting with romset %i\n", romset);
        
        switch (romset)
        {
                case ROM_PC1512:
                f=romfopen("pc1512/40043.v1","rb");
                ff=romfopen("pc1512/40044.v1","rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                loadfont("pc1512/40078.ic127", 2);
                return 1;
                case ROM_PC1640:
                f=romfopen("pc1640/40044.v3","rb");
                ff=romfopen("pc1640/40043.v3","rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                f=romfopen("pc1640/40100","rb");
                if (!f) break;
                fclose(f);
                return 1;
                case ROM_PC200:
                f=romfopen("pc200/pc20v2.1","rb");
                ff=romfopen("pc200/pc20v2.0","rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                loadfont("pc200/40109.bin", 1);
                return 1;
                case ROM_TANDY:
                f=romfopen("tandy/tandy1t1.020","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;
                case ROM_TANDY1000HX:
                f = romfopen("tandy1000hx/v020000.u12", "rb");
                if (!f) break;
                romfread(rom, 0x20000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
                case ROM_TANDY1000SL2:
                f  = romfopen("tandy1000sl2/8079047.hu1" ,"rb");
                ff = romfopen("tandy1000sl2/8079048.hu2","rb");
                if (!f || !ff) break;
                fseek(f,  0x30000/2, SEEK_SET);
                fseek(ff, 0x30000/2, SEEK_SET);
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c + 1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
/*                case ROM_IBMPCJR:
                f=fopen("pcjr/bios.rom","rb");
                romfread(rom+0xE000,8192,1,f);
                fclose(f);
                f=fopen("pcjr/basic.rom","rb");
                romfread(rom+0x6000,32768,1,f);
                fclose(f);
                break;*/
                case ROM_IBMXT:
                f=romfopen("ibmxt/xt.rom","rb");
                if (!f)
                {
                        f = romfopen("ibmxt/5000027.u19", "rb");
                        ff = romfopen("ibmxt/1501512.u18","rb");
                        if (!f || !ff) break;
                        romfread(rom, 0x8000, 1, f);
                        romfread(rom + 0x8000, 0x8000, 1, ff);
                        fclose(ff);
                        fclose(f);
                        return 1;
                }
                else
                {
                        romfread(rom,65536,1,f);
                        fclose(f);
                        return 1;
                }
                break;
                
                case ROM_IBMPCJR:
                f = romfopen("ibmpcjr/bios.rom","rb");
                if (!f) break;
                romfread(rom, 0x10000, 1, f);
                fclose(f);
                return 1;
                
                case ROM_GENXT:
                f=romfopen("genxt/pcxt.rom","rb");
                if (!f) break;
                romfread(rom+0xE000,8192,1,f);
                fclose(f);
                return 1;
                case ROM_DTKXT:
                f=romfopen("dtk/dtk_erso_2.42_2764.bin","rb");
                if (!f) break;
                romfread(rom+0xE000,8192,1,f);
                fclose(f);
                return 1;
                case ROM_OLIM24:
                f  = romfopen("olivetti_m24/olivetti_m24_version_1.43_low.bin" ,"rb");
                ff = romfopen("olivetti_m24/olivetti_m24_version_1.43_high.bin","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x4000; c += 2)
                {
                        rom[c + 0xc000] = getc(f);
                        rom[c + 0xc001] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                        
                case ROM_PC2086:
                f  = romfopen("pc2086/40179.ic129" ,"rb");
                ff = romfopen("pc2086/40180.ic132","rb");
                if (!f || !ff) break;
                pclog("Loading BIOS\n");
                for (c = 0x0000; c < 0x4000; c += 2)
                {
                        rom[c + 0x0000] = getc(f);
                        rom[c + 0x0001] = getc(ff);
                }
                pclog("%02X %02X %02X\n", rom[0xfff0], rom[0xfff1], rom[0xfff2]);
                fclose(ff);
                fclose(f);
                f = romfopen("pc2086/40186.ic171", "rb");
                if (!f) break;
                fclose(f);
                biosmask = 0x3fff;
                return 1;

                case ROM_PC3086:
                f  = romfopen("pc3086/fc00.bin" ,"rb");
                if (!f) break;
                romfread(rom, 0x4000, 1, f);
                fclose(f);
                f = romfopen("pc3086/c000.bin", "rb");
                if (!f) break;
                fclose(f);
                biosmask = 0x3fff;                
                return 1;

                case ROM_IBMAT:
/*                f=romfopen("amic206.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;*/
                case ROM_IBMAT386:
                f = romfopen("ibmat/62x0820.u27", "rb");
                ff  =romfopen("ibmat/62x0821.u47", "rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                case ROM_CMDPC30:
                f  = romfopen("cmdpc30/commodore pc 30 iii even.bin", "rb");
                ff = romfopen("cmdpc30/commodore pc 30 iii odd.bin",  "rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x8000; c += 2)
                {
                        rom[c]     = getc(f);
                        rom[c + 1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x7fff;
                return 1;
                case ROM_DELL200:
                f=romfopen("dells200/dell0.bin","rb");
                ff=romfopen("dells200/dell1.bin","rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
/*                case ROM_IBMAT386:
                f=romfopen("at386/at386.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;*/
                case ROM_AMI386SX:
//                f=romfopen("at386/at386.bin","rb");
                f=romfopen("ami386/ami386.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AMI386DX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                f=romfopen("ami386dx/opt495sx.ami","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;
                case ROM_MR386DX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                f=romfopen("mr386dx/opt495sx.mr","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_ACER386:
                f=romfopen("acer386/acer386.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                rom[0xB0]=0xB0-0x51;
                rom[0x40d4]=0x51; /*PUSH CX*/
                f=romfopen("acer386/oti067.bin","rb");
                if (!f) break;
                fclose(f);
                return 1;

                case ROM_KMXC02:
                f=romfopen("kmxc02/3ctm005.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AMI286:
                f=romfopen("ami286/amic206.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
//                memset(romext,0x63,0x8000);
                return 1;

                case ROM_AWARD286:
                f=romfopen("award286/award.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_GW286CT:
                f=romfopen("gw286ct/2ctc001.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_SPC4200P:
                f=romfopen("spc4200p/u8.01","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_SPC4216P:
                f=romfopen("spc4216p/phoenix.bin","rb");
                if (!f)
                {
                        f = romfopen("spc4216p/7101.u8", "rb");
                        ff = romfopen("spc4216p/ac64.u10","rb");
                        if (!f || !ff) break;
                        for (c = 0x0000; c < 0x10000; c += 2)
                        {
                                rom[c]     = getc(f);
                                rom[c + 1] = getc(ff);
                        }
                        fclose(ff);
                        fclose(f);
                        return 1;
                }
                else
                {
                        romfread(rom,65536,1,f);

                        fclose(f);
                        return 1;
                }
                break;

                case ROM_EUROPC:
//                return 0;
                f=romfopen("europc/50145","rb");
                if (!f) break;
                romfread(rom+0x8000,32768,1,f);
                fclose(f);
//                memset(romext,0x63,0x8000);
                return 1;

                case ROM_IBMPC:
                f=romfopen("ibmpc/pc102782.bin","rb");
                if (!f) break;
//                f=fopen("pc081682.bin","rb");
                romfread(rom+0xE000,8192,1,f);
                fclose(f);
                if (!mem_load_basic("ibmpc"))
                        break;
                return 1;

                case ROM_MEGAPC:
                f  = romfopen("megapc/41651-bios lo.u18",  "rb");
                ff = romfopen("megapc/211253-bios hi.u19", "rb");
                if (!f || !ff) break;
                fseek(f,  0x8000, SEEK_SET);
                fseek(ff, 0x8000, SEEK_SET);                
                for (c = 0x0000; c < 0x10000; c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                        
                case ROM_AMI486:
                f=romfopen("ami486/ami486.bin","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                //is486=1;
                return 1;
                
                case ROM_WIN486:
//                f=romfopen("win486/win486.bin","rb");
                f=romfopen("win486/ali1429g.amw","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                //is486=1;
                return 1;

                case ROM_PCI486:
                f=romfopen("hot-433/hot-433.ami","rb");               
                if (!f) break;
                romfread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                //is486=1;
                return 1;

                case ROM_SIS496:
                f = romfopen("sis496/sis496-1.awa", "rb");
                if (!f) break;
                romfread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                pclog("Load SIS496 %x %x\n", rom[0x1fff0], rom[0xfff0]);
                return 1;
                
                case ROM_430VX:
//                f = romfopen("430vx/ga586atv.bin", "rb");
//                f = fopen("430vx/vx29.bin", "rb");
                f = romfopen("430vx/55xwuq0e.bin", "rb");
//                f=romfopen("430vx/430vx","rb");               
                if (!f) break;
                romfread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                //is486=1;
                return 1;

                case ROM_REVENGE:
                f = romfopen("revenge/1009af2_.bio", "rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                romfread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen("revenge/1009af2_.bi1", "rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                romfread(rom, 0xc000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                //is486=1;
                return 1;
                case ROM_ENDEAVOR:
                f = romfopen("endeavor/1006cb0_.bio", "rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                romfread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen("endeavor/1006cb0_.bi1", "rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                romfread(rom, 0xd000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                //is486=1;
                return 1;

                case ROM_IBMPS1_2011:
#if 0
                f=romfopen("ibmps1es/ibm_1057757_24-05-90.bin","rb");
                ff=romfopen("ibmps1es/ibm_1057757_29-15-90.bin","rb");
                fseek(f, 0x10000, SEEK_SET);
                fseek(ff, 0x10000, SEEK_SET);
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
#endif
//#if 0
                f = romfopen("ibmps1es/f80000.bin", "rb");
                if (!f) break;
                fseek(f, 0x60000, SEEK_SET);
                romfread(rom, 0x20000, 1, f);                
                fclose(f);
//#endif
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS1_2121:
                f = romfopen("ibmps1_2121/fc0000.bin", "rb");
                if (!f) break;
                fseek(f, 0x20000, SEEK_SET);
                romfread(rom, 0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_DESKPRO_386:
                f=romfopen("deskpro386/109592-005.u11.bin","rb");
                ff=romfopen("deskpro386/109591-005.u13.bin","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x8000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x7fff;
                return 1;

                case ROM_AMIXT:
                f = romfopen("amixt/ami_8088_bios_31jan89.bin", "rb");
                if (!f) break;
                romfread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;
                
                case ROM_LTXT:
                f = romfopen("ltxt/27c64.bin", "rb");
                if (!f) break;
                romfread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                if (!mem_load_basic("ltxt"))
                        break;
                return 1;

                case ROM_LXT3:
                f = romfopen("lxt3/27c64d.bin", "rb");
                if (!f) break;
                romfread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                if (!mem_load_basic("lxt3"))
                        break;
                return 1;

                case ROM_PX386: /*Phoenix 80386 BIOS*/
                f=romfopen("px386/3iip001l.bin","rb");
                ff=romfopen("px386/3iip001h.bin","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;

                case ROM_DTK386: /*Uses NEAT chipset*/
                f = romfopen("dtk386/3cto001.bin", "rb");
                if (!f) break;
                romfread(rom, 65536, 1, f);
                fclose(f);
                return 1;

                case ROM_PXXT:
                f = romfopen("pxxt/000p001.bin", "rb");
                if (!f) break;
                romfread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;

                case ROM_JUKOPC:
                f = romfopen("jukopc/000o001.bin", "rb");
                if (!f) break;
                romfread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;
				
		case ROM_IBMPS2_M30_286:
                f = romfopen("ibmps2_m30_286/33f5381a.bin", "rb");
                if (!f) break;
                romfread(rom, 0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M50:
                f=romfopen("i8550021/90x7423.zm14","rb");
                ff=romfopen("i8550021/90x7426.zm16","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                f=romfopen("i8550021/90x7420.zm13","rb");
                ff=romfopen("i8550021/90x7429.zm18","rb");
                if (!f || !ff) break;
                for (c = 0x10000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M55SX:
                f=romfopen("i8555081/33f8146.zm41","rb");
                ff=romfopen("i8555081/33f8145.zm40","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M80:
                f=romfopen("i8580111/15f6637.bin","rb");
                ff=romfopen("i8580111/15f6639.bin","rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
				
                case ROM_ATARIPC3:
                f=romfopen("ataripc3/AWARD_ATARI_PC_BIOS_3.08.BIN","rb");
                if (!f) break;
                romfread(rom+0x8000,32768,1,f);
                fclose(f);
                return 1;

                case ROM_IBMXT286:
                f = romfopen("ibmxt286/BIOS_5162_21APR86_U34_78X7460_27256.BIN", "rb");
                ff  =romfopen("ibmxt286/BIOS_5162_21APR86_U35_78X7461_27256.BIN", "rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
				
                case ROM_EPSON_PCAX:
                f  = romfopen("epson_pcax/EVAX", "rb");
                ff = romfopen("epson_pcax/ODAX",  "rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x8000; c += 2)
                {
                        rom[c]     = getc(f);
                        rom[c + 1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x7fff;
                return 1;

                case ROM_EPSON_PCAX2E:
                f = romfopen("epson_pcax2e/EVAXE", "rb");
                ff = romfopen("epson_pcax2e/ODAXE", "rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
				
                case ROM_EPSON_PCAX3:
                f = romfopen("epson_pcax3/EVAX3", "rb");
                ff = romfopen("epson_pcax3/ODAX3", "rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x10000;c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
				
                case ROM_T3100E:
                loadfont("t3100e/t3100e_font.bin", 5);
                f=romfopen("t3100e/t3100e.rom","rb");
                if (!f) break;
                romfread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_T1000:
                loadfont("t1000/t1000font.rom", 2);
                f=romfopen("t1000/t1000.rom","rb");
                if (!f) break;
                romfread(rom, 0x8000,1,f);
		memcpy(rom + 0x8000, rom, 0x8000);
                biosmask = 0x7fff;
                fclose(f);
                return 1;

                case ROM_T1200:
                loadfont("t1200/t1000font.rom", 2);
                f=romfopen("t1200/t1200_019e.ic15.bin","rb");
                if (!f) break;
                romfread(rom, 0x8000,1,f);
		memcpy(rom + 0x8000, rom, 0x8000);
                biosmask = 0x7fff;
                fclose(f);
                return 1;
        }
        printf("Failed to load ROM!\n");
        if (f) fclose(f);
        if (ff) fclose(ff);
        return 0;
}



//int abrt=0;

void resetreadlookup()
{
        int c;
//        /*if (output) */pclog("resetreadlookup\n");
        memset(readlookup2,0xFF,1024*1024*sizeof(uintptr_t));
        for (c=0;c<256;c++) readlookup[c]=0xFFFFFFFF;
        readlnext=0;
        memset(writelookup2,0xFF,1024*1024*sizeof(uintptr_t));
        memset(page_lookup, 0, (1 << 20) * sizeof(page_t *));
        for (c=0;c<256;c++) writelookup[c]=0xFFFFFFFF;
        writelnext=0;
        pccache=0xFFFFFFFF;
//        readlnum=writelnum=0;

}

int mmuflush=0;
int mmu_perm=4;

void flushmmucache()
{
        int c;
//        /*if (output) */pclog("flushmmucache\n");
/*        for (c=0;c<16;c++)
        {
                if ( readlookup2[0xE0+c]!=0xFFFFFFFF) pclog("RL2 %02X = %08X\n",0xE0+c, readlookup2[0xE0+c]);
                if (writelookup2[0xE0+c]!=0xFFFFFFFF) pclog("WL2 %02X = %08X\n",0xE0+c,writelookup2[0xE0+c]);
        }*/
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;
                }
        }
        mmuflush++;
//        readlnum=writelnum=0;
        pccache=(uint32_t)0xFFFFFFFF;
        pccache2=(uint8_t *)0xFFFFFFFF;
        
//        memset(readlookup,0xFF,sizeof(readlookup));
//        memset(readlookup2,0xFF,1024*1024*4);
//        memset(writelookup,0xFF,sizeof(writelookup));
//        memset(writelookup2,0xFF,1024*1024*4);
/*        if (!(cr0>>31)) return;*/

/*        for (c = 0; c < 1024*1024; c++)
        {
                if (readlookup2[c] != 0xFFFFFFFF)
                {
                        pclog("Readlookup inconsistency - %05X %08X\n", c, readlookup2[c]);
                        dumpregs();
                        exit(-1);
                }
                if (writelookup2[c] != 0xFFFFFFFF)
                {
                        pclog("Readlookup inconsistency - %05X %08X\n", c, readlookup2[c]);
                        dumpregs();
                        exit(-1);
                }
        }*/
        codegen_flush();
}

void flushmmucache_nopc()
{
        int c;
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;
                }
        }
}

void flushmmucache_cr3()
{
        int c;
//        /*if (output) */pclog("flushmmucache_cr3\n");
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)// && !readlookupp[c])
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)// && !writelookupp[c])
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;                        
                }
        }
/*        for (c = 0; c < 1024*1024; c++)
        {
                if (readlookup2[c] != 0xFFFFFFFF)
                {
                        pclog("Readlookup inconsistency - %05X %08X\n", c, readlookup2[c]);
                        dumpregs();
                        exit(-1);
                }
                if (writelookup2[c] != 0xFFFFFFFF)
                {
                        pclog("Readlookup inconsistency - %05X %08X\n", c, readlookup2[c]);
                        dumpregs();
                        exit(-1);
                }
        }*/
}

void mem_flush_write_page(uint32_t addr, uint32_t virt)
{
        int c;
        page_t *page_target = &pages[addr >> 12];
//        pclog("mem_flush_write_page %08x %08x\n", virt, addr);

        for (c = 0; c < 256; c++)        
        {
                if (writelookup[c] != 0xffffffff)
                {
                        uintptr_t target = (uintptr_t)&ram[(uintptr_t)(addr & ~0xfff) - (virt & ~0xfff)];

//                        if ((virt & ~0xfff) == 0xc022e000)
//                                pclog(" Checking %02x %p %p\n", (void *)writelookup2[writelookup[c]], (void *)target);
                        if (writelookup2[writelookup[c]] == target || page_lookup[writelookup[c]] == page_target)
                        {
//                                pclog("  throw out %02x %p %p\n", writelookup[c], (void *)page_lookup[writelookup[c]], (void *)writelookup2[writelookup[c]]);
                                writelookup2[writelookup[c]] = -1;
                                page_lookup[writelookup[c]] = NULL;
                                writelookup[c] = 0xffffffff;
                        }
                }
        }
}

extern int output;

#define mmutranslate_read(addr) mmutranslatereal(addr,0)
#define mmutranslate_write(addr) mmutranslatereal(addr,1)

int pctrans=0;

extern uint32_t testr[9];

uint32_t mmutranslatereal(uint32_t addr, int rw)
{
        uint32_t addr2;
        uint32_t temp,temp2,temp3;
        
                if (cpu_state.abrt) 
                {
//                        pclog("Translate recursive abort\n");
                        return -1;
                }
/*                if ((addr&~0xFFFFF)==0x77f00000) pclog("Do translate %08X %i  %08X  %08X\n",addr,rw,EAX,pc);
                if (addr==0x77f61000) output = 3;
                if (addr==0x77f62000) { dumpregs(); exit(-1); }
                if (addr==0x77f9a000) { dumpregs(); exit(-1); }*/
        addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
        temp=temp2=((uint32_t *)ram)[addr2>>2];
//        if (output == 3) pclog("Do translate %08X %i %08X\n", addr, rw, temp);
        if (!(temp&1))// || (CPL==3 && !(temp&4) && !cpl_override) || (rw && !(temp&2) && (CPL==3 || cr0&WP_FLAG)))
        {
//                if (!nopageerrors) pclog("Section not present! %08X  %08X  %02X  %04X:%08X  %i %i\n",addr,temp,opcode,CS,pc,CPL,rw);

                cr2=addr;
                temp&=1;
                if (CPL==3) temp|=4;
                if (rw) temp|=2;
                cpu_state.abrt = ABRT_PF;
                abrt_error = temp;
/*                if (addr == 0x70046D)
                {
                        dumpregs();
                        exit(-1);
                }*/
                return -1;
        }
        
        if ((temp & 0x80) && (cr4 & CR4_PSE))
        {
                /*4MB page*/
                if ((CPL == 3 && !(temp & 4) && !cpl_override) || (rw && !(temp & 2) && ((CPL == 3 && !cpl_override) || cr0 & WP_FLAG)))
                {
//                        if (!nopageerrors) pclog("Page not present!  %08X   %08X   %02X %02X  %i  %08X  %04X:%08X  %04X:%08X %i  %i %i\n",addr,temp,opcode,opcode2,frame,rmdat32, CS,pc,SS,ESP,ins,CPL,rw);

                        cr2 = addr;
                        temp &= 1;
                        if (CPL == 3)
                                temp |= 4;
                        if (rw)
                                temp |= 2;
                        cpu_state.abrt = ABRT_PF;
                        abrt_error = temp;

                        return -1;
                }

                mmu_perm = temp & 4;
                ((uint32_t *)ram)[addr2>>2] |= 0x20;

                return (temp & ~0x3fffff) + (addr & 0x3fffff);
        }
        
        temp=((uint32_t *)ram)[((temp&~0xFFF)+((addr>>10)&0xFFC))>>2];
        temp3=temp&temp2;
//        if (output == 3) pclog("Do translate %08X %08X\n", temp, temp3);
        if (!(temp&1) || (CPL==3 && !(temp3&4) && !cpl_override) || (rw && !(temp3&2) && ((CPL == 3 && !cpl_override) || cr0&WP_FLAG)))
        {
//                if (!nopageerrors) pclog("Page not present!  %08X   %08X   %02X %02X  %i  %08X  %04X:%08X  %04X:%08X %i  %i %i\n",addr,temp,opcode,opcode2,frame,rmdat32, CS,pc,SS,ESP,ins,CPL,rw);

//                dumpregs();
//                exit(-1);
//                if (addr == 0x815F6E90) output = 3;
/*                if (addr == 0x10ADE020) output = 3;*/
/*                if (addr == 0x10150010 && !nopageerrors)
                {
                        dumpregs();
                        exit(-1);
                }*/

                cr2=addr;
                temp&=1;
                if (CPL==3) temp|=4;
                if (rw) temp|=2;
                cpu_state.abrt = ABRT_PF;
                abrt_error = temp;
//                pclog("%04X\n",abrt);
                return -1;
        }
        mmu_perm=temp&4;
        ((uint32_t *)ram)[addr2>>2]|=0x20;
        ((uint32_t *)ram)[((temp2&~0xFFF)+((addr>>10)&0xFFC))>>2]|=(rw?0x60:0x20);
//        /*if (output) */pclog("Translate %08X %08X %08X  %08X:%08X  %08X\n",addr,(temp&~0xFFF)+(addr&0xFFF),temp,cs,pc,EDI);

        return (temp&~0xFFF)+(addr&0xFFF);
}

uint32_t mmutranslate_noabrt(uint32_t addr, int rw)
{
        uint32_t addr2;
        uint32_t temp,temp2,temp3;
        
        if (cpu_state.abrt) 
                return -1;

        addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
        temp=temp2=((uint32_t *)ram)[addr2>>2];

        if (!(temp&1))
                return -1;

        if ((temp & 0x80) && (cr4 & CR4_PSE))
        {
                /*4MB page*/
                if ((CPL == 3 && !(temp & 4) && !cpl_override) || (rw && !(temp & 2) && (CPL == 3 || cr0 & WP_FLAG)))
                        return -1;

                return (temp & ~0x3fffff) + (addr & 0x3fffff);
        }

        temp=((uint32_t *)ram)[((temp&~0xFFF)+((addr>>10)&0xFFC))>>2];
        temp3=temp&temp2;

        if (!(temp&1) || (CPL==3 && !(temp3&4) && !cpl_override) || (rw && !(temp3&2) && (CPL==3 || cr0&WP_FLAG)))
                return -1;

        return (temp&~0xFFF)+(addr&0xFFF);
}

void mmu_invalidate(uint32_t addr)
{
//        readlookup2[addr >> 12] = writelookup2[addr >> 12] = 0xFFFFFFFF;
        flushmmucache_cr3();
}

void addreadlookup(uint32_t virt, uint32_t phys)
{
//        return;
//        printf("Addreadlookup %08X %08X %08X %08X %08X %08X %02X %08X\n",virt,phys,cs,ds,es,ss,opcode,pc);
        if (virt == 0xffffffff)
                return;
                
        if (readlookup2[virt>>12] != -1) 
        {
/*                if (readlookup2[virt>>12] != phys&~0xfff)
                {
                        pclog("addreadlookup mismatch - %05X000 %05X000\n", readlookup[readlnext], virt >> 12);
                        dumpregs();
                        exit(-1);
                }*/
                return;
        }
        
        
        if (readlookup[readlnext]!=0xFFFFFFFF)
        {
                readlookup2[readlookup[readlnext]] = -1;
//                readlnum--;
        }
        readlookup2[virt>>12] = (uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
        readlookupp[readlnext]=mmu_perm;
        readlookup[readlnext++]=virt>>12;
        readlnext&=(cachesize-1);
        
        cycles -= 9;
}

void addwritelookup(uint32_t virt, uint32_t phys)
{
//        return;
//        printf("Addwritelookup %08X %08X\n",virt,phys);
        if (virt == 0xffffffff)
                return;

        if (page_lookup[virt >> 12])
        {
/*                if (writelookup2[virt>>12] != phys&~0xfff)
                {
                        pclog("addwritelookup mismatch - %05X000 %05X000\n", readlookup[readlnext], virt >> 12);
                        dumpregs();
                        exit(-1);
                }*/
                return;
        }
        
        if (writelookup[writelnext] != -1)
        {
                page_lookup[writelookup[writelnext]] = NULL;
                writelookup2[writelookup[writelnext]] = -1;
//                writelnum--;
        }
//        if (page_lookup[virt >> 12] && (writelookup2[virt>>12] != 0xffffffff))
//                fatal("Bad write mapping\n");

        if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3] || (phys & ~0xfff) == recomp_page)
                page_lookup[virt >> 12] = &pages[phys >> 12];//(uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
        else
                writelookup2[virt>>12] = (uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
//        pclog("addwritelookup %08x %08x %p %p %016llx %p\n", virt, phys, (void *)page_lookup[virt >> 12], (void *)writelookup2[virt >> 12], pages[phys >> 12].dirty_mask, (void *)&pages[phys >> 12]);
        writelookupp[writelnext] = mmu_perm;
        writelookup[writelnext++] = virt >> 12;
        writelnext &= (cachesize - 1);

        cycles -= 9;
}

#undef printf
uint8_t *getpccache(uint32_t a)
{
        uint32_t a2=a;

        if (cr0>>31)
        {
		pctrans=1;
                a = mmutranslate_read(a);
                pctrans=0;

                if (a==0xFFFFFFFF) return ram;
        }
        a&=rammask;

        if (isram[a>>16])
        {
                if ((a >> 16) != 0xF || shadowbios)
                	addreadlookup(a2, a);
                return &ram[(uintptr_t)(a & 0xFFFFF000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        if (_mem_exec[a >> 14])
        {
                return &_mem_exec[a >> 14][(uintptr_t)(a & 0x3000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        pclog("Bad getpccache %08X\n", a);
        return &ff_array[0-(uintptr_t)(a2 & ~0xFFF)];
}
#define printf pclog

uint32_t mem_logical_addr;
uint8_t readmembl(uint32_t addr)
{
        mem_logical_addr = addr;
        if (cr0 >> 31)
        {
                addr = mmutranslate_read(addr);
                if (addr == 0xFFFFFFFF) return 0xFF;
        }
        addr &= rammask;

        if (_mem_read_b[addr >> 14]) return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
//        pclog("Bad readmembl %08X %04X:%08X\n", addr, CS, pc);
        return 0xFF;
}

void writemembl(uint32_t addr, uint8_t val)
{
        mem_logical_addr = addr;

        if (page_lookup[addr>>12])
        {
                page_lookup[addr>>12]->write_b(addr, val, page_lookup[addr>>12]);
                return;
        }
        if (cr0 >> 31)
        {
                addr = mmutranslate_write(addr);
                if (addr == 0xFFFFFFFF) return;
        }
        addr &= rammask;

        if (_mem_write_b[addr >> 14]) _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
//        else                          pclog("Bad writemembl %08X %02X  %04X:%08X\n", addr, val, CS, pc);
}

uint8_t readmemb386l(uint32_t seg, uint32_t addr)
{
        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! rb %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return -1;
        }
        mem_logical_addr = addr = addr + seg;
/*        if (readlookup2[mem_logical_addr >> 12] != 0xFFFFFFFF)
        {
                return ram[readlookup2[mem_logical_addr >> 12] + (mem_logical_addr & 0xFFF)];
        }*/
        
        if (cr0 >> 31)
        {
                addr = mmutranslate_read(addr);
                if (addr == 0xFFFFFFFF) return 0xFF;
        }

        addr &= rammask;

        if (_mem_read_b[addr >> 14]) return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
//        pclog("Bad readmemb386l %08X %04X:%08X\n", addr, CS, pc);
        return 0xFF;
}

void writememb386l(uint32_t seg, uint32_t addr, uint8_t val)
{
        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! wb %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return;
        }
        
        mem_logical_addr = addr = addr + seg;
        if (page_lookup[addr>>12])
        {
                page_lookup[addr>>12]->write_b(addr, val, page_lookup[addr>>12]);
                return;
        }
        if (cr0 >> 31)
        {
                addr = mmutranslate_write(addr);
                if (addr == 0xFFFFFFFF) return;
        }

        addr &= rammask;

/*        if (addr >= 0xa0000 && addr < 0xc0000)
           pclog("writemembl %08X %02X\n", addr, val);*/

        if (_mem_write_b[addr >> 14]) _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
//        else                          pclog("Bad writememb386l %08X %02X %04X:%08X\n", addr, val, CS, pc);
}

uint16_t readmemwl(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! rw %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return -1;
        }
        if (addr2 & 1)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) == 7)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFE)
                {
                        if (cr0 >> 31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffff;
                                if (mmutranslate_read(addr2+1) == 0xffffffff) return 0xffff;
                        }
                        if (is386) return readmemb386l(seg,addr)|(readmemb386l(seg,addr+1)<<8);
                        else       return readmembl(seg+addr)|(readmembl(seg+addr+1)<<8);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint16_t *)(readlookup2[addr2 >> 12] + addr2);
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return 0xFFFF;
        }

        addr2 &= rammask;

        if (_mem_read_w[addr2 >> 14]) return _mem_read_w[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]);

        if (_mem_read_b[addr2 >> 14])
        {
                if (AT) return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[(addr2 + 1) >> 14](addr2 + 1, _mem_priv_r[addr2 >> 14]) << 8);
                else    return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[(seg + ((addr + 1) & 0xffff)) >> 14](seg + ((addr + 1) & 0xffff), _mem_priv_r[addr2 >> 14]) << 8);
        }
//        pclog("Bad readmemwl %08X\n", addr2);
        return 0xffff;
}

void writememwl(uint32_t seg, uint32_t addr, uint16_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! ww %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return;
        }

        if (addr2 & 1)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) == 7)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFE)
                {
                        if (cr0 >> 31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+1) == 0xffffffff) return;
                        }
                        if (is386)
                        {
                                writememb386l(seg,addr,val);
                                writememb386l(seg,addr+1,val>>8);
                        }
                        else
                        {
                                writemembl(seg+addr,val);
                                writemembl(seg+addr+1,val>>8);
                        }
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint16_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }

        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_w(addr2, val, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2 &= rammask;

/*        if (addr2 >= 0xa0000 && addr2 < 0xc0000)
           pclog("writememwl %08X %02X\n", addr2, val);*/
        
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2, val, _mem_priv_w[addr2 >> 14]);
                return;
        }

        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2, val, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[(addr2 + 1) >> 14](addr2 + 1, val >> 8, _mem_priv_w[addr2 >> 14]);
                return;
        }
//        pclog("Bad writememwl %08X %04X\n", addr2, val);
}

uint32_t readmemll(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! rl %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return -1;
        }

        if (addr2 & 3)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) > 4)
                        cycles -= timing_misaligned;
                if ((addr2&0xFFF)>0xFFC)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffffffff;
                                if (mmutranslate_read(addr2+3) == 0xffffffff) return 0xffffffff;
                        }
                        return readmemwl(seg,addr)|(readmemwl(seg,addr+2)<<16);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint32_t *)(readlookup2[addr2 >> 12] + addr2);
        }

        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return 0xFFFFFFFF;
        }

        addr2&=rammask;

        if (_mem_read_l[addr2 >> 14]) return _mem_read_l[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]);

        if (_mem_read_w[addr2 >> 14]) return _mem_read_w[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_w[addr2 >> 14](addr2 + 2, _mem_priv_r[addr2 >> 14]) << 16);
        
        if (_mem_read_b[addr2 >> 14]) return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[addr2 >> 14](addr2 + 1, _mem_priv_r[addr2 >> 14]) << 8) | (_mem_read_b[addr2 >> 14](addr2 + 2, _mem_priv_r[addr2 >> 14]) << 16) | (_mem_read_b[addr2 >> 14](addr2 + 3, _mem_priv_r[addr2 >> 14]) << 24);

//        pclog("Bad readmemll %08X\n", addr2);
        return 0xffffffff;
}

void writememll(uint32_t seg, uint32_t addr, uint32_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! wl %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return;
        }
        if (addr2 & 3)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) > 4)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFC)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+3) == 0xffffffff) return;
                        }
                        writememwl(seg,addr,val);
                        writememwl(seg,addr+2,val>>16);
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint32_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }
        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_l(addr2, val, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2&=rammask;

/*        if (addr >= 0xa0000 && addr < 0xc0000)
           pclog("writememll %08X %08X\n", addr, val);*/

        if (_mem_write_l[addr2 >> 14]) 
        {
                _mem_write_l[addr2 >> 14](addr2, val,           _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 1, val >> 8,  _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 3, val >> 24, _mem_priv_w[addr2 >> 14]);
                return;
        }
//        pclog("Bad writememll %08X %08X\n", addr2, val);
}

uint64_t readmemql(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! rl %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return -1;
        }

        if (addr2 & 7)
        {
                cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFF8)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffffffff;
                                if (mmutranslate_read(addr2+7) == 0xffffffff) return 0xffffffff;
                        }
                        return readmemll(seg,addr)|((uint64_t)readmemll(seg,addr+4)<<32);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint64_t *)(readlookup2[addr2 >> 12] + addr2);
        }
        
        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return 0xFFFFFFFF;
        }

        addr2&=rammask;

        if (_mem_read_l[addr2 >> 14])
                return _mem_read_l[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) |
                                 ((uint64_t)_mem_read_l[addr2 >> 14](addr2 + 4, _mem_priv_r[addr2 >> 14]) << 32);

        return readmemll(seg,addr) | ((uint64_t)readmemll(seg,addr+4)<<32);
}

void writememql(uint32_t seg, uint32_t addr, uint64_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                printf("NULL segment! wl %04X(%08X):%08X %02X %08X\n",CS,cs,cpu_state.pc,opcode,addr);
                return;
        }
        if (addr2 & 7)
        {
                cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFF8)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+7) == 0xffffffff) return;
                        }
                        writememll(seg, addr, val);
                        writememll(seg, addr+4, val >> 32);
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint64_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }
        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_l(addr2, val, page_lookup[addr2>>12]);
                page_lookup[addr2>>12]->write_l(addr2 + 4, val >> 32, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2&=rammask;

        if (_mem_write_l[addr2 >> 14]) 
        {
                _mem_write_l[addr2 >> 14](addr2,   val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_l[addr2 >> 14](addr2+4, val >> 32, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 4, val >> 32, _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 6, val >> 48, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 1, val >> 8,  _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 3, val >> 24, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 4, val >> 32, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 5, val >> 40, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 6, val >> 48, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 7, val >> 56, _mem_priv_w[addr2 >> 14]);
                return;
        }
//        pclog("Bad writememql %08X %08X\n", addr2, val);
}

uint8_t mem_readb_phys(uint32_t addr)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_read_b[addr >> 14]) 
                return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
                
        return 0xff;
}
uint16_t mem_readw_phys(uint32_t addr)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_read_w[addr >> 14] && !(addr & 1)) 
                return _mem_read_w[addr >> 14](addr, _mem_priv_r[addr >> 14]);
                
        return mem_readb_phys(addr) | (mem_readb_phys(addr + 1) << 8);
}
uint32_t mem_readl_phys(uint32_t addr)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_read_l[addr >> 14] && !(addr & 3)) 
                return _mem_read_l[addr >> 14](addr, _mem_priv_r[addr >> 14]);
                
        return mem_readw_phys(addr) | (mem_readw_phys(addr + 2) << 16);
}

void mem_writeb_phys(uint32_t addr, uint8_t val)
{
        mem_logical_addr = 0xffffffff;

        if (_mem_write_b[addr >> 14]) 
                _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
}
void mem_writew_phys(uint32_t addr, uint16_t val)
{
        mem_logical_addr = 0xffffffff;

        if (_mem_write_w[addr >> 14] && !(addr & 1)) 
                _mem_write_w[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
        else
        {
                mem_writeb_phys(addr, val);
                mem_writeb_phys(addr+1, val >> 8);
        }
}
void mem_writel_phys(uint32_t addr, uint32_t val)
{
        mem_logical_addr = 0xffffffff;

        if (_mem_write_l[addr >> 14] && !(addr & 3)) 
                _mem_write_l[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
        else
        {
                mem_writew_phys(addr, val);
                mem_writew_phys(addr+2, val >> 16);
        }
}

uint8_t mem_read_ram(uint32_t addr, void *priv)
{
//        if (addr >= 0xc0000 && addr < 0x0c8000) pclog("Read RAMb %08X\n", addr);
        addreadlookup(mem_logical_addr, addr);
        return ram[addr];
}
uint16_t mem_read_ramw(uint32_t addr, void *priv)
{
//        if (addr >= 0xc0000 && addr < 0x0c8000)  pclog("Read RAMw %08X\n", addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint16_t *)&ram[addr];
}
uint32_t mem_read_raml(uint32_t addr, void *priv)
{
//        if (addr >= 0xc0000 && addr < 0x0c8000) pclog("Read RAMl %08X\n", addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint32_t *)&ram[addr];
}

void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *p)
{      
        if (val != p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
//        pclog("mem_write_ramb_page: %08x %02x %08x %llx %llx\n", addr, val, cs+pc, p->dirty_mask, mask);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                p->mem[addr & 0xfff] = val;
        }
}
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p)
{
        if (val != *(uint16_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                if ((addr & 0xf) == 0xf)
                        mask |= (mask << 1);
//        pclog("mem_write_ramw_page: %08x %04x %08x\n", addr, val, cs+pc);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                *(uint16_t *)&p->mem[addr & 0xfff] = val;
        }
}
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *p)
{       
        if (val != *(uint32_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                if ((addr & 0xf) >= 0xd)
                        mask |= (mask << 1);
//        pclog("mem_write_raml_page: %08x %08x %08x\n", addr, val, cs+pc);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                *(uint32_t *)&p->mem[addr & 0xfff] = val;
        }
}

void mem_write_ram(uint32_t addr, uint8_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}
void mem_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}
void mem_write_raml(uint32_t addr, uint32_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[addr >> 12]);
}


static uint32_t remap_start_addr;

uint8_t mem_read_remapped(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return ram[addr];
}
uint16_t mem_read_remappedw(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint16_t *)&ram[addr];
}
uint32_t mem_read_remappedl(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint32_t *)&ram[addr];
}

void mem_write_remapped(uint32_t addr, uint8_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[oldaddr >> 12]);
}
void mem_write_remappedw(uint32_t addr, uint16_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[oldaddr >> 12]);
}
void mem_write_remappedl(uint32_t addr, uint32_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[oldaddr >> 12]);
}

uint8_t mem_read_bios(uint32_t addr, void *priv)
{
                        if (AMIBIOS && (addr&0xFFFFF)==0xF8281) /*This is read constantly during AMIBIOS POST, but is never written to. It's clearly a status register of some kind, but for what?*/
                        {
//                                pclog("Read magic addr %04X(%06X):%04X\n",CS,cs,pc);
//                                if (pc==0x547D) output=3;
                                return 0x40;
                        }
//        pclog("Read BIOS %08X %02X %04X:%04X\n", addr, rom[addr & biosmask], CS, pc);
        return rom[addr & biosmask];
}
uint16_t mem_read_biosw(uint32_t addr, void *priv)
{
//        pclog("Read BIOS %08X %04X %04X:%04X\n", addr, *(uint16_t *)&rom[addr & biosmask], CS, pc);
        return *(uint16_t *)&rom[addr & biosmask];
}
uint32_t mem_read_biosl(uint32_t addr, void *priv)
{
//        pclog("Read BIOS %08X %02X %04X:%04X\n", addr, *(uint32_t *)&rom[addr & biosmask], CS, pc);
        return *(uint32_t *)&rom[addr & biosmask];
}

uint8_t mem_read_romext(uint32_t addr, void *priv)
{
        return romext[addr & 0x7fff];
}
uint16_t mem_read_romextw(uint32_t addr, void *priv)
{
        return *(uint16_t *)&romext[addr & 0x7fff];
}
uint32_t mem_read_romextl(uint32_t addr, void *priv)
{
        return *(uint32_t *)&romext[addr & 0x7fff];
}

void mem_write_null(uint32_t addr, uint8_t val, void *p)
{
}
void mem_write_nullw(uint32_t addr, uint16_t val, void *p)
{
}
void mem_write_nulll(uint32_t addr, uint32_t val, void *p)
{
}

void mem_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
        start_addr &= ~PAGE_MASK_MASK;
        end_addr = (end_addr + PAGE_MASK_MASK) & ~PAGE_MASK_MASK;        
        
        for (; start_addr <= end_addr; start_addr += (1 << PAGE_MASK_SHIFT))
        {
                uint64_t mask = (uint64_t)1 << ((start_addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                
                pages[start_addr >> 12].dirty_mask[(start_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
        }
}

static inline int mem_mapping_read_allowed(uint32_t flags, int state)
{
//        pclog("mem_mapping_read_allowed: flags=%x state=%x\n", flags, state);
        switch (state & MEM_READ_MASK)
        {
                case MEM_READ_ANY:
                return 1;
                case MEM_READ_EXTERNAL:
                return !(flags & MEM_MAPPING_INTERNAL);
                case MEM_READ_INTERNAL:
                return !(flags & MEM_MAPPING_EXTERNAL);
                default:
                fatal("mem_mapping_read_allowed : bad state %x\n", state);
        }
        return 0;
}

static inline int mem_mapping_write_allowed(uint32_t flags, int state)
{
        switch (state & MEM_WRITE_MASK)
        {
                case MEM_WRITE_DISABLED:
                return 0;
                case MEM_WRITE_ANY:
                return 1;
                case MEM_WRITE_EXTERNAL:
                return !(flags & MEM_MAPPING_INTERNAL);
                case MEM_WRITE_INTERNAL:
                return !(flags & MEM_MAPPING_EXTERNAL);
                default:
                fatal("mem_mapping_write_allowed : bad state %x\n", state);
        }
        return 0;
}

static void mem_mapping_recalc(uint64_t base, uint64_t size)
{
        uint64_t c;
        mem_mapping_t *mapping = base_mapping.next;

        if (!size)
                return;
        /*Clear out old mappings*/
        for (c = base; c < base + size; c += 0x4000)
        {
                _mem_read_b[c >> 14] = NULL;
                _mem_read_w[c >> 14] = NULL;
                _mem_read_l[c >> 14] = NULL;
                _mem_priv_r[c >> 14] = NULL;
                _mem_mapping_r[c >> 14] = NULL;
                _mem_write_b[c >> 14] = NULL;
                _mem_write_w[c >> 14] = NULL;
                _mem_write_l[c >> 14] = NULL;
                _mem_priv_w[c >> 14] = NULL;
                _mem_mapping_w[c >> 14] = NULL;
        }

        /*Walk mapping list*/
        while (mapping != NULL)
        {
                /*In range?*/
                if (mapping->enable && (uint64_t)mapping->base < ((uint64_t)base + (uint64_t)size) && ((uint64_t)mapping->base + (uint64_t)mapping->size) > (uint64_t)base)
                {
                        uint64_t start = (mapping->base < base) ? mapping->base : base;
                        uint64_t end   = (((uint64_t)mapping->base + (uint64_t)mapping->size) < (base + size)) ? ((uint64_t)mapping->base + (uint64_t)mapping->size) : (base + size);
                        if (start < mapping->base)
                                start = mapping->base;

                        for (c = start; c < end; c += 0x4000)
                        {
                                if ((mapping->read_b || mapping->read_w || mapping->read_l) &&
                                     mem_mapping_read_allowed(mapping->flags, _mem_state[c >> 14]))
                                {
                                        _mem_read_b[c >> 14] = mapping->read_b;
                                        _mem_read_w[c >> 14] = mapping->read_w;
                                        _mem_read_l[c >> 14] = mapping->read_l;
                                        if (mapping->exec)
                                                _mem_exec[c >> 14] = mapping->exec + (c - mapping->base);
                                        else
                                                _mem_exec[c >> 14] = NULL;
                                        _mem_priv_r[c >> 14] = mapping->p;
                                        _mem_mapping_r[c >> 14] = mapping;
                                }
                                if ((mapping->write_b || mapping->write_w || mapping->write_l) &&
                                     mem_mapping_write_allowed(mapping->flags, _mem_state[c >> 14]))
                                {
                                        _mem_write_b[c >> 14] = mapping->write_b;
                                        _mem_write_w[c >> 14] = mapping->write_w;
                                        _mem_write_l[c >> 14] = mapping->write_l;
                                        _mem_priv_w[c >> 14] = mapping->p;
                                        _mem_mapping_w[c >> 14] = mapping;
                                }
                        }
                }
                mapping = mapping->next;
        }       
        flushmmucache_cr3();
}

void mem_mapping_add(mem_mapping_t *mapping,
                    uint32_t base, 
                    uint32_t size, 
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p),
                    uint8_t *exec,
                    uint32_t flags,
                    void *p)
{
        mem_mapping_t *dest = &base_mapping;

        /*Add mapping to the end of the list*/
        while (dest->next)
                dest = dest->next;        
        dest->next = mapping;
        
        if (size)
                mapping->enable  = 1;
        else
                mapping->enable  = 0;
        mapping->base    = base;
        mapping->size    = size;
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
        mapping->exec    = exec;
        mapping->flags   = flags;
        mapping->p       = p;
        mapping->next    = NULL;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_handler(mem_mapping_t *mapping,
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p))
{
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
        /*Remove old mapping*/
        mapping->enable = 0;
        mem_mapping_recalc(mapping->base, mapping->size);
        
        /*Set new mapping*/
        mapping->enable = 1;
        mapping->base = base;
        mapping->size = size;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_exec(mem_mapping_t *mapping, uint8_t *exec)
{
        mapping->exec = exec;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_p(mem_mapping_t *mapping, void *p)
{
        mapping->p = p;
}

void mem_mapping_disable(mem_mapping_t *mapping)
{
        mapping->enable = 0;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_enable(mem_mapping_t *mapping)
{
        mapping->enable = 1;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_set_mem_state(uint32_t base, uint32_t size, int state)
{
        uint32_t c;

//        pclog("mem_set_pci_enable: base=%08x size=%08x\n", base, size);
        for (c = 0; c < size; c += 0x4000)
                _mem_state[(c + base) >> 14] = state;

        mem_mapping_recalc(base, size);
}

void mem_add_bios()
{
        if (AT)
        {
                mem_mapping_add(&bios_mapping[0], 0xe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[1], 0xe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[2], 0xe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[3], 0xec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
        }
        mem_mapping_add(&bios_mapping[4], 0xf0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[5], 0xf4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[6], 0xf8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[7], 0xfc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), MEM_MAPPING_EXTERNAL, 0);

        mem_mapping_add(&bios_high_mapping[0], (AT && cpu_16bitbus) ? 0xfe0000 : 0xfffe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        0, 0);
        mem_mapping_add(&bios_high_mapping[1], (AT && cpu_16bitbus) ? 0xfe4000 : 0xfffe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[2], (AT && cpu_16bitbus) ? 0xfe8000 : 0xfffe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[3], (AT && cpu_16bitbus) ? 0xfec000 : 0xfffec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[4], (AT && cpu_16bitbus) ? 0xff0000 : 0xffff0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[5], (AT && cpu_16bitbus) ? 0xff4000 : 0xffff4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[6], (AT && cpu_16bitbus) ? 0xff8000 : 0xffff8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[7], (AT && cpu_16bitbus) ? 0xffc000 : 0xffffc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), 0, 0);
}

int mem_a20_key = 0, mem_a20_alt = 0;
static int mem_a20_state = 2;

void mem_init()
{
        int c;

        ram = malloc(mem_size * 1024);
//        rom = malloc(0x20000);
        readlookup2  = malloc(1024 * 1024 * sizeof(uintptr_t));
        writelookup2 = malloc(1024 * 1024 * sizeof(uintptr_t));
        biosmask = 0xffff;
        pages = malloc((((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        page_lookup = malloc((1 << 20) * sizeof(page_t *));

        memset(ram, 0, mem_size * 1024);
        memset(pages, 0, (((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        
        memset(page_lookup, 0, (1 << 20) * sizeof(page_t *));
        
        for (c = 0; c < (((mem_size + 384) * 1024) >> 12); c++)
        {
                pages[c].mem = &ram[c << 12];
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
        }

        memset(isram, 0, sizeof(isram));
        for (c = 0; c < (mem_size / 64); c++)
        {
                isram[c] = 1;
                if ((c >= 0xa && c <= 0xf) || (cpu_16bitbus && c >= 0xfe && c <= 0xff))
                        isram[c] = 0;
        }

        memset(_mem_read_b,  0, sizeof(_mem_read_b));
        memset(_mem_read_w,  0, sizeof(_mem_read_w));
        memset(_mem_read_l,  0, sizeof(_mem_read_l));
        memset(_mem_write_b, 0, sizeof(_mem_write_b));
        memset(_mem_write_w, 0, sizeof(_mem_write_w));
        memset(_mem_write_l, 0, sizeof(_mem_write_l));
        memset(_mem_exec, 0, sizeof(_mem_exec));
        
        memset(ff_array, 0xff, sizeof(ff_array));

        memset(&base_mapping, 0, sizeof(base_mapping));

        memset(_mem_state, 0, sizeof(_mem_state));

        mem_set_mem_state(0x000000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024)
        {
                if (cpu_16bitbus && mem_size > 16256)
                {
                        mem_set_mem_state(0x100000, (16256 - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((16256 - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
                else
                {
                        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((mem_size - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
        }
	if (mem_size > 768)
        	mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);

        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);
//        pclog("Mem resize %i %i\n",mem_size,c);
        mem_a20_key = 2;
        mem_a20_alt = 0;
}

static void mem_remap_top(int max_size)
{
        int c;
        
        if (mem_size > 640)
        {
                uint32_t start = (mem_size >= 1024) ? mem_size : 1024;
                int size = mem_size - 640;
                if (size > max_size)
                        size = max_size;
                
                remap_start_addr = start * 1024;
                        
                for (c = ((start * 1024) >> 12); c < (((start + size) * 1024) >> 12); c++)
                {
                        pages[c].mem = &ram[0xA0000 + ((c - ((start * 1024) >> 12)) << 12)];
                        pages[c].write_b = mem_write_ramb_page;
                        pages[c].write_w = mem_write_ramw_page;
                        pages[c].write_l = mem_write_raml_page;
                }

                mem_set_mem_state(start * 1024, size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                mem_mapping_add(&ram_remapped_mapping, start * 1024, size * 1024, mem_read_remapped,    mem_read_remappedw,    mem_read_remappedl,    mem_write_remapped, mem_write_remappedw, mem_write_remappedl,   ram + 0xA0000,  MEM_MAPPING_INTERNAL, NULL);
        }
}

void mem_remap_top_256k()
{
        mem_remap_top(256);
}
void mem_remap_top_384k()
{
        mem_remap_top(384);
}

void mem_resize()
{
        int c;
        
        free(ram);
        ram = malloc(mem_size * 1024);
        memset(ram, 0, mem_size * 1024);
        
        free(pages);
        pages = malloc((((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        memset(pages, 0, (((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        for (c = 0; c < (((mem_size + 384) * 1024) >> 12); c++)
        {
                pages[c].mem = &ram[c << 12];
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
        }
        
        memset(isram, 0, sizeof(isram));
        for (c = 0; c < (mem_size / 64); c++)
        {
                isram[c] = 1;
                if ((c >= 0xa && c <= 0xf) || (cpu_16bitbus && c >= 0xfe && c <= 0xff))
                        isram[c] = 0;
        }

        memset(_mem_read_b,  0, sizeof(_mem_read_b));
        memset(_mem_read_w,  0, sizeof(_mem_read_w));
        memset(_mem_read_l,  0, sizeof(_mem_read_l));
        memset(_mem_write_b, 0, sizeof(_mem_write_b));
        memset(_mem_write_w, 0, sizeof(_mem_write_w));
        memset(_mem_write_l, 0, sizeof(_mem_write_l));
        memset(_mem_exec, 0, sizeof(_mem_exec));
        
        memset(&base_mapping, 0, sizeof(base_mapping));
        
        memset(_mem_state, 0, sizeof(_mem_state));

        mem_set_mem_state(0x000000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        
        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024)
        {
                if (cpu_16bitbus && mem_size > 16256)
                {
                        mem_set_mem_state(0x100000, (16256 - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((16256 - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
                else
                {
                        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((mem_size - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
        }
	if (mem_size > 768)
	        mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);

        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);

//        pclog("Mem resize %i %i\n",mem_size,c);
        mem_a20_key = 2;
        mem_a20_alt = 0;
        mem_a20_recalc();
}

void mem_reset_page_blocks()
{
        int c;
        
        for (c = 0; c < ((mem_size * 1024) >> 12); c++)
        {
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
                pages[c].block[0] = pages[c].block[1] = pages[c].block[2] = pages[c].block[3] = NULL;
                pages[c].block_2[0] = pages[c].block_2[1] = pages[c].block_2[2] = pages[c].block_2[3] = NULL;
        }
}

void mem_a20_recalc()
{
        int state = mem_a20_key | mem_a20_alt;
//        pclog("A20 recalc %i %i\n", state, mem_a20_state);
        if (state && !mem_a20_state)
        {
                rammask = (AT && cpu_16bitbus) ? 0xffffff : 0xffffffff;
                flushmmucache();
        }
        else if (!state && mem_a20_state)
        {
                rammask = (AT && cpu_16bitbus) ? 0xefffff : 0xffefffff;
                flushmmucache();
        }
//        pclog("rammask now %08X\n", rammask);
        mem_a20_state = state;
}

uint32_t get_phys_virt,get_phys_phys;
