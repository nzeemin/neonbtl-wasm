/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Emulator.cpp

#include "stdafx.h"
#include "emubase/Emubase.h"
#include <emscripten/emscripten.h>
#include "miniz/zip.h"
#include "util/lz4.h"

#define STRINGIZE(_x) STRINGIZE_(_x)
#define STRINGIZE_(_x) #_x

// DebugPrint and DebugLog
void DebugPrint(LPCTSTR) {}
void DebugPrintFormat(LPCTSTR, ...) {}
void DebugLogClear() {}
void DebugLogCloseFile() {}
void DebugLog(LPCTSTR) {}
void DebugLogFormat(LPCTSTR, ...) {}

const int NEON_SCREEN_WIDTH  = 832;
const int NEON_SCREEN_HEIGHT = 300;

#include "pk11_rom.h"

void Emulator_PrepareScreenRGB32(uint32_t* pBits);

//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = nullptr;

bool g_okEmulatorInitialized = false;
bool g_okEmulatorRunning = false;

bool m_okEmulatorSound = false;
bool m_okEmulatorCovox = false;

uint32_t* g_pFrameBuffer = 0;

long m_nFrameCount = 0;
uint32_t m_dwTickCount = 0;
uint32_t m_dwEmulatorUptime = 0;  // Machine uptime, seconds, from turn on or reset, increments every 25 frames
long m_nUptimeFrameCount = 0;

uint8_t m_KeyboardMatrix[8];


//////////////////////////////////////////////////////////////////////


#ifdef __cplusplus
extern "C" {
#endif

    EMSCRIPTEN_KEEPALIVE void Emulator_Init()
    {
        printf("NeonBTL WASM built with Emscripten "
                STRINGIZE(__EMSCRIPTEN_major__) "." STRINGIZE(__EMSCRIPTEN_minor__) "." STRINGIZE(__EMSCRIPTEN_tiny__)
                " at " __DATE__ "\n");

        ::memset(m_KeyboardMatrix, 0, sizeof(m_KeyboardMatrix));

        g_pFrameBuffer = (uint32_t*)malloc(NEON_SCREEN_WIDTH * NEON_SCREEN_HEIGHT * sizeof(uint32_t));

        ASSERT(g_pBoard == NULL);

        CProcessor::Init();

        g_pBoard = new CMotherboard();

        NeonConfiguration configuration = (NeonConfiguration)1024;
        g_pBoard->SetConfiguration((uint16_t)configuration);

        g_pBoard->Reset();

        g_pBoard->LoadROM((const uint8_t*)pk11_rom);

        g_pBoard->Reset();

        g_okEmulatorInitialized = true;
    }

    EMSCRIPTEN_KEEPALIVE float Emulator_GetUptime()
    {
        return m_dwEmulatorUptime + m_nUptimeFrameCount / 25.0;
    }

    EMSCRIPTEN_KEEPALIVE uint16_t Emulator_GetReg()
    {
        return g_pBoard->GetCPU()->GetPC();
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_Start()
    {
        printf("Emulator_Start()\n");

        g_okEmulatorRunning = true;
    }
    EMSCRIPTEN_KEEPALIVE void Emulator_Stop()
    {
        printf("Emulator_Stop()\n");

        g_okEmulatorRunning = false;
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_Reset()
    {
        printf("Emulator_Reset()\n");

        ASSERT(g_pBoard != NULL);

        g_pBoard->Reset();
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_Unzip(const char* archivename, const char* filename)
    {
        struct zip_t* zip = zip_open(archivename, 0, 'r');

        zip_entry_openbyindex(zip, 0);

        zip_entry_fread(zip, filename);

        zip_entry_close(zip);

        zip_close(zip);

        remove(archivename);
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_DetachFloppyImage(int slot)
    {
        g_pBoard->DetachFloppyImage(slot);

        char buffer[6];
        buffer[0] = '/';
        buffer[1] = 'd';
        buffer[2] = 's';
        buffer[3] = 'k';
        buffer[4] = slot + '0';
        buffer[5] = 0;

        remove(buffer);
    }
    EMSCRIPTEN_KEEPALIVE void Emulator_AttachFloppyImage(int slot)
    {
        char buffer[6];
        buffer[0] = '/';
        buffer[1] = 'd';
        buffer[2] = 's';
        buffer[3] = 'k';
        buffer[4] = slot + '0';
        buffer[5] = 0;

        g_pBoard->AttachFloppyImage(slot, buffer);
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_SystemFrame()
    {
        //printf("Emulator_SystemFrame()\n");

        g_pBoard->SetCPUBreakpoints(nullptr);

        //TODO: Keyboard
        //TODO: Mouse

        if (!g_pBoard->SystemFrame())
            return;

        // Calculate emulator uptime (25 frames per second)
        m_nUptimeFrameCount++;
        if (m_nUptimeFrameCount >= 25)
        {
            m_dwEmulatorUptime++;
            m_nUptimeFrameCount = 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE void* Emulator_PrepareScreen()
    {
        //printf("Emulator_PrepareScreen()\n");
        if (g_pFrameBuffer == 0)
        {
            printf("Emulator_PrepareScreen() null framebuffer\n");
            return g_pFrameBuffer;
        }

        Emulator_PrepareScreenRGB32(g_pFrameBuffer);

        return (void*)g_pFrameBuffer;
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_KeyEvent(uint16_t vscan, bool pressed)
    {
        if (pressed)
            printf("Emulator_KeyEvent(%03x, %d)\n", vscan, pressed);
        if (vscan == 0)
            return;

        if (pressed)
            m_KeyboardMatrix[(vscan >> 8) & 7] |= (vscan & 0xff);
        else
            m_KeyboardMatrix[(vscan >> 8) & 7] &= ~(vscan & 0xff);

        g_pBoard->UpdateKeyboardMatrix(m_KeyboardMatrix);
    }

    EMSCRIPTEN_KEEPALIVE void Emulator_LoadImage()
    {
        const char * imageFileName = "/image";

        Emulator_Stop();

        // Open file
        FILE* fpFile = ::fopen(imageFileName, "rb");
        if (fpFile == nullptr)
        {
            remove(imageFileName);
            printf("Emulator_LoadImage(): failed to open file\n");
            return;
        }

        // Read header
        uint32_t bufHeader[32 / sizeof(uint32_t)];
        uint32_t dwBytesRead = ::fread(bufHeader, 1, 32, fpFile);
        if (dwBytesRead != 32)
        {
            ::fclose(fpFile);
            remove(imageFileName);
            printf("Emulator_LoadImage(): failed to read file\n");
            return;
        }

        // Allocate memory
        int compressedSize = (int)bufHeader[5];
        void* pCompressBuffer = ::calloc(compressedSize, 1);
        if (pCompressBuffer == nullptr)
        {
            ::fclose(fpFile);
            remove(imageFileName);
            printf("Emulator_LoadImage(): calloc failed\n");
            return;
        }

        // Read image
        dwBytesRead = ::fread(pCompressBuffer, 1, compressedSize, fpFile);
        ::fclose(fpFile);
        remove(imageFileName);
        if (dwBytesRead != compressedSize)
        {
            ::free(pCompressBuffer);
            printf("Emulator_LoadImage(): failed to read file\n");
            return;
        }

        uint32_t stateSize = bufHeader[3];
        uint8_t* pImage = (uint8_t*) ::calloc(stateSize, 1);
        if (pImage == nullptr)
        {
            printf("Emulator_LoadImage(): calloc failed\n");
            return;
        }

        // Decompress the state body
        int decompressedSize = LZ4_decompress_safe(
                (const char*)pCompressBuffer, (char*)(pImage + 32), compressedSize, (int)stateSize - 32);
        if (decompressedSize <= 0)
        {
            printf("Failed to decompress the emulator state.");
            ::free(pCompressBuffer);
            ::free(pImage);
            return;
        }
        memcpy(pImage, bufHeader, 32);

        // Restore emulator state from the image
        g_pBoard->LoadFromImage(pImage);

        m_dwEmulatorUptime = bufHeader[4];

        // Free memory
        ::free(pCompressBuffer);
        ::free(pImage);
        printf("Emulator_LoadImage() done\n");
    }

#ifdef __cplusplus
}
#endif

int main()
{
    Emulator_Init();
}


uint32_t Color16Convert(uint16_t color)
{
    return (0xff000000 |
            ((uint32_t)((color & 0x0300) >> 2 | (color & 0x0007) << 3 | (color & 0x0300) >> 7)) << 16 | // R
            ((uint32_t)((color & 0xe000) >> 8 | (color & 0x00e0) >> 3 | (color & 0xC000) >> 14)) << 8 | // G
            ((uint32_t)((color & 0x1C00) >> 5 | (color & 0x0018) | (color & 0x1C00) >> 10))             // B
    );
}

#define FILL1PIXEL(color) { *plinebits++ = color; }
#define FILL2PIXELS(color) { *plinebits++ = color; *plinebits++ = color; }
#define FILL4PIXELS(color) { *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; }
#define FILL8PIXELS(color) { \
    *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; \
    *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; \
}
// Выражение для получения 16-разрядного цвета из палитры; pala = адрес старшего байта
#define GETPALETTEHILO(pala) ((uint16_t)(pBoard->GetRAMByteView(pala) << 8) | pBoard->GetRAMByteView((pala) + 256))

void Emulator_PrepareScreenRGB32(uint32_t* pImageBits)
{
    if (pImageBits == nullptr || g_pBoard == nullptr) return;

    uint32_t linebits[NEON_SCREEN_WIDTH];  // буфер под строку

    const CMotherboard* pBoard = g_pBoard;

    uint16_t vdptaslo = pBoard->GetRAMWordView(0000010);  // VDPTAS
    uint16_t vdptashi = pBoard->GetRAMWordView(0000012);  // VDPTAS
    uint16_t vdptaplo = pBoard->GetRAMWordView(0000004);  // VDPTAP
    uint16_t vdptaphi = pBoard->GetRAMWordView(0000006);  // VDPTAP

    uint32_t tasaddr = (((uint32_t)vdptaslo) << 2) | (((uint32_t)(vdptashi & 0x000f)) << 18);
    uint32_t tapaddr = (((uint32_t)vdptaplo) << 2) | (((uint32_t)(vdptaphi & 0x000f)) << 18);
    uint16_t pal0 = GETPALETTEHILO(tapaddr);
    uint32_t colorBorder = Color16Convert(pal0);  // Глобальный цвет бордюра

    for (int line = 0; line < NEON_SCREEN_HEIGHT; line++)  // Цикл по строкам 0..299
    {
        uint16_t linelo = pBoard->GetRAMWordView(tasaddr);
        uint16_t linehi = pBoard->GetRAMWordView(tasaddr + 2);
        tasaddr += 4;

        uint32_t* plinebits = linebits;
        uint32_t lineaddr = (((uint32_t)linelo) << 2) | (((uint32_t)(linehi & 0x000f)) << 18);
        bool firstOtr = true;  // Признак первого отрезка в строке
        uint32_t colorbprev = 0;  // Цвет бордюра предыдущего отрезка
        int bar = 52;  // Счётчик полосок от 52 к 0
        for (;;)  // Цикл по видеоотрезкам строки, до полного заполнения строки
        {
            uint16_t otrlo = pBoard->GetRAMWordView(lineaddr);
            uint16_t otrhi = pBoard->GetRAMWordView(lineaddr + 2);
            lineaddr += 4;
            // Получаем параметры отрезка
            int otrcount = 32 - (otrhi >> 10) & 037;  // Длина отрезка в 32-разрядных словах
            if (otrcount == 0) otrcount = 32;
            uint32_t otraddr = (((uint32_t)otrlo) << 2) | (((uint32_t)otrhi & 0x000f) << 18);
            uint16_t otrvn = (otrhi >> 6) & 3;  // VN1 VN0 - бит/точку
            bool otrpb = (otrhi & 0x8000) != 0;
            uint16_t vmode = (otrhi >> 6) & 0x0f;  // биты VD1 VD0 VN1 VN0
            // Получить адрес палитры
            uint32_t paladdr = tapaddr;
            if (otrvn == 3 && otrpb)  // Многоцветный режим
            {
                paladdr += (otrhi & 0x10) ? 1024 + 512 : 1024;
            }
            else
            {
                paladdr += (otrpb ? 512 : 0) + (otrvn * 64);
                uint32_t otrpn = (otrhi >> 4) & 3;  // PN1 PN0 - номер палитры
                paladdr += otrpn * 16;
            }
            // Бордюр
            uint16_t palbhi = pBoard->GetRAMWordView(paladdr);
            uint16_t palblo = pBoard->GetRAMWordView(paladdr + 256);
            uint32_t colorb = Color16Convert((uint16_t)((palbhi & 0xff) << 8 | (palblo & 0xff)));
            if (!firstOtr)  // Это не первый отрезок - будет бордюр, цвета по пикселям: AAAAAAAAABBCCCCC
            {
                FILL8PIXELS(colorbprev)  FILL1PIXEL(colorbprev)
                FILL2PIXELS(colorBorder)
                FILL4PIXELS(colorb)  FILL1PIXEL(colorb)
                bar--;  if (bar == 0) break;
            }
            colorbprev = colorb;  // Запоминаем цвет бордюра
            // Определяем, сколько 16-пиксельных полосок нужно заполнить
            int barcount = otrcount * 2;
            if (!firstOtr) barcount--;
            if (barcount > bar) barcount = bar;
            bar -= barcount;
            // Заполняем отрезок
            if (vmode == 0)  // VM1, плотность видео-строки 52 байта, со сдвигом влево на 2 байта
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMByteView(otraddr);
                    otraddr++;
                    for (uint16_t k = 0; k < 8; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL2PIXELS(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 1)  // VM2, плотность видео-строки 52 байта
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // читаем байт - выводим 16 пикселей
                    otraddr++;
                    uint32_t palc = paladdr + (bits & 3);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 2) & 3);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 4) & 3);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + (bits >> 6);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 2 || vmode == 6 ||
                    vmode == 3 && !otrpb ||
                    vmode == 7 && !otrpb)  // VM4, плотность видео-строки 52 байта
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // читаем байт - выводим 16 пикселей
                    otraddr++;
                    uint32_t palc = paladdr + (bits & 15);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL8PIXELS(color)
                    palc = paladdr + (bits >> 4);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL8PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 3 && otrpb ||
                    vmode == 7 && otrpb)  // VM8, плотность видео-строки 52 байта
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // читаем байт - выводим 16 пикселей
                    otraddr++;
                    uint32_t palc = paladdr + bits;
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL8PIXELS(color)
                    FILL8PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 4)  // VM1, плотность видео-строки 52 байта
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr & ~1);
                    if (otraddr & 1) bits = bits >> 8;
                    otraddr++;
                    for (uint16_t k = 0; k < 8; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL2PIXELS(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 5)  // VM2, плотность видео-строки 52 байта
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // читаем байт - выводим 16 пикселей
                    otraddr++;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else if (vmode == 8)  // VM1, плотность видео-строки 104 байта
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);
                    otraddr += 2;
                    for (uint16_t k = 0; k < 16; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL1PIXEL(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 9)
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 16 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL2PIXELS(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL2PIXELS(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL2PIXELS(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL2PIXELS(color3)
                    uint32_t palc4 = (paladdr + 12 + ((bits >> 8) & 3));
                    uint16_t c4 = GETPALETTEHILO(palc4);
                    uint32_t color4 = Color16Convert(c4);
                    FILL2PIXELS(color4)
                    uint32_t palc5 = (paladdr + 12 + ((bits >> 10) & 3));
                    uint16_t c5 = GETPALETTEHILO(palc5);
                    uint32_t color5 = Color16Convert(c5);
                    FILL2PIXELS(color5)
                    uint32_t palc6 = (paladdr + 12 + ((bits >> 12) & 3));
                    uint16_t c6 = GETPALETTEHILO(palc6);
                    uint32_t color6 = Color16Convert(c6);
                    FILL2PIXELS(color6)
                    uint32_t palc7 = (paladdr + 12 + ((bits >> 14) & 3));
                    uint16_t c7 = GETPALETTEHILO(palc7);
                    uint32_t color7 = Color16Convert(c7);
                    FILL2PIXELS(color7)
                    barcount--;
                }
            }
            else if (vmode == 10)  // VM4, плотность видео-строки 104 байта
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 16 пикселей
                    otraddr += 2;
                    uint32_t palc = paladdr + (bits & 15);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 4) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 8) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 12) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 11 && !otrpb)  // VM41, плотность видео-строки 104 байта
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 16 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint32_t palc2 = (paladdr + ((bits >> 8) & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits >> 12) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else if (vmode == 11 && otrpb)  // VM8, плотность видео-строки 104 байта
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 16 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 0xff));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL8PIXELS(color0)
                    uint32_t palc1 = (paladdr + (bits >> 8));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL8PIXELS(color1)
                    barcount--;
                }
            }
            else if (vmode == 13)  // VM2, плотность видео-строки 208 байт
            {
                for (int j = 0; j < barcount * 2; j++)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 8 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL1PIXEL(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL1PIXEL(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL1PIXEL(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL1PIXEL(color3)
                    uint32_t palc4 = (paladdr + 12 + ((bits >> 8) & 3));
                    uint16_t c4 = GETPALETTEHILO(palc4);
                    uint32_t color4 = Color16Convert(c4);
                    FILL1PIXEL(color4)
                    uint32_t palc5 = (paladdr + 12 + ((bits >> 10) & 3));
                    uint16_t c5 = GETPALETTEHILO(palc5);
                    uint32_t color5 = Color16Convert(c5);
                    FILL1PIXEL(color5)
                    uint32_t palc6 = (paladdr + 12 + ((bits >> 12) & 3));
                    uint16_t c6 = GETPALETTEHILO(palc6);
                    uint32_t color6 = Color16Convert(c6);
                    FILL1PIXEL(color6)
                    uint32_t palc7 = (paladdr + 12 + ((bits >> 14) & 3));
                    uint16_t c7 = GETPALETTEHILO(palc7);
                    uint32_t color7 = Color16Convert(c7);
                    FILL1PIXEL(color7)
                }
            }
            else if ((vmode == 14) ||  // VM4, плотность видео-строки 208 байт
                    vmode == 15 && !otrpb)  // VM41, плотность видео-строки 208 байт
            {
                for (int j = 0; j < barcount * 2; j++)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 8 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL2PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL2PIXELS(color1)
                    uint32_t palc2 = (paladdr + ((bits >> 8) & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL2PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits >> 12) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL2PIXELS(color3)
                }
            }
            else if (vmode == 15 && otrpb)
            {
                while (barcount > 0)
                {
                    uint16_t bits0 = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 8 пикселей
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits0 & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits0 >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint16_t bits1 = pBoard->GetRAMWordView(otraddr);  // читаем слово - выводим 8 пикселей
                    otraddr += 2;
                    uint32_t palc2 = (paladdr + (bits1 & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits1 >> 4) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else //if (vmode == 12)  // VM1, плотность видео-строки 208 байт - запрещенный режим
            {
                while (barcount > 0)
                {
                    FILL8PIXELS(colorBorder)
                    FILL8PIXELS(colorBorder)
                    barcount--;
                }
            }

            if (bar <= 0) break;
            firstOtr = false;
        }

        uint32_t* pBits = pImageBits + line * NEON_SCREEN_WIDTH;
        memcpy(pBits, linebits, sizeof(uint32_t) * NEON_SCREEN_WIDTH);
    }
}


//////////////////////////////////////////////////////////////////////
