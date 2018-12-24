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
#define FTDI_READ_BUFFER_SIZE       2048
#define FTDI_READ_COMMAND_SIZE      5



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
    uint8 inBuffer[FTDI_READ_BUFFER_SIZE + FTDI_READ_COMMAND_SIZE]  = {0};
    uint8 outBuffer[FTDI_READ_BUFFER_SIZE + FTDI_READ_COMMAND_SIZE] = {0};
    uint32 sizeToTransfer = 0;
    uint32 sizeTransferred = 0;
    uint32 flashSize = 0;
    uint32 size = 0;
    uint32 offset = 0;
    uint32 freq = 0;

    /*-----------------------------------------------------------------------------------------------------*/
    /* Parse input parameters                                                                              */
    /*-----------------------------------------------------------------------------------------------------*/
    if (argc < 2)
    {
        printf("Usage: flush_dump.exe SPI_FREQ [OFFSET] [SIZE]\n");
        printf("\tSPI_FREQ      - SPI frequency in Hz\n");
        printf("\tOFFSET        - (Optional) Offset inside flash (default=0)\n");
        printf("\tSIZE          - (Optional) Dump size (default=Flash Size\n");
        return 0;
    }

    if (argc >= 2)
    {
        channelConf.ClockRate = atoi(argv[1]);
    }
    if (argc >= 3)
    {
        offset = atoi(argv[2]);
    }
    if (argc >= 4)
    {
        size = atoi(argv[3]) + offset;
    }

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
    outBuffer[0] = 0x9F;
    outBuffer[1] = 0x00;
    outBuffer[2] = 0x00;
    outBuffer[3] = 0x00;
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
    std::string name = std::string("dump.bin");
    FILE* f;
    if (fopen_s(&f, name.c_str(), "w"))
    {
        printf("Couldn't open file for Flash dumping\n");
        return 1;
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
        uint32 transferSize = min(FTDI_READ_BUFFER_SIZE, size - i);
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
        fwrite(&inBuffer[FTDI_READ_COMMAND_SIZE], 1, transferSize, f);
        printf("\r%3.2f%%", ((double)i / flashSize) * 100);
        i += transferSize;
    }
    printf("\r100.00%%\n");

    return 0;
}

