
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <iostream>
using namespace std;

#include <sys/types.h>
#include <sys/stat.h>

#include <log4cpp/Category.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/BasicLayout.hh>
#include <log4cpp/PatternLayout.hh>

typedef struct __data_source
{
	std::string path;
	
	__data_source()
	: path("")
	{}
}data_source_t;

typedef struct __service
{
	std::string address;
	u_int16_t port;
	u_int64_t timeout_ms;
	
	__service()
	: address("127.0.0.1"), port(20001), timeout_ms(1000)
	{}
}service_t;

typedef struct __log
{
	std::string file;
	std::string directory;
	size_t max_files;
	size_t max_size;
	int level;
	
	__log()
	: file("dreamon.log"), directory("./"), max_files(2), max_size(5*1024*1024), level(3)
	{}
}log_t;

typedef struct
{
	u_int64_t opt_price;
	u_int64_t opt_put;
	u_int64_t opt_call;
	u_int64_t opt_open;
	u_int64_t opt_low;
	u_int64_t opt_high;
}sample_t;

#define SAMPLES_PER_RECORD 10
typedef struct
{
	u_int64_t asset_current_price;
	u_int64_t asset_change_percentage;
	u_int64_t asset_change_stddev;
	sample_t samples[SAMPLES_PER_RECORD];
}record_t;

void get_options(int argc, char *argv[], data_source_t * dsrc, service_t * svc, log_t * log);
void show_usage(const char * prog);
void init_log(const log_t & log);
void load_data(const data_source_t & dsrc);
void load_data_file(const char * file);
void load_record(std::list<std::string> & rec_lines);
int parse_rec_hdr(record_t & rec, std::string & hdr);
void serve(const service_t & svc);

std::list< record_t > records;

int main(int argc, char *argv[])
{
	data_source_t dsrc;
	service_t svc;
	log_t log;
	get_options(argc, argv, &dsrc, &svc, &log);

	init_log(log);
	load_data(dsrc);
	serve(svc);
}

void get_options(int argc, char *argv[], data_source_t * dsrc, service_t * svc, log_t * log)
{
	int opt;
	while ((opt = getopt(argc, argv, "hf:a:p:t:l:c:m:s:r:")) != -1)
	{
		switch (opt)
		{
		case 'h':
			show_usage(argv[0]);
			exit(0);
		case 'f':
			dsrc->path = optarg;
			break;
		case 'a':
			svc->address = optarg;
			break;
		case 'p':
			svc->port = atoi(optarg);
			break;
		case 't':
			svc->timeout_ms = atoi(optarg);
			break;
		case 'l':
			log->file = optarg;
			break;
		case 'c':
			log->directory = optarg;
			break;
		case 'm':
			log->max_files = atoi(optarg);
			break;
		case 's':
			log->max_size = atoi(optarg);
			break;
		case 'r':
			log->level = atoi(optarg);
			break;
		default:
			std::cerr << "Invalid program arguments." << std::endl;
			show_usage(argv[0]);
			exit(-1);
		}
	}
}

void show_usage(const char * prog)
{
	std::cout << "Usage:" << std::endl;
	std::cout << prog << "   [ OPTIONS ]" << std::endl;
	std::cout << "-f   source data file" << std::endl;
	std::cout << "-a   service address" << std::endl;
	std::cout << "-p   service port" << std::endl;
	std::cout << "-t   service timeout" << std::endl;
	std::cout << "-l   log file" << std::endl;
	std::cout << "-c   log location" << std::endl;
	std::cout << "-m   max log files" << std::endl;
	std::cout << "-s   max log size" << std::endl;
	std::cout << "-r   log level" << std::endl;
}

void init_log(const log_t & log)
{
	static const char the_layout[] = "%d{%y-%m-%d %H:%M:%S.%l}| %-6p | %-15c | %m%n";

	std::string log_file = log.file;
	log_file.insert(0, "/");
	log_file.insert(0, log.directory);

    log4cpp::Layout * log_layout = NULL;
    log4cpp::Appender * appender = new log4cpp::RollingFileAppender("lpm.appender", log_file.c_str(), log.max_size, log.max_files);

    bool pattern_layout = false;
    try
    {
        log_layout = new log4cpp::PatternLayout();
        ((log4cpp::PatternLayout *)log_layout)->setConversionPattern(the_layout);
        appender->setLayout(log_layout);
        pattern_layout = true;
    }
    catch(...)
    {
        pattern_layout = false;
    }

    if(!pattern_layout)
    {
        log_layout = new log4cpp::BasicLayout();
        appender->setLayout(log_layout);
    }

    log4cpp::Category::getInstance("drmn").addAppender(appender);
    log4cpp::Category::getInstance("drmn").setPriority((log4cpp::Priority::PriorityLevel)log.level);
    log4cpp::Category::getInstance("drmn").notice("dreamon log start");
}

void load_data(const data_source_t & dsrc)
{
	struct stat dsrc_stat;
	if(0 == stat(dsrc.path.c_str(), &dsrc_stat))
	{
		if(S_ISREG(dsrc_stat.st_mode))
			load_data_file(dsrc.path.c_str());
		else
		{
			log4cpp::Category::getInstance("drmn.dsrc").fatal("%s: [%s] isn't a file and only source data files are supported.",
							__FUNCTION__, dsrc.path.c_str());
			exit(__LINE__);
		}
	}
	else
	{
		int errcode = errno;
		char errmsg[256];
		log4cpp::Category::getInstance("drmn.dsrc").fatal("%s: stat() of [%s] failed with error %d : [%s]",
				__FUNCTION__, dsrc.path.c_str(), errcode, strerror_r(errcode, errmsg, 256));
		exit(__LINE__);
	}
}

void load_data_file(const char * file)
{
	FILE * pf = fopen(file, "r");
	if(NULL != pf)
	{
		static const char record_token[] = "Time,Price,%,SD";
		static const size_t record_line_count = 12;
		char buffer[256];
		while(NULL != fgets(buffer, 256, pf))
		{
			if(NULL != strstr(buffer, record_token))
			{
				std::list<std::string> rec_lines;
				for(size_t i = 0; i < record_line_count && NULL != fgets(buffer, 256, pf); ++i)
					rec_lines.push_back(buffer);
				if(rec_lines.size() == record_line_count)
					load_record(rec_lines);
			}
		}
		fclose(pf);
	}
	else
	{
		int errcode = errno;
		char errmsg[256];
		log4cpp::Category::getInstance("drmn.dsrc").fatal("%s: fopen() of [%s] failed with error %d : [%s]",
				__FUNCTION__, file, errcode, strerror_r(errcode, errmsg, 256));
		exit(__LINE__);
	}
}

void load_record(std::list<std::string> & rec_lines)
{
	static const char samples_token[] = "Call,,Put";

	record_t rec;
	std::list<std::string>::iterator i = rec_lines.begin();

	if(0 != parse_rec_hdr(rec, *i))
	{
		log4cpp::Category::getInstance("drmn.dsrc").error("%s: failed parsing record header [%s]",
				__FUNCTION__, i->c_str());
		return;
	}
	++i;

	if(NULL == strstr(i->c_str(), samples_token))
	{
		log4cpp::Category::getInstance("drmn.dsrc").error("%s: invalid samples token [%s]",
				__FUNCTION__, i->c_str());
		return;
	}
	++i;

	while(rec_lines.end() != i)
	{
#error 'continue here'
	}

}
