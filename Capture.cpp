#include <string>
#include <iostream>
#include "Capture.h"

Capture::Capture() {}

Capture::Capture(long long start_time, unsigned char state, float start_value, int momentCounterAtStart)
	: start_time(start_time), state(state), start_value(start_value), momentCounterAtStart(momentCounterAtStart) {
}

Capture::Capture(long long start_time, long long end_time, float start_value, float end_value, unsigned char state, int momentCounterAtStart)
	: start_time(start_time), end_time(end_time), start_value(start_value), end_value(end_value), state(state), momentCounterAtStart(momentCounterAtStart) {}

void Capture::end(long long end_time, float end_value) {
	this->end_time = end_time;
	this->end_value = end_value;
	std::string tmp;
	if (state == '0')
	{
		tmp = "RISING";
	} else if (state == '1')
	{
		tmp = "FALLING";
	} else
	{
		tmp = "STABLE";
	}
	std::cout << "Capture - " << tmp << ": " << this->start_time << ", " << this->end_time << ", " << this->start_value << ", "
		<< this->end_value << ", " << static_cast<unsigned>(this->state) << std::endl;
}

long long Capture::getStart_time() const {
	return this->start_time;
}

long long Capture::getEnd_time() const {
	return this->end_time;
}

float Capture::getStart_value() const {
	return this->start_value;
}

float Capture::getEnd_value() const {
	return this->end_value;
}

unsigned char Capture::getState() const {
	return this->state;
}

int Capture::getMomentCounterAtStart() const
{
	return this->momentCounterAtStart;
}