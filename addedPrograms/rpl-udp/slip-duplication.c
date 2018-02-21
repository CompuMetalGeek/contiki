#include "net/ip/uip.h"
#include "dev/slip.h"
#include "dev/uart1.h"
#include <string.h>

#define UIP_IP_BUF        ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

static uip_ipaddr_t last_sender;
/*---------------------------------------------------------------------------*/
static void
slip_input_callback(void)
{
    uint8_t temp[UIP_BUFSIZE];

    // new buffer to hold data temporarily
    memcpy(&temp,&uip_buf,uip_len);
    // correct location in uip_buf
    memcpy(&uip_buf[UIP_LLH_LEN],&temp,uip_len);
    // process packet and send reply if necessary
    PRINTF("slip_input: len=%u (src=", uip_len);
    PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
    PRINTF(" dst=");
    PRINT6ADDR(&UIP_IP_BUF->destipaddr);
    PRINTF(")   \n");
    tcpip_input();

    /* Save the last sender received over SLIP to avoid bouncing the
     packet back if no route is found */
    uip_ipaddr_copy(&last_sender, &UIP_IP_BUF->srcipaddr);
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
    slip_arch_init(BAUD2UBR(115200));
    process_start(&slip_process, NULL);
    slip_set_input_callback(slip_input_callback);
}
/*---------------------------------------------------------------------------*/
static int
output(void)
{
    uip_ipaddr_t ownAddress;
    uip_gethostaddr(&ownAddress);
    if(uip_ipaddr_cmp(&last_sender, &UIP_IP_BUF->srcipaddr)) {
        /* Do not bounce packets back over SLIP if the packet was received
       over SLIP */
        PRINTF("slip-bridge: Destination off-link but no route src=");
        PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
        PRINTF(" dst=");
        PRINT6ADDR(&UIP_IP_BUF->destipaddr);
        PRINTF("\n");
    } else {//if(uip_ipaddr_cmp(&ownAddress, &UIP_IP_BUF->)){
        PRINTF("slip_output: len=%u (src=", uip_len);
        PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
        PRINTF(" dst=");
        PRINT6ADDR(&UIP_IP_BUF->destipaddr);
        PRINTF(")\n");
        slip_send();
    }
    return 0;
}
const struct uip_fallback_interface rpl_interface = {
    init, output
};
/*---------------------------------------------------------------------------*/
