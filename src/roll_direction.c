#include "copydog.h"
#include "runtime.h"
#include "control.h"
static _Atomic uint64_t PendingDirection;
void CopydogArmRollDirection(float yaw,uint32_t tick){
 uint32_t bits;memcpy(&bits,&yaw,4);
 atomic_store_explicit(&PendingDirection,((uint64_t)bits<<32)|tick,memory_order_release);
}
void CopydogClearRollDirection(void){atomic_store_explicit(&PendingDirection,0,memory_order_release);}
float CopydogRollingAngleAt(void*raw,float original,void*accessor,uint32_t now){
 uint64_t pending=atomic_load_explicit(&PendingDirection,memory_order_acquire);
 if(!pending||(uint32_t)(now-(uint32_t)pending)>128||!CopydogEnabled||exitThread||CopyAction)return original;
 char*module=raw,*owner,*context,*graph,*requestedGraph;int id,resolved;uint64_t count;
 if(!module||!accessor||!readlpv(owner,module+8)||owner!=Player0||!owner||
    !readlpv(count,module+0x9f0)||count!=32||
    !readlpv(resolved,module+0x9dc)||resolved<0||!readlpv(id,accessor)||id!=resolved||
    !readlpv(context,module+0x10)||!context||!readlpv(graph,context+0x28)||!graph||
    !readlpv(requestedGraph,(char*)accessor+8)||requestedGraph!=graph)return original;
 float*direction=GetActorDir(owner),yaw;uint32_t bits=(uint32_t)(pending>>32);float desired,result;
 memcpy(&desired,&bits,4);
 if(!direction||!readlpv(yaw,direction+1)||!CopydogRelativeRollDegrees(desired,yaw,&result))return original;
 return result;
}
float CopydogOverrideRollingAngle(void*module,float original,void*accessor){
 return CopydogRollingAngleAt(module,original,accessor,timeGetTime());
}
