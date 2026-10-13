#include "harness.h"
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include "api.inc"
#include "under_test.inc"
struct endpoint {struct spray_share *share;int id,ready,socket,base,loss,packets,vanilla,data_packets;char path[4096];unsigned char *cached;size_t cached_size;unsigned char cached_hash[32];};
static struct endpoint endpoints[4];
struct queued {int from,to,reliable;size_t size;unsigned char data[SPRAY_SHARE_PACKET];};
static struct queued queue[16384];static int head,tail,bad,drop;
static int send_packet(void *context,int peer,const void *data,size_t size,int reliable){
 struct endpoint *e=context;int to=e->id?0:peer;e->packets++;
 if(((const unsigned char *)data)[13]==DATA)e->data_packets++;
 CHECK(size<=SPRAY_SHARE_PACKET,"packet exceeds tunnel MTU");
 if(e->socket>=0){struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((uint16_t)(e->base+to));
  if(!reliable&&e->loss&&e->packets%7==0)return 1;
  return sendto(e->socket,data,size,0,(struct sockaddr *)&a,sizeof(a))==(ssize_t)size;
 }
 if(!reliable&&drop&&e->packets%7==0)return 1;
 CHECK(tail-head<16384,"queue overflow");struct queued *q=&queue[tail++%16384];q->from=e->id;q->to=to;q->size=size;q->reliable=reliable;memcpy(q->data,data,size);
 if(bad&&!reliable&&((const unsigned char *)data)[13]==DATA)q->data[size-1]^=1;
 return 1;
}
static int accept_spray(void *context,int owner,struct spray_pose *pose,char name[32]){
 (void)context;if(owner<0||owner>=128||pose->bsp!=4||pose->direction[0]!=1.5f)return 0;
 snprintf(name,32,"peer%d",owner);return 1;
}
static int ready(void *context,int slot,int owner,const void *data,size_t size,const struct spray_pose *pose,const char *name,int local){
 struct endpoint *e=context;(void)slot;(void)pose;(void)local;CHECK(owner>=0&&owner<128,"bad owner");CHECK(size&&size<=SPRAY_SHARE_LIMIT,"bad size");CHECK(name[31]==0,"unterminated name");e->ready++;
 free(e->cached);e->cached=malloc(size);CHECK(e->cached,"cache allocation");memcpy(e->cached,data,size);e->cached_size=size;crypto_blake2b(e->cached_hash,32,data,size);
 if(e->path[0]){FILE *f=fopen(e->path,"wb");CHECK(f,"download path unavailable");CHECK(fwrite(data,1,size,f)==size,"short download write");CHECK(!fclose(f),"close failed");}
 return 1;
}
static int cached(void *context,const unsigned char hash[32],size_t size,void *data){struct endpoint *e=context;
 if(size!=e->cached_size||memcmp(hash,e->cached_hash,32))return 0;memcpy(data,e->cached,size);return 1;}
static void setup(void){struct spray_share_callbacks cb={send_packet,accept_spray,ready,cached};for(int i=0;i<4;i++){
 endpoints[i].id=i;endpoints[i].socket=-1;endpoints[i].share=spray_share_new(i==0,UINT64_C(0xabcdef123400)+i,cb,&endpoints[i]);CHECK(endpoints[i].share,"alloc");
 if(i)spray_share_peer(endpoints[i].share,0,1);else for(int j=1;j<4;j++)spray_share_peer(endpoints[0].share,j,1);
}}
static void pump(uint32_t now){for(int i=0;i<3;i++)spray_share_update(endpoints[i].share,now);
 while(head<tail){struct queued q=queue[head++%16384];if(q.to==3){CHECK(q.data[13]==HELLO,"image sent to vanilla peer");continue;}
  if(drop&&q.data[13]==DATA&&head<tail&&queue[head%16384].data[13]==DATA){struct queued next=queue[head%16384];queue[head%16384]=q;q=next;}
  spray_share_receive(endpoints[q.to].share,q.to?0:q.from,q.data,q.size);
  if(drop&&q.data[13]==DATA)spray_share_receive(endpoints[q.to].share,q.to?0:q.from,q.data,q.size);
 }}
int main(int argc,char **argv){const char *case_name=argc>1?argv[1]:"";setup();
 CASE("upstream-host"){
  spray_share_free(endpoints[0].share);endpoints[0].share=NULL;spray_share_free(endpoints[2].share);endpoints[2].share=NULL;
  for(uint32_t t=0;t<15000;t+=16)pump(t);
  CHECK(!spray_share_available(endpoints[1].share)&&endpoints[1].packets==3,"upstream fallback probe limit");
  struct spray_pose pose={{0,0,0},{1.5f,0,0},4};CHECK(!spray_share_publish(endpoints[1].share,1,"x",1,pose,"test"),"image sent to vanilla host");
  for(int i=0;i<4;i++)spray_share_free(endpoints[i].share);return 0;
 }
 if(!strcmp(case_name,"socket")){
  CHECK(argc==7,"socket arguments");int id=atoi(argv[2]),base=atoi(argv[3]);struct endpoint *e=&endpoints[id];e->base=base;e->loss=1;
  snprintf(e->path,sizeof(e->path),"%s",argv[5]);e->socket=socket(AF_INET,SOCK_DGRAM,0);CHECK(e->socket>=0,"socket unavailable");
  struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((uint16_t)(base+id));
  CHECK(!bind(e->socket,(struct sockaddr *)&a,sizeof(a)),"bind failed");FILE *f=fopen(argv[4],"rb");CHECK(f,"source absent");fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);void *data=malloc((size_t)length);CHECK(data&&fread(data,1,length,f)==(size_t)length,"source read");fclose(f);
  struct spray_pose pose={{0,0,0},{1.5f,0,0},4};int published=0,loops=atoi(argv[6]);
  for(int n=0;n<loops;n++){
   unsigned char bytes[SPRAY_SHARE_PACKET+1];struct sockaddr_in sender;socklen_t len=sizeof(sender);ssize_t count;
   spray_share_update(e->share,(uint32_t)n*16);
   while((count=recvfrom(e->socket,bytes,sizeof(bytes),MSG_DONTWAIT,(struct sockaddr *)&sender,&len))>0){int peer=ntohs(sender.sin_port)-base;spray_share_receive(e->share,id?0:peer,bytes,(size_t)count);}
   if(id==1&&!published&&spray_share_available(e->share)){CHECK(spray_share_publish(e->share,1,data,length,pose,"peer1"),"publish failed");published=1;}
   usleep(1000);
  }
  CHECK(e->ready>0,"peer never received PNG");printf("peer=%d ready=%d packets=%d loss=1/7\n",id,e->ready,e->packets);close(e->socket);free(data);
 }else{
  for(uint32_t t=0;t<200;t+=16)pump(t);
  CHECK(spray_share_available(endpoints[1].share)&&spray_share_available(endpoints[2].share),"handshake failed");
  struct spray_pose pose={{0,0,0},{1.5f,0,0},4};size_t size=SPRAY_SHARE_LIMIT;unsigned char *data=malloc(size+1);CHECK(data,"alloc");for(size_t i=0;i<=size;i++)data[i]=(unsigned char)(i*13);
  CASE("limit"){CHECK(SPRAY_SHARE_LIMIT==5u*1024u*1024u,"wrong limit");CHECK(sizeof(((struct incoming *)0)->bits)*8u*CHUNK>=SPRAY_SHARE_LIMIT,"receive bitmap too small");CHECK(!spray_share_publish(endpoints[1].share,1,data,size+1,pose,"test"),"over-limit admitted");}
  CASE("invalid-placement"){pose.direction[0]=99;CHECK(spray_share_publish(endpoints[1].share,1,data,32,pose,"test"),"local publish");for(uint32_t t=200;t<5000;t+=16)pump(t);CHECK(!endpoints[0].ready&&!endpoints[2].ready,"invalid placement relayed");free(data);return 0;}
  CASE("loss")drop=1;
  CASE("corruption")bad=1;
  CASE("upstream")spray_share_peer(endpoints[0].share,3,1);
  CASE("wrong-token")size=32;
  CHECK(spray_share_publish(endpoints[1].share,1,data,size,pose,"test"),"publish failed");
  CASE("disconnect")spray_share_peer(endpoints[0].share,1,0);
  CASE("wrong-token"){endpoints[1].share->peers[0].token++;}
  for(uint32_t t=200;t<(!strcmp(case_name,"wrong-token")?4000u:300000u);t+=16)pump(t);
  if(!strcmp(case_name,"corruption")||!strcmp(case_name,"disconnect")||!strcmp(case_name,"wrong-token"))CHECK(!endpoints[0].ready&&!endpoints[2].ready,"invalid transfer admitted");
  else{CHECK(endpoints[0].ready>=1&&endpoints[2].ready>=1,"share incomplete: ready %d,%d; receive %u; sender next %u,slot %d, pending %u; capable %d",endpoints[0].ready,endpoints[2].ready,endpoints[0].share->peers[1].receive.count,endpoints[1].share->peers[0].next,endpoints[1].share->peers[0].sending,endpoints[1].share->peers[0].pending,endpoints[0].share->peers[1].capable);int slot=slot_for(endpoints[2].share,1);CHECK(endpoints[2].share->images[slot].size==size&&!memcmp(endpoints[2].share->images[slot].data,data,size),"download differs");
   CASE("cache"){int before=endpoints[2].packets;pose.origin[1]=.2f;CHECK(spray_share_publish(endpoints[1].share,1,data,size,pose,"test"),"repeat");for(uint32_t t=300000;t<310000;t+=16)pump(t);CHECK(endpoints[2].ready>=2&&endpoints[2].packets-before<20,"cache retransferred entire PNG");}
   CASE("cache-other-owner"){
    int before=endpoints[0].data_packets+endpoints[1].data_packets+endpoints[2].data_packets;
    pose.origin[1]=.4f;CHECK(spray_share_publish(endpoints[2].share,2,data,size,pose,"other"),"other owner publish");
    for(uint32_t t=300000;t<310000;t+=16)pump(t);
    CHECK(endpoints[0].data_packets+endpoints[1].data_packets+endpoints[2].data_packets==before,"same content from another owner was retransferred");
    int a=-1,b=-1;for(int i=0;i<SPRAY_SHARE_SLOTS;i++){if(endpoints[0].share->images[i].owner==1&&endpoints[0].share->images[i].data)a=i;if(endpoints[0].share->images[i].owner==2&&endpoints[0].share->images[i].data)b=i;}
    CHECK(a>=0&&b>=0&&endpoints[0].share->images[a].id!=endpoints[0].share->images[b].id,"cached host relay reused a client id");
   }
   CASE("cache-reconnect"){
    int before=endpoints[0].data_packets;struct spray_share_callbacks cb={send_packet,accept_spray,ready,cached};
    spray_share_free(endpoints[2].share);endpoints[2].share=spray_share_new(0,UINT64_C(0x88776655),cb,&endpoints[2]);
    spray_share_peer(endpoints[0].share,2,0);spray_share_peer(endpoints[0].share,2,1);spray_share_peer(endpoints[2].share,0,1);
    for(uint32_t t=300000;t<310000;t+=16)pump(t);
    CHECK(endpoints[2].ready>=2,"persistent cache did not apply placement after reconnect");CHECK(endpoints[0].data_packets==before,"persistent cache retransferred image bytes");
   }
   CASE("host-restart"){
    spray_share_free(endpoints[0].share);struct spray_share_callbacks cb={send_packet,accept_spray,ready};endpoints[0].share=spray_share_new(1,UINT64_C(0x987654321),cb,&endpoints[0]);
    for(int i=1;i<4;i++)spray_share_peer(endpoints[0].share,i,1);int before=endpoints[2].ready;
    for(uint32_t t=300000;t<600000;t+=16)pump(t);CHECK(endpoints[2].ready>before,"host key/map reset did not recover");
   }
  }free(data);
 }
 for(int i=0;i<4;i++)spray_share_free(endpoints[i].share);return 0;
}
