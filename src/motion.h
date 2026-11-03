#pragma once
#include <algorithm>
namespace motion {
// Absolute-time interpolation remains smooth even when the message loop misses a frame.
inline double ease(double t){t=std::clamp(t,0.0,1.0);return 1.0-(1.0-t)*(1.0-t)*(1.0-t);}
struct Tween {
 double from=0,to=0,started=0,duration=160;
 double value(double now)const{return from+(to-from)*ease((now-started)/duration);}
 bool moving(double now)const{return from!=to&&now<started+duration;}
 void target(double next,double now,double milliseconds=160){
  if(next==to)return;
  from=value(now);to=next;started=now;duration=std::max(1.0,milliseconds);
 }
 void snap(double next){from=to=next;}
};
}
