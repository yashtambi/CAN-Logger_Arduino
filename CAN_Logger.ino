
/*
   (C) Yash Tambi
   yash.tambi@gmail.com

   CAN_Tx format on Serial:
   $<cmd>,<id>,<dlc>,<byte0>,<byte1>,...,<byteN>!
      N: max 8
*/

#include <EEPROM.h>
#include <mcp_can.h>
#include <SPI.h>
#include <SD.h>

#define CAN_INT             2               // Setting pin 2 for /INT input
#define CAN_CS              3
#define SD_CS               4
#define FILENUM_RST         A0
#define ERROR_LED_CAN       A1
#define ERROR_LED_SD        A2
#define CAN_NEWMSG_LED      A3
#define CAN_SWITCH          A4

#define FILENAME_EXT            ".csv"
#define DELIMITER               ","

#define CAN_TX_STARTCHAR        '$'
#define CAN_TX_ENDCHAR          '!'

#define BAUDRATE                115200
#define CAN_MAX_DLC             8

#define CAN_BAUDRATE            CAN_500KBPS

#define SEND_OVER_CAN           "cansend"

#define LOG_SD
#define LOG_SERIAL

String filename = "";
String txString = "";

typedef struct {
  uint8_t buf[8];
  uint32_t id;
  uint8_t len;
} canData;
canData rx, tx;

typedef struct {
  bool msgCompleteFlag;
  bool dataFlag;
} msg;
msg serialMsg;


MCP_CAN CAN0(CAN_CS);


void gpio_init (void) {
  pinMode(CAN_NEWMSG_LED, OUTPUT);
  pinMode(ERROR_LED_CAN, OUTPUT);
  pinMode(ERROR_LED_SD, OUTPUT);
  pinMode(FILENUM_RST, INPUT);
  pinMode(CAN_INT, INPUT);

  digitalWrite(CAN_NEWMSG_LED, LOW);
  digitalWrite(ERROR_LED_SD, LOW);
  digitalWrite(ERROR_LED_CAN, LOW);
}


void check_filenum_rst(void) {
  if (!digitalRead(FILENUM_RST))                          // If pin 2 is low, read receive buffer
  {
    uint16_t addr = 255;
    EEPROM.write(addr, 0);
    digitalWrite(ERROR_LED_SD, HIGH);
    digitalWrite(ERROR_LED_CAN, HIGH);
    delay(100);
    digitalWrite(ERROR_LED_SD, LOW);
    digitalWrite(ERROR_LED_CAN, LOW);
  }
}


void can_init (void) {
  if (CAN0.begin(CAN_BAUDRATE) == CAN_OK) {
    Serial.print("CAN Initialized!\r\n");
    digitalWrite(ERROR_LED_CAN, LOW);
  }
  else {
    Serial.print("CAN Init Fail!\r\n");
    digitalWrite(ERROR_LED_CAN, HIGH);
  }
}


void sd_init (void) {
  Serial.print("Initializing SD card...");

  if (!SD.begin(SD_CS)) {
    Serial.println("Card failed, or not present");
    digitalWrite(ERROR_LED_SD, HIGH);
  } else {
    uint16_t filenum = 0;
    uint16_t addr = 400;
    Serial.println("Card Initialized.");

    filenum = EEPROM.read(addr);
    EEPROM.write(addr, ((filenum + 1) % 255));
    delay(1000);
    filename = String(filenum) + FILENAME_EXT;
    Serial.print("Writing to file: ");
    Serial.println(filename);

    write_file_header();
  }
}


void file_write (String str) {
#ifdef LOG_SD
  File dataFile = SD.open(filename, FILE_WRITE);

  if (dataFile) {
    digitalWrite(ERROR_LED_SD, LOW);
    dataFile.println(str);
    dataFile.close();
  }
  else digitalWrite(ERROR_LED_SD, HIGH);
#endif
}


void write_file_header(void) {
  String header = "";
  header += "Time (ms)";
  header += DELIMITER;
  header += "Direction";
  header += DELIMITER;
  header += "CAN ID";
  header += DELIMITER;
  header += "DLC";
  for (uint8_t i = 0; i < 8; i++) {
    header += DELIMITER;
    header += "Byte ";
    header += String(i);
  }

  file_write(header);
}


void log_can_data (void) {
  digitalWrite(CAN_NEWMSG_LED, HIGH);
  String dataString = "";

  CAN0.readMsgBuf(&rx.len, rx.buf);              // Read data: len = data length, buf = data byte(s)
  rx.id = CAN0.getCanId();                    // Get message ID

  dataString += String(millis());
  dataString += DELIMITER;
  dataString += "rx";
  dataString += DELIMITER;
  dataString += String(rx.id, HEX);
  dataString += DELIMITER;
  dataString += String(rx.len, HEX);
  for (uint8_t i = 0; i < rx.len; i++) {
    dataString += DELIMITER;
    dataString += String(rx.buf[i], HEX);
  }

  Serial.println(dataString);

  file_write(dataString);

}


void serial_cmd_parser (void) {
  char *token;
  const char s[2] = ",";
  char d[50];
  uint8_t i = 0;

  txString.toCharArray(d, txString.length() + 1);

  token = strtok(d, s);

  if (strcmp(token, SEND_OVER_CAN) == 0) {
    tx.id = (long unsigned int) atol(token);
    token = strtok(NULL, s);
    tx.len = (unsigned char)(atol(token));
    if (tx.len > CAN_MAX_DLC)
      tx.len = 0;
    while ( token != NULL )
    {
      token = strtok(NULL, s);
      tx.buf[i++] = (unsigned char)(atol(token));
    }
    send_can_data();
  }

  serialMsg.msgCompleteFlag = false;
}


void send_can_data(void) {
  CAN0.sendMsgBuf(tx.id, 0, tx.len, tx.buf);

  String dataString = "";

  dataString += String(millis());
  dataString += DELIMITER;
  dataString += "tx";
  dataString += DELIMITER;
  dataString += String(tx.id, HEX);
  dataString += DELIMITER;
  dataString += String(tx.len, HEX);
  for (uint8_t i = 0; i < tx.len; i++) {
    dataString += DELIMITER;
    dataString += String(tx.buf[i], HEX);
  }

  Serial.println(dataString);
  file_write(dataString);
}


bool check_string() {
  if (txString.length() > 0)
    return true;
  else
    return false;
}


void serialEvent() {
  while (Serial.available()) {

    char input = (char)Serial.read();

    if ((input == CAN_TX_STARTCHAR) ) {
      serialMsg.dataFlag = true;
      serialMsg.msgCompleteFlag = false;
      txString = "";
    } else if (input == CAN_TX_ENDCHAR) {
      serialMsg.msgCompleteFlag = true;
      serialMsg.dataFlag = false;
    } else if (serialMsg.dataFlag == true) {
      txString += input;
    }

  }
}


void setup() {
  Serial.begin(BAUDRATE);

  gpio_init();
  check_filenum_rst();

#ifdef LOG_SD
  sd_init ();
#endif

  can_init ();

  serialMsg.msgCompleteFlag = false;
}


void loop() {
  digitalWrite(CAN_NEWMSG_LED, LOW);

  if (!digitalRead(CAN_INT)) {                         //If pin 2 is low, read receive buffer
    log_can_data();
  }
  if (serialMsg.msgCompleteFlag == true) {             //if new tx message received, send message
    serial_cmd_parser();
  }
}
