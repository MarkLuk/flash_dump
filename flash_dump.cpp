/*---------------------------------------------------------------------------------------------------------*/
/*   flash_dump.cpp                                                                                        */
/*            This file contains flash_dump code                                                           */
/*  Project:                                                                                               */
/*            flash_dump                                                                                   */
/*---------------------------------------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
/*                                                INCLUDES                                                 */
/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
#include <windows.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <stdio.h>
#include <time.h>

#include "ftd2xx.h"
#include "libMPSSE_spi.h"

/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
/*                                                 DEFINES                                                 */
/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
#define SPI_DEVICE_BUFFER_SIZE		256
#define FTDI_READ_COMMAND_SIZE      5

/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
/*                                                 MACROS                                                  */
/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/

#define FILE_OPEN(f, name)                                                                                  \
{                                                                                                           \
    if (fopen_s(&(f), (name).c_str(), "wb"))                                                                \
    {                                                                                                       \
        printf("File open error\n");                                                                        \
        return 1;                                                                                           \
    }                                                                                                       \
}

#define FILE_WRITE(f, buf, size)                                                                            \
{                                                                                                           \
    if (fwrite(buf, 1, size, f) != size)                                                                    \
    {                                                                                                       \
        printf("Write error\n");                                                                            \
        return 1;                                                                                           \
    }                                                                                                       \
}


/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
/*                                             LOCAL FUNCTIONS                                             */
/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/

bool is_empty_line(uint8* buf, uint32 size)
{
    for (uint32 i = 0; i < size; ++i)
    {
        if (buf[i] != 0xFF)
        {
            return false;
        }
    }

    return true;
}

std::string int_to_hexstring(uint32 num)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(sizeof(num) * 2) << std::hex << num;
    return std::string(stream.str());
}

/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
/*                                            MAIN ENTRY POINT                                             */
/*---------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    /*-----------------------------------------------------------------------------------------------------*/
    /* Variables                                                                                           */
    /*-----------------------------------------------------------------------------------------------------*/
    FT_HANDLE ftHandle;
    uint8 buffer[SPI_DEVICE_BUFFER_SIZE] = { 0 };
    FT_STATUS status = FT_OK;
    FT_DEVICE_LIST_INFO_NODE devList = { 0 };
    ChannelConfig channelConf = { 0 };
    uint8 address = 0;
    uint32 channels = 0;
    uint16 data = 0;
    uint32 i = 0;
    uint8* inBuffer;
    uint8* outBuffer;
    uint32 sizeToTransfer = 0;
    uint32 sizeTransferred = 0;
    uint32 flashSize = 0;
    uint32 size = 0;
    uint32 offset = 0;
    uint32 freq = 0;
    uint32 read_page_size = 4096;
    uint32 skip_empty = 0;
    uint32 data_written = 0;
    uint32 data_skipped = 1;
    FILE* f = NULL;

    /*-----------------------------------------------------------------------------------------------------*/
    /* Parse input parameters                                                                              */
    /*-----------------------------------------------------------------------------------------------------*/
    if (argc < 2)
    {
        printf("Usage: flush_dump.exe SPI_FREQ [SPLIT] [OFFSET] [SIZE] [READ_PAGE]\n");
        printf("\tSPI_FREQ      - SPI frequency in Hz\n");
        printf("\tSPLIT         - (Optional) if 1, FF pages are ignored (default=0\n)");
        printf("\tOFFSET        - (Optional) Offset inside flash (default=0)\n");
        printf("\tSIZE          - (Optional) Dump size (default=Flash Size\n");
        printf("\tREAD_PAGE     - (Optional) Read page size (default 4096)\n");
        return 0;
    }
   
    if (argc >= 2)
    {
        channelConf.ClockRate = atoi(argv[1]);
    }
    if (argc >= 3)
    {
        skip_empty = atoi(argv[2]);
    }
    if (argc >= 4)
    {
        offset = atoi(argv[3]);
    }
    if (argc >= 5)
    {
        size = atoi(argv[4]) + offset;
    }
    if (argc >= 6)
    {
        read_page_size = atoi(argv[5]);
    }

    // Allocate buffers
    inBuffer = (uint8*)malloc(read_page_size + FTDI_READ_COMMAND_SIZE);
    outBuffer = (uint8*)malloc(read_page_size + FTDI_READ_COMMAND_SIZE);

    /*-----------------------------------------------------------------------------------------------------*/
    /* Perform SPI channel configuraiton                                                                   */
    /*-----------------------------------------------------------------------------------------------------*/
    channelConf.LatencyTimer = 1;
    channelConf.configOptions = SPI_CONFIG_OPTION_MODE0 | SPI_CONFIG_OPTION_CS_DBUS3 | SPI_CONFIG_OPTION_CS_ACTIVELOW;
    channelConf.Pin = 0xFFFFFFFF;

    /*-----------------------------------------------------------------------------------------------------*/
    /* Init SPI library                                                                                    */
    /*-----------------------------------------------------------------------------------------------------*/
    Init_libMPSSE();
    status = SPI_GetNumChannels(&channels);
    printf("Number of available SPI channels = %d\n", (int)channels);

    if (channels == 0)
    {
        printf("SPI communication failed\n");
        return 1;
    }

    /*-----------------------------------------------------------------------------------------------------*/
    /* Open the first available channel                                                                    */
    /*-----------------------------------------------------------------------------------------------------*/
    status = SPI_OpenChannel(0, &ftHandle);
    status = SPI_InitChannel(ftHandle, &channelConf);

    /*-----------------------------------------------------------------------------------------------------*/
    /* Perform "Get JEDEC ID" command                                                                      */
    /*-----------------------------------------------------------------------------------------------------*/
    outBuffer[0] = 0x9F;        // Command
    outBuffer[1] = 0x00;        // 24bit address
    outBuffer[2] = 0x00;        //
    outBuffer[3] = 0x00;        //
    sizeToTransfer = 4;
    status = SPI_ReadWrite(ftHandle, inBuffer, outBuffer, sizeToTransfer, &sizeTransferred, SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES |
                                                                                            SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE |
                                                                                            SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);
    flashSize = 1UL << inBuffer[3];
    printf("Manifacture ID 0x%02X\n", inBuffer[1]);
    printf("Capacity       %ldMB\n", flashSize / 1024 / 1024);
    if (size == 0)
    {
        size = flashSize;
    }

    /*-----------------------------------------------------------------------------------------------------*/
    /* Error checking                                                                                      */
    /*-----------------------------------------------------------------------------------------------------*/
    if (size > flashSize)
    {
        printf("Offset+Size are too big\n");
        return 1;
    }

    /*-----------------------------------------------------------------------------------------------------*/
    /* Open file for binary dump                                                                           */
    /*-----------------------------------------------------------------------------------------------------*/
    if (!skip_empty)
    {
        FILE_OPEN(f, std::string("dump.bin"));
    }

    /*-----------------------------------------------------------------------------------------------------*/
    /* Perform flash dump                                                                                  */
    /*-----------------------------------------------------------------------------------------------------*/
    printf("Dumping flash...\n");
    i = offset;
    while (i < size)
    {
        /*-------------------------------------------------------------------------------------------------*/
        /* Build fast read command                                                                         */
        /*-------------------------------------------------------------------------------------------------*/
        outBuffer[0] = 0x0B;                // Command
        outBuffer[1] = (i >> 16) & 0xFF;    // 24bit address
        outBuffer[2] = (i >> 8) & 0xFF;
        outBuffer[3] = (i     ) & 0xFF;
        outBuffer[4] = 0;                   // Dummy clocks

        /*-------------------------------------------------------------------------------------------------*/
        /* Perform SPI transfer                                                                            */
        /*-------------------------------------------------------------------------------------------------*/
        uint32 transferSize = min(read_page_size, size - i);
        sizeToTransfer = FTDI_READ_COMMAND_SIZE + transferSize;
        sizeTransferred = 0;
        status = SPI_ReadWrite(ftHandle, inBuffer, outBuffer, sizeToTransfer, &sizeTransferred, SPI_TRANSFER_OPTIONS_SIZE_IN_BYTES |
                                                                                                SPI_TRANSFER_OPTIONS_CHIPSELECT_ENABLE |
                                                                                                SPI_TRANSFER_OPTIONS_CHIPSELECT_DISABLE);
        if (sizeTransferred != sizeToTransfer)
        {
            printf("Error occurred\n");
            exit(1);
        }

        /*-------------------------------------------------------------------------------------------------*/
        /* Write to file                                                                                   */
        /*-------------------------------------------------------------------------------------------------*/
        if (skip_empty)
        {
            /*---------------------------------------------------------------------------------------------*/
            /* If current page is empty, we close the last file (assuming it has data in-it)               */
            /*---------------------------------------------------------------------------------------------*/
            if (is_empty_line(&inBuffer[FTDI_READ_COMMAND_SIZE], transferSize))
            {
                if (data_written && !data_skipped)
                {
                    fclose(f);
                    data_skipped = 1;
                }
            }
            /*---------------------------------------------------------------------------------------------*/
            /* If current page is not empty, and we previously skipped data - open new file                */
            /*---------------------------------------------------------------------------------------------*/
            else
            {
                /*-----------------------------------------------------------------------------------------*/
                /* Open new file if needed                                                                 */
                /*-----------------------------------------------------------------------------------------*/
                if (data_skipped)
                {
                    std::string name = std::string("dump") + int_to_hexstring(i) + std::string(".bin");
                    FILE_OPEN(f, name);
                }

                /*-----------------------------------------------------------------------------------------*/
                /* Write data                                                                              */
                /*-----------------------------------------------------------------------------------------*/
                FILE_WRITE(f, &inBuffer[FTDI_READ_COMMAND_SIZE], transferSize);
                data_skipped = 0;
                data_written = 1;
            }
        }
        else
        {
            /*---------------------------------------------------------------------------------------------*/
            /* Skipping is not enabled, just write to file                                                 */
            /*---------------------------------------------------------------------------------------------*/
            FILE_WRITE(f, &inBuffer[FTDI_READ_COMMAND_SIZE], transferSize);
        }

        /*-------------------------------------------------------------------------------------------------*/
        /* Update percentage                                                                               */
        /*-------------------------------------------------------------------------------------------------*/
        printf("\r%3.2f%%", ((double)i / flashSize) * 100);
        i += transferSize;
    }
    printf("\r100.00%%\n");

    return 0;
}

