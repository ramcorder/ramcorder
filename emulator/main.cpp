// main.cpp
//{{{  includes
#ifdef _WIN32
  #include <windows.h>
#endif

#include <array>
#include <vector>
#include <string>

#include "utils/cLog.h"
#include "serialPort/serialPort.h"

using namespace std;
//}}}
//{{{
int check (enum sp_return result)
{
  char* error_message;

  switch (result) {
    case SP_OK:
    default:
      return result;

    case SP_ERR_ARG:
      cLog::log (LOGERROR, fmt::format ("Error: Invalid argument"));
      #ifdef _WIN32
        Sleep (2000);
      #endif
      abort();

    case SP_ERR_FAIL:
      error_message = sp_last_error_message();
      cLog::log (LOGERROR, fmt::format ("Error: Failed: {}", error_message));
      sp_free_error_message(error_message);
      #ifdef _WIN32
        Sleep (2000);
      #endif
      abort();

    case SP_ERR_SUPP:
      cLog::log (LOGERROR, fmt::format ("Error: Not supported"));
      #ifdef _WIN32
        Sleep (2000);
      #endif
      abort();

    case SP_ERR_MEM:
      cLog::log (LOGERROR, fmt::format ("Error: Couldn't allocate memory"));
      #ifdef _WIN32
        Sleep (2000);
      #endif
      abort();
  }
}
//}}}

constexpr bool kNtsc = false;
//{{{
class cRamcorderMaster {
public:
  cRamcorderMaster() {}
  ~cRamcorderMaster() {}

  //{{{
  void init() {

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
  }
  //}}}

  //{{{
  uint16_t getSize() {
    return mClipLength;
  }
  //}}}

  //{{{
  bool selectProtocol (uint8_t protocol) {
    return true;
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
  bool go (bool playGo, bool recordGo, uint16_t goDelay) {
    return true;
  }
  //}}}
  //{{{
  bool stop() {
    return true;
  }
  //}}}

private:
  //{{{
  void startPacket (char command) {
  }
  //}}}
  //{{{
  void addUint8 (uint8_t value) {
  }
  //}}}
  //{{{
  void addChar (char c) {
  }
  //}}}
  //{{{
  void addWord (int16_t value) {
  }
  //}}}

  //{{{
  bool sendCommand (bool waitForReply) {
    return true;
  }
  //}}}

  uint8_t mStatus = 0;
  array <uint8_t,4> mTimecode = {0};
  uint16_t mFieldNum = 0;
  uint16_t mClipLength = 0;

  array <uint8_t, 16> mPacket = {0};
};
//}}}

int main (int numArgs, char** args)
{
  vector <string> portNames;

  string usePortName;
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
    else
      usePortName = param;
  }
  //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, fmt::format ("ramcorder emulator - serial port test"));

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

  //{{{  open and configure port
  cLog::log (LOGINFO, fmt::format ("configure port {} 9600 8N1, no flow control", usePortName));
  struct sp_port* port;
  check (sp_get_port_by_name (usePortName.c_str(), &port));
  check (sp_open (port, SP_MODE_READ_WRITE));
  check (sp_set_baudrate (port, 9600));
  check (sp_set_bits (port, 8));
  check (sp_set_parity( port, SP_PARITY_NONE));
  check (sp_set_stopbits (port, 1));
  check (sp_set_flowcontrol (port, SP_FLOWCONTROL_NONE));
  //}}}

  //{{{  tx port
  const char* data = "Hello - this is some looped back data! 1234567890";
  size_t size = strlen (data);

  cLog::log (LOGINFO, fmt::format ("tx - {} {}:bytes on port:{}", data, size, sp_get_port_name (port)));
  unsigned int timeout = 1000; // 1 second
  int bytesSent = check (sp_blocking_write (port, data, size, timeout));
  if (bytesSent == size)
    cLog::log (LOGINFO, fmt::format ("tx {}:bytes ok", size));
  else
    cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes sent", bytesSent, size));
  //}}}

  // about to receive the data on port
  cLog::log (LOGINFO, fmt::format ("about to rx {}:bytes on port:{}", size, sp_get_port_name (port)));

  //{{{  rx port
  // allocate buffer to receive data, 1 second timeout
  char* buf = (char*)malloc (size + 1);
  int bytesRead = check (sp_blocking_read (port, buf, size, timeout));
  if (bytesRead == size)
    cLog::log (LOGINFO, fmt::format ("rx {}:bytes successfully", size));
  else
    cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes received", bytesRead, size));

  // Check if we received the same data we sent
  buf[bytesRead] = '\0';
  cLog::log (LOGINFO, fmt::format ("rx - {}", buf));

  // Free receive buffer
  free (buf);
  //}}}

  //{{{  close,free port
  check (sp_close (port));
  sp_free_port (port);
  //}}}

  #ifdef _WIN32
    Sleep (2000);
  #endif

  return EXIT_SUCCESS;
}
