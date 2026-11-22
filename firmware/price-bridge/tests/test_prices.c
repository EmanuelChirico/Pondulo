#include "price_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *list =
 "PRODUCT_ADD:1:Manzo:1890\r\nPRODUCT_CODE:1:0001\r\n"
 "PRODUCT_ADD:2:Pollo:690\r\nPRODUCT_CODE:2:0002\r\n"
 "PRODUCT_ADD:3:Maiale:990\r\nPRODUCT_CODE:3:0003\r\n"
 "PRODUCT_ADD:4:Agnello:999999\r\nPRODUCT_CODE:4:0004\r\n";
static uint32_t out[PRICE_COUNT];
static unsigned feed(price_parser_t *p, const char *s, uint32_t ms) {
 unsigned n=0;
 while (*s) n += price_parser_feed(p,*s++,ms,out);
 return n;
}
int main(void) {
 price_parser_t p; price_parser_init(&p);
 assert(feed(&p,"WEB_SERVER_READY\r\nIP_ADDRESS:192.168.1.2\r\n",0)==0);
 assert(feed(&p,list,100)==1);
 assert(out[0]==1890 && out[1]==690 && out[2]==990 && out[3]==999999);
 assert(feed(&p,list,200)==1); /* repeated Save */
 uint32_t old[4]; memcpy(old,out,sizeof(old));
 assert(feed(&p,"PRODUCT_ADD:1:Manzo:42\nPRODUCT_CODE:1:0001\n",300)==0);
 assert(memcmp(out,old,sizeof(old))==0); /* incomplete leaves published data */
 assert(feed(&p,"PRODUCT_ADD:2:Pollo:50\nPRODUCT_CODE:2:0002\n",2401)==0); /* timeout */
 assert(feed(&p,list,2500)==1);
 const char *invalid[]={"0","1000000","-1","+2","1.2","123x","999999999999999999999999"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
   char line[100]; snprintf(line,sizeof(line),"PRODUCT_ADD:1:Manzo:%s\nPRODUCT_CODE:1:0001\n",invalid[i]);
   assert(feed(&p,line,2600)==0); assert(p.step==0);
 }
 assert(feed(&p,"PRODUCT_ADD:1:Pollo:30\nPRODUCT_CODE:1:0001\n",2700)==0);
 assert(feed(&p,"PRODUCT_ADD:1:Manzo:30\nPRODUCT_CODE:1:0002\n",2700)==0);
 assert(feed(&p,"PRODUCT_ADD:1:Manzo:30\nPRODUCT_ADD:2:Pollo:40\n",2700)==0);
 for(unsigned i=0;i<200;++i) assert(!price_parser_feed(&p,'X',2800,out));
 assert(!price_parser_feed(&p,'\n',2800,out));
 assert(feed(&p,list,2900)==1); /* recovery after overflow */
 assert(feed(&p,"PRODUCT_ADD:1:Manzo:20",3000)==0);
 assert(!price_parser_feed(&p,'\0',3000,out));
 assert(feed(&p,"\nPRODUCT_CODE:1:0001\n",3000)==0);
 assert(feed(&p,list,3100)==1);
 price_parser_init(&p);
 /* Byte by byte, including timer wrap, must still produce one list. */
 unsigned count=0; uint32_t now=0xffffff80U;
 for(const char *s=list;*s;++s) count+=price_parser_feed(&p,*s,now++,out);
 assert(count==1);
 assert(price_total_cents(1000,1890)==1890);
 assert(price_total_cents(500,1890)==945);
 assert(price_total_cents(65535,999999)==65534934);
 puts("PASS: complete/repeated/fragmented lists, limits, malformed/order/code/name, timeout, overflow, NUL, timer wrap, price arithmetic");
 return 0;
}
