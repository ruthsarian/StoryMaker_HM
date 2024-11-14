# About
This project is an attempt at communicating with [Hallmark Keepsake Disney The Haunted Mansion Collection](https://care.hallmark.com/s/article/Keepsake-Ornament-K) ornaments released starting in 2023. This project uses an [NRF24L01 module](https://www.seeedstudio.com/blog/2019/11/21/nrf24l01-getting-started-arduino-guide/) along with an [Arduino Nano](https://store-usa.arduino.cc/products/arduino-nano?selectedStore=us) to communicate with the ornaments.

## Beware!
This is a work in progress. The code and information is presented as-is without warranty or guarantee that it'll work. I'm simply sharing what I've developed, what I have found works for me, and hopefully it will work for you too. 

# Code Operation
This program has two operating modes: story teller and story maker. It begins in story teller mode.

## Story Teller Mode
Story Teller mode hops between the three RF channels used by ornaments listening for incoming data from ornaments. Any valid data packets it receives it outputs to serial. This lets us capture RF comms from ornaments and allows us to study the protocol and reverse engineer the process. A button press in story teller mode will put the program into story maker mode.

## Story Maker Mode
Story Maker mode is designed to send out RF commands to activate ornaments. With a short button press it will send out whatever commands I'm currently testing in the code. This is where I am currently doing my testing/debugging for identifying commands and command parameters. With a long button press (over 1 second) the program will initiate a full haunted mansion show where all listening ornaments participate. A very long button press (over 5 seconds) will switch the program back to Story Teller mode.

## Activating Ornaments
Currently I know of only one command to activate ornaments and it is the command sent out by the mansion to begin a full show. This command includes a value that indicates which ornaments will be included in the show. This value is determined by listening for acknowledgements from ornaments between announcing the intent to start a full show and the start of the show. This value includes a marker for the mansion itself. If that marker is not included, all powered and listening ornaments will immediately activate, otherwise all ornaments wait for their turn in the show. 

When the show starts, the mansion issues a series of commands that count down to the start of the show. This provides ornaments with the opportunity to sync-up their timing for the start of the show.

The timing of events for the full show are hard-coded into the ornaments. The mansion does **NOT** direct when an ornament should activate. Knowing which ornaments are included in the show, each ornament is able to calculate its own timings on when to blink in sync with the show and to start their own portion of the show.

# Wiring
TODO: Provide explanation on how to connect the Arduino Nano, nRF24L01 module, and button together for this to all work. For now look you'll have to go by the source code which identifies which pins on the Arduino connect to what.

# Technical Details

## Hardware
Ornaments use an [RTX7310](https://fcc.report/FCC-ID/SQ9RTX7310/5689705.pdf) 2.4GHz module to communicate with each other. This module appears to use a protocol similar to [Nordic Semiconductor's ShockBurst](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/intro-to-shockburstenhanced-shockburst). As such it's possible to communicate with the ornaments using a Nordic Semiconductor [NRF24L01-based module](https://www.seeedstudio.com/blog/2019/11/21/nrf24l01-getting-started-arduino-guide/). You'll also need something to manage the NRF24L01 module. This code relies on an [Arduino Nano](https://store-usa.arduino.cc/products/arduino-nano?selectedStore=us) for this task, but in theory any [Arduino IDE](https://docs.arduino.cc/software/ide/) supported microcontroller should be able to do this.

## Ornament Operation
Ornaments communicate on one of 3 radio channels. When an ornament first powers up it announces its presence on all three radio channels. This announcement packet includes the new radio channel any listening ornaments should switch to as well as a group ID that all ornaments should include with future data packets. Data is transmitted in 8-byte packets. 

When an ornament button is pressed, that ornament will start sending out packets that include a countdown in order to sync up the other ornaments. This start can be picked up by the mansion itself, interrupted, and then the mansion begins sending out its own commands to start a full show.

## Data Packet
|  Byte | Purpose / Value |
| ----: | :-------------- |
| `01` | command |
| `02` | command parameter |
| `03` | countdown |
| `04` | `0x00` |
| `05` | group id, byte 1 |
| `06` | group id, byte 2 |
| `07` | `0x0a` |
| `08` | `0x00` |

## Commands
| Command Byte | Purpose |
| -----------: | :------ |
| `00` | *unused* |
| `01` | playback interrupt |
| `02` | mansion/timed playback |
| `03` | acknowledge playback |
| `04` | *unused* |
| `05` | *unused* |
| `06` | playback countdown |
| `07` | *unused* |
| `08` | *unused* |
| `09` | *unused* |
| `0a` | *unused* |
| `0b` | ornament presence declaration, new group channel 0x1c |
| `0c` | ornament presence declaration, new group channel 0x31 |
| `0d` | ornament presence declaration, new group channel 0x44 |
| `0e` | *unused* |
| `0f` | ornament declaration on group channel |r

## Ornament IDs
|    ID | Ornament |
| ----: | :------- |
| `00` | ??? |
| `01` | Mansion |
| `02` | Coffin |
| `03` | Organ |
| `04` | Leota |
| `05` | Bride |
| `06` | ??? |
| `07` | ??? |

## Playback Pattern Value
When the mansion is present and an ornament button is pressed, a playback pattern value is included in the timed playback command. This playback pattern is a value that identifies all the ornaments that will participate in the playback. This value is generated by setting a specific bit in the byte value to 1 for each ornament that will participate. The bit for a given ornament is identified by taking the value 1 and bit-shifting it left the value of the ornament's ID. For example the mansion has an ID of 1 which, when bit-shifted left, becomes 2 or `0000 0010` in binary. A full participation playback would have a binary value of `1111 1110`. 

# How Was This Information Obtained?

## FCC Documentation
Wireless communication devices need to be registered with the FCC and the FCC ID must be clearly marked on the device. In this case the FCC ID `SQ9RTX7310` was printed on the wireless module inside the ornament I opened. I found available FCC documentation for this ID online here: https://fcc.report/FCC-ID/SQ9RTX7310/. 

The [user manual](https://fcc.report/FCC-ID/SQ9RTX7310/5689705.pdf) mentions it's a 2.4GHz module that uses SPI for communications. It also contains a block diagram of the chip. 

Looking at [other devices registered under the same company](https://fccid.io/SQ9) reveals another 2.4GHz module labeled [DKL1608](https://fccid.io/SQ9DKL1608). Its [user manual](https://fccid.io/SQ9DKL1608/User-Manual/User-Manual-pdf-3301004.pdf) was much more forthcoming about the chip in question and included SPI communication protocol details and register names. The [parts list](https://fccid.io/SQ9DKL1608/Parts-List/Tune-Up-Info/Parts-list-pdf-3301002.pdf) seems to identify the IC itself as `SSV7241`. Operating on the assumption this was the same or a similar to chip what was in the RTX7310 I searched for information in the `SSV7241` as well some of the register names listed in the user manual. 

This revealed a relationship between the `SSV7241` and the `NRF24L01` both with matching register names and that the nRF24L01 module was being used to emulate the `SSV7241` according to [2.4GHz protocol emulator project](https://github.com/pascallanger/DIY-Multiprotocol-TX-Module/blob/master/Protocols_Details.md). The block diagram in [the datasheet for the nRF24L01](https://www.sparkfun.com/datasheets/Components/SMD/nRF24L01Pluss_Preliminary_Product_Specification_v1_0.pdf) was curiously similar to the block diagram from the RTX7310.

## Logic Analyzer
Using a [cheap USB-C mini logic analyzer](https://www.ebay.com/itm/145184521106) and [PulseView](https://sigrok.org/wiki/PulseView) I was able to capture the SPI communications between the main microcontroller of the ornament and the RTX7310 module. This revealed commands being issued to the module that matched with the SPI commands from the nRF and SSV modules. However commands and values used to initialize the module at startup were different from the documentation for those other ICs, such as writing to register `0x1F` which is not documented in the nRF chip. Register 0x1F is documented in the SSV chip documentation, however it shows it as a 1-byte register and the startup command is writing 5 bytes to this register. Indicating the RTX7310 is neither an nRF nor an SSV chip. 

Searching for the 5 bytes of data being written to register `0x1F` returned results showing this as part of [the initialization routine for the 2.4GHz chip XN297](https://www.cnblogs.com/qianmn/articles/17720027.html). The [datasheet for the XN297 from PanChip](https://www.panchip.com/static/upload/file/20190916/1568621331607821.pdf) shows that register `0x1F` is 5 bytes wide. It also shows that the chip supports 3-pin SPI operations, which is what the ornament uses as it has only a single data line present instead of separate data in and data out lines used for 4-pin SPI. The final piece of information which confirms the chip in the ornament is an XN297 is that the datasheet includes the exact same block diagram as the one in the FCC documentation for the RTX7310. 

Comparing the captured data from the logic analyzer with the datasheet for the XN297 it's possible to identify how the chip is being initialized and the radio channels and addresses used to transmit and receive data. This revealed that the XN297 was being initialized with data scrambling enabled. 

Luckily this scrambling routine has already been reversed by others. Several projects on github provide code to scramble and descramble data for XN297 transceivers such as [this Deviation firmware for RC transmitters](https://github.com/DeviationTX/deviation/tree/master).

## Listening
At this point I was able to program an Arduino Nano to use an nRF24L01 module to listen in on the radio channels and addresses used by the ornaments and output the descrambled data. After collecting and comparing a lot of this data I started working out, as best I could, the protocol for the ornaments. Further testing with a simple transmitter helped refine and prove the protocol information was correct.

# References
- https://lastminuteengineers.com/nrf24l01-arduino-wireless-communication/
- https://nrf24.github.io/RF24/
- https://github.com/DeviationTX/deviation/blob/master/src/protocol/spi/nrf24l01.c
- https://github.com/martinbudden/NRF24_RX/blob/master/src/xn297.cpp
