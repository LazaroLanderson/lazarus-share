#pragma once
#include <deque>
class BitrateController {
public:
    explicit BitrateController(int target=8000) { reset(target); }
    void reset(int target);
    void target(int value);
    void resetFeedback(){lossWindows_=delayWindows_=healthy_=0;smoothed_=-1;references_.clear();}
    int update(double loss, double rttMs, bool valid);
    int value() const { return current_; }
private:
    int ceiling_=8000,current_=8000,lossWindows_=0,delayWindows_=0,healthy_=0;
    double smoothed_=-1;
    std::deque<double> references_;
};
