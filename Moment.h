#pragma once

class Moment {
private:
    float value;
    long long time;

public:
    Moment();

    Moment(float valueIn, long long timeIn);

public:
    float getValue() const;

    long long getTime() const;
};