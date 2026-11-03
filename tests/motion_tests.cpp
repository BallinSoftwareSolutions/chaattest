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
  motion::Scroll scroll;scroll.target(76,0);
  const double position=scroll.value(35),speed=scroll.speed(35);
  scroll.target(152,35);
  require(std::abs(scroll.value(35)-position)<1e-10,"wheel burst jumped position");
  require(std::abs(scroll.speed(35)-speed)<1e-10,"wheel burst reset momentum");
  double prior=scroll.value(35);
  for(int now=36;now<=535;++now){double next=scroll.value(now);require(next>=prior&&next<=152,"scroll overshot");prior=next;}
  require(scroll.value(535)==152&&!scroll.moving(535),"scroll did not stop");
  scroll.snap(0);scroll.target(76,0);double turn=scroll.value(40);
  scroll.target(0,40);require(scroll.value(40)==turn,"reversal jumped");
  for(int now=41;now<=540;++now){double next=scroll.value(now);require(next>=0&&next<=turn,"reversal escaped bounds");turn=next;}
  scroll.snap(0);scroll.target(9.5,0);require(scroll.value(500)==9.5,"fractional wheel input lost");
  std::cout<<"PASS: bounded motion, exact settling, interrupted-hover continuity, frame independence and stable initial state\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
