#include "Moment.h"

Moment::Moment(float valueIn, long long timeIn) : value(valueIn), time(timeIn) {}

long long Moment::getTime() const {
    return this->time;
}

float Moment::getValue() const {
    return this->value;
}