#pragma once

class Capture {
private:
    long long start_time;
    long long end_time;
    float start_value;
    float end_value;
    unsigned char state;
	int momentCounterAtStart;

public:
    Capture();

    Capture(long long start_time, unsigned char state, float start_value, int momentCounterAtStart);

    Capture(long long start_time, long long end_time, float start_value, float end_value, unsigned char state, int momentCounterAtStart);

public:
    void end(long long end_time, float end_value);

    long long getStart_time() const;

    long long getEnd_time() const;

    float getStart_value() const;

    float getEnd_value() const;

    unsigned char getState() const;

	int getMomentCounterAtStart() const;
};
