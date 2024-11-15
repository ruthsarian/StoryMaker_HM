/* Story Maker HM v0.1 : ruthsarian@gmail.com
 * --
 * A program for toying with Hallmark Storyteller Haunted Mansion ornaments
 * released in 2023 and 2024.
 *
 * This code is presented as-is without warranty. 
 *
 * The XN297 is used in these ornaments. We can use an NRF24L01 module to communicate with 
 * the XN297 in the ornaments. To do this we need to emulate the XN297's behavior:
 *  - 3-byte preamble { 0x55, 0x0f, 0x71 }
 *  - scrambled packet data
 *  - 2-byte CRC
 * 
 * To emulate the preamble we have to set TX address to 3-byte width and set preamble as the address. 
 * The actual address will be included in the packet data. We also have to emulate the "encryption" 
 * scheme (referred to as scrambling here) used by the XN297.
 *
 * When receiving using the NRF24L01, 
 *  - set address width to 5 bytes
 *  - scramble the 5-byte address 
 *  - listen on the scrambled address
 *
 * When transmitting using the NRF24L01
 *  - set address width to 3 bytes
 *  - set TX address to { 0x71, 0x0f, 0x55 } // (LSB first)
 *  - construct a packet of the following format
 *    - 5-byte address
 *    - data
 *    - 2-byte CRC
 *  - max packet size is 32 bytes
 *  - thus max data size is 25 bytes
 *
 * The XN297 packets include a 2-byte CRC value which, when receiving, we can choose to ignore.
 * When transmitting we must calculate the CRC and include it in the packet.
 *
 * References
 *   https://lastminuteengineers.com/nrf24l01-arduino-wireless-communication/
 *   https://nrf24.github.io/RF24/
 *   https://github.com/DeviationTX/deviation/blob/master/src/protocol/spi/nrf24l01.c
 *   https://github.com/martinbudden/NRF24_RX/blob/master/src/xn297.cpp
 */

#define ENABLE_SSD1306    // if defined, support for the OLED display is enabled

// nRF2401 Module Support
#include <RF24.h>
#include <RF24_config.h>
#include <nRF24L01.h>
#include <printf.h>

// SSD1306 OLED Display Support
#ifdef ENABLE_SSD1306
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>

  // SSD1306 OLED Display Setup
  // default pins are SDA and SCL for the device you're using
  // for a Nano, SDA = A4, SCL = A5
  #define SCREEN_WIDTH    128
  #define SCREEN_HEIGHT   32
  #define OLED_RESET      -1    // Reset pin # (or -1 if sharing Arduino reset pin)
  #define SCREEN_ADDRESS  0x3C  // 0x3D for 128x64, 0x3C for 128x32

  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

// nRF2401 Module Setup
#define SCK_PIN 13
#define SDO_PIN 12
#define SDI_PIN 11
#define CS_PIN  10
#define CE_PIN  9
#define CSN_PIN 8
#define BTN_PIN 6

// radio properties
#define RADIO_PKT_MAX_LEN 32                  // max length for a packet
#define RADIO_PD_SIZE     8                   // payload (data) size; does not include CRC or Address as part of this
#define RADIO_RX_AW       5                   // address width when receiving
#define RADIO_TX_AW       3                   // address width when transmitting
#define RADIO_DR          RF24_1MBPS          // RF24_1MBPS, RF24_2MBPS, RF24_250KBPS 
#define RADIO_RTD         0
#define RADIO_RTC         0
#define RADIO_CRC_LEN     RF24_CRC_DISABLED   // RF24_CRC_DISABLED, RF24_CRC_8, RF24_CRC_16 
#define CRC_POLYNOMIAL    0x1021
#define CRC_INITIAL       0xb5d2

// button press type -- todo: enum this
#define NO_BTN_PRESS          0
#define SHORT_BTN_PRESS       1
#define LONG_BTN_PRESS        2
#define REALLY_LONG_BTN_PRESS 3

// misc
#define PRINT_BUF_SIZE  16

// machine state
typedef enum {
  UNINITIALIZED,
  STORYTELLER,
  STORYMAKER
} machine_state_t;
machine_state_t machine_state = STORYTELLER;

// radio variables
RF24 radio(CE_PIN, CSN_PIN);

const uint8_t rx_address[RADIO_RX_AW]   = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };  // address to send/receive data
const uint8_t tx_address[RADIO_TX_AW]   = { 0x55, 0x0F, 0x71 };              // XN297 preamble; used as TX address to emulate the preamble
const uint8_t channel[3]                = { 0x1C, 0x31, 0x44 };              // 28, 49, 68

      uint8_t xmit_packet[RADIO_PD_SIZE]    = { 0 };
      uint8_t recv_packet[RADIO_PKT_MAX_LEN] = { 0 };
      uint8_t radio_channel                  = channel[0];

// XN297 scramble lookup tables
const uint8_t xn297_scramble[] = {
  0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
  0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f,
  0xc7, 0x62, 0x97, 0xd5, 0x0b, 0x79, 0xca, 0xcc,
  0x1b, 0x5d, 0x19, 0x10, 0x24, 0xd3, 0xdc, 0x3f,
  0x8e, 0xc5, 0x2f
};
const uint16_t xn297_crc_xorout_scrambled[] = {
  0x0000, 0x3448, 0x9BA7, 0x8BBB, 0x85E1, 0x3E8C,
  0x451E, 0x18E6, 0x6B24, 0xE7AB, 0x3828, 0x814B,
  0xD461, 0xF494, 0x2503, 0x691D, 0xFE8B, 0x9BA7,
  0x8B17, 0x2920, 0x8B5F, 0x61B1, 0xD391, 0x7401,
  0x2138, 0x129F, 0xB3A0, 0x2988
};

// XN297 CRC calculator
uint16_t crc16_update(uint16_t crc, uint8_t a, uint8_t bits) {
    crc ^= (a << 8);
    while (bits--) {
        if (crc & 0x8000) {
            crc = (crc << 1) ^ CRC_POLYNOMIAL;
        } else {
            crc = crc << 1;
        }
    }
    return crc;
}

// XN297 address scramble
void scramble_address(uint8_t *address, uint8_t len) {
  uint8_t i;
  for(i=0; i<len; i++) {
    address[i] ^= xn297_scramble[len - i - 1];
  }
}

// reverse bits in a byte
uint8_t bit_reverse(uint8_t b_in) {
    uint8_t b_out = 0;
    for (int i = 0; i < 8; ++i) {
        b_out = (b_out << 1) | (b_in & 1);
        b_in >>= 1;
    }
    return b_out;
}

// XN297 packet descramble
void descramble_buf(uint8_t *buf, uint8_t len) {
  uint8_t i;
  for (i=0; i<len; i++) {
    buf[i] ^= xn297_scramble[RADIO_RX_AW + i];
    buf[i] = bit_reverse(buf[i]);
  }
}

void print_msg(const __FlashStringHelper* msg) {
  char buffer[128];
  memset(buffer, '\0', sizeof(buffer));
  strlcpy_P(buffer, (const char PROGMEM *)msg, sizeof(buffer));
  print_msg(buffer);
}

void print_msg(char *msg) {
  Serial.println(msg);
  #ifdef ENABLE_SSD1306
    display.clearDisplay();
    display.setCursor(0,0);
    display.print(msg);
    display.display();
  #endif
}

void print_buf(uint8_t *buf, uint8_t len) {
  uint8_t i;
  char tmp[PRINT_BUF_SIZE];

  snprintf(tmp, PRINT_BUF_SIZE,"%08lu: ", millis());
  Serial.print(tmp);

  snprintf(tmp, PRINT_BUF_SIZE,"[%02x] ", radio.getChannel());
  Serial.print(tmp);
  #ifdef ENABLE_SSD1306
    display.print(F("DATA: "));
  #endif

  for(i=0; i<len; i++) {
    snprintf(tmp, PRINT_BUF_SIZE, "%02x ", buf[i]);
    Serial.print(tmp);
    #ifdef ENABLE_SSD1306
      display.print(tmp);
    #endif
  }
  Serial.println();
  #ifdef ENABLE_SSD1306
    display.println();
    display.display();
  #endif
}

void xmit_payload(uint8_t *msg, uint8_t len) {
  uint8_t last = 0;
  uint8_t i;
  uint16_t crc = CRC_INITIAL;
  char tmp[PRINT_BUF_SIZE];

  // cheap buffer overrun protection
  // RADIO_RX_AW + msg len + 2 (CRC)
  len = (len > 25) ? 25 : len;

  // add address to packet
  for(i=0; i<RADIO_RX_AW; i++) {
    recv_packet[last] = rx_address[RADIO_RX_AW - i - 1];   // reversing byte order of address when writing to packet
    recv_packet[last] ^= xn297_scramble[i];                // scramble the address
    last++;
  }

  // add message to packet
  for (i=0; i<len; i++) {
    recv_packet[last] = bit_reverse(msg[i]);
    recv_packet[last] ^= xn297_scramble[RADIO_RX_AW + i];
    last++;
  }

  // calculate CRC
  for (i=0; i<last; i++) {
    crc = crc16_update(crc, recv_packet[i], 8);
  }
  crc ^= xn297_crc_xorout_scrambled[RADIO_RX_AW - 3 + len];

  // add CRC to packet
  recv_packet[last++] = crc >> 8;
  recv_packet[last++] = crc & 0xff;

  /*
  Serial.println("PACKET:");
  for(i=0;i<last;i++) {
    snprintf(tmp, PRINT_BUF_SIZE, "%02x ", recv_packet[i]);
    Serial.print(tmp);
  }
  Serial.println();
  */

  // transmit
  radio.write(recv_packet, last);
}

uint8_t button_handler() {
  static uint8_t btn = 0;
  static uint32_t last = 0;

  // when BTN_PIN goes low, the button is being pressed
  if (btn == 0 && digitalRead(BTN_PIN) == LOW) {
    btn = 1;
    last = millis();
  } else if (btn == 1 && digitalRead(BTN_PIN) == HIGH) {
    btn = 0;
    if (millis() - last > 3000) {
      return(REALLY_LONG_BTN_PRESS);
    } else if (millis() - last > 1000) {
      return(LONG_BTN_PRESS);
    } else {
      return(SHORT_BTN_PRESS);
    }
  }
  return(NO_BTN_PRESS);
}

void setup_receive() {
  uint8_t addr[RADIO_RX_AW];

  radio.stopListening();
  radio.flush_rx();
  radio.flush_tx();

  radio.setAddressWidth(RADIO_RX_AW);
  radio.setPayloadSize(RADIO_PD_SIZE + 2);

  memcpy(addr, rx_address, RADIO_RX_AW);
  scramble_address(addr, RADIO_RX_AW);
  radio.openReadingPipe(1, addr);

  //radio.printPrettyDetails();
  radio.startListening();
}

void setup_transmit() {
  radio.stopListening();
  radio.flush_rx();
  radio.flush_tx();

  radio.setAddressWidth(RADIO_TX_AW);
  radio.setPayloadSize(RADIO_RX_AW + RADIO_PD_SIZE + 2);

  radio.openWritingPipe(tx_address);
  radio.setChannel(radio_channel);
  //radio.printPrettyDetails();
}

void setup_radio() {
  static machine_state_t last_state = UNINITIALIZED;

  if (machine_state == last_state) {
    return;
  }

  switch(machine_state) {
    case STORYTELLER:
      setup_receive();
      break;
    case STORYMAKER:
      setup_transmit();
      break;
  }

  last_state = machine_state;
}

void storyteller() {
  static uint32_t channel_switch_time = 0;
  static uint8_t ch = 0;
  uint8_t i;
  static uint16_t crc;
  static uint8_t addr[RADIO_RX_AW];
  static uint8_t addr_set = 0;
  char tmp[PRINT_BUF_SIZE];

  // scramble the receive address
  // i could hard-code the scrambled address and declare it a const...
  if (!addr_set) {
    memcpy(addr, rx_address, RADIO_RX_AW);
    scramble_address(addr, RADIO_RX_AW);
    addr_set = 1;
  }

  // look for available data from the radio
  if (radio.available()) {
    radio.read(recv_packet, RADIO_PKT_MAX_LEN);
    radio.flush_rx();

    // calculate CRC from payload
    crc = CRC_INITIAL;
    for (i=0; i<RADIO_RX_AW; i++) {
      crc = crc16_update(crc, addr[RADIO_RX_AW - 1 - i], 8);
    }
    for(i=0; i<RADIO_PD_SIZE; i++) {
      crc = crc16_update(crc, recv_packet[i], 8);
    }
    crc ^= xn297_crc_xorout_scrambled[RADIO_RX_AW - 3 + RADIO_PD_SIZE];

    // perform CRC check
    if (crc == (((recv_packet[RADIO_PD_SIZE] & 0xff) << 8) | (recv_packet[RADIO_PD_SIZE + 1] & 0xff))) {

      // descramble the payload
      descramble_buf(recv_packet, RADIO_PD_SIZE);

      // display update
      #ifdef ENABLE_SSD1306
        display.clearDisplay();
        display.setCursor(0,0);
        display.setTextSize(1);       // size(2) = 10 characters per line
      #endif

      // print the descrambled packet
      print_buf(recv_packet, RADIO_PD_SIZE);

      // get new radio channel
      switch (recv_packet[0]) {
        case 0x0b:
          radio_channel = 0x1c;
          break;
        case 0x0c:
          radio_channel = 0x31;
          break;
        case 0x0d:
          radio_channel = 0x44;
          break;
        case 0x06:
          radio_channel = radio.getChannel();
          break;
      }

      // get new group id
      xmit_packet[4] = recv_packet[4];
      xmit_packet[5] = recv_packet[5];
    }
  }

  // after some time, switch to the next channel. we do this to scan all known radio channels for
  // ornaments. the channel and group id stuff we identify through this we'll use when we transmit.
  if (millis() > channel_switch_time) {
    if (channel_switch_time > 0) {
      radio.stopListening();
      radio.flush_rx();
      radio.flush_tx();
      ch = (ch + 1) % (sizeof(channel)/sizeof(byte));
      radio.setChannel(channel[ch]);
      radio.startListening();
      //Serial.println(".");
    }

    channel_switch_time = millis() + 10;    // the more data we hear on the current channel, the longer we'll listen
  }
}

void send_it() {
  uint32_t start, tmp;

  if (machine_state != STORYMAKER) {
    return;
  }

  // send command to trigger immediate ornament playback
  xmit_packet[0] = 0x02;    // playback cmd
  xmit_packet[1] = 0x00;    // playback pattern doesn't seem to matter (as long as 0x02 is not part of this value)
  xmit_packet[2] = 0x3f;    // countdown; each 'tick' = 4ms
  xmit_packet[5] &= 0x0f;

  print_msg(F("Sending immediate playback command."));
  start = millis() + (xmit_packet[2] * 4);
  while(xmit_packet[2] > 0) {
    radio.flush_rx();
    radio.flush_tx();
    xmit_payload(xmit_packet, RADIO_PD_SIZE);
    tmp = millis();
    if (start > tmp) {
      xmit_packet[2] = (uint8_t)((start - tmp)/4);
    }
  }
  print_msg(F("Done."));
}

void cancel_playback() {
  uint8_t i;

  if (machine_state != STORYMAKER || radio_channel == 0 || xmit_packet[4] == 0) {
    return;
  }

  print_msg(F("Canceling Playback!"));
  xmit_packet[0] = 0x01;
  for (i=0;i<9;i++) {
      radio.flush_rx();
      radio.flush_tx();
      xmit_payload(xmit_packet, RADIO_PD_SIZE);
      delay(2);
  }
  print_msg(F("Done."));
}

void conduct_full_haunted_mansion_show() {
  uint16_t i, j, k;
  uint32_t start, tmp;

  if (machine_state != STORYMAKER) {
    return;
  }

  Serial.println();
  Serial.println(F("-- Full Haunted Mansion Show --"));

  // -- STEP 1 --
  //
  // xmit group id and radio channel so everyone is listening on the same station to the same group id
  print_msg(F("Telling every ornament to tune in..."));

  // initialize payload
  // 0c = channel 0x31, 0d = channel 0x44, 0b = channel 0x1c
  switch (radio_channel) {
    case 0x44:
      xmit_packet[0] = 0x0d;
      break;
    case 0x31:
      xmit_packet[0] = 0x0c;
      break;
    case 0x1c:
      xmit_packet[0] = 0x0b;
      break;
    default:
      Serial.print("ERROR: unknown radio channel ");
      Serial.println(radio_channel, HEX);
      break;
  }
  xmit_packet[1] = 0;
  xmit_packet[2] = 0;
  xmit_packet[3] = 0;
  //xmit_packet[5] |= 0x10;

  // announce 75 times on each channel
  for(i=0;i<sizeof(channel);i++) {
    // set channel
    radio.flush_rx();
    radio.flush_tx();
    radio.setChannel(channel[i]);

    // send reg_cmd many times
    for(j=0; j<75; j++) {
      radio.flush_rx();
      radio.flush_tx();
      xmit_payload(xmit_packet, RADIO_PD_SIZE);
      delay(2);
    }
  }

  /*
  // announce 8 times on each channel 95 times
  // this is what i saw stock ornaments do, but in practice I don't think this is needed
  for(i=0;i<95;i++) {
    for(j=0;j<sizeof(channel);j++) {
      for(k=0;k<8;k++) {
      radio.flush_rx();
      radio.flush_tx();
      xmit_payload(xmit_packet, RADIO_PD_SIZE);
      delay(2);            }
    }
  }
  */

  radio.flush_rx();
  radio.flush_tx();

  // switch to our target channel
  radio.setChannel(radio_channel);

  // set payload for 'this is the channel'
  xmit_packet[2] = xmit_packet[0];
  xmit_packet[0] = 0x0f;

  // announce we are staying on the current channel
  for (i=0;i<9;i++) {
      radio.flush_rx();
      radio.flush_tx();
      xmit_payload(xmit_packet, RADIO_PD_SIZE);
      delay(2);
  }
  Serial.println(F(" Done."));

  // -- STEP 2 --
  //
  // Announce that we're going to start a full show to all listening ornaments
  print_msg(F("Mansion Playback Announce (BONG)"));
  xmit_packet[0] = 0x06;
  xmit_packet[1] = 0x00;    // 0x05 and 0x00 work;
  xmit_packet[2] = 0x4f;
  xmit_packet[5] &= 0x0f;   // set top 4 bits to 0
  do {
    xmit_packet[2]--;
    radio.flush_rx();
    radio.flush_tx();
    xmit_payload(xmit_packet, RADIO_PD_SIZE);
    delay(11);
  } while (xmit_packet[2] > 0);

  // delay for acks from ornaments
  delay(2500);

  // -- STEP 3 --
  //
  // Playback sync countdown
  print_msg(F("Mansion Playback Countdown"));
  xmit_packet[0] = 0x02;    // playback cmd
  xmit_packet[1] = 0xfe;    // playback pattern: all ornaments
  xmit_packet[2] = 0x9f;    // countdown; each 'tick' = 4ms
  
  start = millis() + (xmit_packet[2] * 4);
  while(xmit_packet[2] > 0) {
    radio.flush_rx();
    radio.flush_tx();
    xmit_payload(xmit_packet, RADIO_PD_SIZE);
    tmp = millis();
    if (start > tmp) {
      xmit_packet[2] = (uint8_t)((start - tmp)/4);
    }
  }

  print_msg(F("Let The Show Begin!"));
}

void storymaker() {
  static uint8_t short_step = 0;

  switch (button_handler()) {
    case SHORT_BTN_PRESS:
      if (short_step) {
        cancel_playback();
      } else {
        send_it();
      }
      short_step ^= 1;
      break;

    // trigger full haunted mansion show
    case LONG_BTN_PRESS:
      conduct_full_haunted_mansion_show();
      break;

    case REALLY_LONG_BTN_PRESS:
      print_msg(F("Story Teller Mode"));
      machine_state = STORYTELLER;
      return;
      break;
  }
}

void setup() {
  uint8_t i;
  uint32_t seed = 0;

  // seed RNG
  for(i=0;i<32;i++) {
    seed |= ((analogRead(0) & 1) << i);
  }
  randomSeed(seed);

  // setup serial
  Serial.begin(115200);

  // enable NRF library to output to serial
  printf_begin();

  // setup button pin
  pinMode(BTN_PIN, INPUT_PULLUP);

  // Initialize OLDED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  #ifdef ENABLE_SSD1306
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    }
    display.display();                    // display splash screen
    delay(2000);                          // Pause for 2 seconds
    display.clearDisplay();               // Clear the buffer
    display.setTextColor(SSD1306_WHITE);  // set text color
    display.cp437(true);                  // use full 256 char 'Code Page 437' font
  #endif

  // initialize nRF module
  radio.begin();
  radio.disableDynamicPayloads();                     //    Dynamic Payloads can be enabled and reception is okay even if xmit is not dynamic
  radio.setChannel(channel[0]);                       // ** this can cause a reception issue
  radio.setDataRate(RADIO_DR);                        // ** this can cause a reception issue
  radio.setRetries(RADIO_RTD, RADIO_RTC);             //    Not relevant for reception? 
  radio.setAutoAck(false);                            // ** this can cause a reception issue
  radio.setCRCLength(RADIO_CRC_LEN);                  //    CRC can be disabled and still receive even if xmit is generating CRC

  // initialize transmit packet with a random group id
  xmit_packet[4] = random(0x20, 0x3f) * 2;
  xmit_packet[5] = (random(1,5)*2) + 0x10;
  xmit_packet[6] = 0x0a;

  // set the initial radio_channel to transmit on
  radio_channel = channel[0];

  // We're ready to go.
  print_msg(F("Ready."));
}

void loop() {

  // set the radio to transmit or receive depending on the current machine_state value
  setup_radio();

  // do something depending on the current machine_state value
  switch (machine_state) {
    case STORYTELLER:
      storyteller();
      if (button_handler()) {
        print_msg(F("Story Maker Mode"));
        machine_state = STORYMAKER;
        print_buf(xmit_packet, RADIO_PD_SIZE);
      }
      break;
    case STORYMAKER:
      storymaker();
      break;
  }
}