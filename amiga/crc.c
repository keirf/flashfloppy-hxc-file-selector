/*************************************************************************
 * Example Table Driven CRC16 Routine using 4-bit message chunks
 *
 * By Ashley Roll
 * Digital Nemesis Pty Ltd
 * www.digitalnemesis.com
 *
 * The following is an example of implementing a restricted size CRC16
 * table lookup. No optimisation as been done so the code is clear and
 * easy to understand.
 *
 * Test Vector: "123456789" (character string, no quotes)
 * Generated CRC: 0x29B1
 *
 *************************************************************************/

#include "crc.h"

/* CRC16 Lookup tables (High and Low Byte) for 4 bits per iteration. */
static const uint16_t CRC16_LookupHigh[16] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
        0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1
};
static const uint16_t CRC16_LookupLow[16] = {
        0x00, 0x21, 0x42, 0x63, 0x84, 0xA5, 0xC6, 0xE7,
        0x08, 0x29, 0x4A, 0x6B, 0x8C, 0xAD, 0xCE, 0xEF
};

/* Before each message CRC is generated, the CRC register must be initialised 
 * by calling this function. */
void CRC16_Init(uint8_t *CRC16_High, uint8_t *CRC16_Low)
{
    /* Initialise the CRC to 0xFFFF for the CCITT specification */
    *CRC16_High = *CRC16_Low = 0xFF;
}

/* Process 4 bits of the message to update the CRC Value.
 * Note that the data must be in the low nibble of val. */
void CRC16_Update4Bits(uint8_t *CRC16_High, uint8_t *CRC16_Low, uint8_t val)
{
    uint8_t t;

    /* Extract the Most significant 4 bits of the CRC register. */
    t = *CRC16_High >> 4;

    /* XOR in the Message Data into the extracted bits. */
    t = t ^ val;

    /* Shift the CRC Register left 4 bits. */
    *CRC16_High = (*CRC16_High << 4) | (*CRC16_Low >> 4);
    *CRC16_Low = *CRC16_Low << 4;

    /* Do the table lookups and XOR the result into the CRC Tables. */
    *CRC16_High = *CRC16_High ^ CRC16_LookupHigh[t];
    *CRC16_Low = *CRC16_Low ^ CRC16_LookupLow[t];
}

/* Process one Message Byte to update the current CRC Value. */
void CRC16_Update(uint8_t *CRC16_High, uint8_t *CRC16_Low, uint8_t val)
{
    /* High nibble first, then low. */
    CRC16_Update4Bits(CRC16_High,CRC16_Low, (uint8_t)(val >> 4) );
    CRC16_Update4Bits(CRC16_High,CRC16_Low, (uint8_t)(val & 0x0F) );
}
