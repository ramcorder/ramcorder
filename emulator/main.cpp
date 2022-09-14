// main.cpp
//{{{  includes
#ifdef _WIN32
  #include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "serialPort/serialPort.h"

#include "utils/cLog.h"

using namespace std;
//}}}
//{{{
int check (enum sp_return result)
{
  char* error_message;

  switch (result) {
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

    case SP_OK:

    default:
      return result;
  }
}
//}}}

int main (int numArgs, char** args)
{
  eLogLevel logLevel = LOGINFO;
  //{{{  parse params
  for (int i = 1; i < numArgs; i++) {
    string param = args[i];
    if (param == "log1") { logLevel = LOGINFO1; }
    else if (param == "log2") { logLevel = LOGINFO2; }
    else if (param == "log3") { logLevel = LOGINFO3; }
  }
  //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, fmt::format ("serial ports"));

  struct sp_port** port_list;
  enum sp_return result = sp_list_ports (&port_list);
  if (result != SP_OK) {
    cLog::log (LOGINFO, fmt::format ("sp_list_ports failed"));
    return -1;
  }

  int i;
  for (i = 0; port_list[i] != NULL; i++) {
    struct sp_port* port = port_list[i];
    char* port_name = sp_get_port_name (port);
    cLog::log (LOGINFO, fmt::format ("found port {}", port_name));
  }
  cLog::log (LOGINFO, fmt::format ("found {} ports", i));
  cLog::log (LOGINFO, fmt::format ("freeing port list"));
  sp_free_port_list (port_list);


  // Get the port names from the command line
  if (numArgs < 2 || numArgs > 3) {
    cLog::log (LOGINFO, fmt::format ("Usage: {} <port 1> [<port 2>]", args[0]));
    #ifdef _WIN32
      Sleep (2000);
    #endif
    return -1;
  }

  int num_ports = numArgs - 1;
  char** port_names = args + 1;

  // The ports we will use
  struct sp_port* ports[2];

  // Open and configure each port
  for (int i = 0; i < num_ports; i++) {
    cLog::log (LOGINFO, fmt::format ("Looking for port {}", port_names[i]));
    check (sp_get_port_by_name (port_names[i], &ports[i]));

    cLog::log (LOGINFO, fmt::format ("Opening port"));
    check (sp_open (ports[i], SP_MODE_READ_WRITE));

    printf ("Setting port to 9600 8N1, no flow control\n");
    check (sp_set_baudrate (ports[i], 9600));
    check (sp_set_bits (ports[i], 8));
    check (sp_set_parity( ports[i], SP_PARITY_NONE));
    check (sp_set_stopbits (ports[i], 1));
    check (sp_set_flowcontrol (ports[i], SP_FLOWCONTROL_NONE));
  }

  // Now send some data on each port and receive it back
  for (int tx = 0; tx < num_ports; tx++) {
    // Get the ports to send and receive on
    int rx = num_ports == 1 ? 0 : ((tx == 0) ? 1 : 0);
    struct sp_port *tx_port = ports[tx];
    struct sp_port *rx_port = ports[rx];

    // The data we will send
    const char* data = "Hello!";
    size_t size = strlen (data);

    // We'll allow a 1 second timeout for send and receive
    unsigned int timeout = 1000;

    // On success, sp_blocking_write() and sp_blocking_read() return the number of bytes sent/received before the timeout expired
    int result;

    /* Send data. */
    cLog::log (LOGINFO, fmt::format ("sending {} ({} bytes) on port {}", data, size, sp_get_port_name (tx_port)));
    result = check (sp_blocking_write (tx_port, data, size, timeout));

    // Check whether we sent all of the data
    if (result == size)
      cLog::log (LOGINFO, fmt::format ("sent {} bytes successfully", size));
    else
      cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes sent", result, size));

    // Allocate a buffer to receive data
    char* buf = (char*)malloc (size + 1);

    // Try to receive the data on the other port
    cLog::log (LOGINFO, fmt::format ("receiving {} bytes on port {}", size, sp_get_port_name (rx_port)));
    result = check(sp_blocking_read (rx_port, buf, size, timeout));

    // Check whether we received the number of bytes we wanted
    if (result == size)
      cLog::log (LOGINFO, fmt::format ("received {} bytes successfully", size));
    else
      cLog::log (LOGINFO, fmt::format ("timed out, {}/{} bytes received", result, size));

    // Check if we received the same data we sent
    buf[result] = '\0';
    cLog::log (LOGINFO, fmt::format("received {}", buf));

    // Free receive buffer
    free (buf);
  }

  // Close ports and free resources
  for (int i = 0; i < num_ports; i++) {
    check (sp_close (ports[i]));
    sp_free_port (ports[i]);
  }

  #ifdef _WIN32
    Sleep (2000);
  #endif

  return EXIT_SUCCESS;
}
