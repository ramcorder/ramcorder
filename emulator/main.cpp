// main.cpp - ramcorder emulator
// commandLine params - space seperated words
//   log1   - logLevel 1
//   log2   - logLevel 2
//   log3   - logLevel 3
//   mono   - no ansi control in console
//   colour - ansi colour control in console
//   loop   - com port loopback test
//   master - v series emulator
//   slave  - ramcorder emulator
//   serialPortName -  windows usually "COMx", linux usually "/dev/ttySx"
//{{{  includes
#ifdef _WIN32
  // temporary, for Sleep, to see the failures
  #include <windows.h>
  #include <VersionHelpers.h>
#endif

#include <cstdint>
#include <array>
#include <vector>
#include <string>

#include "utils/cLog.h"
#include "serialPort/serialPort.h"

using namespace std;
//}}}

const string kVersion = "0.99.5 compiled " __TIME__  " " __DATE__;
//{{{  constexpr
constexpr uint8_t kPacketMax = 16;

constexpr bool kNtsc = false;

constexpr char kCommandStatusReport   = 'S';
constexpr char kCommandExtraStatus    = 's';
constexpr char kCommandAcknowledge    = 'A';
constexpr char kCommandSelectProtocol = 'K';
constexpr char kCommandRecord         = 'R';
constexpr char kCommandView           = 'V';
constexpr char kCommandGo             = 'G';
constexpr char kCommandPosition       = 'P';

constexpr uint8_t kParamStatus         = 1;
constexpr uint8_t kParamClipSelect     = 2;
constexpr uint8_t kParamClipLength     = 4;
constexpr uint8_t kParamFrameNumber    = 5;
constexpr uint8_t kParamClipDefinition = 7;
constexpr uint8_t kParamTimecode       = 8;
constexpr uint8_t kParamId             = 9;
constexpr uint8_t kParamProtocol       = 10;
constexpr uint8_t kParamGoDelay        = 11;

constexpr uint8_t kParamStatusIdle = 0;
constexpr uint8_t kParamStatusAck =  4;
constexpr uint8_t kParamStatusNak =  5;
constexpr uint8_t kParamStatusBusy = 6;
constexpr uint8_t kParamStatusRst =  7;
constexpr uint8_t kParamStatusSvc =  8;

constexpr uint8_t kProtocolDpbGrab = 0;

constexpr char kParamIdDpb         = 'P';
constexpr char kParamIdCarousel    = 'J';

constexpr uint8_t kParamclipSelOutput      = 0x00;
constexpr uint8_t kParamclipSelInput       = 0x01;
constexpr uint8_t kParamSelPlayBounce      = 0x08;
constexpr uint8_t kParamClipSelRecordLoop  = 0x10;
constexpr uint8_t kParamClipSelField       = 0x20;
constexpr uint8_t kParaClipSelPlaybackLoop = 0x40;
constexpr uint8_t kParamSelPlayReverse     = 0x80;

constexpr uint8_t kParamSel2field2dom = 0x01;
constexpr uint8_t kParamSel2expand    = 0x02;
constexpr uint8_t kParamSel2compress  = 0x04;
//}}}
enum eMode { eLoopback, eMaster, eSlave };

//{{{
class cRamcorderPacket {
public:
  cRamcorderPacket() = default;
  //{{{
  virtual ~cRamcorderPacket() {
    // close,free port
    if (mPort) {
      spCheck (sp_close (mPort));
      sp_free_port (mPort);
    }
  }
  //}}}

  //{{{
  void initialise (const string& portName) {

    cLog::log (LOGINFO, fmt::format ("initialise port {} 9600, 8bit, evenParity, 1 stop, no flow control", portName));

    mPortName = portName;

    spCheck (sp_get_port_by_name (mPortName.c_str(), &mPort));
    spCheck (sp_open (mPort, SP_MODE_READ_WRITE));
    spCheck (sp_set_baudrate (mPort, 9600));
    spCheck (sp_set_bits (mPort, 8));
    spCheck (sp_set_parity( mPort, SP_PARITY_EVEN));
    spCheck (sp_set_stopbits (mPort, 1));
    spCheck (sp_set_flowcontrol (mPort, SP_FLOWCONTROL_NONE));
  }
  //}}}

  virtual void go() = 0;

protected:
  struct sp_port* getPort() { return mPort; }

  //{{{
  int spCheck (enum sp_return result) {
  // dumb sp error checking, abort at sign of any trouble
    char* error_message;

    switch (result) {
      case SP_OK:
      default:
        return result;

      case SP_ERR_ARG:
        cLog::log (LOGERROR, fmt::format ("Error: Invalid argument"));
        break;

      case SP_ERR_FAIL:
        error_message = sp_last_error_message();
        cLog::log (LOGERROR, fmt::format ("Error: Failed: {}", error_message));
        sp_free_error_message (error_message);
        break;

      case SP_ERR_SUPP:
        cLog::log (LOGERROR, fmt::format ("Error: Not supported"));
        break;

      case SP_ERR_MEM:
        cLog::log (LOGERROR, fmt::format ("Error: Couldn't allocate memory"));
        break;
    }

    #ifdef _WIN32
      Sleep (2000);
    #endif
    abort();
  }
  //}}}

  //{{{
  void startPacket (char command) {

    mPacket = { 0 };
    mPacket[1] = static_cast <uint8_t>(command);

    mCount = 2;
  }
  //}}}
  //{{{
  void addUint8 (uint8_t value) {

    if (mCount >= kPacketMax-2) // must be room for checksum
      cLog::log (LOGERROR, fmt::format ("addUint8 - too many bytes in packet adding {}", value));
    else {
      mPacket[mCount] = value;
      mCount++;
    }
  }
  //}}}
  //{{{
  void addChar (char ch) {
    addUint8 (static_cast <uint8_t>(ch));
  }
  //}}}
  //{{{
  void addWord (int16_t value) {

    addUint8 (value >> 8);
    addUint8 (value & 0xff);
  }
  //}}}

  //{{{
  uint8_t decToBcd (uint8_t value) {

    if (value > 99) {
      cLog::log (LOGERROR, fmt::format ("decToBcd - value too big {}", value));
      return 0;
    }

    return ((value / 10) << 4) | (value % 10);
  }
  //}}}

  //{{{
  void flushRx() {
  // flush rx

    cLog::log (LOGINFO, fmt::format ("flushRx"));
    sp_flush (getPort(), SP_BUF_INPUT);
  }
  //}}}
  //{{{
  bool txPacket() {

    cLog::log (LOGINFO, fmt::format ("txPacket"));

    // set first byte of command packet from header and packet length count (minus header and command byte)
    mPacket[0] = 0xF0 | ((mCount - 2) & 0xF);

    // calc checksum
    uint16_t checksum = 0;
    for (uint8_t i = 0; i < mCount; i++)
      checksum += mPacket[i];
    checksum = (0x100 - (checksum & 0xff)) & 0xff;

    // append checksum to packet
    mPacket[mCount] = static_cast <uint8_t>(checksum);
    mCount++;

    const unsigned int timeout = 200; // 0.2s
    int bytesSent = spCheck (sp_blocking_write (getPort(), mPacket.data(), mCount, timeout));
    if (bytesSent != mCount) {
      cLog::log (LOGERROR, fmt::format ("txPacket - timed out - {} bytes sent of ", bytesSent, getPacketString()));
      return false;
    }

    cLog::log (LOGINFO, fmt::format ("txPacket - tx {}", getPacketString()));
    return true;
  }
  //}}}
  //{{{
  bool rxPacket (int headerTimeout) {
  // wait for and rx reply

    cLog::log (LOGINFO, fmt::format ("rxPacket"));

    // rx header
    mPacket = { 0 };
    int headerByteRead = spCheck (sp_blocking_read (getPort(), &mPacket[0], 1, headerTimeout));
    if (headerByteRead != 1) {
      // header error, unlikely unless we use the timeout
      cLog::log (LOGERROR, fmt::format ("rx header failed"));
      return false;
    }

    if ((mPacket[0] & 0xF0) != 0xF0) {
      cLog::log (LOGERROR, fmt::format ("rx {}:bytes - header byte not header {}", headerByteRead, mPacket[0]));
      return false;
    }

    // add header and command bytes
    mCount = (mPacket[0] & 0x0F) + 2;

    // header already rxed
    int packetBodyBytesExpected = mCount;
    cLog::log (LOGINFO, fmt::format ("rx header {:x} expect {} more bytes", mPacket[0], packetBodyBytesExpected));

    // rx packet body, with timeout
    const unsigned int timeout = 1000; // 1 second
    int packetBodyBytesRead = spCheck (sp_blocking_read (getPort(), mPacket.data()+1, packetBodyBytesExpected, timeout));
    if (packetBodyBytesRead != packetBodyBytesExpected) {
      // packet body error
      cLog::log (LOGERROR, fmt::format ("rx {}:bytes - packet body not ok - {}", packetBodyBytesRead, getPacketString()));
      return false;
    }

    uint16_t checksum = 0;
    for (uint8_t i = 0; i < mCount; i++)
      checksum += mPacket[i];
    checksum = (0x100 - (checksum & 0xFF)) & 0xFF;

    if (checksum != mPacket[mCount]) {
      cLog::log (LOGERROR, fmt::format ("rx checksum {:x} != {:x} - {}", mPacket[mCount], checksum, getPacketString()));
      return false;
    }

    cLog::log (LOGINFO, fmt::format ("rx {}:bytes - packet ok - {}", packetBodyBytesRead, getPacketString()));
    return true;
  }
  //}}}

  uint8_t mCount = 0;
  array <uint8_t, kPacketMax> mPacket = { 0 };

private:
  //{{{
  string getPacketString() {
  // create debug string, dumb way to form string, could do better

    string packetString;
    for (uint8_t i = 0; i < mCount; i++)
      packetString += fmt::format ("{:02x} ", mPacket[i]);

    return packetString;
  }
  //}}}

  string mPortName;
  struct sp_port* mPort;
};
//}}}
//{{{
class cRamcorderLoopback : public cRamcorderPacket {
public:
  //{{{
  cRamcorderLoopback() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder loopback"));
  }
  //}}}
  virtual ~cRamcorderLoopback() = default;

  //{{{
  void go() final {

    const unsigned int timeout = 1000; // 1 second
    const string test = "Loopback data - 01234567890 abcdefghijklmnpqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    // tx port
    cLog::log (LOGINFO, fmt::format ("setup to tx {}:bytes on port:{}", test.size(), sp_get_port_name (getPort())));
    cLog::log (LOGINFO, fmt::format ("tx - {}", test));

    int bytesSent = spCheck (sp_blocking_write (getPort(), test.data(), test.size(), timeout));
    if (bytesSent == test.size())
      cLog::log (LOGINFO, fmt::format ("tx {}:bytes ok", test.size()));
    else
      cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes sent", bytesSent, test.size()));

    cLog::log (LOGINFO, fmt::format ("setup to rx {}:bytes on port:{}", test.size(), sp_get_port_name(getPort())));

    // allocate buffer to receive data, 1 second timeout
    char* buf = (char*)malloc (test.size() + 1);
    int bytesRead = spCheck (sp_blocking_read (getPort(), buf, test.size(), timeout));
    if (bytesRead == test.size())
      cLog::log (LOGINFO, fmt::format ("rx {}:bytes ok", test.size()));
    else
      cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes received", bytesRead, test.size()));

    // spCheck if we received the same data we sent
    buf[bytesRead] = '\0';
    cLog::log (LOGINFO, fmt::format ("rx - {}", buf));

    // Free receive buffer
    free (buf);
  }
  //}}}
};
//}}}
//{{{
class cRamcorderMaster : public cRamcorderPacket {
public:
  //{{{
  cRamcorderMaster() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder master"));

    mStatus = 0;

    if (kNtsc)
      mClipLength = 383;
    else
      mClipLength = 323;

    mTimecode [0] = 0;
    mTimecode[1] = 0;
    mTimecode[2] = 0;
    mTimecode[3] = 0;

    mFieldNumber = 0;
  }
  //}}}
  virtual ~cRamcorderMaster() = default;

  //{{{
  uint16_t getNumFrames() {
    return mClipLength;
  }
  //}}}

  //{{{
  void go() final {
  // simulate a typical ramcorder sequence

    selectProtocol (kProtocolDpbGrab);

    // some sort of ramcorder exercises
    //selectClip (...)
    //start (...)
    //stop()
  }
  //}}}

  //{{{
  bool selectProtocol (uint8_t protocol) {

    // first packet
    startPacket (kCommandStatusReport);

    addUint8 (kParamId);
    addChar (kParamIdDpb);

    addUint8 (kParamTimecode);
    addUint8 (decToBcd (12));
    addUint8 (0);
    addUint8 (0);
    addUint8 (0);

    bool ok = sendCommand (true);

    // second packet
    startPacket (kCommandSelectProtocol);
    addUint8 (kParamProtocol);
    addUint8 (protocol);

    ok = sendCommand (true);

    return ok;
  }
  //}}}
  //{{{
  bool position (bool waitForReply, uint16_t field, bool fieldMode, bool field2Dom, bool reverse) {

    cLog::log (LOGINFO, fmt::format ("implement position"));
    return true;
  }
  //}}}
  //{{{
  bool selectClip (bool inputClip, bool fieldMode, bool field2Dom,
                   uint16_t startField, uint16_t finishField,
                   bool play_reverse, bool play_bounce,
                   bool rec_loop, bool play_loop,
                   bool cine_expand, bool cine_compress) {

    cLog::log (LOGINFO, fmt::format ("implement selectClip"));
    return true;
  }
  //}}}
  //{{{
  bool pollField (uint16_t field, bool field2dom) {

    cLog::log (LOGINFO, fmt::format ("implement pollField"));
    return false;
  }
  //}}}
  //{{{
  bool start (bool playGo, bool recordGo, uint16_t goDelay) {

    cLog::log (LOGINFO, fmt::format ("implement start"));
    return true;
  }
  //}}}
  //{{{
  bool stop() {

    cLog::log (LOGINFO, fmt::format ("implement stop"));
    return true;
  }
  //}}}

private:
  //{{{
  bool sendCommand (bool waitForReply) {

    cLog::log (LOGINFO, fmt::format ("sendCommand"));

    // flush rx buffer
    flushRx();

    // tx packet
    if (!txPacket()) {
      cLog::log (LOGERROR, fmt::format ("sendCommand - tx timed out"));
      return false;
    }

    if (waitForReply) {
      // wait for reply, 0.2s timeout
      if (!rxPacket (200)) {
        cLog::log (LOGERROR, fmt::format ("sendCommand - reply timed out"));
        return false;
      }

      if (mPacket[1] != kCommandAcknowledge) {
      //{{{  not an acknowledge
        cLog::log (LOGERROR, fmt::format ("sendCommand - unexpected reply{}:{:x}",
                                          static_cast<char>(mPacket[1]), mPacket[1]));
        return false;
      }
      //}}}

      cLog::log (LOGINFO, fmt::format ("sendCommand - got acknowledge"));

      uint8_t paramIndex = 2;
      while (paramIndex < mCount) {
        switch (mPacket[paramIndex]) {
          //{{{
          case kParamStatus:
            mStatus = mPacket[paramIndex + 1];
            cLog::log (LOGINFO, fmt::format ("carousel status {:x}", mStatus));
            paramIndex += 2;
            break;
          //}}}
          //{{{
          case kParamClipSelect:
            cLog::log (LOGERROR, fmt::format ("unexpected kParamClipSelect {:2}", mPacket[paramIndex + 1]));
            paramIndex += 2;
            break;
          //}}}
          //{{{
          case kParamClipDefinition:
            cLog::log (LOGERROR, fmt::format ("unexpected kParamClipDefinition {:x} {:x} {:x} {:x}",
                                              mPacket[paramIndex + 1], mPacket[paramIndex + 2],
                                              mPacket[paramIndex + 3], mPacket[paramIndex + 4]));
            paramIndex += 5;
            break;
          //}}}
          //{{{
          case kParamTimecode:
            cLog::log (LOGERROR, fmt::format ("unexpected kParamTimecode {:x} {:x} {:x} {:x}",
                                              mPacket[paramIndex + 1], mPacket[paramIndex + 2],
                                              mPacket[paramIndex + 3], mPacket[paramIndex + 4]));
            paramIndex += 5;
            break;
          //}}}
          //{{{
          case kParamClipLength:
            mClipLength = (mPacket[paramIndex + 1] * 0x100) + mPacket[paramIndex + 2];
            cLog::log (LOGINFO, fmt::format ("carousel clip length {}", mClipLength));
            paramIndex += 3;
            break;
          //}}}
          //{{{
          case kParamId:
            if (mPacket[2] != 'J')
              cLog::log (LOGERROR, fmt::format ("id not carousel {}", mPacket[paramIndex + 1]));
            else
              cLog::log (LOGINFO, fmt::format ("id carousel{}", mPacket[paramIndex + 1]));
            paramIndex += 2;
            break;
          //}}}
          //{{{
          case kParamProtocol:
            cLog::log (LOGERROR, fmt::format ("unexpected kParamProtocol {:x}", mPacket[paramIndex + 1]));
            paramIndex += 2;
            break;
          //}}}
          //{{{
          case kParamFrameNumber:
            mFieldNumber = (mPacket[paramIndex + 1] * 0x100) + mPacket[paramIndex + 2];
            cLog::log (LOGINFO, fmt::format ("carousel at field {}", mFieldNumber));
            paramIndex += 3;
            break;
          //}}}
          default:
            break;
        }
      }
    }

    return true;
  }
  //}}}

  uint8_t mStatus = 0;
  array <uint8_t, 4> mTimecode = { 0 };
  uint16_t mFieldNumber = 0;
  uint16_t mClipLength = kNtsc ? 383 : 323;
};
//}}}
//{{{
class cRamcorderSlave : public cRamcorderPacket {
public:
  //{{{
  cRamcorderSlave() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder slave"));
  }
  //}}}
  virtual ~cRamcorderSlave() = default;

  void go() final {
    while (true) {
      // listen for command
      if (rxPacket (0)) {
        // valid packet, action command
        cLog::log (LOGINFO, fmt::format ("slave rx command {}:{:x}", static_cast<char>(mPacket[1]), mPacket[1]));

        // point to first param
        uint8_t packetIndex = 2;

        // decode command
        switch (mPacket[1]) {
          //{{{
          case kCommandStatusReport:
            cLog::log (LOGINFO, fmt::format ("commandStatusReport"));

            // look for params
            while (packetIndex < mCount) {
              if (mPacket[packetIndex] == kParamId) {
                cLog::log (LOGINFO, fmt::format ("- paramId:{:x} id:{:x}", mPacket[packetIndex], mPacket[packetIndex+1]));
                packetIndex += 2;
              }
              else if (mPacket[packetIndex] == kParamTimecode) {
                cLog::log (LOGINFO, fmt::format ("- paramTimecode:{:x} {:x} {:x} {:x} {:x}",
                                                 mPacket[packetIndex],
                                                 mPacket[packetIndex+1], mPacket[packetIndex+2],
                                                 mPacket[packetIndex+3], mPacket[packetIndex+4]));
                packetIndex += 5;
              }
              else {
                cLog::log (LOGERROR, fmt::format ("- unknown param:{:x}", mPacket[packetIndex]));
                packetIndex++;
              }
            }

            // send acknowledge
            // - send timecode as well?
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          //}}}
          //{{{
          case kCommandExtraStatus:
            cLog::log (LOGINFO, fmt::format ("commandExtraStatus - poll fieldNum"));

            // acknowledgeCommand
            startPacket (kCommandAcknowledge);

            // acknowledgeStatus
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);

            // fieldNumber
            addUint8 (kParamFrameNumber);
            addWord (mFieldNumber);

            txPacket();

            break;
          //}}}
          //{{{
          case kCommandSelectProtocol:
            cLog::log (LOGINFO, fmt::format ("commandSelectProtocol paramProtocol:{:x} protocol:{:x}",
                                             mPacket[packetIndex], mPacket[packetIndex+1]));

            // send acknowledge
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          //}}}
          //{{{
          case kCommandRecord:
            cLog::log (LOGINFO, fmt::format ("commandRecord"));

            // send acknowledge
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          //}}}
          //{{{
          case kCommandView:
            cLog::log (LOGINFO, fmt::format ("commandView"));

            // send acknowledge
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          //}}}
          //{{{
          case kCommandGo:
            cLog::log (LOGINFO, fmt::format ("commandGo paramGoDelay:{:x} delay:{:x}",
                                             mPacket[packetIndex], mPacket[packetIndex+1]));

            // send acknowledge
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          //}}}
          //{{{
          case kCommandPosition: {
            cLog::log (LOGINFO, fmt::format ("commandPosition"));

            // clipSelect param
            cLog::log (LOGINFO, fmt::format ("- paramClipSelect:{:x} mode:{:x} dom:{:x}",
                                             mPacket[packetIndex], mPacket[packetIndex+1], mPacket[packetIndex+2]));
            packetIndex += 3;

            // clipDefinition param
            uint16_t startPlayFieldNumber = (mPacket[packetIndex+1] * 0x100) + mPacket[packetIndex+2];
            uint16_t stopPlayFieldNumber = (mPacket[packetIndex+3] * 0x100) + mPacket[packetIndex+4];
            mFieldNumber = startPlayFieldNumber;
            cLog::log (LOGINFO, fmt::format ("- paramClipDefinition:{:x} start:{:x} stop:{:x}",
                                             mPacket[packetIndex], startPlayFieldNumber, stopPlayFieldNumber));
            packetIndex += 5;

            // send acknowledge
            startPacket (kCommandAcknowledge);
            addUint8 (kParamStatus);
            addUint8 (kParamStatusAck);
            txPacket();

            break;
          }
          //}}}
          default:
            cLog::log (LOGERROR, fmt::format ("unexpected command {:x}", mPacket[1]));
            break;
        }
      }
    }
  }

private:
  uint16_t mFieldNumber = 0;
};
//}}}

int main (int numArgs, char** args) {

  // set command line switches and defaults
  string usePortName;
  eMode mode = eSlave;
  eLogLevel logLevel = LOGINFO;
  //{{{  guess colour console availabilty
  bool mono = true;

  #ifdef _WIN32
    if (!IsWindows10OrGreater())
      mono = false;
  #else
    mono = false;
  #endif
  //}}}
  //{{{  parse commandLine params
  for (int i = 1; i < numArgs; i++) {
    string param = args[i];
    if (param == "log1")
      logLevel = LOGINFO1;
    else if (param == "log2")
      logLevel = LOGINFO2;
    else if (param == "log3")
      logLevel = LOGINFO3;
    else if (param == "mono")
      mono = true;
    else if (param == "colour")
      mono = false;
    else if (param == "loop")
      mode = eLoopback;
    else if (param == "master")
      mode = eMaster;
    else if (param == "slave")
      mode = eSlave;
    else
      usePortName = param;
  }
  //}}}

  // init logging
  if (mono)
    cLog::disableAnsii();
  cLog::init (logLevel);
  cLog::log (LOGNOTICE, fmt::format ("ramcorder emulator - version {}", kVersion));

  // get ports
  vector <string> portNames;
  //{{{  get portList, portnames
  struct sp_port** portList;
  enum sp_return result = sp_list_ports (&portList);
  if (result != SP_OK) {
    cLog::log (LOGINFO, fmt::format ("sp_list_ports failed"));
    #ifdef _WIN32
      Sleep (2000);
    #endif
    return -1;
  }

  // get portNames
  for (int i = 0; portList[i] != NULL; i++)
    portNames.push_back (sp_get_port_name (portList[i]));

  sp_free_port_list (portList);
  //}}}
  //{{{  abort if no ports
  if (portNames.empty()) {
    cLog::log (LOGINFO, fmt::format ("sp_list_ports - no ports"));

    #ifdef _WIN32
      Sleep (2000);
    #endif

    return -1;
  }
  //}}}
  //{{{  list portNames
  for (auto& portName : portNames)
    cLog::log (LOGINFO, fmt::format ("found port {}", portName));
  //}}}

  // if no usePortName use first available
  if (usePortName.empty())
    usePortName = portNames.front();

  // run ramcorder
  switch (mode) {
    //{{{
    case eLoopback: {
      cRamcorderLoopback loopback;
      loopback.initialise (usePortName);
      loopback.go();
      break;
    }
    //}}}
    //{{{
    case eMaster: {
      cRamcorderMaster master;
      master.initialise (usePortName);
      master.go();
      break;
    }
    //}}}
    //{{{
    case eSlave: {
      cRamcorderSlave slave;
      slave.initialise (usePortName);
      slave.go();
      break;
    }
    //}}}
  }

  #ifdef _WIN32
    Sleep (2000);
  #endif

  return EXIT_SUCCESS;
}
