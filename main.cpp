#pragma comment(lib, "Ws2_32.lib")
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <winsock2.h>
#include "CircularBuffer.h"

#include "Moment.h"
#include "Capture.h"


#define BUFLEN 512
#define PORT 55056

#define STATE_RISING '0'
#define STATE_FALLING '1'
#define STATE_STABLE '2'

#define LOG_INCOMING 0

#define MINIMUM_MOMENTS_FOR_GESTURE 15
#define MOMENT_BUFFER_SIZE 100

using namespace std;

static const int FALLING_THRESHOLD[] = { -4, -0.5 };
static const int RISING_THRESHOLD[] = { 4, 0.5 };

static const unsigned char COMTP_SENSOR_DATA = 1;
static const unsigned char COMTP_SHAKING_STARTED = 2;
static const unsigned char COMTP_SHAKING_STOPPED = 3;

// TODO: do we actually need this?
static const double CAPTURES_MATCH_GESTURE_THRESHOLD = 1.5;
static const double DTW_MATCH_GESTURE_THRESHOLD = 5;

static const double MOMENT_LOW_PASS_FILTER = 0.5;

unsigned int numGestures;
// per gesture
vector<string> preGestureNames;
// per gesture per dimension per sample
vector<vector<vector<float>*>*> preGestureValues;
// per resizement per gesture per dimension per sample
vector<vector<vector<vector<float>*>*>*> resizedAveragedPreGestureValues;
vector<vector<unsigned int>*> numPreCaptures;
vector<vector<CircularBuffer<Capture*>*>*> preCaptures; // the only reason that we make it a CircularBuffer is because we have to convert a CircularBuffer to a vector otherwise

// for each dimension for each time a moment
vector<CircularBuffer<Moment *> *> * momentBuffer;
// per dimension per resizement an average
vector<vector<float>*> momentBufferAverage;
vector<int> state;
// for each dimension for each time a capture
vector<CircularBuffer<Capture*>*>* captureBuffer;
bool isFirstSample = true;
int momentCounter = 0;
vector<double> momentDifference(6);

int maxPreCaptures = 0;




//////// HELPER FUNCTIONS




unsigned char getByteFromBuffer(char *buf, int offset) {
	unsigned char result;
	memcpy(&result, &buf[offset], 1);

	return result;
}

float getFloatFromBuffer(char *buf, int offset) {
	float tmp;
	float result;
	memcpy(&tmp, &buf[offset], 4);

	char *floatToConvert = reinterpret_cast<char *>(&tmp);
	char *returnFloat = reinterpret_cast<char *>(&result);
	returnFloat[0] = floatToConvert[3];
	returnFloat[1] = floatToConvert[2];
	returnFloat[2] = floatToConvert[1];
	returnFloat[3] = floatToConvert[0];

	return result;
}

long long getLongFromBuffer(char *buf, int offset) {
	long long tmp;
	long long result;
	memcpy(&tmp, &buf[offset], 8);

	char *longToConvert = reinterpret_cast<char *>(&tmp);
	char *returnLong = reinterpret_cast<char *>(&result);
	returnLong[0] = longToConvert[7];
	returnLong[1] = longToConvert[6];
	returnLong[2] = longToConvert[5];
	returnLong[3] = longToConvert[4];
	returnLong[4] = longToConvert[3];
	returnLong[5] = longToConvert[2];
	returnLong[6] = longToConvert[1];
	returnLong[7] = longToConvert[0];

	return result;
}

vector<float>* resizeVector(int newSize, vector<float>* originalVector)
{
	double scalingFactor = double(originalVector->size()) / double(newSize);
	vector<float>* result = new vector<float>;
	for (int k = 0; k < newSize; k++)
	{
		double avg = 0;
		if (k > 0) { // just for efficiency
			double firstFactor = ceil(k * scalingFactor - 0.0001) - k * scalingFactor;
			if (abs(firstFactor) > 0.0001) {
				avg += firstFactor * (*originalVector)[floor(k * scalingFactor)];
			}
		}
		for (int l = ceil(k * scalingFactor - 0.0001); l < floor((k + 1) * scalingFactor); l++)
		{
			avg += (*originalVector)[l];
		}
		double lastFactor = (k + 1) * scalingFactor - floor((k + 1) * scalingFactor + 0.0001);
		if (abs(lastFactor) > 0.0001) {
			avg += lastFactor * (*originalVector)[floor((k + 1) * scalingFactor + 0.0001)];
		}
		avg /= scalingFactor;
		result->push_back(float(avg));
	}
	return result;
}

float addToAverage(float average, int size, float value)
{
	return (size * average + value) / (size + 1);
}




//////// GRT




int squaredDistance(float f1, float f2) {
	return (f1 - f2) * (f1 - f2);
}

bool executeDTW(const int gesture, const int dimension, const int sizeTillEnd, const int offsetFromEnd) {
	const int size = sizeTillEnd - offsetFromEnd;
	if (size < MINIMUM_MOMENTS_FOR_GESTURE)
	{
		return false;
	}

	const float momentAverage = (*(momentBufferAverage[dimension]))[size];

	if (size > preGestureValues[gesture]->back()->size())
	{
		auto newSize = preGestureValues[gesture]->back()->size();

		vector<vector<double>> d(newSize, vector<double>(newSize));
		vector<vector<double>> D(newSize, vector<double>(newSize));

		vector<Moment*> moments = (*momentBuffer)[dimension]->getData();
		vector<float> momentValues = vector<float>(moments.size());
		for (auto i = 0; i < moments.size(); i++)
		{
			momentValues[i] = moments[i]->getValue();
		}
		vector<float>::const_iterator first = momentValues.begin() + ((*momentBuffer)[dimension]->getNumValuesInBuffer() - sizeTillEnd);
		vector<float>::const_iterator last = momentValues.end() - offsetFromEnd;
		vector<float> subMomentBuffer(first, last);
		vector<float>* resizedMomentBuffer = resizeVector(newSize, &subMomentBuffer);
		vector<float> resizedAveragedMomentBuffer(newSize);

		for (auto i = 0; i < newSize; i++)
		{
			resizedAveragedMomentBuffer[i] = (*resizedMomentBuffer)[i] - momentAverage;
		}

		for (auto i = 0; i < newSize; i++)
		{
			const auto preGestureValue = (*(*(*resizedAveragedPreGestureValues[newSize])[gesture])[dimension])[i];
			for (auto j = 0; j < newSize; j++)
			{
				d[i][j] = squaredDistance(preGestureValue, resizedAveragedMomentBuffer[j]);
			}
		}

		D[0][0] = d[0][0];

		for (auto i = 1; i < newSize; i++)
		{
			D[i][0] = d[i][0] + D[i - 1][0];
			D[0][i] = d[0][i] + D[0][i - 1];
		}

		for (auto i = 1; i < newSize; i++)
		{
			for (auto j = 1; j < newSize; j++)
			{
				auto distance = min(min(D[i - 1][j], D[i - 1][j - 1]), D[i][j - 1]);
				distance += d[i][j];
				D[i][j] = distance += d[i][j];
			}
		}
		cout << "Total distance = " << D.back().back() << endl;
		if (D.back().back() < 30000)
		{
			return true;
		}
		return false;
	}


	auto newSize = size;

	vector<vector<double>> d(newSize, vector<double>(newSize));
	vector<vector<double>> D(newSize, vector<double>(newSize));

	vector<Moment*> moments = (*momentBuffer)[dimension]->getData();
	vector<float> momentValues = vector<float>(moments.size());
	for (auto i = 0; i < moments.size(); i++)
	{
		momentValues[i] = moments[i]->getValue();
	}
	vector<float>::const_iterator first = momentValues.begin() + ((*momentBuffer)[dimension]->getNumValuesInBuffer() - sizeTillEnd);
	vector<float>::const_iterator last = momentValues.end() - offsetFromEnd;
	vector<float> subMomentBuffer(first, last);

	vector<float> averagedMomentBuffer;
	for (auto i = 0; i < newSize; i++)
	{
		averagedMomentBuffer.push_back(subMomentBuffer[i] - momentAverage);
	}

	for (auto i = 0; i < newSize; i++)
	{
		const float preGestureValue = (*(*(*resizedAveragedPreGestureValues[newSize])[gesture])[dimension])[i];
		for (auto j = 0; j < newSize; j++)
		{
			d[i][j] = squaredDistance(preGestureValue, averagedMomentBuffer[j]);
		}
	}

	D[0][0] = d[0][0];

	for (auto i = 1; i < newSize; i++)
	{
		D[i][0] = d[i][0] + D[i - 1][0];
		D[0][i] = d[0][i] + D[0][i - 1];
	}

	for (auto i = 1; i < newSize; i++)
	{
		for (auto j = 1; j < newSize; j++)
		{
			auto distance = min(min(D[i - 1][j], D[i - 1][j - 1]), D[i][j - 1]);
			distance += d[i][j];
			D[i][j] = distance;
		}
	}
	cout << "Total distance = " << D.back().back() << endl;
	if (D.back().back() < 30000)
	{
		return true;
	}
	return false;
}




//////// UWF




void checkCaptureBuffer() {
	for (int i = 0; i < numGestures; i++) {
		for (int j = 0; j < 6; j++) {
			const int numCaptures = (*captureBuffer)[j]->getNumValuesInBuffer();

			bool matched = false;
			vector<vector<int>> dtwTemplate((*numPreCaptures[i])[j] + 1, vector<int>(numCaptures + 1));
			dtwTemplate[0][0] = 0;
			for (int k = 1; k < (*numPreCaptures[i])[j] + 1; k++) {
				dtwTemplate[k][0] = 999999;
			}
			for (int k = 1; k < numCaptures + 1; k++) {
				dtwTemplate[0][k] = 999999;
			}

			// TODO this should be reversed to improve efficiency
			for (int captureOffset = 0; captureOffset <= numCaptures - int((*numPreCaptures[i])[j]); captureOffset++) {
				vector<vector<int>> dtw = dtwTemplate; // copying is more efficient than constructing a new one
				int momentOffset = -1;
				int offsetFromEnd = 0;
				for (int k = 0; k < (*numPreCaptures[i])[j]; k++) {
					const char preCaptureState = (*(*preCaptures[i])[j])[k]->getState();
					for (int l = 0; l < numCaptures - captureOffset; l++) {
						const int captureNum = l + captureOffset;
						const char captureState = (*(*captureBuffer)[j])[captureNum]->getState();
						const int captureStartMoment = (*(*captureBuffer)[j])[captureNum]->getMomentCounterAtStart();

						if (captureStartMoment < momentCounter - MOMENT_BUFFER_SIZE)
						{
							dtw[k + 1][l + 1] = 999999;
							continue;
						}
						if (momentOffset == -1)
						{
							momentOffset = momentCounter - captureStartMoment;
							if (momentOffset < MINIMUM_MOMENTS_FOR_GESTURE)
							{
								goto endCaptureOffsetLoop;
							}
						}

						auto cost = -1;

						// Last Capture is STABLE
						if (captureState == STATE_STABLE && captureNum == numCaptures - 1)
						{
							cost = 0;

							float lastValue = (*(*momentBuffer)[j])[(*momentBuffer)[j]->getNumValuesInBuffer() - 1]->getValue();
							bool success = false;
							int type = j < 3 ? 0 : 1;

							if ((*(*captureBuffer)[j])[captureNum - 1]->getState() == STATE_FALLING)
							{
								for (int m = (*momentBuffer)[j]->getNumValuesInBuffer() - 2; m >= 0; m--)
								{
									if (lastValue - (*(*momentBuffer)[j])[m]->getValue() <= FALLING_THRESHOLD[type])
									{
										offsetFromEnd = (*momentBuffer)[j]->getNumValuesInBuffer() - m;
										success = true;
										break;
									}
								}
							}
							if ((*(*captureBuffer)[j])[captureNum - 1]->getState() == STATE_RISING)
							{
								for (int m = (*momentBuffer)[j]->getNumValuesInBuffer() - 2; m >= 0; m--)
								{
									if (lastValue - (*(*momentBuffer)[j])[m]->getValue() >= RISING_THRESHOLD[type])
									{
										offsetFromEnd = (*momentBuffer)[j]->getNumValuesInBuffer() - m;
										success = true;
										break;
									}
								}
							}

							if (!success)
							{
								cout << "Something went wrong while determining offsetFromEnd" << endl;
							}
						}
						else if (preCaptureState == captureState) {
							cost = 0;
						}
						else if (preCaptureState == STATE_FALLING) {
							if (captureState == STATE_STABLE) {
								cost = 2;
							}
							else if (captureState == STATE_RISING) {
								cost = 5;
							}
						}
						else if (preCaptureState == STATE_STABLE) {
							if (captureState == STATE_FALLING) {
								cost = 2;
							}
							else if (captureState == STATE_RISING) {
								cost = 2;
							}
						}
						else if (preCaptureState == STATE_RISING) {
							if (captureState == STATE_FALLING) {
								cost = 5;
							}
							else if (captureState == STATE_STABLE) {
								cost = 2;
							}
						}
						dtw[k + 1][l + 1] = cost + min(dtw[k][l + 1], min(dtw[k + 1][l], dtw[k][l]));
					}
				}
				if (momentOffset == -1)
				{
					if (j == 2)
					{
						cout << "Gesture matched!!!" << endl;
						for (int l = 0; l < 6; l++) {
							(*captureBuffer)[l]->reset();
						}
						goto endGestureLoop;
					}
					matched = true;
					goto endCaptureOffsetLoop;
				}
				// After reversing, save the start time of the first capture that matches the gesture and the end time of the last gesture
				// scaling factor = (end time - start time) / (gesture start time - gesture end time)
				if (double(dtw[(*numPreCaptures[i])[j]][numCaptures - captureOffset]) / double(numCaptures - captureOffset) < CAPTURES_MATCH_GESTURE_THRESHOLD) {
					cout << "UWF matched.......   " << endl;
					if (momentCounter - momentOffset < MINIMUM_MOMENTS_FOR_GESTURE)
					{
						cout << "Too few moments for a gesture..." << endl;
					}
					else if (executeDTW(i, j, momentOffset, offsetFromEnd)) {
						cout << "--------------- Gesture partly matched!" << endl;
						if (j == 2)
						{
							cout << "-------------------------- Gesture matched!!!" << endl;
							for (int l = 0; l < 6; l++) {
								(*captureBuffer)[l]->reset();
							}
							goto endGestureLoop;
						}
						matched = true;
						goto endCaptureOffsetLoop;
					}
				}
			}
		endCaptureOffsetLoop:
			int _;
			if (matched == false)
			{
				break;
			}
		}
	endDimensionLoop:
		int _;
	}
endGestureLoop:
	int _;
}

void goToRising(int dimension, Moment *moment, bool evaluate) {
	if ((*captureBuffer)[dimension]->getNumValuesInBuffer() > 0) {
		(*captureBuffer)[dimension]->getBack()->end(
			moment->getTime(),
			moment->getValue());
	}
	(*captureBuffer)[dimension]->push_back(new Capture(moment->getTime(),
		STATE_RISING,
		moment->getValue(),
		momentCounter));
	state[dimension] = STATE_RISING;
	cout << dimension << " - State is RISING" << endl;
	if (evaluate) {
		checkCaptureBuffer();
	}
}

void goToStable(int dimension, Moment *moment, bool evaluate) {
	if ((*captureBuffer)[dimension]->getNumValuesInBuffer() > 0) {
		(*captureBuffer)[dimension]->getBack()->end(
			moment->getTime(),
			moment->getValue());
	}
	(*captureBuffer)[dimension]->push_back(new Capture(moment->getTime(),
		STATE_STABLE,
		moment->getValue(),
		momentCounter));
	state[dimension] = STATE_STABLE;
	cout << dimension << " - State is STABLE" << endl;
	if (evaluate) {
		checkCaptureBuffer();
	}
}

void goToFalling(int dimension, Moment *moment, bool evaluate) {
	if ((*captureBuffer)[dimension]->getNumValuesInBuffer() > 0) {
		(*captureBuffer)[dimension]->getBack()->end(
			moment->getTime(),
			moment->getValue());
	}
	(*captureBuffer)[dimension]->push_back(new Capture(moment->getTime(),
		STATE_FALLING,
		moment->getValue(),
		momentCounter));
	state[dimension] = STATE_FALLING;
	cout << dimension << " - State is FALLING" << endl;
	if (evaluate) {
		checkCaptureBuffer();
	}
}

void executeUWF(vector<Moment*> moments, bool evaluate = true) {
	if (!isFirstSample) {
		for (int i = 1; i < 2; i++) {
			const unsigned int type = i < 3 ? 0u : 1u;
			const float value_0 = moments[i]->getValue() - ((*momentBuffer)[i]->getBack()->getValue() - 360);
			const float value_1 = moments[i]->getValue() - (*momentBuffer)[i]->getBack()->getValue();
			const float value_2 = moments[i]->getValue() - ((*momentBuffer)[i]->getBack()->getValue() + 360);
			const float abs_value_0 = abs(value_0);
			const float abs_value_1 = abs(value_1);
			const float abs_value_2 = abs(value_2);
			float diff;
			if (abs_value_0 < abs_value_1 && abs_value_0 < abs_value_2)
			{
				diff = value_0;
			}
			else if (abs_value_1 < abs_value_0 && abs_value_1 < abs_value_2)
			{
				diff = value_1;
			}
			else
			{
				diff = value_2;
			}
			//cout << diff << "     | " << moments[i]->getValue() << endl;
			if (state[i] == STATE_RISING) {
				if (diff < FALLING_THRESHOLD[type])
				{
					goToFalling(i, moments[i], evaluate);
				}
				else if (diff < RISING_THRESHOLD[type]) {
					momentDifference[i] = MOMENT_LOW_PASS_FILTER * momentDifference[i] + (1 - MOMENT_LOW_PASS_FILTER) * diff;
					if (momentDifference[i] < RISING_THRESHOLD[type]) {
						goToStable(i, moments[i], evaluate);
					}
				}
				else
				{
					momentDifference[i] = RISING_THRESHOLD[type] * 6;
				}
			}
			else if (state[i] == STATE_FALLING) {
				if (diff >= RISING_THRESHOLD[type])
				{
					goToRising(i, moments[i], evaluate);
				}
				else if (diff >= FALLING_THRESHOLD[type]) {
					momentDifference[i] = MOMENT_LOW_PASS_FILTER * momentDifference[i] + (1 - MOMENT_LOW_PASS_FILTER) * diff;
					if (momentDifference[i] >= FALLING_THRESHOLD[type]) {
						goToStable(i, moments[i], evaluate);
					}
				}
				else
				{
					momentDifference[i] = FALLING_THRESHOLD[type] * 3;
				}
			}
			else if (state[i] == STATE_STABLE) {
				if (!isFirstSample) {
					if (diff >= RISING_THRESHOLD[type])
					{
						goToRising(i, moments[i], evaluate);
					}
					else if (diff < FALLING_THRESHOLD[type])
					{
						goToFalling(i, moments[i], evaluate);
					}
				}
			}
		}
	}
	else {
		isFirstSample = false;
	}
}





//////// FILE READING






void useGRT() {
	preGestureValues = vector<vector<vector<float>*>*>();

	cout << "Testing file \"walking.grt\"" << endl << endl;
	ifstream in;
	in.open("C:\\Users\\jverb\\Documents\\Git\\UnrealUWFTesting\\walking.grt");
	if (!in.is_open()) {
		cout << "File cannot be opened" << endl;
	}

	string word;

	//Check to make sure this is a file with the Training File Format
	in >> word;
	if (word != "GRT_LABELLED_TIME_SERIES_CLASSIFICATION_DATA_FILE_V1.0") {
		cout << "loadDatasetFromFile(std::string filename) - Failed to find file header!" << std::endl;
	}

	//Get the name of the dataset
	in >> word;
	if (word != "DatasetName:") {
		cout << "loadDatasetFromFile(std::string filename) - failed to find DatasetName!" << std::endl;
	}
	in >> word;

	in >> word;
	if (word != "InfoText:") {
		cout << "loadDatasetFromFile(std::string filename) - failed to find InfoText!" << std::endl;
	}

	//Load the info text
	in >> word;
	while (word != "NumDimensions:") {
		in >> word;
	}

	//Get the number of dimensions in the training data
	if (word != "NumDimensions:") {
		cout << "loadDatasetFromFile(std::string filename) - Failed to find NumDimensions!" << std::endl;
	}
	in >> word;

	//Get the total number of training examples in the training data
	in >> word;
	if (word != "TotalNumTrainingExamples:") {
		cout << "loadDatasetFromFile(std::string filename) - Failed to find TotalNumTrainingExamples!" << std::endl;
	}
	in >> word;

	//Get the total number of classes in the training data
	in >> word;
	if (word != "NumberOfClasses:") {
		cout << "loadDatasetFromFile(std::string filename) - Failed to find NumberOfClasses!" << std::endl;
	}
	in >> numGestures;

	//Get the total number of classes in the training data
	in >> word;
	if (word != "ClassIDsAndCounters:") {
		cout << "loadDatasetFromFile(std::string filename) - Failed to find ClassIDsAndCounters!" << std::endl;
	}

	for (UINT i = 0; i < numGestures; i++) {
		// Skip because a gesture cannot have multiple samples
		int a, b;
		in >> a;
		in >> b;
	}

	int a;
	in >> a;

	/*if (useExternalRanges) {
		externalRanges.resize(numDimensions);
		for (UINT i = 0; i<externalRanges.size(); i++) {
			in >> externalRanges[i].minValue;
			in >> externalRanges[i].maxValue;
		}
	}*/

	//Get the main training data
	in >> word;
	if (word != "LabelledTimeSeriesTrainingData:") {
		in.close();
		cout << "loadDatasetFromFile(std::string filename) - Failed to find LabelledTimeSeriesTrainingData!" << std::endl;
	}

	//Load each of the time series
	for (UINT x = 0; x < numGestures; x++) {
		preGestureValues.push_back(new vector<vector<float>*>());
		for (int i = 0; i < 6; i++) {
			preGestureValues.back()->push_back(new vector<float>());
		}
		numPreCaptures.push_back(new vector<unsigned int>());
		UINT classLabel = 0;
		UINT timeSeriesLength = 0;
		UINT duration = 0;

		in >> word;
		if (word != "************TIME_SERIES************") {
			in.close();
			cout << "loadDatasetFromFile(std::string filename) - Failed to find TimeSeries Header!" << std::endl;
		}

		in >> word;
		if (word != "ClassID:") {
			in.close();
			cout << "loadDatasetFromFile(std::string filename) - Failed to find ClassID!" << std::endl;
		}
		in >> classLabel;

		in >> word;
		if (word != "TimeSeriesLength:") {
			in.close();
			cout << "loadDatasetFromFile(std::string filename) - Failed to find TimeSeriesLength!" << std::endl;
		}
		in >> timeSeriesLength;

		in >> word;
		if (word != "Duration:") {
			in.close();
			cout << "loadDatasetFromFile(std::string filename) - Failed to find Duration!" << std::endl;
		}
		in >> 

		in >> word;
		if (word != "TimeSeriesData:") {
			in.close();
			cout << "loadDatasetFromFile(std::string filename) - Failed to find TimeSeriesData!" << std::endl;
		}

		long long tmpTimer = 0;

		for (UINT i = 0; i < timeSeriesLength; i++) {
			vector<Moment*> moments = vector<Moment*>();
			for (UINT j = 0; j < 3; j++) {
				float y;
				in >> y;
				(*preGestureValues.back())[j]->push_back(y);
				moments.push_back(new Moment(y, tmpTimer));
			}
			for (UINT j = 3; j < 6; j++) {
				float y;
				in >> y;
				(*preGestureValues.back())[j]->push_back(y / 100.f);
				moments.push_back(new Moment(y / 100.f, tmpTimer));
			}
			executeUWF(moments, false);
			for (int j = 0; j < 6; j++) {
				(*momentBuffer)[j]->push_back(moments[j]);
			}
			tmpTimer += 50;
		}
		preCaptures.push_back(captureBuffer);
		for (int i = 0; i < 6; i++) {
			numPreCaptures.back()->push_back((*captureBuffer)[i]->getNumValuesInBuffer());
		}

		// Make a new one because the old one is now used in preCaptures; modifying it will modify preCaptures
		captureBuffer = new vector<CircularBuffer<Capture*>*>();
		for (int i = 0; i < 6; i++) {
			captureBuffer->push_back(new CircularBuffer<Capture*>(10));
		}
		for (int i = 0; i < 6; i++) {
			(*momentBuffer)[i]->reset();
		}
		state.clear();
		for (int i = 0; i < 6; i++) {
			state.push_back(STATE_STABLE);
		}
		isFirstSample = true;

		for (int i = MINIMUM_MOMENTS_FOR_GESTURE; i < preGestureValues.back()->back()->size() + 1; i++)
		{
			resizedAveragedPreGestureValues[i]->push_back(new vector<vector<float>*>);
			for (int j = 0; j < 6; j++) {
				vector<float>* resizement = resizeVector(i, (*preGestureValues.back())[j]);
				float average = 0.f;
				for (float value : *resizement)
				{
					average += value;
				}
				average /= resizement->size();
				for (auto i = 0; i < resizement->size(); i++)
				{
					(*resizement)[i] -= average;
				}
				resizedAveragedPreGestureValues[i]->back()->push_back(resizement);
			}
		}

	}
	in.close();
}





//////// MAIN





int main() {
	cout << "UWF testing" << endl;

	momentBuffer = new vector<CircularBuffer<Moment*>*>();
	// for each dimension for each time a moment
	for (int i = 0; i < 6; i++) {
		momentBuffer->push_back(new CircularBuffer<Moment *>(MOMENT_BUFFER_SIZE));
	}

	for (int i = 0; i < 6; i++) {
		state.push_back(STATE_STABLE);
	}

	/*for (int i = 0; i < numPreCaptures.size(); i++) {
		for (int j = 0; j < numPreCaptures[i]->size(); j++) {
			maxPreCaptures = max(maxPreCaptures, (*numPreCaptures[i])[j]);
		}
	}*/

	captureBuffer = new vector<CircularBuffer<Capture*>*>();
	for (int i = 0; i < 6; i++) {
		captureBuffer->push_back(new CircularBuffer<Capture*>(10));
	}

	for (int i = 0; i < MOMENT_BUFFER_SIZE + 1; i++)
	{
		resizedAveragedPreGestureValues.push_back(new vector<vector<vector<float>*>*>);
	}

	for (int i = 0; i < 6; i++) {
		momentBufferAverage.push_back(new vector<float>(MOMENT_BUFFER_SIZE + 1, 0));
	}

	useGRT();

	SOCKET s;
	struct sockaddr_in server, si_other;
	char buf[BUFLEN];
	WSADATA wsa;

	int slen = sizeof(si_other);

	//Initialise winsock
	printf("Initialising Winsock...\n");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		printf("Failed. Error Code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//Create a socket
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		printf("Could not create socket : %d", WSAGetLastError());
	}
	printf("Socket created.\n");

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

	//Bind
	if (bind(s, reinterpret_cast<struct sockaddr *>(&server), sizeof(server)) == SOCKET_ERROR) {
		printf("Bind failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	puts("Bind done");








	//keep listening for data
	while (true) {
		//printf("Waiting for data...");
		fflush(stdout);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		//try to receive some data, this is a blocking call
		if (recvfrom(s, buf, BUFLEN, 0, reinterpret_cast<struct sockaddr *>(&si_other), &slen) == SOCKET_ERROR) {
			printf("recvfrom() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}

		//print details of the client/peer and the data received
		//printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		//printf("Data: %s\n", buf);
		unsigned char request = getByteFromBuffer(buf, 0);
		if (request == COMTP_SENSOR_DATA) {
			float xrot = getFloatFromBuffer(buf, 1);
			float yrot = getFloatFromBuffer(buf, 5);
			float zrot = getFloatFromBuffer(buf, 9);
			long long rot_timestamp = getLongFromBuffer(buf, 13);

			float xacc = getFloatFromBuffer(buf, 21);
			float yacc = getFloatFromBuffer(buf, 25);
			float zacc = getFloatFromBuffer(buf, 29);
			long long acc_timestamp = getLongFromBuffer(buf, 33);

			momentCounter++;

			vector<Moment*> moments = vector<Moment*>();
			moments.push_back(new Moment(xrot, rot_timestamp));
			moments.push_back(new Moment(yrot, rot_timestamp));
			cout << yrot << endl;
			moments.push_back(new Moment(zrot, rot_timestamp));
			moments.push_back(new Moment(xacc, acc_timestamp));
			moments.push_back(new Moment(yacc, acc_timestamp));
			moments.push_back(new Moment(zacc, acc_timestamp));

			for (auto i = 0; i < 6; i++)
			{
				int numValues = (*momentBuffer)[i]->getNumValuesInBuffer();
				for (auto j = numValues; j >= 2; j--)
				{
					(*(momentBufferAverage[i]))[j] = addToAverage((*(momentBufferAverage[i]))[j - 1], j, moments[i]->getValue());
				}
				(*(momentBufferAverage[i]))[1] = moments[i]->getValue();
			}

#if LOG_INCOMING != 0
			printf("Rotation: %f, %f, %f, %lld - Acceleration: %f, %f, %f, %lld - Touch: %f, %f, %c, %lld\n",
				xrot,
				yrot,
				zrot,
				rot_timestamp,
				xacc,
				yacc,
				zacc,
				acc_timestamp,
				xtouch,
				ytouch,
				touch_state,
				touch_timestamp);
#endif

			executeUWF(moments);
			for (int i = 0; i < 6; i++) {
				(*momentBuffer)[i]->push_back(moments[i]);
			}
		}
		else if (request == COMTP_SHAKING_STARTED)
		{
			// Do nothing
		}
		else if (request == COMTP_SHAKING_STOPPED)
		{
			// Do nothing
		}
	}
}
