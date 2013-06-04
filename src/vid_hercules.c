/*Hercules emulation*/
#include "ibm.h"
#include "mem.h"
#include "timer.h"
#include "video.h"

void hercules_recalctimings();
void hercules_write(uint32_t addr, uint8_t val, void *priv);
uint8_t hercules_read(uint32_t addr, void *priv);

static uint8_t hercules_ctrl, hercules_ctrl2, hercules_stat;
uint8_t crtcm[32],crtcmreg;

void hercules_out(uint16_t addr, uint8_t val, void *priv)
{
//        pclog("Herc out %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0x3B0: case 0x3B2: case 0x3B4: case 0x3B6:
                crtcmreg = val & 31;
                return;
                case 0x3B1: case 0x3B3: case 0x3B5: case 0x3B7:
                crtcm[crtcmreg] = val;
                if (crtcm[10]==6 && crtcm[11]==7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
                {
                        crtcm[10]=0xB;
                        crtcm[11]=0xC;
                }
                hercules_recalctimings();
                return;
                case 0x3B8:
                hercules_ctrl = val;
                return;
                case 0x3BF:
                hercules_ctrl2 = val;
                video_write_b800 = (val&2) ? hercules_write : video_write_null;
                video_read_b800  = (val&2) ? hercules_read  : video_read_null;
                return;
        }
}

uint8_t hercules_in(uint16_t addr, void *priv)
{
 //       pclog("Herc in %04X %02X %04X:%04X %04X\n",addr,(hercules_stat & 0xF) | ((hercules_stat & 8) << 4),CS,pc,CX);
        switch (addr)
        {
                case 0x3B0: case 0x3B2: case 0x3B4: case 0x3B6:
                return crtcmreg;
                case 0x3B1: case 0x3B3: case 0x3B5: case 0x3B7:
                return crtcm[crtcmreg];
                case 0x3BA:
                return (hercules_stat & 0xF) | ((hercules_stat & 8) << 4);
        }
        return 0xff;
}

void hercules_write(uint32_t addr, uint8_t val, void *priv)
{
//        pclog("Herc write %08X %02X\n",addr,val);
        vram[addr&0xFFFF]=val;
}

uint8_t hercules_read(uint32_t addr, void *priv)
{
        return vram[addr&0xFFFF];
}

void hercules_recalctimings()
{
	double _dispontime, _dispofftime;
        disptime=crtc[0]+1;
        _dispontime=crtc[1];
        _dispofftime=disptime-_dispontime;
        _dispontime*=MDACONST;
        _dispofftime*=MDACONST;
	dispontime = (int)(_dispontime * (1 << TIMER_SHIFT));
	dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}

int mdacols[256][2][2];

static int linepos,displine;
static int vc,sc;
static uint16_t ma,maback;
static int con,coff,cursoron;
static int cgadispon,cgablink;
static int vsynctime,vadj;

void hercules_poll()
{
        uint16_t ca=(crtcm[15]|(crtcm[14]<<8))&0x3FFF;
        int drawcursor;
        int x,c;
        int oldvc;
        uint8_t chr,attr;
        uint16_t dat;
        int cols[4];
        int oldsc;
        int blink;
        if (!linepos)
        {
                //pclog("Poll %i %i\n",vc,sc);
                vidtime+=dispofftime;
                hercules_stat|=1;
                linepos=1;
                oldsc=sc;
                if ((crtcm[8]&3)==3) sc=(sc<<1)&7;
                if (cgadispon)
                {
                        if (displine<firstline)
                        {
                                firstline=displine;
                        }
                        lastline=displine;
                        cols[0]=0;
                        cols[1]=7;
                        if ((hercules_ctrl & 2) && (hercules_ctrl2 & 1))
                        {
                                ca=(sc&3)*0x2000;
                                if ((hercules_ctrl & 0x80) && (hercules_ctrl2 & 2)) ca+=0x8000;
//                                printf("Draw herc %04X\n",ca);
                                for (x=0;x<crtcm[1];x++)
                                {
                                        dat=(vram[((ma<<1)&0x1FFF)+ca]<<8)|vram[((ma<<1)&0x1FFF)+ca+1];
                                        ma++;
                                        for (c=0;c<16;c++)
                                            buffer->line[displine][(x<<4)+c]=(dat&(32768>>c))?7:0;
                                }
                        }
                        else
                        {
                                for (x=0;x<crtcm[1];x++)
                                {
                                        chr=vram[(ma<<1)&0x3FFF];
                                        attr=vram[((ma<<1)+1)&0x3FFF];
                                        drawcursor=((ma==ca) && con && cursoron);
                                        blink=((cgablink&16) && (hercules_ctrl&0x20) && (attr&0x80) && !drawcursor);
                                        if (sc==12 && ((attr&7)==1))
                                        {
                                                for (c=0;c<9;c++)
                                                    buffer->line[displine][(x*9)+c]=mdacols[attr][blink][1];
                                        }
                                        else
                                        {
                                                for (c=0;c<8;c++)
                                                    buffer->line[displine][(x*9)+c]=mdacols[attr][blink][(fontdatm[chr][sc]&(1<<(c^7)))?1:0];
                                                if ((chr&~0x1F)==0xC0) buffer->line[displine][(x*9)+8]=mdacols[attr][blink][fontdatm[chr][sc]&1];
                                                else                   buffer->line[displine][(x*9)+8]=mdacols[attr][blink][0];
                                        }
                                        ma++;
                                        if (drawcursor)
                                        {
                                                for (c=0;c<9;c++)
                                                    buffer->line[displine][(x*9)+c]^=mdacols[attr][0][1];
                                        }
                                }
                        }
                }
                sc=oldsc;
                if (vc==crtcm[7] && !sc)
                {
                        hercules_stat|=8;
//                        printf("VSYNC on %i %i\n",vc,sc);
                }
                displine++;
                if (displine>=500) displine=0;
        }
        else
        {
                vidtime+=dispontime;
                if (cgadispon) hercules_stat&=~1;
                linepos=0;
                if (vsynctime)
                {
                        vsynctime--;
                        if (!vsynctime)
                        {
                                hercules_stat&=~8;
//                                printf("VSYNC off %i %i\n",vc,sc);
                        }
                }
                if (sc==(crtcm[11]&31) || ((crtcm[8]&3)==3 && sc==((crtcm[11]&31)>>1))) { con=0; coff=1; }
                if (vadj)
                {
                        sc++;
                        sc&=31;
                        ma=maback;
                        vadj--;
                        if (!vadj)
                        {
                                cgadispon=1;
                                ma=maback=(crtcm[13]|(crtcm[12]<<8))&0x3FFF;
                                sc=0;
                        }
                }
                else if (sc==crtcm[9] || ((crtcm[8]&3)==3 && sc==(crtcm[9]>>1)))
                {
                        maback=ma;
                        sc=0;
                        oldvc=vc;
                        vc++;
                        vc&=127;
                        if (vc==crtcm[6]) cgadispon=0;
                        if (oldvc==crtcm[4])
                        {
//                                printf("Display over at %i\n",displine);
                                vc=0;
                                vadj=crtcm[5];
                                if (!vadj) cgadispon=1;
                                if (!vadj) ma=maback=(crtcm[13]|(crtcm[12]<<8))&0x3FFF;
                                if ((crtcm[10]&0x60)==0x20) cursoron=0;
                                else                        cursoron=cgablink&16;
                        }
                        if (vc==crtcm[7])
                        {
                                cgadispon=0;
                                displine=0;
                                vsynctime=16;//(crtcm[3]>>4)+1;
                                if (crtcm[7])
                                {
//                                        printf("Lastline %i Firstline %i  %i\n",lastline,firstline,lastline-firstline);
                                        if ((hercules_ctrl & 2) && (hercules_ctrl2 & 1)) x = crtcm[1] << 4;
                                        else                                             x = crtcm[1] * 9;
                                        lastline++;
                                        if (x!=xsize || (lastline-firstline)!=ysize)
                                        {
                                                xsize=x;
                                                ysize=lastline-firstline;
//                                                printf("Resize to %i,%i - R1 %i\n",xsize,ysize,crtcm[1]);
                                                if (xsize<64) xsize=656;
                                                if (ysize<32) ysize=200;
                                                updatewindowsize(xsize,ysize);
                                        }
                                startblit();
                                        video_blit_memtoscreen_8(0, firstline, xsize, ysize);
                                endblit();
                                        frames++;
                                        if ((hercules_ctrl & 2) && (hercules_ctrl2 & 1))
                                        {
                                                video_res_x = crtcm[1] * 16;
                                                video_res_y = crtcm[6] * 4;
                                                video_bpp = 1;
                                        }
                                        else
                                        {
                                                video_res_x = crtcm[1];
                                                video_res_y = crtcm[6];
                                                video_bpp = 0;
                                        }
                                }
                                firstline=1000;
                                lastline=0;
                                cgablink++;
                        }
                }
                else
                {
                        sc++;
                        sc&=31;
                        ma=maback;
                }
                if ((sc==(crtcm[10]&31) || ((crtcm[8]&3)==3 && sc==((crtcm[10]&31)>>1))))
                {
                        con=1;
//                        printf("Cursor on - %02X %02X %02X\n",crtcm[8],crtcm[10],crtcm[11]);
                }
        }
}

int hercules_init()
{
        mem_sethandler(0xb0000, 0x08000, hercules_read, NULL, NULL, hercules_write, NULL, NULL,  NULL);
        return 0;
}

GFXCARD vid_hercules =
{
        hercules_init,
        /*IO at 3Cx/3Dx*/
        video_out_null,
        video_in_null,
        /*IO at 3Ax/3Bx*/
        hercules_out,
        hercules_in,

        hercules_poll,
        hercules_recalctimings,

        video_write_null,
        hercules_write,
        video_write_null,

        video_read_null,
        hercules_read,
        video_read_null
};