#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

int	wantedDevice =-1;

bool convertFile(std::string fileName) {
	bool result =false;
	std::string outputName =fileName +".mid";
	printf("Converting %s to %s...\n", fileName.c_str(), outputName.c_str());
	FILE* fileHandle =fopen(fileName.c_str(), "rb");
	if (fileHandle) {
		fseek(fileHandle, 0, SEEK_END);
		size_t fileSize =ftell(fileHandle);
		std::vector<uint8_t> fileData(fileSize);
		fseek(fileHandle, 0, SEEK_SET);
		fread(&fileData[0], 1, fileSize, fileHandle);
		fclose(fileHandle);
		if (fileSize >=0x018A && !memcmp(&fileData[0], "HMIMIDI", 7)) {
			uint16_t  numberOfTracks =fileData.at(0x1A) | fileData.at(0x1B) <<8;
			uint16_t  timeBase       =fileData.at(0x1C) | fileData.at(0x1D) <<8;
			uint16_t  ticksPerSecond =fileData.at(0x1E) | fileData.at(0x1F) <<8;
			uint16_t  songDuration   =fileData.at(0x20) | fileData.at(0x21) <<8;
			uint8_t*  tracksPriority =&fileData[0x0022];
			uint8_t*  tracksDevices  =&fileData[0x0042];
			uint32_t  position       =0x0186;
			std::vector<uint8_t> outputData;
			outputData.push_back('M'); outputData.push_back('T'); outputData.push_back('h'); outputData.push_back('d');
			outputData.push_back(0x0); outputData.push_back(0x0); outputData.push_back(0x0); outputData.push_back(0x6);
			outputData.push_back(0x0); outputData.push_back(0x1);
			outputData.push_back(numberOfTracks >>8 &0xFF); outputData.push_back(numberOfTracks >>0 &0xFF);
			outputData.push_back(timeBase >>8 &0xFF);       outputData.push_back(timeBase >>0 &0xFF);
			int includedTracks =0;
			for (int trackNumber =0; trackNumber <numberOfTracks; trackNumber++) {
				uint32_t trackStartPosition =position;
				uint16_t foundTrackNumber  =fileData.at(position +0) | fileData.at(position +1)<<8;
				uint16_t trackLength       =fileData.at(position +2) | fileData.at(position +3)<<8;
				uint16_t midiChannelNumber =fileData.at(position +4) | fileData.at(position +5)<<8;
				if (foundTrackNumber !=trackNumber) printf("Found track #%u differs from expected track number #%u\n", foundTrackNumber, trackNumber);
				position +=6;

				bool includeTrack =wantedDevice ==-1 || trackNumber ==0;
				std::string deviceNames ="[";
				uint8_t* trackDevices =tracksDevices +trackNumber *10;
				bool hasDeviceList =false;
				for (int i =0; i <5; i++) if (trackDevices[i *2 +1] ==0xA0) {
					hasDeviceList =true;
					uint8_t device =trackDevices[i *2 +0];
					if (device ==wantedDevice) includeTrack =true;
					switch(device) {
						case 0: deviceNames +="G"; break;
						case 2: deviceNames +="F"; break;
						case 3: deviceNames +="C"; break;
						case 4: deviceNames +="M"; break;
						case 5: deviceNames +="D"; break;
						case 6: deviceNames +="I"; break;
						case 7: deviceNames +="W"; break;
						case 10:deviceNames +="U"; break;
						default:fprintf(stderr, "Track %u: Unknown device %u\n", trackNumber, device); break;
					}
				}
				if (!hasDeviceList) includeTrack =true;
				uint16_t priority =tracksPriority[midiChannelNumber *2 +0] | tracksPriority[midiChannelNumber *2 +1] <<8;
				if (priority >9) fprintf(stderr, "Track %u: Strange priority %u\n", trackNumber, priority);
				if (priority !=0 && trackNumber >0) deviceNames +=priority +'0';
				deviceNames +="]";

				if (includeTrack) {
					outputData.push_back('M'); outputData.push_back('T'); outputData.push_back('r'); outputData.push_back('k');
					outputData.push_back(0x0); outputData.push_back(0x0); outputData.push_back(0x0); outputData.push_back(0x0);
					size_t MTrkStart =outputData.size();
	
					if (trackNumber ==0) {
						outputData.push_back(0x00);
						outputData.push_back(0xFF); outputData.push_back(0x51); outputData.push_back(0x03);
						uint32_t smfTempo =500000; // 120 bpm
						outputData.push_back(smfTempo >>16 &0xFF);
						outputData.push_back(smfTempo >> 8 &0xFF);
						outputData.push_back(smfTempo >> 0 &0xFF);
					}
					if (deviceNames.size() >2) {
						outputData.push_back(0x00);
						outputData.push_back(0xFF); outputData.push_back(0x03); outputData.push_back(deviceNames.size());
						outputData.insert(outputData.end(), deviceNames.begin(), deviceNames.end());
					}
	
					uint8_t  lastStatus =0, metaType;
					uint32_t commandLength;
					uint32_t waitTimeCumulative =0;
					uint32_t rest =0;
					bool finished =false;
					while(!finished) {
						uint32_t waitTimeRaw =0;
						int shift =0;
						while (1) {
							uint8_t c=fileData.at(position++);;
							waitTimeRaw |=(c &0x7F) <<shift;
							shift +=7;
							if (c &0x80) break;
						}
						waitTimeCumulative +=waitTimeRaw;

						/* Exclude loop-related commands except for loop start and loop end */
						if (fileData.at(position +0) >=0xB0 && fileData.at(position +0) <=0xBF && fileData.at(position +1) >=102 && fileData.at(position +1) <=119 && fileData.at(position +1) !=110 && fileData.at(position +1) !=111) {
							position +=3;
							continue;
						}

						uint32_t waitTime =(waitTimeCumulative *2 *timeBase +rest) /ticksPerSecond; /* The *2 comes from cancelling 1,000,000 microseconds and the tempo of 120 beats per minute written as 500,000 microseconds per quarter note */
						         rest     =(waitTimeCumulative *2 *timeBase +rest) %ticksPerSecond;
						int a = (waitTime & 0xf0000000) >> 28;
						int b = (waitTime & 0x0fe00000) >> 21;
						int c = (waitTime & 0x001fc000) >> 14;
						int d = (waitTime & 0x00003f80) >>  7;
						int e = (waitTime & 0x0000007f);
						if (a)  outputData.push_back(a | 0x80);
						if (a||b) outputData.push_back(b | 0x80);
						if (a||b||c) outputData.push_back(c | 0x80);
						if (a||b||c||d) outputData.push_back(d | 0x80);
						outputData.push_back(e);
						waitTimeCumulative =0;

						uint8_t command =fileData.at(position++);
						if (~command &0x80) command =lastStatus;
						if (command >=0xB0 && command <=0xBF && fileData.at(position) >=110 && fileData.at(position) <=111) { /* Loop start/end */
							std::string markerText =fileData.at(position) ==111? "Loop end": "Loop start";
							outputData.push_back(0xFF);
							outputData.push_back(0x01);
							outputData.push_back(markerText.size());
							outputData.insert(outputData.end(), markerText.begin(), markerText.end());
							position +=2;
							continue;
						}
						outputData.push_back(command);
						switch (command) {
							case 0xFF:
								outputData.push_back(metaType =fileData.at(position++));
								outputData.push_back(commandLength =fileData.at(position++));
								while (commandLength--) outputData.push_back(fileData.at(position++));
								finished =metaType ==0x2F;
								break;
							case 0xF0:
								outputData.push_back(commandLength =fileData.at(position++));
								while (commandLength--) outputData.push_back(fileData.at(position++));
								break;
							default:
								if (command >0xF0) {
									fprintf(stderr, "Unknown command byte %02X\n", command);
									return(false);
								}
								outputData.push_back(fileData.at(position++));
								if (command <0xC0 || command >=0xE0) outputData.push_back(fileData.at(position++));
								break;
						}
					}
					size_t trackSize =outputData.size() -MTrkStart;
					outputData[MTrkStart -4 +0] =trackSize >>24 &0xFF;
					outputData[MTrkStart -4 +1] =trackSize >>16 &0xFF;
					outputData[MTrkStart -4 +2] =trackSize >> 8 &0xFF;
					outputData[MTrkStart -4 +3] =trackSize >> 0 &0xFF;
					includedTracks++;
				}
				position =trackStartPosition +trackLength;
			}
			if (includedTracks !=numberOfTracks) {
				outputData[10] =includedTracks >>8 &0xFF;
				outputData[11] =includedTracks >>0 &0xFF;
			}
			if (includedTracks >1) {
				FILE* outputHandle =fopen(outputName.c_str(), "wb");
				if (outputHandle) {
					fwrite(&outputData[0], 1, outputData.size(), outputHandle);
					fclose(outputHandle);
					result =true;
				} else
					perror(outputName.c_str());
			} else {
				printf("No output tracks for selected device.\n");
				result =true;
			}
		} else
			fprintf(stderr, "Not a proper HMIDIR file\n");
	} else
		perror(fileName.c_str());

	return result;
}

int main (const int argc, const char **argv) {
	for (int arg =1; arg <argc; arg++) if (argv[arg][0] =='-') {
		if (!strcmp(argv[arg], "--device")) {
			++arg;
			if (arg >=argc) { fprintf(stderr, "--device requires an argument"); return EXIT_FAILURE; }
			uint8_t device =argv[arg][0];
			switch(device) {
				case 'G': wantedDevice =0;  printf("Only extracting General MIDI tracks.\n"); break;
				case 'F': wantedDevice =2;  printf("Only extracting FM tracks.\n"); break;
				case 'C': wantedDevice =3;  printf("Only extracting C tracks.\n"); break;
				case 'M': wantedDevice =4;  printf("Only extracting MT-32 tracks.\n"); break;
				case 'D': wantedDevice =5;  printf("Only extracting Digital tracks.\n"); break;
				case 'I': wantedDevice =6;  printf("Only extracting Internal Speaker tracks.\n"); break;
				case 'W': wantedDevice =7;  printf("Only extracting W tracks.\n"); break;
				case 'U' :wantedDevice =10; printf("Only extracting Ultrasound tracks.\n"); break;
				default:fprintf(stderr, "--device: Unknown device %c\n", device); break;
			}
		} else {
			fprintf(stderr, "%s: Invalid option\n", argv[arg]);
			return EXIT_FAILURE;
		}
	} else {
		bool result =convertFile(argv[arg]);
		if (!result) return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}