/*
 *    ||          ____  _ __                           
 * +------+      / __ )(_) /_______________ _____  ___ 
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie simulation firmware
 *
 * Copyright (c) 2018  Eric Goubault, Sylvie Putot, Franck Djeumou
 *             Cosynux , LIX , France
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * socketlink.c: Socket implementation of the CRTP link
 */
#define DEBUG_MODULE "SOCKET_LINK"
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "socketlink.h"
#include "crtp.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "queuemonitor.h"
#include "semphr.h"
#include "debug.h"
#include "cfassert.h"

#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

#include <errno.h>

// Global variables for socket management
static struct sockaddr_in myaddr;
static struct sockaddr_in remaddr;
static socklen_t addrlen;
static int fd;
static struct pollfd fds[1];


static bool isInit = false;
static xQueueHandle crtpPacketDelivery;

static int socketlinkSendPacket(CRTPPacket *p);
static int socketlinkSetEnable(bool enable);
static int socketlinkReceiveCRTPPacket(CRTPPacket *p);


static struct crtpLinkOperations socketlinkOp =
{
  .setEnable         = socketlinkSetEnable,
  .sendPacket        = socketlinkSendPacket,
  .receivePacket     = socketlinkReceiveCRTPPacket,
};

static CRTPPacket p;
static uint8_t socket_buff[33];

static void socketlinkTask(void *param)
{

  DEBUG_PRINT("socketlinkTask started\n");
  int recvlen;
  while(1)
  {
    while (poll(&fds[0], (sizeof(fds[0]) / sizeof(fds[0])), -1) < 0){
      // Failed when no data because of EINTR caused by FreeRTOS linux port)
      // Workaround is to put a delay for the moment
      vTaskDelay(M2T(1));
    }

    // Check if any data was received
    if (fds[0].revents & POLLIN){
      // Fetch a socket data
      recvlen = recvfrom(fd, p.raw, sizeof(p.raw), 0, (struct sockaddr *)&remaddr, &addrlen);
      if (recvlen > 0){
        p.size = recvlen - 1; // We remove the header size
        xQueueSend(crtpPacketDelivery, &p, 0);
      } else {
        DEBUG_PRINT("error : %s \n" , strerror(errno));
      }
    } 
  }

}

static int socketlinkReceiveCRTPPacket(CRTPPacket *p)
{
  if (xQueueReceive(crtpPacketDelivery, p, M2T(100)) == pdTRUE)
  {
    return 0;
  }

  return -1;
}

static int socketlinkSendPacket(CRTPPacket *p)
{
  int dataSize;

  // Data size up to 31 Bytes
  ASSERT(p->size < 32); 

  if (p->size > CRTP_MAX_DATA_SIZE)
    return 0;

  dataSize = p->size + 1;
  memcpy(&(socket_buff[0]) , p->raw , dataSize);
  dataSize = sendto(fd, socket_buff, dataSize, 0, (struct sockaddr *)&remaddr, addrlen);
  // DEBUG_PRINT("sending : port: %d channel: %d data:%d %d %d size: %d\n" , p->port , p->channel, p->data[0], p->data[1], p->data[2], p->size);
  // Shutdown if not able to send ???
  return (dataSize > 0);
}

static int socketlinkSetEnable(bool enable)
{
  return 0;
}

/*
 * Public functions
 */
void socketlinkInit()
{
  if(isInit)
    return;

  // Create UDP socket
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
    isInit = false;
    DEBUG_PRINT("cannot create socket\n");
    ASSERT_FAILED();
    return;
  }
  DEBUG_PRINT("Create socket succeed \n");

  // Let the OS pick this instance port and address
  memset((char *)&myaddr, 0, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myaddr.sin_port = htons(0);

  if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0){
    isInit =false;
    DEBUG_PRINT("Binding failed \n");
    ASSERT_FAILED();
    return;
  }
  DEBUG_PRINT("Binding succeed \n");

  // Initialize destination address (gazebo handler server)
  memset((char *)&remaddr, 0, sizeof(remaddr));\
  if (strcmp(address_host, "INADDR_ANY") == 0){
    remaddr.sin_addr.s_addr =  htonl(INADDR_ANY);
  } else if (inet_addr(address_host) == INADDR_NONE){
    isInit = false;
    return;
  } else{
    remaddr.sin_addr.s_addr = inet_addr(address_host); // Set the address if valid
  }
  remaddr.sin_port = htons(crtp_port); // Set the port
  //Initialize addrlen
  addrlen =  sizeof(remaddr);

  // initialize the poll structure
  fds[0].fd = fd;
  fds[0].events = POLLIN;

  // Wait for validation by the SITL instance
  DEBUG_PRINT("Waiting for connection with gazebo ... \n");
  bool commInitialized = false;
  int recvlen;
  uint8_t count;
  const uint8_t max_count = 10;

  p.header = 0xF3;  // send null header for identification process
  p.size = 0;       // No data when doing identification process
  while(!commInitialized)
  {
    count = 0;
    socketlinkSendPacket(&p);
    while(count < max_count){
      recvlen = recvfrom(fd, socket_buff, sizeof(socket_buff), 0, (struct sockaddr *)&remaddr, &addrlen);
      if (recvlen == 1 && socket_buff[0] == 0xF3){
          commInitialized = true;
          break;
      }
      count++;
      vTaskDelay(M2T(10));
    }
  }

  DEBUG_PRINT("Connection established with gazebo \n");


  // Create RX queue and start socketlink task
  crtpPacketDelivery = xQueueCreate(160, sizeof(CRTPPacket));
  DEBUG_QUEUE_MONITOR_REGISTER(crtpPacketDelivery);

  BaseType_t xReturned = xTaskCreate(socketlinkTask, USBLINK_TASK_NAME,
              USBLINK_TASK_STACKSIZE, NULL, USBLINK_TASK_PRI-1, NULL);

  if (xReturned != pdPASS) {
    DEBUG_PRINT("Failed to create socketlinkTask\n");
    ASSERT_FAILED();
    return;
  }

  DEBUG_PRINT("Successfuly created socketlinkTask\n");

  isInit = true;
}

bool socketlinkTest()
{
  return isInit;
}

struct crtpLinkOperations * socketlinkGetLink()
{
  return &socketlinkOp;
}