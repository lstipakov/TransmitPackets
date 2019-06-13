#include "pch.h"

#pragma warning(disable:4127)
#pragma warning(disable:4244)
#pragma warning(disable:4267)

#define HOST "8.8.8.8"
#define PORT 40007

#define DATAGRAMS_COUNT 100

using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned short>;

VOID
socketCreate(SOCKET *socket, ADDRESS_FAMILY *addressFamily, LPFN_TRANSMITPACKETS *transmitPackets) {
  // Return value of WSAStartup
  WSADATA             wsaData;

  // Temporarily used to represent the destination IP address
  SOCKADDR_STORAGE    destAddr;

  // Used by WSAIoctl
  DWORD               bytesReturned;

  // GUID of the TransmitPacket Winsock2 function which we will
  // use to send the traffic at the client side.
  GUID TransmitPacketsGuid = WSAID_TRANSMITPACKETS;

  // Start Winsock
  int returnValue = WSAStartup(MAKEWORD(2, 2), &wsaData);

  if (returnValue != 0) {
    printf("%s:%d - WSAStartup failed (%d)\n",
      __FILE__, __LINE__, returnValue);
    exit(1);
  }

  // First attempt to convert the string to an IPv4 address
  int sockaddrLen = sizeof(destAddr);
  destAddr.ss_family = AF_INET;
  returnValue = WSAStringToAddressA((LPSTR)HOST,
    AF_INET,
    NULL,
    (LPSOCKADDR)&destAddr,
    &sockaddrLen);


  // Set the destination port.
  SS_PORT((PSOCKADDR)&destAddr) = htons(PORT);

  // Copy the address family back to caller
  *addressFamily = destAddr.ss_family;

  // Create a UDP socket
  *socket = WSASocket(destAddr.ss_family,
    SOCK_DGRAM,
    IPPROTO_UDP,
    NULL,
    0,
    WSA_FLAG_OVERLAPPED);

  if (*socket == INVALID_SOCKET) {
    printf("%s:%d - WSASocket failed (%d)\n",
      __FILE__, __LINE__, WSAGetLastError());
    exit(1);
  }

  // Connect the new socket to the destination
  returnValue = WSAConnect(*socket,
    (PSOCKADDR)&destAddr,
    sizeof(destAddr),
    NULL,
    NULL,
    NULL,
    NULL);

  if (returnValue != NO_ERROR) {
    printf("%s:%d - WSAConnect failed (%d)\n",
      __FILE__, __LINE__, WSAGetLastError());
    exit(1);
  }

  // Query the function pointer for the TransmitPacket function
  returnValue = WSAIoctl(*socket,
    SIO_GET_EXTENSION_FUNCTION_POINTER,
    &TransmitPacketsGuid,
    sizeof(GUID),
    transmitPackets,
    sizeof(PVOID),
    &bytesReturned,
    NULL,
    NULL);

  if (returnValue == SOCKET_ERROR) {
    printf("%s:%d - WSAIoctl failed (%d)\n",
      __FILE__, __LINE__, WSAGetLastError());
    exit(1);
  }
}

int main(int argc, char *argv[])
{
  ADDRESS_FAMILY              addressFamily;
  SOCKET                      socket;
  LPFN_TRANSMITPACKETS        transmitPacketsFn;

  socketCreate(&socket, &addressFamily, &transmitPacketsFn);

  LPTRANSMIT_PACKETS_ELEMENT bufs = (LPTRANSMIT_PACKETS_ELEMENT)calloc(DATAGRAMS_COUNT, sizeof(TRANSMIT_PACKETS_ELEMENT));
  ZeroMemory(bufs, DATAGRAMS_COUNT * sizeof(TRANSMIT_PACKETS_ELEMENT));

  int bytes_generated = 0;

  random_bytes_engine rbe;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(1, 1000);
  std::vector<std::vector<unsigned char>> words;
  for (int i = 0; i < DATAGRAMS_COUNT; ++i)
  {
    std::vector<unsigned char> data(dis(gen));
    std::generate(begin(data), end(data), std::ref(rbe));
    words.push_back(data);

    bufs[i].dwElFlags = TP_ELEMENT_MEMORY | TP_ELEMENT_EOP;
    bufs[i].pBuffer = (PVOID)(words[i].data());
    bufs[i].cLength = words[i].size();

    bytes_generated += words[i].size();
  }

  WSAOVERLAPPED sendOverlapped;
  ZeroMemory(&sendOverlapped, sizeof(sendOverlapped));
  sendOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

  if (sendOverlapped.hEvent == NULL) {
    printf("%s:%d - CreateEvent failed (%d)\n",
      __FILE__, __LINE__, GetLastError());
    exit(1);
  }

  // Send the first burst of packets
  BOOL result = (*transmitPacketsFn)(socket, bufs, DATAGRAMS_COUNT, 0xFFFFFFFF, &sendOverlapped, TF_USE_KERNEL_APC);

  if (result == FALSE) {
    DWORD lastError;

    lastError = WSAGetLastError();
    if (lastError != ERROR_IO_PENDING) {
      printf("%s:%d - TransmitPackets failed (%d)\n",
	__FILE__, __LINE__, GetLastError());
      exit(1);
    }
  }

  HANDLE          waitEvents[1];
  DWORD           waitResult;

  waitEvents[0] = sendOverlapped.hEvent;

  waitResult = WaitForMultipleObjects(ARRAYSIZE(waitEvents), (PHANDLE)waitEvents, FALSE, INFINITE);

  switch (waitResult) {
    case WAIT_OBJECT_0: {
      // The transmit packet has completed its send,
      BOOL    overlappedResult;
      DWORD   bytesSent;
      DWORD   ignoredFlags;

      overlappedResult = WSAGetOverlappedResult(
	socket,
	&sendOverlapped,
	&bytesSent,
	FALSE,
	&ignoredFlags);

      if (overlappedResult == FALSE) {
	printf("%s:%d - TransmitPackets failed (%d)\n",
	  __FILE__, __LINE__, WSAGetLastError());
	exit(1);
      }
      else {
	std::cout << "Bytes generated: " << bytes_generated << ", bytes_sent " << bytesSent << std::endl;
	exit(0);
      }
      break;
    }
    default:
      // The wait call failed.
      printf("%s:%d - WaitForMultipleObjects failed (%d)\n",
	__FILE__, __LINE__, GetLastError());
      exit(1);
  }

  return 0;
}
