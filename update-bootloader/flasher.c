/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, version 
 *  2 of the License.
 *
 *  Copyright 2007 Spiro Trikaliotis
 *
 */

/*! ************************************************************** 
** \file flasher.c \n
** \author Spiro Trikaliotis \n
** \version $Id: flasher.c,v 1.2 2007/07/22 15:33:29 strik Exp $ \n
** \n
** \brief Flash the bootloader from the application space
**
****************************************************************/


#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <avr/boot.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include <util/delay.h>

#include "../bootloader/xu1541bios.h"

#define STATIC static

extern void spm_copy(uint8_t what, uint16_t address, uint16_t data);
extern void spm_end(void);

STATIC
uint16_t OwnSpm = 0;

static
uint8_t data[SPM_PAGESIZE];

STATIC
void
delay_ms(unsigned int ms) {
        unsigned int i;
        for (i = 0; i < ms; i++)
                _delay_ms(1);
}

xu1541_bios_data_t bios_data;

STATIC
void
start_bootloader(void) {
        ((start_flash_bootloader_t) pgm_read_word_near(&bios_data.start_flash_bootloader))();
}

STATIC
void
spm(uint8_t what, uint16_t address, uint16_t data) {
        if (OwnSpm) {
                ((spm_t) OwnSpm)(what, address, data);
        } else {
                ((spm_t) pgm_read_word_near(&bios_data.spm))(what, address, data);
        }
}

#undef boot_page_erase
STATIC
void
boot_page_erase(uint16_t address) 
{
        spm(__BOOT_PAGE_ERASE, address, 0);
}

#undef boot_page_fill
STATIC
void
boot_page_fill(uint16_t address, uint16_t data) 
{
        spm(__BOOT_PAGE_FILL, address, data);
}

#undef boot_page_write
STATIC
void
boot_page_write(uint16_t address) 
{
        spm(__BOOT_PAGE_WRITE, address, 0);
}

STATIC
void
blink(unsigned long count)
{
/**/
        DDRD  |=  _BV(1);
        PORTD &= ~_BV(1);

        count *= 2;

        while (--count) {
                delay_ms(300);
                PORTD ^= _BV(1);
        }

        delay_ms(1000);
/**/
}

STATIC
void
boot_program_page(uint16_t page, uint8_t *buf)
{
        uint16_t i;

        eeprom_busy_wait();
        boot_spm_busy_wait();      // Wait until the memory is erased.

        boot_page_erase(page);
        boot_spm_busy_wait();      // Wait until the memory is erased.

        for (i = 0; i < SPM_PAGESIZE; i += 2)
        {
                // Set up little-endian word.

                uint16_t w = *buf++;
                w += (*buf++) << 8;

                boot_page_fill(page + i, w);
        }

        boot_page_write(page);     // Store buffer in flash page.
}

STATIC
void
boot_read_page(uint16_t page, uint8_t *buf)
{
        uint8_t i;

        for (i = 0; i < SPM_PAGESIZE; i++) {
                *buf++ = pgm_read_byte_near(page + i);
        }
}

STATIC
void
program_copy(uint16_t to, uint16_t from, uint16_t length)
{
        do {
                boot_read_page(from, data);
                boot_program_page(to, data);

                if (length > SPM_PAGESIZE)
                        length -= SPM_PAGESIZE;
                else
                        length = 0;

                to     += SPM_PAGESIZE;
                from   += SPM_PAGESIZE;

        } while (length != 0);
}

STATIC
void
program_spm(void)
{
        uint16_t addressOwnSpm = 0x1840;

        // \TODO: determine if the SPM implementation is already there. In this case,
        // use it, as the "original" SPM might have been already overwritten!

        program_copy(addressOwnSpm, ((uint16_t) &spm_copy) << 1, sizeof(spm_copy));

        OwnSpm = addressOwnSpm >> 1;
}

int
main(void)
{
        cli();

        blink(1);

        /*
         * first of all, make sure we all called in case there is some interruption (i.e., power failure)
         */

        boot_read_page(0x1800, data);
        
        data[0] = 0xff; /* replace the RESET with a RJMP $0 */
        data[1] = 0xc3;

        boot_program_page(0x1800, data);

        /*
         * Now, flash my own SPM command into the bootloader area
         */

        program_spm();

        // flash 0x0880-0x0fff to 0x1880 - 0x1fff

        program_copy(0x1880, 0x0880, 0x2000-0x1880);

        // flash 0x0700-0x07ff to 0x1700 - 0x17ff

        program_copy(0x1700, 0x0700, 0x100);


        // use the newly flashed SPM in the bootloader
 
        OwnSpm = 0;

        // flash 0x0840-0x093f to 0x1840 - 0x193f

        program_copy(0x1840, 0x0840, 0x100);

        // flash 0x0800-0x083f to 0x1800 - 0x183f
        // THIS HAS TO BE THE LAST FLASH, as this restores the RESET vector to the bootloader!

        program_copy(0x1800, 0x0800, 0x40);

        blink(3);

        /* make sure we are not called anymore */
        boot_page_erase(0);

        start_bootloader();

        return 0;
}
