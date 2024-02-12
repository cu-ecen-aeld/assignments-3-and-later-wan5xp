#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
	if(argc != 3){
		syslog(LOG_ERR, "Incomplete parameters\n");
		return 1;
	}
	char * writefile = argv[1];
	char * writestr = argv[2];
	syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

	FILE *fp = fopen(writefile, "w");
	if(fp == NULL){
		syslog(LOG_ERR, "Cannot open this file %s\n", writefile);
		return 1;
	}
	fprintf(fp, "%s", writestr);

	fclose(fp);
	return 0;
}
