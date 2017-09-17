#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
}
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <libgen.h>
#include "Processor.h"
#include "NotUsedException.h"

#define BUILD_BATCH true

Processor::Processor()
{
	throw NotUsedException();
}

Processor::Processor(Database * database, char * folderInProcess, char * folderInWindows, char * folderOutWindows, char * folderOutProcess)
{
	this->database = database;
	this->folderInProcess = folderInProcess;
	this->folderOutProcess = folderOutProcess;
	this->folderInWindows = folderInWindows;
	this->folderOutWindows = folderOutWindows;
}

char * Processor::scat(const char * s1, const char * s2)
{
	char * str = (char *) malloc(sizeof(char) * (strlen(s1) + strlen(s2) + 1));
	strcpy(str, s1);
	strcat(str, s2);
	return str;
}

char * Processor::convertTime(char * out, int time)
{
	int sec = time % 60;
	time /= 60;
	int min = time % 60;
	time /= 60;
	sprintf(out, "%02dh%02dm%02d", time, min, sec);
	return out;
}

VInfos * Processor::getVInfos(char * filename, const char * name)
{
	//Prepare structure
	VInfos * vInfos = (VInfos *) malloc(sizeof(VInfos));
	vInfos->codec = nullptr;
	vInfos->fps = 0;
	vInfos->duration = 0;
	vInfos->filename = name;
	vInfos->outFilename = asMP4(name);
	vInfos->isPicture = false;
	convertTime(vInfos->stringDuration, (int) vInfos->duration);
	
	//Open file.
	AVFormatContext * pFormatCtx = avformat_alloc_context();
	int errorID = avformat_open_input(&pFormatCtx, filename, nullptr, nullptr);
	
	if(errorID < 0 || pFormatCtx->nb_streams == 0) //If an error happened when reading the file.
	{
		char * errorStr;
		errorStr = (char *) malloc(100 * sizeof(char));
		if(errorStr == nullptr)
			return vInfos;
		av_strerror(errorID, errorStr, 100);
		printf("ERROR: %s\n", errorStr);
		free(errorStr);
	}
	else
	{
		if(avformat_find_stream_info(pFormatCtx, nullptr) < 0)
			return vInfos; // Couldn't find stream information
		
		vInfos->duration = pFormatCtx->duration / ((double) AV_TIME_BASE);
		convertTime(vInfos->stringDuration, (int) vInfos->duration);
		
		for(unsigned int i = 0; i < pFormatCtx->nb_streams; i++) //For each available stream.
		{
			AVStream * stream = pFormatCtx->streams[i];
			AVCodecParameters * codecParameters = stream->codecpar;
			enum AVCodecID codecID = codecParameters->codec_id;
			const AVCodecDescriptor * codecDescriptor = avcodec_descriptor_get(codecID);
			if(codecDescriptor->type == AVMEDIA_TYPE_VIDEO) //If this is a video stream.
			{
				vInfos->isPicture = true;
				vInfos->codec = codecDescriptor->name;
				
				AVRational r = stream->avg_frame_rate;
				vInfos->fps = ((double) r.num) / r.den;
				break;
			}
		}
		avformat_close_input(&pFormatCtx);
		avformat_free_context(pFormatCtx);
	}
	return vInfos;
}

bool Processor::isSystemFile(char * filename)
{
	if(*filename == '.')
		return true;
	char * dot = strrchr(filename, '.');
	return dot == nullptr || strcmp(dot, ".ini") == 0 || strcmp(dot, ".txt") == 0;
}

bool Processor::shouldSkip(char * filename)
{
	char * dot = strrchr(filename, '.');
	return dot == nullptr || strcmp(dot, ".loc") == 0 || strcmp(dot, ".msg") == 0;
}

bool Processor::isPictureFile(char * filename)
{
	char * dot = strrchr(filename, '.');
	if(dot == nullptr)
		return false;
	return strcmp(dot, ".jpg") == 0 || strcmp(dot, ".png") == 0 || strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".JPG") == 0 || strcmp(dot, ".PNG") == 0;
}

char * Processor::asMP4(const char * filename)
{
	char * nFilename = strdup(filename);
	char * dot = strrchr(nFilename, '.');
	if(strcmp(dot, ".mp4") != 0)
	{
		if(strlen(dot) < 4)
			nFilename = (char *) realloc(&nFilename, sizeof(char) * ((dot - nFilename) + 5));
		dot = strrchr(nFilename, '.');
		strcpy(dot, ".mp4");
	}
	return nFilename;
}

void Processor::process()
{
	std::cout << "Processing folder " << folderInWindows << std::endl;

#ifndef _WIN32
	mkdir(folderOutProcess, S_IRWXU);
#endif
	
	char filePath[512];
	char * dirName = basename(folderInProcess);
	
	DIR * dir = opendir(folderInProcess);
	struct dirent * file;
	while((file = readdir(dir)) != nullptr) //Loop through all the files
	{
		if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
			continue;
		
		if(file->d_type == DT_DIR)
		{
			char * temp = scat(folderInWindows, file->d_name);
			char * nFolderInWindows = scat(temp, "\\");
			free(temp);
			
			temp = scat(folderOutWindows, file->d_name);
			char * nFolderOutWindows = scat(temp, "\\");
			free(temp);
			
			temp = scat(folderInProcess, file->d_name);
			char * nFolderInProcess = scat(temp, "/");
			free(temp);
			
			Processor * processor = new Processor(database, nFolderInProcess, nFolderInWindows, nFolderOutWindows, folderOutProcess);
			processor->process();
			delete processor;
			
			free(nFolderOutWindows);
			free(nFolderInWindows);
			continue;
		}
		
		//Continue only if we should.
		if(isSystemFile(file->d_name))
			continue;
		
		if(shouldSkip(file->d_name))
			continue;
		
		//Get the informations about this video.
		sprintf(filePath, "%s/%s", folderInProcess, file->d_name);
		std::cout << "Processing file " << filePath << std::endl;
		VInfos * vInfos = getVInfos(filePath, file->d_name);
		
		if(vInfos->isPicture == 0 || isPictureFile(file->d_name))
		{
			database->registerPicture(database, file->d_name);
			free(vInfos->outFilename);
			free(vInfos);
			continue;
		}
		
		database->registerVideo(database, vInfos);
		
		if((vInfos->fps > 0 && vInfos->fps < 60) && strcmp(vInfos->codec, "h264") == 0) //If we want to convert the video.
		{
			if(BUILD_BATCH)
			{
				//Prepare folders & filenames
				char batFilename[200];
				sprintf(batFilename, "%s %s %s %s %f.bat", vInfos->stringDuration, dirName, file->d_name, vInfos->codec, vInfos->fps);
				char * fileInWindows = scat(folderInWindows, file->d_name);
				char * fileOutWindows = scat(folderOutWindows, vInfos->outFilename);
				char * fileBatWindows = scat(R"(***REMOVED***)", batFilename);
				char * fileBatMac = scat(folderOutProcess, batFilename);
				
				//Write file content.
				FILE * batFile;
				if((batFile = fopen(fileBatMac, "w")) != nullptr)
				{
					fprintf(batFile, "mkdir \"%s\"\r\n", folderOutWindows);
					fprintf(batFile, "ffmpeg -n -i \"%s\" -c:v libx265 -preset medium -crf 28 -c:a aac -b:a 128k \"%s\"\r\n", fileInWindows, fileOutWindows);
					fprintf(batFile, "if exist \"%s\" call \"D:\\Documents\\Logiciels\\deleteJS.bat\" \"%s\"\r\n", fileOutWindows, fileInWindows);
					fprintf(batFile, "if exist \"%s\" del \"%s\"\r\n", fileBatWindows, fileBatWindows);
					fclose(batFile);
					std::cout << "\tWrote file " << fileBatMac << "." << std::endl;
				}
				else
					std::cout << "\tError writing file " << fileBatMac << std::endl;
				
				//Clean the house.
				free(fileBatMac);
				free(fileBatWindows);
				free(fileOutWindows);
				free(fileInWindows);
			}
		}
		else
		{
			if(vInfos->codec != nullptr && strcmp(vInfos->codec, "hevc") == 0) //Ignore h265 as this is the result we want.
			{
			}
			else if(vInfos->fps > 239)
				std::cout << "\tSkipped slowmotion (" << vInfos->codec << "," << vInfos->fps << "," << vInfos->stringDuration << "," << (vInfos->isPicture ? "P" : "V") << "):" << vInfos->filename << std::endl;
			else
				std::cout << "\tSkipped file (" << vInfos->codec << "," << vInfos->fps << "," << vInfos->stringDuration << "," << (vInfos->isPicture ? "P" : "V") << "):" << vInfos->filename << std::endl;
		}
		free(vInfos->outFilename);
		free(vInfos);
	}
	
	closedir(dir);
}