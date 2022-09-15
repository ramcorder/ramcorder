// main.cpp
//{{{  includes
#ifdef _WIN32
  #include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

vector <string> portNames;

int main (int numArgs, char** args)
{
  eLogLevel logLevel = LOGINFO;
  string usePortName;
  //{{{  parse params
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
  cLog::log (LOGNOTICE, fmt::format ("ramcorder emulator - serial ports test"));

  //{{{  get portList
  struct sp_port** portList;

  enum sp_return result = sp_list_ports (&portList);
  if (result != SP_OK) {
    cLog::log (LOGINFO, fmt::format ("sp_list_ports failed"));
    #ifdef _WIN32
      Sleep (2000);
    #endif
    return -1;
  }
  //}}}
  //{{{  get portNames
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

  if (usePortName.empty())
    usePortName = portNames.front();


  // Open and configure port
  cLog::log (LOGINFO, fmt::format ("Looking for port {}", usePortName));
  struct sp_port* port;
  check (sp_get_port_by_name (usePortName.c_str(), &port));

  cLog::log (LOGINFO, fmt::format ("Opening port, 9600 8N1, no flow control"));
  check (sp_open (port, SP_MODE_READ_WRITE));
  check (sp_set_baudrate (port, 9600));
  check (sp_set_bits (port, 8));
  check (sp_set_parity( port, SP_PARITY_NONE));
  check (sp_set_stopbits (port, 1));
  check (sp_set_flowcontrol (port, SP_FLOWCONTROL_NONE));

  // send some data on port and receive it back
  const char* data = "Hello - this is some looped back data! 1234567890";
  size_t size = strlen (data);

  // Send data
  cLog::log (LOGINFO, fmt::format ("sending {} ({} bytes) on port {}", data, size, sp_get_port_name (port)));
  unsigned int timeout = 1000; // 1 second
  int bytesSent = check (sp_blocking_write (port, data, size, timeout));
  if (bytesSent == size)
    cLog::log (LOGINFO, fmt::format ("sent {} bytes successfully", size));
  else
    cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes sent", bytesSent, size));

  // about to receive the data on port
  cLog::log (LOGINFO, fmt::format ("receiving {} bytes on port {}", size, sp_get_port_name (port)));

  // Allocate buffer to receive data, 1 second timeout
  char* buf = (char*)malloc (size + 1);
  int bytesRead = check (sp_blocking_read (port, buf, size, timeout));
  if (bytesRead == size)
    cLog::log (LOGINFO, fmt::format ("received {} bytes successfully", size));
  else
    cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes received", bytesRead, size));

  // Check if we received the same data we sent
  buf[result] = '\0';
  cLog::log (LOGINFO, fmt::format ("received {}", buf));

  // Free receive buffer
  free (buf);

  // close port and free resources
  check (sp_close (port));
  sp_free_port (port);

  #ifdef _WIN32
    Sleep (2000);
  #endif

  return EXIT_SUCCESS;
}
