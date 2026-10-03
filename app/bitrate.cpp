#include "bitrate.h"
#include <algorithm>
#include <cmath>
void BitrateController::reset(int value) { ceiling_=current_=std::max(1,value); lossWindows_=delayWindows_=healthy_=0; smoothed_=-1; references_.clear(); }
void BitrateController::target(int value) { ceiling_=std::max(1,value); current_=std::min(current_,ceiling_); }
int BitrateController::update(double loss,double rtt,bool valid) {
    if(!valid || !std::isfinite(loss) || !std::isfinite(rtt) || loss<0 || loss>1 || rtt<0)return current_;
    smoothed_=smoothed_<0?rtt:.8*smoothed_+.2*rtt;
    references_.push_back(rtt);if(references_.size()>30)references_.pop_front();
    double baseline=*std::min_element(references_.begin(),references_.end());
    double growth=smoothed_-baseline;
    lossWindows_=loss>=.02?lossWindows_+1:0;
    delayWindows_=growth>std::max(50.,baseline*.25)?delayWindows_+1:0;
    bool congested=loss>=.05 || lossWindows_>=2 || delayWindows_>=3;
    if(congested) {current_=std::max(std::min(300,ceiling_),int(current_*.85));healthy_=lossWindows_=delayWindows_=0;}
    else if(loss<.01 && growth<=20) {if(++healthy_>=3){current_=std::min(ceiling_,current_+std::max(100,current_/20));healthy_=0;}}
    else healthy_=0;
    return current_;
}
