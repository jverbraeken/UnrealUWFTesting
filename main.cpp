#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <iomanip>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
using namespace std;

class Capture;

unsigned int numGestures;
vector<string> gestureNames;
vector<unsigned int> numDimensions;
vector<vector<unsigned int>> numCaptures;
vector<vector<vector<Capture>>> captures;

class Capture {
private:
    long start_time;
    long end_time;
    long start_value;
    long end_value;
    unsigned char state;

public:
    Capture(long start_time, long end_time, long start_value, long end_value, unsigned char state) : start_time(
            start_time), end_time(end_time), start_value(start_value), end_value(end_value), state(state) {}

public:
    long getStart_time() const {
        return start_time;
    }

    long getEnd_time() const {
        return end_time;
    }

    long getStart_value() const {
        return start_value;
    }

    long getEnd_value() const {
        return end_value;
    }

    unsigned char getState() const {
        return state;
    }
};

int main() {
    cout << "UWF testing" << endl;
    cout << "Testing file \"test.uwf\"" << endl << endl;
    struct stat buffer;
    ifstream in;
    in.open("E:\\Documents\\Git\\UnrealUWFTesting\\asdf.uwf");
    if (!in.is_open()) {
        return 0;
    }
    in >> numGestures;
    for (int i = 0; i < numGestures; i++) {
        string gesture_text;
        unsigned int _numDimensions;
        string _gestureName;
        in >> gesture_text;
        in >> _numDimensions;
        in >> _gestureName;
        numDimensions.push_back(_numDimensions);
        gestureNames.push_back(_gestureName);
        numCaptures.push_back(vector<unsigned int>());
        captures.push_back(vector<vector<Capture>>());
        for (int j = 0; j < numDimensions.back(); j++) {
            string dimension_text;
            unsigned int _numCaptures;
            in >> dimension_text;
            in >> _numCaptures;
            numCaptures.back().push_back(_numCaptures);
            captures.back().push_back(vector<Capture>());
            for (int k = 0; k < numCaptures.back().back(); k++) {
                string capture_text;
                long start_time;
                long end_time;
                float start_value;
                float end_value;
                unsigned int state;
                in >> capture_text >> start_time >> end_time >> start_value >> end_value >> state;
                captures.back().back().push_back(Capture(start_time, end_time, start_value, end_value, state));
            }
        }
    }
        in.close();
}
#pragma clang diagnostic pop