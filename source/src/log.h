#ifndef SOURCE_SRC_LOG_H_
#define SOURCE_SRC_LOG_H_

bool initlogging(const char *identity, int facility_, int consolethres, int filethres, int syslogthres, bool logtimestamp);
void exitlogging();
bool logline(int level, const char *msg, ...);


#endif /* SOURCE_SRC_LOG_H_ */
