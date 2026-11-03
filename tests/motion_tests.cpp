#include "motion.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(){
 auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
 try{
  require(motion::ease(-1)==0&&motion::ease(2)==1,"easing escaped bounds");
  motion::Tween t;t.target(1,100,200);double last=0;
  for(int now=100;now<=300;++now){double next=t.value(now);require(next>=last&&next>=0&&next<=1,"switch overshot or reversed");last=next;}
  require(t.value(300)==1&&!t.moving(300),"animation did not settle exactly");
  t.snap(0);t.target(1,100,200);auto before=t.value(150);t.target(0,150,200);
  require(std::abs(before-t.value(150))<1e-12,"rapid pointer reversal introduced a visual jump");
  require(t.value(350)==0&&!t.moving(350),"reversed animation kept running at idle");
  motion::Tween slow,fast;slow.target(1,0,220);fast.target(1,0,220);
  for(int frame=0;frame<180;frame+=7)(void)fast.value(frame);
  require(slow.value(180)==fast.value(180),"animation speed depends on repaint count");
  t.snap(1);require(t.value(0)==1&&!t.moving(0),"initial state incorrectly animates a fake change");
  std::cout<<"PASS: bounded motion, exact settling, interrupted-hover continuity, frame independence and stable initial state\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
