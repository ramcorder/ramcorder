// main.cpp - ramcorder emulator
//{{{  includes
#ifdef _WIN32
  // temporary, for Sleep, to see the failures
  #include <windows.h>
#endif

#include <cstdint>
#include <array>
#include <vector>
#include <string>

#include "utils/cLog.h"
#include "serialPort/serialPort.h"

using namespace std;
//}}}

enum eMode { eLoopback, eMaster, eSlave };
//{{{  constexpr
constexpr uint8_t kPacketMax = 16;

constexpr bool kNtsc = false;

constexpr char kCommandStatusReport = 'S';
constexpr char kCommandExtraStatus = 's';
constexpr char kCommandAcknowledge = 'A';
constexpr char kCommandSelectProtocol = 'K';
constexpr char kCommandRecord = 'R';
constexpr char kCommandView = 'V';
constexpr char kCommandGo = 'G';
constexpr char kCommandPosition = 'P';

constexpr uint8_t kParamStatus = 1;
constexpr uint8_t kParamClipSelect = 2;
constexpr uint8_t kParamClipLength = 4;
constexpr uint8_t kParamFrameNnumber = 5;
constexpr uint8_t kParamClipDefinition = 7;
constexpr uint8_t kParamTimecode = 8;
constexpr uint8_t kParamId = 9;
constexpr uint8_t kParamProtocol = 10;
constexpr uint8_t kParamGoDelay = 11;

constexpr char kParamIdDpb = 'P';

constexpr uint8_t kProtocolDpbGrab = 0;
//}}}

//{{{
class cRamcorderBase {
public:
  cRamcorderBase() {}
  //{{{
  virtual ~cRamcorderBase() {
    // close,free port
    if (mPort) {
      spCheck (sp_close (mPort));
      sp_free_port (mPort);
    }
  }
  //}}}

  //{{{
  void initialise (const string& portName) {

    mPortName = portName;
    cLog::log (LOGINFO, fmt::format ("initialise port {} 9600 8N1, no flow control", mPortName));

    mStatus = 0;

    if (kNtsc)
      mClipLength = 383;
    else
      mClipLength = 323;

    mTimecode [0] = 0;
    mTimecode[1] = 0;
    mTimecode[2] = 0;
    mTimecode[3] = 0;

    mFieldNum = 0;

    spCheck (sp_get_port_by_name (mPortName.c_str(), &mPort));
    spCheck (sp_open (mPort, SP_MODE_READ_WRITE));
    spCheck (sp_set_baudrate (mPort, 9600));
    spCheck (sp_set_bits (mPort, 8));
    spCheck (sp_set_parity( mPort, SP_PARITY_NONE));
    spCheck (sp_set_stopbits (mPort, 1));
    spCheck (sp_set_flowcontrol (mPort, SP_FLOWCONTROL_NONE));
  }
  //}}}

  //{{{
  uint16_t getSize() {
    return mClipLength;
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
  void startCommand (char command) {

    mPacket[0] = 2;
    mPacket[1] = uint8_t (command);
  }
  //}}}
  //{{{
  void addUint8 (uint8_t value) {

    mPacket[0]++;

    if (mPacket[0] >= kPacketMax)
      cLog::log (LOGERROR, fmt::format ("addUint8 - too many bytes in packet"));
    else
      mPacket[mPacket[0]] = value;
  }
  //}}}
  //{{{
  void addChar (char ch) {

    addUint8 (uint8_t (ch));
  }
  //}}}
  //{{{
  void addWord (int16_t value) {

    addUint8 (value >> 8);
    addUint8 (value & 0xFF);
  }
  //}}}
  //{{{
  bool sendCommand (bool waitForReply) {

    // flush rx buffer

    // send command packet

    if (waitForReply) {
      // wait for reply
    }

    return true;
  }
  //}}}
  //{{{
  bool rxReply() {
  // wait for and rx reply

    return true;
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

private:
  string mPortName;
  struct sp_port* mPort;

  array <uint8_t, kPacketMax> mPacket = { 0 };

  uint8_t mStatus = 0;
  array <uint8_t, 4> mTimecode = { 0 };
  uint16_t mFieldNum = 0;
  uint16_t mClipLength = 0;
};
//}}}
//{{{
class cRamcorderLoopback : public cRamcorderBase {
public:
  //{{{
  cRamcorderLoopback() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder loopback"));
  }
  //}}}
  virtual ~cRamcorderLoopback() {}

  //{{{
  void go() {

    // tx port
    string test = "Loopback data - 01234567890 abcdefghijklmnpqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    cLog::log (LOGINFO, fmt::format ("setup to tx {}:bytes on port:{}", test.size(), sp_get_port_name (getPort())));
    cLog::log (LOGINFO, fmt::format ("tx - {}", test));

    const unsigned int timeout = 1000; // 1 second
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
class cRamcorderMaster : public cRamcorderBase {
public:
  //{{{
  cRamcorderMaster() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder master"));
  }
  //}}}
  virtual ~cRamcorderMaster() {}

  //{{{
  void go() {
  // simulate a typical ramcorder sequence

    selectProtocol (kProtocolDpbGrab);

    // some sort of ramcorder exercises
    //selectClip (...)
    //start (...)
    //stop()
  }
  //}}}

private:
  //{{{
  bool selectProtocol (uint8_t protocol) {

    startCommand (kCommandStatusReport);
    addUint8 (kParamId);
    addChar (kParamIdDpb);
    addUint8 (kParamTimecode);
    addUint8 (decToBcd (12));
    addUint8 (0);
    addUint8 (0);
    addUint8 (0);
    bool ok = sendCommand (true);

    startCommand (kCommandSelectProtocol);
    addUint8 (kParamProtocol);
    addUint8 (protocol);
    ok = sendCommand (true);

    return ok;
  }
  //}}}
  //{{{
  bool position (bool waitForReply, uint16_t field, bool fieldMode, bool field2Dom, bool reverse) {
    return true;
  }
  //}}}
  //{{{
  bool selectClip (bool inputClip, bool fieldMode, bool field2Dom,
                   uint16_t startField, uint16_t finishField,
                   bool play_reverse, bool play_bounce,
                   bool rec_loop, bool play_loop,
                   bool cine_expand, bool cine_compress) {
    return true;
  }
  //}}}
  //{{{
  bool pollField (uint16_t field, bool field2dom) {
    return false;
  }
  //}}}
  //{{{
  bool start (bool playGo, bool recordGo, uint16_t goDelay) {
    return true;
  }
  //}}}
  //{{{
  bool stop() {
    return true;
  }
  //}}}

};
//}}}
//{{{
class cRamcorderSlave : public cRamcorderBase {
public:
  //{{{
  cRamcorderSlave() {
    cLog::log (LOGINFO, fmt::format ("creating ramcorder slave"));
  }
  //}}}
  virtual ~cRamcorderSlave() {}

  void go() {
    // listen for command

    // action command, possible blackmagic stuff

    // send info/acknowledge
  }
};
//}}}

int main (int numArgs, char** args) {

  // set command line switches
  string usePortName;
  eMode mode = eLoopback;
  eLogLevel logLevel = LOGINFO;
  //{{{  parse commandLine params
  for (int i = 1; i < numArgs; i++) {
    string param = args[i];
    if (param == "log1")
      logLevel = LOGINFO1;
    else if (param == "log2")
      logLevel = LOGINFO2;
    else if (param == "log3")
      logLevel = LOGINFO3;
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
  cLog::init (logLevel);
  cLog::log (LOGNOTICE, fmt::format ("ramcorder emulator"));

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
    }
    //}}}
    //{{{
    case eSlave: {
      cRamcorderSlave slave;
      slave.initialise (usePortName);
      slave.go();
    }
    //}}}
  }

  #ifdef _WIN32
    Sleep (2000);
  #endif

  return EXIT_SUCCESS;
}
